# NewICD3 Universal IC Simulator Makefile
# ========================================
# 
# This Makefile builds the NewICD3 universal IC simulator with layered
# architecture supporting driver transparency and hardware simulation.
#
# Architecture:
#   Application Layer  → src/app_layer/
#   Driver Layer       → src/driver_layer/  
#   Interface Layer    → src/interface_layer/
#   Device Models      → src/device_models/ (Python)
#
# Usage:
#   make all              - Build all targets
#   make test             - Run unit tests
#   make integration-test - Run integration tests
#   make clean            - Clean build artifacts
#   make format          - Format source code
#   make help            - Show help message

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -O0 -D_GNU_SOURCE
INCLUDES = -Iinclude
LDFLAGS = 

# Code formatting
CLANG_FORMAT = clang-format
FORMAT_STYLE = -style="{BasedOnStyle: GNU, IndentWidth: 4, UseTab: Never}" 

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

# Source files
INTERFACE_SRCS = $(SRC_DIR)/interface_layer/driver_interface.c
LOGGING_SRCS = $(SRC_DIR)/interface_layer/logging.c
DRIVER_SRCS = $(SRC_DIR)/driver_layer/device_driver.c
APP_SRCS = $(SRC_DIR)/app_layer/main.c
TEST_SRCS = $(TEST_DIR)/test_interface_layer.c

# Legacy test files (consolidated for reference)
LEGACY_TEST_SRCS = $(TEST_DIR)/legacy/test_memset_issue.c \
                   $(TEST_DIR)/legacy/test_rep_stos.c \
                   $(TEST_DIR)/legacy/test_driver_memset.c

# Object files
INTERFACE_OBJS = $(BUILD_DIR)/driver_interface.o
LOGGING_OBJS = $(BUILD_DIR)/logging.o
DRIVER_OBJS = $(BUILD_DIR)/device_driver.o
APP_OBJS = $(BUILD_DIR)/main.o
TEST_OBJS = $(BUILD_DIR)/test_interface_layer.o

# Legacy test executables
LEGACY_TEST_TARGETS = $(BIN_DIR)/test_memset_issue \
                      $(BIN_DIR)/test_rep_stos \
                      $(BIN_DIR)/test_driver_memset

# Targets
MAIN_TARGET = $(BIN_DIR)/icd3_simulator
TEST_TARGET = $(BIN_DIR)/test_interface_layer

.PHONY: all clean test legacy-tests directories format check-format help

all: directories $(MAIN_TARGET) $(TEST_TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Main application
$(MAIN_TARGET): $(INTERFACE_OBJS) $(LOGGING_OBJS) $(DRIVER_OBJS) $(APP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Test executable
$(TEST_TARGET): $(INTERFACE_OBJS) $(LOGGING_OBJS) $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Object files
$(BUILD_DIR)/driver_interface.o: $(INTERFACE_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/logging.o: $(LOGGING_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/device_driver.o: $(DRIVER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/main.o: $(APP_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_interface_layer.o: $(TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Legacy test targets (optional - for compatibility)
legacy-tests: directories $(LEGACY_TEST_TARGETS)

$(BIN_DIR)/test_memset_issue: $(INTERFACE_OBJS) $(LOGGING_OBJS) $(BUILD_DIR)/test_memset_issue.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN_DIR)/test_rep_stos: $(INTERFACE_OBJS) $(LOGGING_OBJS) $(BUILD_DIR)/test_rep_stos.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN_DIR)/test_driver_memset: $(INTERFACE_OBJS) $(LOGGING_OBJS) $(BUILD_DIR)/test_driver_memset.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_memset_issue.o: $(TEST_DIR)/legacy/test_memset_issue.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_rep_stos.o: $(TEST_DIR)/legacy/test_rep_stos.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_driver_memset.o: $(TEST_DIR)/legacy/test_driver_memset.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Code formatting and quality
format:
	@echo "Formatting source code..."
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) $(TEST_DIR) -name "*.c" -o -name "*.h" | \
		xargs $(CLANG_FORMAT) $(FORMAT_STYLE) -i; \
		echo "Code formatting completed"; \
	else \
		echo "clang-format not found, skipping formatting"; \
	fi

check-format:
	@echo "Checking code formatting..."
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) $(TEST_DIR) -name "*.c" -o -name "*.h" | \
		xargs $(CLANG_FORMAT) $(FORMAT_STYLE) --dry-run --Werror; \
		echo "Code formatting check passed"; \
	else \
		echo "clang-format not found, skipping format check"; \
	fi

# Enhanced test targets with better output
test: $(TEST_TARGET)
	@echo "=========================================="
	@echo "Running NewICD3 Interface Layer Tests"
	@echo "=========================================="
	@$(TEST_TARGET)
	@echo "=========================================="
	@echo "All tests completed successfully!"
	@echo "=========================================="

integration-test: $(MAIN_TARGET)
	@echo "=========================================="
	@echo "Running NewICD3 Integration Tests"
	@echo "=========================================="
	@$(MAIN_TARGET)
	@echo "=========================================="
	@echo "Integration tests completed!"
	@echo "=========================================="

# Clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Help
help:
	@echo "NewICD3 Universal IC Simulator Build System"
	@echo "==========================================="
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build all targets (default)"
	@echo "  test             - Run unit tests with enhanced output"
	@echo "  legacy-tests     - Build legacy test executables (optional)"
	@echo "  integration-test - Run integration tests"
	@echo "  clean            - Clean build artifacts"
	@echo "  format           - Format source code with clang-format"
	@echo "  check-format     - Check code formatting compliance"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Architecture Overview:"
	@echo "  Application Layer    - src/app_layer/     - System initialization & test execution"
	@echo "  Driver Layer         - src/driver_layer/  - CMSIS-compliant device drivers"
	@echo "  Interface Layer      - src/interface_layer/ - Memory protection & signal handling"
	@echo "  Device Models        - src/device_models/ - Python hardware behavior simulation"
	@echo "  Tests                - tests/             - Comprehensive test suite"
	@echo "  Legacy Tests         - tests/legacy/      - Archived standalone test files"
	@echo ""
	@echo "Build artifacts:"
	@echo "  $(MAIN_TARGET) - Main simulator executable"
	@echo "  $(TEST_TARGET)  - Test suite executable"
	@echo "  Legacy test executables available via 'make legacy-tests'"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - GCC compiler"
	@echo "  - Python 3.x (for device models)"
	@echo "  - clang-format (optional, for code formatting)"