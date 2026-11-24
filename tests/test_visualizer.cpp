#include <csvql/csvql.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check_impl(bool cond, const char* expr, int line) {
    if (!cond) {
        std::ostringstream oss;
        oss << "Assertion failed at line " << line << ": " << expr;
        throw std::runtime_error(oss.str());
    }
}

#define ASSERT_TRUE(cond) check_impl(static_cast<bool>(cond), #cond, __LINE__)
#define ASSERT_EQ(a, b) check_impl((a) == (b), #a " == " #b, __LINE__)

template <typename Fn>
void run_test(const char* name, Fn&& fn) {
    try {
        fn();
        ++g_passed;
        std::cout << "  [PASS] " << name << "\n";
    } catch (const std::exception& ex) {
        ++g_failed;
        std::cout << "  [FAIL] " << name << " -> " << ex.what() << "\n";
    }
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}

void test_table_unicode_alignment_and_nulls() {
    csvql::Table t({"sector", "count", "avg_ret"});
    t.add_row({"Tech", 6, 2.03});
    t.add_row({"Energy", 5, -1.5});
    t.add_row({"Finance", 5, csvql::Value::null()});

    csvql::RenderOptions opts;
    opts.use_unicode = true;
    opts.use_color = false;

    std::ostringstream oss;
    csvql::render_table(t, oss, opts);
    const auto lines = split_lines(oss.str());
    // 1 top + 1 header + 1 sep + 3 rows + 1 bottom + 1 footer = 8 lines
    ASSERT_EQ(lines.size(), 8u);

    const int expected_w = csvql::detail::utf8_display_width(lines[0]);
    for (std::size_t i = 0; i < 7; ++i) {
        ASSERT_EQ(csvql::detail::utf8_display_width(lines[i]), expected_w);
    }
    ASSERT_TRUE(lines[5].find("NULL") != std::string::npos);
    ASSERT_TRUE(lines[7] == "(3 rows)");
}

void test_table_ascii_mode_strict_ascii() {
    csvql::Table t({"name", "val"});
    t.add_row({"Alpha", 10});
    t.add_row({"Multi\nLine", -0.0});

    csvql::RenderOptions opts;
    opts.use_unicode = false;
    opts.use_color = false;

    std::ostringstream oss;
    csvql::render_table(t, oss, opts);
    const std::string out = oss.str();

    for (unsigned char c : out) {
        ASSERT_TRUE(c < 0x80);  // Strictly 7-bit ASCII
    }
    ASSERT_TRUE(out.find("-0") == std::string::npos);
    ASSERT_TRUE(out.find("Multi Line") != std::string::npos);
}

void test_diverging_and_positive_bar_charts() {
    csvql::Table t({"sector", "avg_ret"});
    t.add_row({"Tech", 2.0});
    t.add_row({"Flat", 0.0});
    t.add_row({"Energy", -1.5});

    csvql::RenderOptions opts;
    opts.use_unicode = false;
    opts.use_color = false;
    opts.max_bar_width = 20;

    std::ostringstream oss;
    csvql::render_bar_chart(t, "sector", "avg_ret", oss, opts);
    const auto lines = split_lines(oss.str());
    ASSERT_EQ(lines.size(), 3u);

    // Zero axis '|' must align at the exact same column on all three rows.
    const std::size_t axis0 = lines[0].find('|');
    const std::size_t axis1 = lines[1].find('|');
    const std::size_t axis2 = lines[2].find('|');
    ASSERT_TRUE(axis0 != std::string::npos);
    ASSERT_EQ(axis0, axis1);
    ASSERT_EQ(axis1, axis2);

    ASSERT_TRUE(lines[0].find("+2.00") != std::string::npos);
    ASSERT_TRUE(lines[2].find("-1.50") != std::string::npos);

    // All-zero guard (no division by zero)
    csvql::Table zeros({"k", "v"});
    zeros.add_row({"A", 0});
    zeros.add_row({"B", 0});
    std::ostringstream oss_zero;
    csvql::render_bar_chart(zeros, "k", "v", oss_zero, opts);
    ASSERT_TRUE(oss_zero.str().find("A") != std::string::npos);

    // Regression test (v0.2): mixed digit widths align at right edge; all-negative mode works
    csvql::Table neg({"k", "v"});
    neg.add_row({"BigLoss", -12.5});
    neg.add_row({"SmallLoss", -1.25});
    std::ostringstream oss_neg;
    csvql::render_bar_chart(neg, "k", "v", oss_neg, opts);
    const auto neg_lines = split_lines(oss_neg.str());
    ASSERT_EQ(neg_lines.size(), 2u);
    ASSERT_EQ(neg_lines[0].size(), neg_lines[1].size());
}

void test_histogram_and_sparkline_edge_cases() {
    csvql::Table t({"x"});
    for (int i = 0; i < 10; ++i) t.add_row({1.0});
    for (double v : {3.0, 5.0, 6.0}) {
        t.add_row({v});
    }

    csvql::RenderOptions opts;
    opts.use_unicode = false;
    opts.use_color = false;
    opts.histogram_bins = 3;

    std::ostringstream h_oss;
    csvql::render_histogram(t, "x", h_oss, opts);
    const auto h_lines = split_lines(h_oss.str());
    ASSERT_EQ(h_lines.size(), 3u);
    // Regression test (v0.2): 2-digit and 1-digit counts align vertically at ')'
    ASSERT_EQ(h_lines[0].size(), h_lines[1].size());
    ASSERT_EQ(h_lines[1].size(), h_lines[2].size());

    std::ostringstream s_oss;
    csvql::render_sparkline(t, "x", s_oss, opts);
    ASSERT_TRUE(s_oss.str().find("n=13") != std::string::npos);
    ASSERT_TRUE(s_oss.str().find("min=1.00") != std::string::npos);
    ASSERT_TRUE(s_oss.str().find("max=6.00") != std::string::npos);

    // Constant values (min == max) must not divide by zero
    csvql::Table constant({"x"});
    constant.add_row({5.0});
    constant.add_row({5.0});
    std::ostringstream c_oss;
    csvql::render_histogram(constant, "x", c_oss, opts);
    ASSERT_EQ(split_lines(c_oss.str()).size(), 1u);
    ASSERT_TRUE(c_oss.str().find("2 (100.0%)") != std::string::npos);

    // Empty / non-numeric table must render friendly message
    csvql::Table empty({"x"});
    std::ostringstream e_oss;
    csvql::render_histogram(empty, "x", e_oss, opts);
    ASSERT_TRUE(e_oss.str().find("no numeric data") != std::string::npos);
}

void test_v02_table_zero_rows_cjk_and_all_negative_bars() {
    // 1. 0-row table omits double inner separator (top, header, bottom, footer = 4 lines)
    csvql::Table zero_rows({"ticker", "price"});
    csvql::RenderOptions uopts;
    uopts.use_unicode = true;
    uopts.use_color = false;

    std::ostringstream z_oss;
    csvql::render_table(zero_rows, z_oss, uopts);
    const auto z_lines = split_lines(z_oss.str());
    ASSERT_EQ(z_lines.size(), 4u);
    ASSERT_TRUE(z_oss.str().find("├") == std::string::npos);
    ASSERT_EQ(z_lines[3], "(0 rows)");

    // 2. CJK multi-byte wide characters align vertical borders accurately
    csvql::Table cjk({"地區", "金額"});
    cjk.add_row({"台北", 1200});
    cjk.add_row({"HK", 900});
    std::ostringstream cjk_oss;
    csvql::render_table(cjk, cjk_oss, uopts);
    const auto cjk_lines = split_lines(cjk_oss.str());
    const int box_w = csvql::detail::utf8_display_width(cjk_lines[0]);
    for (std::size_t i = 0; i < 6; ++i) {
        ASSERT_EQ(csvql::detail::utf8_display_width(cjk_lines[i]), box_w);
    }

    // 3. All-negative diverging bar chart places zero-axis on right and bars to the left
    csvql::Table neg({"ticker", "ret"});
    neg.add_row({"SLB", -2.65});
    neg.add_row({"XOM", -1.45});
    csvql::RenderOptions aopts;
    aopts.use_unicode = false;
    aopts.use_color = false;
    aopts.max_bar_width = 12;

    std::ostringstream n_oss;
    csvql::render_bar_chart(neg, "ticker", "ret", n_oss, aopts);
    const auto n_lines = split_lines(n_oss.str());
    ASSERT_EQ(n_lines.size(), 2u);
    ASSERT_EQ(n_lines[0].find('|'), n_lines[1].find('|'));
    ASSERT_TRUE(n_lines[0].find('#') < n_lines[0].find('|'));
}

}  // namespace

int main() {
    std::cout << "Running test_visualizer...\n";
    run_test("table_unicode_alignment_and_nulls", test_table_unicode_alignment_and_nulls);
    run_test("table_ascii_mode_strict_ascii", test_table_ascii_mode_strict_ascii);
    run_test("diverging_and_positive_bar_charts", test_diverging_and_positive_bar_charts);
    run_test("histogram_and_sparkline_edge_cases", test_histogram_and_sparkline_edge_cases);
    run_test("v02_table_zero_rows_cjk_and_all_negative_bars", test_v02_table_zero_rows_cjk_and_all_negative_bars);

    std::cout << "Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
