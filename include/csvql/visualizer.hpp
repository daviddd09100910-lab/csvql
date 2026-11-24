#pragma once

#include "table.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace csvql {

/// Rendering options shared across tables, bar charts, histograms, and sparklines.
struct RenderOptions {
    bool use_unicode = true;
    bool use_color = true;
    int max_bar_width = 32;
    int precision = 2;
    int histogram_bins = 6;
    int max_cell_width = 40;
    std::string title;
};

/// Enables UTF-8 output and ANSI VT color processing on Windows consoles (no-op elsewhere).
inline void enable_console_utf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            constexpr DWORD kVtProcessing = 0x0004;  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            SetConsoleMode(hOut, mode | kVtProcessing);
        }
    }
#endif
}

namespace detail {

inline const char* ansi_reset(const RenderOptions& opts) { return opts.use_color ? "\033[0m" : ""; }
inline const char* ansi_bold(const RenderOptions& opts) { return opts.use_color ? "\033[1m" : ""; }
inline const char* ansi_dim(const RenderOptions& opts) { return opts.use_color ? "\033[90m" : ""; }
inline const char* ansi_cyan(const RenderOptions& opts) { return opts.use_color ? "\033[36m" : ""; }
inline const char* ansi_green(const RenderOptions& opts) { return opts.use_color ? "\033[32m" : ""; }
inline const char* ansi_red(const RenderOptions& opts) { return opts.use_color ? "\033[31m" : ""; }
inline const char* ansi_yellow(const RenderOptions& opts) { return opts.use_color ? "\033[33m" : ""; }

inline std::string sanitize_single_line(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            if (out.empty() || out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

inline int codepoint_display_width(unsigned int cp) {
    if (cp == 0) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF)) {
        return 2;
    }
    return 1;
}

inline std::size_t utf8_decode_step(const std::string& s, std::size_t i, unsigned int& cp) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        cp = ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        cp = ((c0 & 0x0Fu) << 12) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        cp = ((c0 & 0x07u) << 18) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        return 4;
    }
    cp = c0;
    return 1;
}

inline int utf8_display_width(const std::string& s) {
    int width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned int cp = 0;
        i += utf8_decode_step(s, i, cp);
        width += codepoint_display_width(cp);
    }
    return width;
}

inline std::string truncate_utf8_width(const std::string& s, int max_width, bool use_unicode) {
    if (max_width <= 0 || utf8_display_width(s) <= max_width) {
        return s;
    }
    const std::string ellipsis = use_unicode ? "…" : "..";
    const int ell_w = utf8_display_width(ellipsis);
    if (max_width <= ell_w) {
        return std::string(static_cast<std::size_t>(max_width), '.');
    }
    const int target = max_width - ell_w;
    int cur = 0;
    std::size_t i = 0;
    std::string out;
    while (i < s.size()) {
        unsigned int cp = 0;
        const std::size_t step = utf8_decode_step(s, i, cp);
        const int cw = codepoint_display_width(cp);
        if (cur + cw > target) {
            break;
        }
        out.append(s, i, step);
        cur += cw;
        i += step;
    }
    out += ellipsis;
    return out;
}

inline std::string repeat_str(const std::string& unit, int count) {
    std::string out;
    if (count <= 0) return out;
    out.reserve(unit.size() * static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out += unit;
    }
    return out;
}

inline std::string pad_right_vis(const std::string& plain, int width) {
    const int vis = utf8_display_width(plain);
    if (vis >= width) return plain;
    return plain + std::string(static_cast<std::size_t>(width - vis), ' ');
}

inline std::string pad_left_vis(const std::string& plain, int width) {
    const int vis = utf8_display_width(plain);
    if (vis >= width) return plain;
    return std::string(static_cast<std::size_t>(width - vis), ' ') + plain;
}

inline void print_title(std::ostream& out, const RenderOptions& opts) {
    if (!opts.title.empty()) {
        out << ansi_bold(opts) << ansi_cyan(opts) << opts.title << ansi_reset(opts) << "\n";
    }
}

inline std::size_t require_column_index(const Table& table, const std::string& name) {
    auto idx = table.column_index(name);
    if (!idx.has_value()) {
        throw std::invalid_argument("Visualizer: unknown column '" + name + "'");
    }
    return *idx;
}

}  // namespace detail

/// Renders a Table with Unicode or ASCII borders, column alignment, and optional ANSI colors.
inline void render_table(const Table& table, std::ostream& out, const RenderOptions& opts = {}) {
    detail::print_title(out, opts);

    if (table.col_count() == 0) {
        out << detail::ansi_dim(opts) << "(empty table)" << detail::ansi_reset(opts) << "\n";
        return;
    }

    const std::size_t cols = table.col_count();
    const std::size_t rows = table.row_count();

    std::vector<bool> col_is_numeric(cols, false);
    std::vector<bool> col_has_fraction(cols, false);
    for (std::size_t c = 0; c < cols; ++c) {
        bool saw_non_null = false;
        bool all_num = true;
        bool has_frac = false;
        for (std::size_t r = 0; r < rows; ++r) {
            const Value& v = table.cell(r, c);
            if (v.is_null()) continue;
            saw_non_null = true;
            if (!v.is_number()) {
                all_num = false;
                break;
            }
            if (!detail::is_whole_number(v.as_number())) {
                has_frac = true;
            }
        }
        col_is_numeric[c] = saw_non_null && all_num;
        col_has_fraction[c] = has_frac;
    }

    std::vector<std::string> headers_fmt(cols);
    std::vector<int> widths(cols, 0);
    for (std::size_t c = 0; c < cols; ++c) {
        std::string h = detail::sanitize_single_line(table.headers()[c]);
        if (opts.max_cell_width > 0) {
            h = detail::truncate_utf8_width(h, opts.max_cell_width, opts.use_unicode);
        }
        widths[c] = std::max(1, detail::utf8_display_width(h));
        headers_fmt[c] = std::move(h);
    }

    std::vector<std::vector<std::string>> cells_fmt(rows, std::vector<std::string>(cols));
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            const Value& v = table.cell(r, c);
            const bool compact_int = !col_has_fraction[c];
            std::string s = detail::sanitize_single_line(v.to_string(opts.precision, compact_int));
            if (opts.max_cell_width > 0) {
                s = detail::truncate_utf8_width(s, opts.max_cell_width, opts.use_unicode);
            }
            widths[c] = std::max(widths[c], detail::utf8_display_width(s));
            cells_fmt[r][c] = std::move(s);
        }
    }

    const std::string h_line = opts.use_unicode ? "─" : "-";
    const std::string v_line = opts.use_unicode ? "│" : "|";
    const std::string tl = opts.use_unicode ? "┌" : "+";
    const std::string tm = opts.use_unicode ? "┬" : "+";
    const std::string tr = opts.use_unicode ? "┐" : "+";
    const std::string ml = opts.use_unicode ? "├" : "+";
    const std::string mm = opts.use_unicode ? "┼" : "+";
    const std::string mr = opts.use_unicode ? "┤" : "+";
    const std::string bl = opts.use_unicode ? "└" : "+";
    const std::string bm = opts.use_unicode ? "┴" : "+";
    const std::string br = opts.use_unicode ? "┘" : "+";

    auto print_border = [&](const std::string& left, const std::string& mid, const std::string& right) {
        out << left;
        for (std::size_t c = 0; c < cols; ++c) {
            out << detail::repeat_str(h_line, widths[c] + 2);
            out << (c + 1 < cols ? mid : right);
        }
        out << "\n";
    };

    print_border(tl, tm, tr);

    // Header row
    out << v_line;
    for (std::size_t c = 0; c < cols; ++c) {
        const std::string padded = detail::pad_right_vis(headers_fmt[c], widths[c]);
        out << " " << detail::ansi_bold(opts) << detail::ansi_cyan(opts) << padded
            << detail::ansi_reset(opts) << " " << v_line;
    }
    out << "\n";

    if (rows > 0) {
        print_border(ml, mm, mr);
    }

    // Data rows
    for (std::size_t r = 0; r < rows; ++r) {
        out << v_line;
        for (std::size_t c = 0; c < cols; ++c) {
            const Value& v = table.cell(r, c);
            const std::string& plain = cells_fmt[r][c];
            const std::string padded = col_is_numeric[c]
                ? detail::pad_left_vis(plain, widths[c])
                : detail::pad_right_vis(plain, widths[c]);

            out << " ";
            if (v.is_null()) {
                out << detail::ansi_dim(opts) << padded << detail::ansi_reset(opts);
            } else {
                out << padded;
            }
            out << " " << v_line;
        }
        out << "\n";
    }

    print_border(bl, bm, br);
    out << detail::ansi_dim(opts) << "(" << rows << (rows == 1 ? " row)" : " rows)")
        << detail::ansi_reset(opts) << "\n";
}

/// Renders a horizontal bar chart (automatically diverging around zero when negative values appear).
inline void render_bar_chart(
    const Table& table,
    std::size_t label_col_idx,
    std::size_t value_col_idx,
    std::ostream& out,
    const RenderOptions& opts = {}) {

    detail::print_title(out, opts);
    if (label_col_idx >= table.col_count() || value_col_idx >= table.col_count()) {
        throw std::out_of_range("render_bar_chart: column index out of range");
    }

    struct Item {
        std::string label;
        double value = 0.0;
    };

    std::vector<Item> items;
    items.reserve(table.row_count());
    bool has_fraction = false;

    for (std::size_t r = 0; r < table.row_count(); ++r) {
        const Value& v = table.cell(r, value_col_idx);
        if (!v.is_number()) continue;
        const Value& lbl = table.cell(r, label_col_idx);
        std::string lbl_str = lbl.is_null() ? "NULL" : detail::sanitize_single_line(lbl.to_string(opts.precision, true));
        lbl_str = detail::truncate_utf8_width(lbl_str, 22, opts.use_unicode);
        const double num = v.as_number();
        if (!detail::is_whole_number(num)) {
            has_fraction = true;
        }
        items.push_back({std::move(lbl_str), num});
    }

    if (items.empty()) {
        out << detail::ansi_dim(opts) << "(no numeric data to plot)" << detail::ansi_reset(opts) << "\n";
        return;
    }

    double min_v = items[0].value;
    double max_v = items[0].value;
    int max_lbl_w = 1;
    for (const auto& it : items) {
        min_v = std::min(min_v, it.value);
        max_v = std::max(max_v, it.value);
        max_lbl_w = std::max(max_lbl_w, detail::utf8_display_width(it.label));
    }

    const std::string fill_ch = opts.use_unicode ? "█" : "#";
    const std::string axis_ch = opts.use_unicode ? "│" : "|";
    const int bar_budget = std::max(8, opts.max_bar_width);

    // Pre-format values so the right-hand numeric column aligns vertically across all rows
    std::vector<std::string> val_strs;
    val_strs.reserve(items.size());
    int max_val_w = 1;
    for (const auto& it : items) {
        std::string val_str = detail::format_number(it.value, opts.precision, !has_fraction);
        if (min_v < 0.0 && it.value > 0.0 && !val_str.empty() && val_str[0] != '+') {
            val_str = "+" + val_str;
        }
        max_val_w = std::max(max_val_w, detail::utf8_display_width(val_str));
        val_strs.push_back(std::move(val_str));
    }

    if (min_v < 0.0) {
        const bool has_pos = (max_v > 0.0);
        const int left_w = has_pos ? std::max(6, bar_budget / 2) : bar_budget;
        const int right_w = has_pos ? std::max(6, bar_budget / 2) : 0;
        const double max_abs = std::max(std::abs(min_v), std::abs(max_v));
        const double denom = max_abs > 0.0 ? max_abs : 1.0;

        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            const std::string lbl = detail::pad_right_vis(it.label, max_lbl_w);
            const std::string val_padded = detail::pad_left_vis(val_strs[i], max_val_w);

            if (it.value < 0.0) {
                int len = static_cast<int>(std::llround((std::abs(it.value) / denom) * left_w));
                if (len == 0) len = 1;
                len = std::min(len, left_w);
                const std::string pad_l(static_cast<std::size_t>(left_w - len), ' ');
                const std::string bar = detail::repeat_str(fill_ch, len);
                const std::string pad_r(static_cast<std::size_t>(right_w), ' ');
                out << lbl << "  " << pad_l << detail::ansi_red(opts) << bar << detail::ansi_reset(opts)
                    << detail::ansi_dim(opts) << axis_ch << detail::ansi_reset(opts) << pad_r
                    << "  " << detail::ansi_red(opts) << val_padded << detail::ansi_reset(opts) << "\n";
            } else if (it.value > 0.0) {
                int len = static_cast<int>(std::llround((it.value / denom) * right_w));
                if (len == 0) len = 1;
                len = std::min(len, right_w);
                const std::string pad_l(static_cast<std::size_t>(left_w), ' ');
                const std::string bar = detail::repeat_str(fill_ch, len);
                const std::string pad_r(static_cast<std::size_t>(right_w - len), ' ');
                out << lbl << "  " << pad_l
                    << detail::ansi_dim(opts) << axis_ch << detail::ansi_reset(opts)
                    << detail::ansi_green(opts) << bar << detail::ansi_reset(opts) << pad_r
                    << "  " << detail::ansi_green(opts) << val_padded << detail::ansi_reset(opts) << "\n";
            } else {
                const std::string pad_l(static_cast<std::size_t>(left_w), ' ');
                const std::string pad_r(static_cast<std::size_t>(right_w), ' ');
                out << lbl << "  " << pad_l
                    << detail::ansi_dim(opts) << axis_ch << detail::ansi_reset(opts) << pad_r
                    << "  " << val_padded << "\n";
            }
        }
    } else {
        const double denom = max_v > 0.0 ? max_v : 1.0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            const std::string lbl = detail::pad_right_vis(it.label, max_lbl_w);
            int len = static_cast<int>(std::llround((it.value / denom) * bar_budget));
            if (it.value > 0.0 && len == 0) len = 1;
            len = std::min(len, bar_budget);
            const std::string bar = detail::repeat_str(fill_ch, len);
            const std::string pad(static_cast<std::size_t>(bar_budget - len), ' ');
            const std::string val_padded = detail::pad_left_vis(val_strs[i], max_val_w);

            out << lbl << " " << detail::ansi_dim(opts) << axis_ch << detail::ansi_reset(opts) << " "
                << detail::ansi_green(opts) << bar << detail::ansi_reset(opts) << pad
                << "  " << val_padded << "\n";
        }
    }
}

inline void render_bar_chart(
    const Table& table,
    const std::string& label_col,
    const std::string& value_col,
    std::ostream& out,
    const RenderOptions& opts = {}) {
    render_bar_chart(
        table,
        detail::require_column_index(table, label_col),
        detail::require_column_index(table, value_col),
        out,
        opts);
}

/// Renders an equal-width binned histogram of numeric values in a column.
inline void render_histogram(
    const Table& table,
    std::size_t value_col_idx,
    std::ostream& out,
    const RenderOptions& opts = {}) {

    detail::print_title(out, opts);
    if (value_col_idx >= table.col_count()) {
        throw std::out_of_range("render_histogram: column index out of range");
    }

    std::vector<double> vals;
    vals.reserve(table.row_count());
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        const Value& v = table.cell(r, value_col_idx);
        if (v.is_number()) {
            vals.push_back(v.as_number());
        }
    }

    if (vals.empty()) {
        out << detail::ansi_dim(opts) << "(no numeric data for histogram)" << detail::ansi_reset(opts) << "\n";
        return;
    }

    const auto mm = std::minmax_element(vals.begin(), vals.end());
    const double min_v = *mm.first;
    const double max_v = *mm.second;

    int bins = std::clamp(opts.histogram_bins, 1, 50);
    if (std::abs(max_v - min_v) < 1e-12) {
        bins = 1;
    }

    std::vector<std::size_t> counts(static_cast<std::size_t>(bins), 0);
    if (bins == 1) {
        counts[0] = vals.size();
    } else {
        const double span = max_v - min_v;
        for (double v : vals) {
            double norm = (v - min_v) / span;
            int idx = static_cast<int>(std::floor(norm * bins));
            if (idx < 0) idx = 0;
            if (idx >= bins) idx = bins - 1;
            ++counts[static_cast<std::size_t>(idx)];
        }
    }

    std::vector<std::string> range_labels(static_cast<std::size_t>(bins));
    int max_lbl_w = 1;
    std::size_t max_count = 1;
    for (int b = 0; b < bins; ++b) {
        const double lo = (bins == 1) ? min_v : min_v + (max_v - min_v) * (static_cast<double>(b) / bins);
        const double hi = (bins == 1) ? max_v : min_v + (max_v - min_v) * (static_cast<double>(b + 1) / bins);
        const char right_br = (b + 1 == bins) ? ']' : ')';
        range_labels[static_cast<std::size_t>(b)] =
            "[" + detail::format_number(lo, opts.precision, false) + ", " +
            detail::format_number(hi, opts.precision, false) + right_br;
        max_lbl_w = std::max(max_lbl_w, detail::utf8_display_width(range_labels[static_cast<std::size_t>(b)]));
        max_count = std::max(max_count, counts[static_cast<std::size_t>(b)]);
    }

    const std::string fill_ch = opts.use_unicode ? "▇" : "#";
    const std::string axis_ch = opts.use_unicode ? "│" : "|";
    const int bar_budget = std::max(8, opts.max_bar_width);
    const int max_cnt_w = static_cast<int>(std::to_string(max_count).size());

    std::vector<std::string> pct_strs(static_cast<std::size_t>(bins));
    int max_pct_w = 1;
    for (int b = 0; b < bins; ++b) {
        const std::size_t cnt = counts[static_cast<std::size_t>(b)];
        const double pct = 100.0 * static_cast<double>(cnt) / static_cast<double>(vals.size());
        std::ostringstream pct_oss;
        pct_oss << std::fixed << std::setprecision(1) << pct << "%";
        pct_strs[static_cast<std::size_t>(b)] = pct_oss.str();
        max_pct_w = std::max(max_pct_w, static_cast<int>(pct_strs[static_cast<std::size_t>(b)].size()));
    }

    for (int b = 0; b < bins; ++b) {
        const std::size_t cnt = counts[static_cast<std::size_t>(b)];
        int len = static_cast<int>(
            std::llround((static_cast<double>(cnt) / static_cast<double>(max_count)) * bar_budget));
        if (cnt > 0 && len == 0) len = 1;
        const std::string bar = detail::repeat_str(fill_ch, len);
        const std::string pad(static_cast<std::size_t>(bar_budget - len), ' ');
        const std::string cnt_str = detail::pad_left_vis(std::to_string(cnt), max_cnt_w);
        const std::string pct_str = detail::pad_left_vis(pct_strs[static_cast<std::size_t>(b)], max_pct_w);

        out << detail::pad_right_vis(range_labels[static_cast<std::size_t>(b)], max_lbl_w)
            << " " << detail::ansi_dim(opts) << axis_ch << detail::ansi_reset(opts) << " "
            << detail::ansi_yellow(opts) << bar << detail::ansi_reset(opts) << pad
            << "  " << cnt_str << " (" << pct_str << ")\n";
    }
}

inline void render_histogram(
    const Table& table,
    const std::string& value_col,
    std::ostream& out,
    const RenderOptions& opts = {}) {
    render_histogram(table, detail::require_column_index(table, value_col), out, opts);
}

/// Renders a single-line sparkline trend with summary statistics (n, min, avg, max).
inline void render_sparkline(
    const Table& table,
    std::size_t value_col_idx,
    std::ostream& out,
    const RenderOptions& opts = {}) {

    detail::print_title(out, opts);
    if (value_col_idx >= table.col_count()) {
        throw std::out_of_range("render_sparkline: column index out of range");
    }

    std::vector<double> vals;
    vals.reserve(table.row_count());
    double sum = 0.0;
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        const Value& v = table.cell(r, value_col_idx);
        if (v.is_number()) {
            const double d = v.as_number();
            vals.push_back(d);
            sum += d;
        }
    }

    if (vals.empty()) {
        out << detail::ansi_dim(opts) << "(no numeric data for sparkline)" << detail::ansi_reset(opts) << "\n";
        return;
    }

    static const char* kUniLevels[8] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    static const char kAsciiLevels[9] = "._-:=+*#";

    const auto mm = std::minmax_element(vals.begin(), vals.end());
    const double min_v = *mm.first;
    const double max_v = *mm.second;
    const double avg_v = sum / static_cast<double>(vals.size());
    const double span = max_v - min_v;

    out << detail::ansi_cyan(opts);
    for (double v : vals) {
        int level = 3;
        if (span > 1e-12) {
            level = static_cast<int>(std::llround(((v - min_v) / span) * 7.0));
            if (level < 0) level = 0;
            if (level > 7) level = 7;
        }
        if (opts.use_unicode) {
            out << kUniLevels[level];
        } else {
            out << kAsciiLevels[level];
        }
    }
    out << detail::ansi_reset(opts)
        << "  (n=" << vals.size()
        << ", min=" << detail::format_number(min_v, opts.precision, false)
        << ", avg=" << detail::format_number(avg_v, opts.precision, false)
        << ", max=" << detail::format_number(max_v, opts.precision, false) << ")\n";
}

inline void render_sparkline(
    const Table& table,
    const std::string& value_col,
    std::ostream& out,
    const RenderOptions& opts = {}) {
    render_sparkline(table, detail::require_column_index(table, value_col), out, opts);
}

}  // namespace csvql
