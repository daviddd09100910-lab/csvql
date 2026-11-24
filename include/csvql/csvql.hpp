#pragma once

/// Umbrella header for the csvql header-only C++17 library.
/// Usage:
///   #include <csvql/csvql.hpp>

#include "table.hpp"
#include "csv_parser.hpp"
#include "query_engine.hpp"
#include "visualizer.hpp"

namespace csvql {

/// Loads a CSV file from disk and immediately runs a SQL-like query on it.
inline Table query_csv(
    const std::string& filepath,
    const std::string& sql,
    const CsvOptions& csv_opts = {}) {
    const Table table = load_csv(filepath, csv_opts);
    return query(table, sql);
}

}  // namespace csvql
