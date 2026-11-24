#pragma once

#include "table.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace csvql {

/// Options controlling fault-tolerant RFC-4180 CSV parsing.
struct CsvOptions {
    char delimiter = ',';
    bool has_header = true;
    bool trim_whitespace = true;
    bool treat_na_as_null = true;
};

namespace detail {

struct ParsedField {
    std::string text;
    bool was_quoted = false;
};

struct ParsedRecord {
    std::size_t start_line = 1;
    std::vector<ParsedField> fields;
};

inline bool is_na_sentinel(const std::string& s) {
    if (s.empty()) return true;
    const std::string lower = to_lower_ascii(s);
    return lower == "na" || lower == "n/a" || lower == "null" ||
           lower == "nan" || lower == "none" || lower == "--";
}

inline std::optional<double> try_parse_strict_number(const std::string& s) {
    if (s.empty()) return std::nullopt;
    bool has_digit = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isspace(c)) return std::nullopt;
        if (std::isdigit(c)) has_digit = true;
        if (c == 'x' || c == 'X') return std::nullopt;
    }
    if (!has_digit) return std::nullopt;

    errno = 0;
    char* end = nullptr;
    const double val = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) {
        return std::nullopt;
    }
    if ((errno == ERANGE && !std::isfinite(val)) || !std::isfinite(val)) {
        return std::nullopt;
    }
    return (val == 0.0) ? 0.0 : val;
}

inline std::vector<ParsedRecord> parse_raw_records(
    const std::string& input,
    char delimiter,
    std::vector<ParseWarning>& warnings) {

    std::vector<ParsedRecord> records;
    std::size_t pos = 0;

    // Strip UTF-8 BOM if present.
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF) {
        pos = 3;
    }

    ParsedRecord current;
    current.start_line = 1;
    ParsedField field;
    bool in_quotes = false;
    bool at_field_start = true;
    std::size_t line = 1;

    auto finish_field = [&]() {
        current.fields.push_back(std::move(field));
        field = ParsedField{};
        at_field_start = true;
    };

    auto finish_record = [&]() {
        finish_field();
        const bool is_blank_line =
            current.fields.size() == 1 &&
            !current.fields[0].was_quoted &&
            trim_ascii(current.fields[0].text).empty();
        if (!is_blank_line) {
            records.push_back(std::move(current));
        }
        current = ParsedRecord{};
        current.start_line = line;
    };

    while (pos < input.size()) {
        const char ch = input[pos];
        if (in_quotes) {
            if (ch == '"') {
                if (pos + 1 < input.size() && input[pos + 1] == '"') {
                    field.text.push_back('"');
                    pos += 2;
                    continue;
                }
                in_quotes = false;
                ++pos;
                continue;
            }
            if (ch == '\r') {
                if (pos + 1 < input.size() && input[pos + 1] == '\n') {
                    ++pos;
                }
                field.text.push_back('\n');
                ++line;
                ++pos;
                continue;
            }
            if (ch == '\n') {
                field.text.push_back('\n');
                ++line;
                ++pos;
                continue;
            }
            field.text.push_back(ch);
            ++pos;
        } else {
            if (at_field_start && ch == '"') {
                in_quotes = true;
                field.was_quoted = true;
                at_field_start = false;
                ++pos;
                continue;
            }
            at_field_start = false;

            if (ch == delimiter) {
                finish_field();
                ++pos;
                continue;
            }
            if (ch == '\r') {
                if (pos + 1 < input.size() && input[pos + 1] == '\n') {
                    ++pos;
                }
                ++line;
                finish_record();
                ++pos;
                continue;
            }
            if (ch == '\n') {
                ++line;
                finish_record();
                ++pos;
                continue;
            }
            field.text.push_back(ch);
            ++pos;
        }
    }

    if (in_quotes) {
        warnings.push_back(ParseWarning{
            current.start_line,
            current.fields.size() + 1,
            field.text,
            "Unclosed quote repaired at end of input"});
    }

    if (!at_field_start || !current.fields.empty()) {
        finish_record();
    }

    return records;
}

}  // namespace detail

/// Parses CSV content from an already loaded string without throwing on dirty cells.
inline Table parse_csv_string(const std::string& content, const CsvOptions& opts = {}) {
    std::vector<ParseWarning> warnings;
    std::vector<detail::ParsedRecord> records =
        detail::parse_raw_records(content, opts.delimiter, warnings);

    if (records.empty()) {
        Table empty_table;
        empty_table.set_warnings(std::move(warnings));
        return empty_table;
    }

    std::vector<std::string> headers;
    std::size_t data_start_idx = 0;

    if (opts.has_header) {
        const auto& hdr_record = records[0];
        std::unordered_set<std::string> used;
        headers.reserve(hdr_record.fields.size());
        for (std::size_t c = 0; c < hdr_record.fields.size(); ++c) {
            std::string h = hdr_record.fields[c].text;
            if (opts.trim_whitespace) {
                h = detail::trim_ascii(h);
            }
            if (h.empty()) {
                h = "col_" + std::to_string(c);
                warnings.push_back(ParseWarning{
                    hdr_record.start_line, c + 1, "", "Empty header renamed to '" + h + "'"});
            }
            const std::string base = h;
            int suffix = 2;
            while (used.count(detail::to_lower_ascii(h)) > 0) {
                h = base + "_" + std::to_string(suffix++);
            }
            if (h != base) {
                warnings.push_back(ParseWarning{
                    hdr_record.start_line, c + 1, base,
                    "Duplicate header renamed to '" + h + "'"});
            }
            used.insert(detail::to_lower_ascii(h));
            headers.push_back(std::move(h));
        }
        data_start_idx = 1;
    } else {
        const std::size_t width = records[0].fields.size();
        headers.reserve(width);
        for (std::size_t c = 0; c < width; ++c) {
            headers.push_back("col_" + std::to_string(c));
        }
    }

    Table table(std::move(headers));
    const std::size_t expected_cols = table.col_count();

    for (std::size_t r = data_start_idx; r < records.size(); ++r) {
        const auto& rec = records[r];
        std::vector<Value> row;
        row.reserve(expected_cols);

        if (rec.fields.size() < expected_cols) {
            warnings.push_back(ParseWarning{
                rec.start_line,
                rec.fields.size() + 1,
                "",
                "Row has fewer columns (" + std::to_string(rec.fields.size()) +
                    " < " + std::to_string(expected_cols) + "); padded with NULL"});
        } else if (rec.fields.size() > expected_cols) {
            warnings.push_back(ParseWarning{
                rec.start_line,
                expected_cols + 1,
                rec.fields[expected_cols].text,
                "Row has extra columns (" + std::to_string(rec.fields.size()) +
                    " > " + std::to_string(expected_cols) + "); truncated"});
        }

        const std::size_t limit = std::min(expected_cols, rec.fields.size());
        for (std::size_t c = 0; c < limit; ++c) {
            std::string text = rec.fields[c].text;
            if (opts.trim_whitespace) {
                text = detail::trim_ascii(text);
            }
            if (opts.treat_na_as_null && detail::is_na_sentinel(text)) {
                if (!text.empty()) {
                    warnings.push_back(ParseWarning{
                        rec.start_line,
                        c + 1,
                        text,
                        "Dirty sentinel '" + text + "' in column '" +
                            table.headers()[c] + "' coerced to NULL"});
                }
                row.push_back(Value::null());
                continue;
            }
            if (text.empty()) {
                row.push_back(Value::null());
                continue;
            }
            if (auto num = detail::try_parse_strict_number(text)) {
                row.push_back(Value::number(*num));
            } else {
                row.push_back(Value::text(std::move(text)));
            }
        }

        while (row.size() < expected_cols) {
            row.push_back(Value::null());
        }
        table.add_row(std::move(row));
    }

    table.set_warnings(std::move(warnings));
    return table;
}

/// Parses CSV from an input stream.
inline Table parse_csv(std::istream& in, const CsvOptions& opts = {}) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse_csv_string(buffer.str(), opts);
}

/// Loads and parses a CSV file from disk. Throws std::runtime_error if unreadable.
inline Table load_csv(const std::string& filepath, const CsvOptions& opts = {}) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("Failed to open CSV file: '" + filepath + "'");
    }
    return parse_csv(ifs, opts);
}

}  // namespace csvql
