cmake_minimum_required(VERSION 3.16)
project(csvql VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CSVQL_IS_TOP_LEVEL ON)
else()
    set(CSVQL_IS_TOP_LEVEL OFF)
endif()

option(CSVQL_BUILD_TESTS "Build csvql unit tests" ${CSVQL_IS_TOP_LEVEL})
option(CSVQL_BUILD_EXAMPLES "Build csvql examples and demo CLI" ${CSVQL_IS_TOP_LEVEL})

add_library(csvql INTERFACE)
add_library(csvql::csvql ALIAS csvql)

target_include_directories(csvql INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(csvql INTERFACE cxx_std_17)
target_compile_options(csvql INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

function(csvql_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

if(CSVQL_BUILD_TESTS)
    enable_testing()

    add_executable(test_parser_and_query tests/test_parser_and_query.cpp)
    target_link_libraries(test_parser_and_query PRIVATE csvql::csvql)
    csvql_apply_warnings(test_parser_and_query)
    add_test(NAME parser_and_query COMMAND test_parser_and_query)

    add_executable(test_visualizer tests/test_visualizer.cpp)
    target_link_libraries(test_visualizer PRIVATE csvql::csvql)
    csvql_apply_warnings(test_visualizer)
    add_test(NAME visualizer COMMAND test_visualizer)
endif()

if(CSVQL_BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE csvql::csvql)
    csvql_apply_warnings(basic_usage)

    add_executable(csvql_demo examples/demo_cli.cpp)
    target_link_libraries(csvql_demo PRIVATE csvql::csvql)
    csvql_apply_warnings(csvql_demo)
endif()
