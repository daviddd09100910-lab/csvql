CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD_DIR := build

ifeq ($(OS),Windows_NT)
    EXE := .exe
    MKDIR_P = powershell -NoProfile -Command "New-Item -ItemType Directory -Force $(BUILD_DIR) | Out-Null"
    RM_RF = powershell -NoProfile -Command "if (Test-Path $(BUILD_DIR)) { Remove-Item -Recurse -Force $(BUILD_DIR) }"
else
    EXE :=
    MKDIR_P = mkdir -p $(BUILD_DIR)
    RM_RF = rm -rf $(BUILD_DIR)
endif

TARGETS := \
	$(BUILD_DIR)/test_parser_and_query$(EXE) \
	$(BUILD_DIR)/test_visualizer$(EXE) \
	$(BUILD_DIR)/basic_usage$(EXE) \
	$(BUILD_DIR)/csvql_demo$(EXE)

.PHONY: all test demo clean

all: $(TARGETS)

$(BUILD_DIR)/test_parser_and_query$(EXE): tests/test_parser_and_query.cpp include/csvql/*.hpp
	@$(MKDIR_P)
	$(CXX) $(CXXFLAGS) tests/test_parser_and_query.cpp -o $@

$(BUILD_DIR)/test_visualizer$(EXE): tests/test_visualizer.cpp include/csvql/*.hpp
	@$(MKDIR_P)
	$(CXX) $(CXXFLAGS) tests/test_visualizer.cpp -o $@

$(BUILD_DIR)/basic_usage$(EXE): examples/basic_usage.cpp include/csvql/*.hpp
	@$(MKDIR_P)
	$(CXX) $(CXXFLAGS) examples/basic_usage.cpp -o $@

$(BUILD_DIR)/csvql_demo$(EXE): examples/demo_cli.cpp include/csvql/*.hpp
	@$(MKDIR_P)
	$(CXX) $(CXXFLAGS) examples/demo_cli.cpp -o $@

test: $(BUILD_DIR)/test_parser_and_query$(EXE) $(BUILD_DIR)/test_visualizer$(EXE)
	./$(BUILD_DIR)/test_parser_and_query$(EXE)
	./$(BUILD_DIR)/test_visualizer$(EXE)

demo: $(BUILD_DIR)/csvql_demo$(EXE)
	./$(BUILD_DIR)/csvql_demo$(EXE) --demo

clean:
	$(RM_RF)
