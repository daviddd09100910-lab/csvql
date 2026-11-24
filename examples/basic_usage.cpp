#include <csvql/csvql.hpp>

#include <fstream>
#include <iostream>
#include <string>

static std::string resolve_sample(const std::string& rel) {
    if (std::ifstream(rel).good()) return rel;
    std::string up = "../" + rel;
    if (std::ifstream(up).good()) return up;
    return rel;
}

int main() {
    csvql::enable_console_utf8();

    // 1. Load CSV with automatic fault-tolerant NULL handling ("N/A" -> NULL)
    const csvql::Table stocks = csvql::load_csv(resolve_sample("data/stocks.csv"));

    // 2. Run a runtime SQL-like aggregation query (zero recompilation needed)
    const csvql::Table summary = csvql::query(
        stocks,
        "SELECT sector, COUNT(*) AS tickers, AVG(daily_return_pct) AS avg_ret, MAX(adr_pct) AS max_adr "
        "GROUP BY sector "
        "HAVING COUNT(*) >= 3 "
        "ORDER BY avg_ret DESC");

    // 3. Render both a formatted terminal table and a diverging zero-axis bar chart
    csvql::RenderOptions opts;
    opts.title = "Sector Summary (via csvql)";
    csvql::render_table(summary, std::cout, opts);

    std::cout << "\n";
    opts.title = "Average Daily Return (%) by Sector";
    csvql::render_bar_chart(summary, "sector", "avg_ret", std::cout, opts);
    return 0;
}
