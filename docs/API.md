# `csvql` API & SQL Reference

Header-only C++17 namespace: `csvql` (`#include <csvql/csvql.hpp>`).

---

## 1. Data Model (`include/csvql/table.hpp`)

### `csvql::Value`

Stores one of three runtime states via `std::variant<std::monostate, double, std::string>`:

- **Null**: `Value::null()` (default constructed)
- **Number**: `Value::number(double)` (non-finite `NaN`/`Inf` automatically normalized to Null; `-0.0` normalized to `0.0`)
- **String**: `Value::text(std::string)`

Key methods:
- `bool is_null() const`, `bool is_number() const`, `bool is_string() const`
- `double as_number() const` (throws `std::runtime_error` if not a number)
- `const std::string& as_string() const` (throws `std::runtime_error` if not a string)
- `std::string to_string(int precision = 2, bool compact_int = true) const`

### `csvql::ParseWarning`

```cpp
struct ParseWarning {
    std::size_t line = 0;      // 1-based CSV record line
    std::size_t column = 0;    // 1-based column index
    std::string raw_text;
    std::string message;
};
```

### `csvql::Table`

```cpp
Table();
explicit Table(std::vector<std::string> headers);

void set_headers(std::vector<std::string> headers);
void add_row(std::vector<Value> row);  // throws std::invalid_argument on width mismatch

std::size_t row_count() const;
std::size_t col_count() const;
bool empty() const;

const std::vector<std::string>& headers() const;
const std::vector<std::vector<Value>>& rows() const;
const std::vector<ParseWarning>& warnings() const;

std::optional<std::size_t> column_index(const std::string& name) const;  // exact, then case-insensitive
bool has_column(const std::string& name) const;
const Value& cell(std::size_t row, std::size_t col) const;
const Value& cell(std::size_t row, const std::string& col_name) const;
```

---

## 2. Fault-Tolerant CSV Parser (`include/csvql/csv_parser.hpp`)

### `csvql::CsvOptions`

```cpp
struct CsvOptions {
    char delimiter = ',';
    bool has_header = true;
    bool trim_whitespace = true;
    bool treat_na_as_null = true;
};
```

### Functions

- `Table parse_csv_string(const std::string& content, const CsvOptions& opts = {})`
- `Table parse_csv(std::istream& in, const CsvOptions& opts = {})`
- `Table load_csv(const std::string& filepath, const CsvOptions& opts = {})` (throws `std::runtime_error` if the file cannot be opened)

### Guarantees

- Strips UTF-8 BOM (`0xEF 0xBB 0xBF`).
- Supports RFC-4180 quoted fields, embedded commas, multiline quoted values, `""` quote escaping, and `\r\n` / `\n` line endings.
- Coerces empty strings and NA markers (`N/A`, `NA`, `null`, `NULL`, `NaN`, `None`, `--`) to `Value::null()` when `treat_na_as_null == true`, recording non-empty markers in `table.warnings()`.
- Keeps `"0x10"`, `"inf"`, and `"10A"` strictly as `String` (never misparsed as hex floats, infinity, or truncated numbers).
- Pads short rows with `Null` and truncates extra columns while logging `ParseWarning`s.

---

## 3. SQL-like Query Engine (`include/csvql/query_engine.hpp`)

```cpp
class QueryError : public std::runtime_error;
Table query(const Table& table, const std::string& sql);
```

And convenience wrapper in `include/csvql/csvql.hpp`:

```cpp
Table query_csv(const std::string& filepath, const std::string& sql, const CsvOptions& csv_opts = {});
```

### Supported Grammar (Case-Insensitive Keywords)

```sql
SELECT (* | col | AGG(col|*) [AS alias]) [, ...]
[FROM table_or_filename]
[WHERE <bool_expr>]
[GROUP BY col [, ...]]
[HAVING <bool_expr>]
[ORDER BY (col | alias | AGG(col|*)) [ASC | DESC] [, ...]]
[LIMIT <non_negative_int>]
[;]
```

- **Aggregates**: `COUNT(*)`, `COUNT(col)` (ignores `NULL`), `SUM(col)`, `AVG(col)`, `MIN(col)`, `MAX(col)`.
- **Global Aggregation**: `SELECT COUNT(*), AVG(price)` works without `GROUP BY`; returns 1 row (`COUNT(*) = 0`, others `NULL`) if 0 rows pass `WHERE`.
- **Boolean Operators & Precedence**: `(...)` > `NOT` > `AND` > `OR`.
- **Predicates**: `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `CONTAINS` (case-insensitive substring match), `IS NULL`, `IS NOT NULL`.
- **Literals**: numbers (including signed `-2.5` / `+1.0` / scientific `1e-3`), single- or double-quoted strings (`'Tech'`, `"Tech"`), `NULL`. Backticked identifiers (`` `col name` ``) are supported for spaced column names.
- **Sorting**: `std::stable_sort`; `NULL` values always sort last.

---

## 4. Terminal Visualizer (`include/csvql/visualizer.hpp`)

### `csvql::RenderOptions`

```cpp
struct RenderOptions {
    bool use_unicode = true;   // false falls back strictly to 7-bit ASCII
    bool use_color = true;     // false omits ANSI escape sequences
    int max_bar_width = 32;
    int precision = 2;
    int histogram_bins = 6;
    int max_cell_width = 40;   // <= 0 disables truncation
    std::string title;
};
```

### Functions

- `void enable_console_utf8()` — configures UTF-8 codepage and VT ANSI processing on Windows consoles (no-op on POSIX).
- `void render_table(const Table& table, std::ostream& out, const RenderOptions& opts = {})`
- `void render_bar_chart(const Table& table, const std::string& label_col, const std::string& value_col, std::ostream& out, const RenderOptions& opts = {})` (plus `std::size_t` column index overload)
- `void render_histogram(const Table& table, const std::string& value_col, std::ostream& out, const RenderOptions& opts = {})` (plus `std::size_t` overload)
- `void render_sparkline(const Table& table, const std::string& value_col, std::ostream& out, const RenderOptions& opts = {})` (plus `std::size_t` overload)
