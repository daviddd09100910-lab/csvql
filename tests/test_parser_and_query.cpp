#include <csvql/csv_parser.hpp>
#include <csvql/query_engine.hpp>
#include <csvql/table.hpp>

#include <cmath>
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
#define ASSERT_NEAR(a, b, eps) check_impl(std::abs((a) - (b)) <= (eps), #a " ~= " #b, __LINE__)
#define ASSERT_THROWS(stmt, ExType)                                                         \
    do {                                                                                    \
        bool caught = false;                                                                \
        try {                                                                               \
            stmt;                                                                           \
        } catch (const ExType&) {                                                           \
            caught = true;                                                                  \
        }                                                                                   \
        if (!caught) {                                                                      \
            throw std::runtime_error(std::string("Expected exception " #ExType " at line ") + \
                                     std::to_string(__LINE__));                             \
        }                                                                                   \
    } while (false)

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

void test_rfc4180_and_bom() {
    const std::string csv =
        "\xEF\xBB\xBF"
        "id,name,note,score\r\n"
        "1,\"Alice, A.\",\"Line1\nLine2\",95.5\r\n"
        "2,\"Bob \"\"The Builder\"\"\",clean,100\r\n"
        "3,Charlie,0x10,inf\r\n";

    const csvql::Table t = csvql::parse_csv_string(csv);
    ASSERT_EQ(t.col_count(), 4u);
    ASSERT_EQ(t.row_count(), 3u);
    ASSERT_EQ(t.headers()[0], "id");
    ASSERT_EQ(t.cell(0, "name").as_string(), "Alice, A.");
    ASSERT_EQ(t.cell(0, "note").as_string(), "Line1\nLine2");
    ASSERT_NEAR(t.cell(0, "score").as_number(), 95.5, 1e-9);
    ASSERT_EQ(t.cell(1, "name").as_string(), "Bob \"The Builder\"");
    ASSERT_TRUE(t.cell(2, "note").is_string());   // "0x10" must stay string, not hex float
    ASSERT_TRUE(t.cell(2, "score").is_string());  // "inf" must stay string, not infinity
}

void test_dirty_data_fault_tolerance() {
    const std::string csv =
        "ticker,sector,price,adr\n"
        "NVDA,Tech,140.5,4.1\n"
        "BLK,Finance,N/A,1.5\n"
        "SHORT,Tech,99.0\n"
        "EXTRA,Tech,50.0,2.0,junk_col\n"
        "BADNUM,Energy,10A,NaN\n";

    const csvql::Table t = csvql::parse_csv_string(csv);
    ASSERT_EQ(t.row_count(), 5u);
    ASSERT_TRUE(t.cell(1, "price").is_null());
    ASSERT_TRUE(t.cell(2, "adr").is_null());
    ASSERT_NEAR(t.cell(3, "adr").as_number(), 2.0, 1e-9);
    ASSERT_TRUE(t.cell(4, "price").is_string());  // "10A" is not silently truncated to 10
    ASSERT_TRUE(t.cell(4, "adr").is_null());
    ASSERT_TRUE(t.warnings().size() >= 4u);
}

void test_where_boolean_precedence_and_operators() {
    const std::string csv =
        "ticker,sector,price,ret\n"
        "NVDA,Tech,140,3.5\n"
        "AAPL,Tech,230,0.9\n"
        "XOM,Energy,115,-1.8\n"
        "SLB,Energy,44,-2.4\n"
        "BLK,Finance,990,N/A\n";
    const csvql::Table t = csvql::parse_csv_string(csv);

    // Parentheses override AND > OR precedence
    const csvql::Table r1 = csvql::query(
        t,
        "SELECT ticker, ret FROM stocks "
        "WHERE (sector = 'Tech' OR sector = 'Energy') AND ret < -1.0 "
        "ORDER BY ret ASC;");
    ASSERT_EQ(r1.row_count(), 2u);
    ASSERT_EQ(r1.cell(0, 0).as_string(), "SLB");
    ASSERT_EQ(r1.cell(1, 0).as_string(), "XOM");

    // NOT, CONTAINS, IS NULL, IS NOT NULL
    const csvql::Table r2 = csvql::query(
        t, "SELECT ticker WHERE ret IS NULL");
    ASSERT_EQ(r2.row_count(), 1u);
    ASSERT_EQ(r2.cell(0, 0).as_string(), "BLK");

    const csvql::Table r3 = csvql::query(
        t, "SELECT ticker WHERE sector CONTAINS 'fin' AND NOT ret IS NULL");
    ASSERT_EQ(r3.row_count(), 0u);

    const csvql::Table r4 = csvql::query(
        t, "SELECT ticker WHERE sector CONTAINS 'ech' AND price >= 140 ORDER BY price DESC LIMIT 1");
    ASSERT_EQ(r4.row_count(), 1u);
    ASSERT_EQ(r4.cell(0, 0).as_string(), "AAPL");
}

void test_order_by_unprojected_column_and_nulls_last() {
    const std::string csv =
        "ticker,price,ret\n"
        "A,100,1.0\n"
        "B,300,N/A\n"
        "C,200,-2.0\n";
    const csvql::Table t = csvql::parse_csv_string(csv);

    const csvql::Table r1 = csvql::query(t, "SELECT ticker ORDER BY price DESC");
    ASSERT_EQ(r1.col_count(), 1u);
    ASSERT_EQ(r1.cell(0, 0).as_string(), "B");
    ASSERT_EQ(r1.cell(1, 0).as_string(), "C");
    ASSERT_EQ(r1.cell(2, 0).as_string(), "A");

    const csvql::Table r2 = csvql::query(t, "SELECT ticker, ret ORDER BY ret DESC");
    ASSERT_EQ(r2.cell(0, 0).as_string(), "A");
    ASSERT_EQ(r2.cell(1, 0).as_string(), "C");
    ASSERT_EQ(r2.cell(2, 0).as_string(), "B");
    ASSERT_TRUE(r2.cell(2, 1).is_null());
}

void test_group_by_aggregates_and_having() {
    const std::string csv =
        "ticker,sector,price,ret\n"
        "NVDA,Tech,140,3.0\n"
        "MSFT,Tech,420,1.0\n"
        "XOM,Energy,110,-2.0\n"
        "CVX,Energy,150,-1.0\n"
        "SLB,Energy,40,N/A\n"
        "BLK,Finance,990,0.5\n";
    const csvql::Table t = csvql::parse_csv_string(csv);

    const csvql::Table r = csvql::query(
        t,
        "SELECT sector, COUNT(*) AS n, COUNT(ret) AS valid_ret, "
        "SUM(price) AS tot_p, AVG(ret) AS avg_ret, MIN(price) AS min_p, MAX(price) AS max_p "
        "GROUP BY sector "
        "HAVING COUNT(*) >= 2 AND avg_ret > 0 "
        "ORDER BY AVG(ret) DESC");

    ASSERT_EQ(r.row_count(), 1u);
    ASSERT_EQ(r.cell(0, "sector").as_string(), "Tech");
    ASSERT_NEAR(r.cell(0, "n").as_number(), 2.0, 1e-9);
    ASSERT_NEAR(r.cell(0, "valid_ret").as_number(), 2.0, 1e-9);
    ASSERT_NEAR(r.cell(0, "tot_p").as_number(), 560.0, 1e-9);
    ASSERT_NEAR(r.cell(0, "avg_ret").as_number(), 2.0, 1e-9);
    ASSERT_NEAR(r.cell(0, "min_p").as_number(), 140.0, 1e-9);
    ASSERT_NEAR(r.cell(0, "max_p").as_number(), 420.0, 1e-9);

    // Check COUNT(ret) ignoring NULL on Energy
    const csvql::Table energy = csvql::query(
        t,
        "SELECT sector, COUNT(*) AS n, COUNT(ret) AS valid_ret, AVG(ret) AS avg_ret "
        "WHERE sector = 'Energy' GROUP BY sector");
    ASSERT_EQ(energy.row_count(), 1u);
    ASSERT_NEAR(energy.cell(0, "n").as_number(), 3.0, 1e-9);
    ASSERT_NEAR(energy.cell(0, "valid_ret").as_number(), 2.0, 1e-9);
    ASSERT_NEAR(energy.cell(0, "avg_ret").as_number(), -1.5, 1e-9);
}

void test_global_aggregation_including_zero_rows() {
    const std::string csv =
        "ticker,price\n"
        "A,10\n"
        "B,20\n";
    const csvql::Table t = csvql::parse_csv_string(csv);

    const csvql::Table all_agg = csvql::query(t, "SELECT COUNT(*) AS cnt, AVG(price) AS avg_p");
    ASSERT_EQ(all_agg.row_count(), 1u);
    ASSERT_NEAR(all_agg.cell(0, "cnt").as_number(), 2.0, 1e-9);
    ASSERT_NEAR(all_agg.cell(0, "avg_p").as_number(), 15.0, 1e-9);

    const csvql::Table empty_agg = csvql::query(
        t, "SELECT COUNT(*) AS cnt, AVG(price) AS avg_p WHERE price > 1000");
    ASSERT_EQ(empty_agg.row_count(), 1u);
    ASSERT_NEAR(empty_agg.cell(0, "cnt").as_number(), 0.0, 1e-9);
    ASSERT_TRUE(empty_agg.cell(0, "avg_p").is_null());
}

void test_query_errors() {
    const csvql::Table t = csvql::parse_csv_string("a,b\n1,text\n");

    ASSERT_THROWS(csvql::query(t, "SELEC *"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT missing_col"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT * WHERE COUNT(*) > 1"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT a, COUNT(*)"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT SUM(b)"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT * HAVING a > 0"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT * WHERE a = 'unterminated"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT * LIMIT -1"), csvql::QueryError);
    ASSERT_THROWS(csvql::query(t, "SELECT * extra_token"), csvql::QueryError);

    // Regression tests (v0.2): BOM-prefixed SQL and ORDER BY / HAVING error detection on <= 1 groups
    const csvql::Table bom_sql = csvql::query(t, "\xEF\xBB\xBFSELECT a FROM t");
    ASSERT_EQ(bom_sql.row_count(), 1u);
    ASSERT_THROWS(
        csvql::query(t, "SELECT b, COUNT(*) GROUP BY b HAVING COUNT(*) > 99 ORDER BY bad_col"),
        csvql::QueryError);
    ASSERT_THROWS(
        csvql::query(t, "SELECT b, COUNT(*) GROUP BY b ORDER BY bad_col"),
        csvql::QueryError);
    ASSERT_THROWS(
        csvql::query(t, "SELECT b, COUNT(*) WHERE a > 999 GROUP BY b HAVING bad_col > 0"),
        csvql::QueryError);

    // Regression tests (v0.2+): dotted FROM table name, multi-column ORDER BY, and empty 0-col table
    const csvql::Table multi = csvql::parse_csv_string("g,v\nB,10\nA,20\nA,30\n");
    const csvql::Table from_res = csvql::query(
        multi, "SELECT g, v FROM stocks.csv ORDER BY g ASC, v DESC");
    ASSERT_EQ(from_res.row_count(), 3u);
    ASSERT_EQ(from_res.cell(0, 0).as_string(), "A");
    ASSERT_NEAR(from_res.cell(0, 1).as_number(), 30.0, 1e-9);
    ASSERT_NEAR(from_res.cell(1, 1).as_number(), 20.0, 1e-9);
    csvql::Table empty_tbl;
    ASSERT_THROWS(csvql::query(empty_tbl, "SELECT *"), csvql::QueryError);
}

}  // namespace

int main() {
    std::cout << "Running test_parser_and_query...\n";
    run_test("rfc4180_and_bom", test_rfc4180_and_bom);
    run_test("dirty_data_fault_tolerance", test_dirty_data_fault_tolerance);
    run_test("where_boolean_precedence_and_operators", test_where_boolean_precedence_and_operators);
    run_test("order_by_unprojected_column_and_nulls_last", test_order_by_unprojected_column_and_nulls_last);
    run_test("group_by_aggregates_and_having", test_group_by_aggregates_and_having);
    run_test("global_aggregation_including_zero_rows", test_global_aggregation_including_zero_rows);
    run_test("query_errors", test_query_errors);

    std::cout << "Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
