#include <csvql/csvql.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string resolve_data_path(const std::string& path) {
    if (std::ifstream(path, std::ios::binary).good()) return path;
    std::string up = "../" + path;
    if (std::ifstream(up, std::ios::binary).good()) return up;
    return path;
}

std::string trim_copy(const std::string& s) {
    std::size_t a = 0;
    // Strip UTF-8 BOM if present on piped stdin lines
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        a = 3;
    }
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

int parse_positive_int(const std::string& tok, const std::string& label) {
    if (tok.empty()) {
        throw std::invalid_argument("Invalid " + label + ": expected a positive integer");
    }
    for (char ch : tok) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            throw std::invalid_argument("Invalid " + label + " '" + tok +
                                        "': expected a positive integer");
        }
    }
    long long val = 0;
    try {
        val = std::stoll(tok);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid " + label + " '" + tok + "': out of range");
    }
    if (val <= 0 || val > 1000000) {
        throw std::invalid_argument("Invalid " + label + " '" + tok +
                                    "': must be between 1 and 1000000");
    }
    return static_cast<int>(val);
}

std::string format_columns_list(const csvql::Table& t) {
    std::string out;
    for (std::size_t i = 0; i < t.col_count(); ++i) {
        if (i > 0) out += ", ";
        out += t.headers()[i];
    }
    return out;
}

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    bool in_quote = false;
    char quote_ch = '\0';
    bool has_tok = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quote) {
            if (c == quote_ch) {
                in_quote = false;
            } else {
                cur.push_back(c);
            }
        } else if (c == '\'' || c == '"') {
            in_quote = true;
            quote_ch = c;
            has_tok = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (has_tok) {
                parts.push_back(std::move(cur));
                cur.clear();
                has_tok = false;
            }
        } else {
            cur.push_back(c);
            has_tok = true;
        }
    }
    if (has_tok) {
        parts.push_back(std::move(cur));
    }
    return parts;
}

void print_section_banner(const std::string& text, const csvql::RenderOptions& opts) {
    const std::string bar = opts.use_unicode
        ? "────────────────────────────────────────────────────────────────────"
        : "--------------------------------------------------------------------";
    std::cout << "\n"
              << (opts.use_color ? "\033[1;36m" : "") << text
              << (opts.use_color ? "\033[0m" : "") << "\n"
              << (opts.use_color ? "\033[90m" : "") << bar
              << (opts.use_color ? "\033[0m" : "") << "\n";
}

void run_demo(csvql::RenderOptions opts) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    const std::string stocks_path = resolve_data_path("data/stocks.csv");
    const std::string sales_path = resolve_data_path("data/sales.csv");

    const csvql::Table stocks = csvql::load_csv(stocks_path);
    const csvql::Table sales = csvql::load_csv(sales_path);

    print_section_banner("[Part A] Fault-Tolerant CSV Load + Runtime SQL-like Query (stocks.csv)", opts);
    std::cout << "Loaded " << stocks.row_count() << " rows x " << stocks.col_count()
              << " cols (" << stocks.warnings().size()
              << " dirty 'N/A' cells safely coerced to NULL without throwing)\n";
    if (!stocks.warnings().empty()) {
        const auto& w = stocks.warnings().front();
        std::cout << "  Sample warning -> line " << w.line << ", col " << w.column
                  << ": " << w.message << "\n";
    }

    const std::string stock_sql =
        "SELECT sector, COUNT(*) AS tickers, COUNT(adr_pct) AS valid_adr, "
        "AVG(daily_return_pct) AS avg_ret, MAX(adr_pct) AS max_adr "
        "GROUP BY sector "
        "HAVING COUNT(*) >= 3 "
        "ORDER BY avg_ret DESC";
    std::cout << "SQL> SELECT sector, COUNT(*) AS tickers, COUNT(adr_pct) AS valid_adr,\n"
              << "            AVG(daily_return_pct) AS avg_ret, MAX(adr_pct) AS max_adr\n"
              << "     GROUP BY sector HAVING COUNT(*) >= 3 ORDER BY avg_ret DESC\n\n";

    const csvql::Table sector_summary = csvql::query(stocks, stock_sql);
    opts.title.clear();
    csvql::render_table(sector_summary, std::cout, opts);

    print_section_banner("[Part B] Diverging Bar Chart, Return Histogram & Cross-Domain Reuse", opts);
    csvql::RenderOptions chart_opts = opts;
    chart_opts.title = "1) Average Daily Return (%) by Sector";
    csvql::render_bar_chart(sector_summary, "sector", "avg_ret", std::cout, chart_opts);

    std::cout << "\n";
    chart_opts.title = "2) Daily Return Distribution across All Stocks (%)";
    chart_opts.histogram_bins = 5;
    csvql::render_histogram(stocks, "daily_return_pct", std::cout, chart_opts);

    std::cout << "\n";
    chart_opts.title = "3) Daily Return Sparkline";
    csvql::render_sparkline(stocks, "daily_return_pct", std::cout, chart_opts);

    std::cout << "\n";
    const std::string sales_sql =
        "SELECT category, SUM(units) AS units, SUM(revenue) AS total_rev, "
        "AVG(margin_pct) AS avg_margin "
        "GROUP BY category ORDER BY total_rev DESC";
    std::cout << "Cross-domain check (data/sales.csv):\n"
              << "SQL> SELECT category, SUM(units) AS units, SUM(revenue) AS total_rev,\n"
              << "            AVG(margin_pct) AS avg_margin GROUP BY category ORDER BY total_rev DESC\n\n";
    const csvql::Table sales_summary = csvql::query(sales, sales_sql);
    chart_opts.title = "4) E-Commerce Total Revenue by Category ($)";
    csvql::render_bar_chart(sales_summary, "category", "total_rev", std::cout, chart_opts);

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count() / 1000.0;
    std::cout << "\nCompleted full load + 2 SQL aggregations + 4 terminal visuals in "
              << std::fixed << std::setprecision(2) << elapsed_ms << " ms.\n"
              << "Tip: Run with --repl to ask custom SQL queries interactively.\n";
}

void print_repl_help() {
    std::cout
        << "REPL Commands:\n"
        << "  <SQL query>           Run SELECT ... WHERE ... GROUP BY ... HAVING ... ORDER BY ... LIMIT\n"
        << "  :schema               Show columns and row/warning counts of active CSV\n"
        << "  :head [n]             Preview first n rows (default 8) of active CSV\n"
        << "  :warnings             List CSV parse warnings (e.g., coerced N/A cells)\n"
        << "  :load <path>          Load a different CSV file\n"
        << "  :bar <label> <value>  Plot bar chart from the latest query/table\n"
        << "  :hist <value> [bins]  Plot histogram from the latest query/table\n"
        << "  :spark <value>        Plot sparkline from the latest query/table\n"
        << "  :reset                Reset plotting target back to full loaded CSV\n"
        << "  :ascii / :unicode     Switch box-drawing mode\n"
        << "  :color                Toggle ANSI color output\n"
        << "  :demo                 Run the 1-minute automated showcase\n"
        << "  :quit / :exit         Exit REPL\n";
}

void print_schema(const csvql::Table& t, const std::string& path, const csvql::RenderOptions& opts) {
    std::cout << "Source: " << path << " (" << t.row_count() << " rows, "
              << t.col_count() << " cols, " << t.warnings().size() << " parse warnings)\n";
    csvql::Table schema({"#", "column", "inferred_type", "nulls"});
    for (std::size_t c = 0; c < t.col_count(); ++c) {
        std::string kind = "empty";
        bool has_num = false;
        bool has_str = false;
        std::size_t null_cnt = 0;
        for (std::size_t r = 0; r < t.row_count(); ++r) {
            const auto& cell = t.cell(r, c);
            if (cell.is_null()) ++null_cnt;
            else if (cell.is_number()) has_num = true;
            else if (cell.is_string()) has_str = true;
        }
        if (has_num && !has_str) kind = "number";
        else if (has_str && !has_num) kind = "string";
        else if (has_num && has_str) kind = "mixed";
        schema.add_row({csvql::Value(c), csvql::Value(t.headers()[c]), csvql::Value(kind), csvql::Value(null_cnt)});
    }
    csvql::RenderOptions s_opts = opts;
    s_opts.title.clear();
    csvql::render_table(schema, std::cout, s_opts);
}

int run_repl(std::string initial_path, csvql::RenderOptions opts) {
    std::string active_path = resolve_data_path(initial_path);
    csvql::Table active_table;
    try {
        active_table = csvql::load_csv(active_path);
    } catch (const std::exception& ex) {
        std::cerr << "[error] " << ex.what() << "\n";
        return 1;
    }

    csvql::Table last_result = active_table;
    std::cout << "csvql interactive REPL (C++17 header-only engine)\n"
              << "Loaded '" << active_path << "': " << active_table.row_count() << " rows, "
              << active_table.col_count() << " cols, " << active_table.warnings().size() << " warnings.\n"
              << "Type a SQL-like query or ':help' for commands.\n";

    std::string line;
    while (true) {
        std::cout << "\ncsvql> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }
        const std::string cmd = trim_copy(line);
        if (cmd.empty()) {
            continue;
        }

        try {
            if (cmd[0] == ':') {
                const auto parts = split_ws(cmd);
                const std::string op = csvql::detail::to_lower_ascii(parts[0]);
                if (op == ":quit" || op == ":exit" || op == ":q") {
                    break;
                } else if (op == ":help" || op == ":h") {
                    print_repl_help();
                } else if (op == ":schema") {
                    print_schema(active_table, active_path, opts);
                } else if (op == ":head") {
                    std::size_t n = 8;
                    if (parts.size() >= 2) {
                        n = static_cast<std::size_t>(parse_positive_int(parts[1], "row count"));
                    }
                    last_result = csvql::query(active_table, "SELECT * LIMIT " + std::to_string(n));
                    csvql::render_table(last_result, std::cout, opts);
                } else if (op == ":warnings") {
                    if (active_table.warnings().empty()) {
                        std::cout << "No parse warnings.\n";
                    } else {
                        for (const auto& w : active_table.warnings()) {
                            std::cout << "  line " << w.line << ", col " << w.column
                                      << ": " << w.message << "\n";
                        }
                    }
                } else if (op == ":load") {
                    if (parts.size() < 2) {
                        std::cout << "Usage: :load <csv_path>\n";
                        continue;
                    }
                    const std::string next_path = resolve_data_path(parts[1]);
                    active_table = csvql::load_csv(next_path);
                    active_path = next_path;
                    last_result = active_table;
                    std::cout << "Loaded '" << active_path << "': " << active_table.row_count()
                              << " rows, " << active_table.col_count() << " cols, "
                              << active_table.warnings().size() << " warnings.\n";
                } else if (op == ":bar") {
                    if (parts.size() < 3) {
                        std::cout << "Usage: :bar <label_col> <value_col>\n";
                        continue;
                    }
                    csvql::RenderOptions b_opts = opts;
                    b_opts.title = parts[2] + " by " + parts[1];
                    try {
                        csvql::render_bar_chart(last_result, parts[1], parts[2], std::cout, b_opts);
                    } catch (const std::out_of_range&) {
                        throw;
                    } catch (const std::invalid_argument& ex) {
                        throw std::invalid_argument(
                            std::string(ex.what()) + "\n       Available columns in current result: [" +
                            format_columns_list(last_result) + "]. Run ':reset' or 'SELECT *' to reset.");
                    }
                } else if (op == ":hist") {
                    if (parts.size() < 2) {
                        std::cout << "Usage: :hist <value_col> [bins]\n";
                        continue;
                    }
                    csvql::RenderOptions h_opts = opts;
                    h_opts.title = "Histogram of " + parts[1];
                    if (parts.size() >= 3) {
                        h_opts.histogram_bins = parse_positive_int(parts[2], "bin count");
                    }
                    try {
                        csvql::render_histogram(last_result, parts[1], std::cout, h_opts);
                    } catch (const std::invalid_argument& ex) {
                        throw std::invalid_argument(
                            std::string(ex.what()) + "\n       Available columns in current result: [" +
                            format_columns_list(last_result) + "]. Run ':reset' or 'SELECT *' to reset.");
                    }
                } else if (op == ":spark") {
                    if (parts.size() < 2) {
                        std::cout << "Usage: :spark <value_col>\n";
                        continue;
                    }
                    csvql::RenderOptions s_opts = opts;
                    s_opts.title = "Sparkline of " + parts[1];
                    try {
                        csvql::render_sparkline(last_result, parts[1], std::cout, s_opts);
                    } catch (const std::invalid_argument& ex) {
                        throw std::invalid_argument(
                            std::string(ex.what()) + "\n       Available columns in current result: [" +
                            format_columns_list(last_result) + "]. Run ':reset' or 'SELECT *' to reset.");
                    }
                } else if (op == ":reset") {
                    last_result = active_table;
                    std::cout << "Reset plotting target to active table (" << active_table.row_count() << " rows).\n";
                } else if (op == ":ascii") {
                    opts.use_unicode = false;
                    std::cout << "Switched to ASCII rendering mode.\n";
                } else if (op == ":unicode") {
                    opts.use_unicode = true;
                    std::cout << "Switched to Unicode rendering mode.\n";
                } else if (op == ":color") {
                    opts.use_color = !opts.use_color;
                    std::cout << "ANSI color " << (opts.use_color ? "enabled" : "disabled") << ".\n";
                } else if (op == ":demo") {
                    run_demo(opts);
                } else {
                    std::cout << "Unknown command '" << op << "'. Type ':help' for commands.\n";
                }
            } else {
                const auto t0 = std::chrono::steady_clock::now();
                last_result = csvql::query(active_table, cmd);
                const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                csvql::render_table(last_result, std::cout, opts);
                std::cout << "Query time: " << std::fixed << std::setprecision(2)
                          << (static_cast<double>(us) / 1000.0) << " ms\n";
            }
        } catch (const std::exception& ex) {
            std::cout << (opts.use_color ? "\033[31m" : "")
                      << "[error] " << ex.what()
                      << (opts.use_color ? "\033[0m" : "") << "\n";
        }
    }
    return 0;
}

void print_cli_usage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  " << prog << " --demo [--ascii] [--no-color]\n"
        << "  " << prog << " --repl [csv_path] [--ascii] [--no-color]\n"
        << "  " << prog << " <csv_path> \"<sql_query>\" [--bar <label> <val>] [--hist <val>] [--spark <val>] [--ascii] [--no-color]\n";
}

}  // namespace

int main(int argc, char** argv) {
    csvql::enable_console_utf8();

    csvql::RenderOptions opts;
    bool mode_demo = false;
    bool mode_repl = false;
    std::string bar_label;
    std::string bar_value;
    std::string hist_col;
    std::string spark_col;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--demo") {
            mode_demo = true;
        } else if (arg == "--repl") {
            mode_repl = true;
        } else if (arg == "--ascii") {
            opts.use_unicode = false;
        } else if (arg == "--no-color") {
            opts.use_color = false;
        } else if (arg == "--help" || arg == "-h") {
            print_cli_usage(argv[0]);
            return 0;
        } else if (arg == "--bar" && i + 2 < argc) {
            bar_label = argv[++i];
            bar_value = argv[++i];
        } else if (arg == "--hist" && i + 1 < argc) {
            hist_col = argv[++i];
        } else if (arg == "--spark" && i + 1 < argc) {
            spark_col = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            print_cli_usage(argv[0]);
            return 1;
        } else {
            positional.push_back(arg);
        }
    }

    if (mode_demo || (argc == 1)) {
        run_demo(opts);
        return 0;
    }

    if (mode_repl) {
        const std::string path = positional.empty() ? "data/stocks.csv" : positional[0];
        return run_repl(path, opts);
    }

    if (positional.size() < 2) {
        print_cli_usage(argv[0]);
        return 1;
    }

    try {
        const csvql::Table src = csvql::load_csv(resolve_data_path(positional[0]));
        const csvql::Table res = csvql::query(src, positional[1]);
        csvql::render_table(res, std::cout, opts);

        if (!bar_label.empty()) {
            std::cout << "\n";
            csvql::RenderOptions b_opts = opts;
            b_opts.title = bar_value + " by " + bar_label;
            csvql::render_bar_chart(res, bar_label, bar_value, std::cout, b_opts);
        }
        if (!hist_col.empty()) {
            std::cout << "\n";
            csvql::RenderOptions h_opts = opts;
            h_opts.title = "Histogram of " + hist_col;
            csvql::render_histogram(res, hist_col, std::cout, h_opts);
        }
        if (!spark_col.empty()) {
            std::cout << "\n";
            csvql::RenderOptions s_opts = opts;
            s_opts.title = "Sparkline of " + spark_col;
            csvql::render_sparkline(res, spark_col, std::cout, s_opts);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[error] " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
