#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace csvql {

namespace detail {

inline std::string to_lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

inline std::string to_upper_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

inline std::string trim_ascii(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

inline bool is_whole_number(double v) {
    if (!std::isfinite(v) || std::abs(v) >= 1e15) {
        return false;
    }
    return std::abs(v - std::round(v)) < 1e-9;
}

inline std::string format_number(double v, int precision, bool compact_int) {
    if (!std::isfinite(v)) {
        return "NULL";
    }
    if (std::abs(v) < 1e-12) {
        v = 0.0;
    }
    if (compact_int && is_whole_number(v)) {
        long long rounded = static_cast<long long>(std::llround(v));
        return std::to_string(rounded);
    }
    if (precision < 0) {
        precision = 0;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    std::string out = oss.str();
    // Normalize "-0.00" or "-0" to positive zero representation.
    if (!out.empty() && out[0] == '-') {
        bool all_zero = true;
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (out[i] != '0' && out[i] != '.') {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            out.erase(out.begin());
        }
    }
    return out;
}

}  // namespace detail

/// Dynamic cell value holding Null, finite double, or UTF-8 string.
class Value {
public:
    using Storage = std::variant<std::monostate, double, std::string>;

    Value() : data_(std::monostate{}) {}
    Value(std::monostate) : data_(std::monostate{}) {}
    Value(double num)
        : data_(std::isfinite(num) ? Storage{(num == 0.0) ? 0.0 : num} : Storage{std::monostate{}}) {}
    Value(int num) : data_(static_cast<double>(num)) {}
    Value(long long num) : data_(static_cast<double>(num)) {}
    Value(std::size_t num) : data_(static_cast<double>(num)) {}
    Value(const char* text) : data_(text ? Storage{std::string(text)} : Storage{std::monostate{}}) {}
    Value(std::string text) : data_(std::move(text)) {}
    Value(std::nullptr_t) = delete;
    Value(bool) = delete;

    static Value null() { return Value(); }
    static Value number(double v) { return Value(v); }
    static Value text(std::string s) { return Value(std::move(s)); }

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    bool is_number() const { return std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }

    double as_number() const {
        if (!is_number()) {
            throw std::runtime_error("csvql::Value is not a number");
        }
        return std::get<double>(data_);
    }

    const std::string& as_string() const {
        if (!is_string()) {
            throw std::runtime_error("csvql::Value is not a string");
        }
        return std::get<std::string>(data_);
    }

    std::string to_string(int precision = 2, bool compact_int = true) const {
        if (is_null()) {
            return "NULL";
        }
        if (is_number()) {
            return detail::format_number(std::get<double>(data_), precision, compact_int);
        }
        return std::get<std::string>(data_);
    }

    const Storage& raw() const { return data_; }

    bool operator==(const Value& other) const {
        if (is_null() && other.is_null()) {
            return true;
        }
        if (is_number() && other.is_number()) {
            return std::abs(as_number() - other.as_number()) < 1e-9;
        }
        if (is_string() && other.is_string()) {
            return as_string() == other.as_string();
        }
        return false;
    }

    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    Storage data_;
};

/// Total ordering across Values (Null < Number < String).
inline int compare_values(const Value& a, const Value& b) {
    if (a.is_null() && b.is_null()) return 0;
    if (a.is_null()) return -1;
    if (b.is_null()) return 1;

    if (a.is_number() && b.is_number()) {
        double da = a.as_number();
        double db = b.as_number();
        if (std::abs(da - db) < 1e-12) return 0;
        return da < db ? -1 : 1;
    }
    if (a.is_number() && b.is_string()) return -1;
    if (a.is_string() && b.is_number()) return 1;

    const std::string& sa = a.as_string();
    const std::string& sb = b.as_string();
    if (sa == sb) return 0;
    return sa < sb ? -1 : 1;
}

/// Comparable comparison for WHERE/HAVING predicates (returns nullopt if null or mismatched types).
inline std::optional<int> compare_comparable(const Value& a, const Value& b) {
    if (a.is_null() || b.is_null()) {
        return std::nullopt;
    }
    if (a.is_number() && b.is_number()) {
        double da = a.as_number();
        double db = b.as_number();
        if (std::abs(da - db) < 1e-9) return 0;
        return da < db ? -1 : 1;
    }
    if (a.is_string() && b.is_string()) {
        const std::string& sa = a.as_string();
        const std::string& sb = b.as_string();
        if (sa == sb) return 0;
        return sa < sb ? -1 : 1;
    }
    return std::nullopt;
}

/// Non-fatal diagnostic recorded when fault-tolerant CSV parsing repairs dirty cells.
struct ParseWarning {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string raw_text;
    std::string message;
};

/// Tabular dataset serving as the core exchange type between parser, query engine, and visualizer.
class Table {
public:
    using Row = std::vector<Value>;

    Table() = default;

    explicit Table(std::vector<std::string> headers) {
        set_headers(std::move(headers));
    }

    void set_headers(std::vector<std::string> headers) {
        headers_ = std::move(headers);
        rebuild_index();
    }

    const std::vector<std::string>& headers() const { return headers_; }
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<ParseWarning>& warnings() const { return warnings_; }

    std::size_t row_count() const { return rows_.size(); }
    std::size_t col_count() const { return headers_.size(); }
    bool empty() const { return rows_.empty(); }

    /// Finds a column index by exact match first, falling back to case-insensitive match.
    std::optional<std::size_t> column_index(const std::string& name) const {
        auto it = exact_index_.find(name);
        if (it != exact_index_.end()) {
            return it->second;
        }
        auto lower_it = lower_index_.find(detail::to_lower_ascii(name));
        if (lower_it != lower_index_.end()) {
            return lower_it->second;
        }
        return std::nullopt;
    }

    bool has_column(const std::string& name) const {
        return column_index(name).has_value();
    }

    void add_row(Row row) {
        if (row.size() != headers_.size()) {
            throw std::invalid_argument(
                "Table::add_row column count mismatch: expected " +
                std::to_string(headers_.size()) + ", got " + std::to_string(row.size()));
        }
        rows_.push_back(std::move(row));
    }

    const Row& row(std::size_t r) const { return rows_.at(r); }

    const Value& cell(std::size_t r, std::size_t c) const {
        if (r >= rows_.size() || c >= headers_.size()) {
            throw std::out_of_range("Table::cell index out of range");
        }
        return rows_[r][c];
    }

    const Value& cell(std::size_t r, const std::string& col_name) const {
        auto idx = column_index(col_name);
        if (!idx.has_value()) {
            throw std::invalid_argument("Unknown column: '" + col_name + "'");
        }
        return cell(r, *idx);
    }

    void add_warning(ParseWarning warning) {
        warnings_.push_back(std::move(warning));
    }

    void set_warnings(std::vector<ParseWarning> warnings) {
        warnings_ = std::move(warnings);
    }

private:
    void rebuild_index() {
        exact_index_.clear();
        lower_index_.clear();
        for (std::size_t i = 0; i < headers_.size(); ++i) {
            exact_index_.emplace(headers_[i], i);
            lower_index_.emplace(detail::to_lower_ascii(headers_[i]), i);
        }
    }

    std::vector<std::string> headers_;
    std::vector<Row> rows_;
    std::vector<ParseWarning> warnings_;
    std::unordered_map<std::string, std::size_t> exact_index_;
    std::unordered_map<std::string, std::size_t> lower_index_;
};

}  // namespace csvql
