@echo off
setlocal enabledelayedexpansion

if not exist build mkdir build

set CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude

echo [1/4] Compiling unit tests and examples...
g++ %CXXFLAGS% tests\test_parser_and_query.cpp -o build\test_parser_and_query.exe || exit /b 1
g++ %CXXFLAGS% tests\test_visualizer.cpp -o build\test_visualizer.exe || exit /b 1
g++ %CXXFLAGS% examples\basic_usage.cpp -o build\basic_usage.exe || exit /b 1
g++ %CXXFLAGS% examples\demo_cli.cpp -o build\csvql_demo.exe || exit /b 1

echo.
echo [2/4] Running parser and query tests...
build\test_parser_and_query.exe || exit /b 1

echo.
echo [3/4] Running visualizer tests...
build\test_visualizer.exe || exit /b 1

echo.
echo [4/4] Running basic_usage smoke check...
build\basic_usage.exe || exit /b 1

echo.
echo All csvql build and test checks passed!
