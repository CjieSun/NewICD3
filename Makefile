# NewICD3 Universal IC Simulator Makefile
# ========================================

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -O0 -D_GNU_SOURCE
INCLUDES = -Iinclude
LDFLAGS = 

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

# Source files
INTERFACE_SRCS = $(SRC_DIR)/interface_layer/driver_interface.c
DRIVER_SRCS = $(SRC_DIR)/driver_layer/device_driver.c
UART_SRCS = $(SRC_DIR)/driver_layer/uart_driver.c
APP_SRCS = $(SRC_DIR)/app_layer/main.c
TEST_SRCS = $(TEST_DIR)/test_interface_layer.c
UART_TEST_SRCS = $(TEST_DIR)/test_uart_integration.c

# Object files
INTERFACE_OBJS = $(BUILD_DIR)/driver_interface.o
DRIVER_OBJS = $(BUILD_DIR)/device_driver.o
UART_OBJS = $(BUILD_DIR)/uart_driver.o
APP_OBJS = $(BUILD_DIR)/main.o
TEST_OBJS = $(BUILD_DIR)/test_interface_layer.o
UART_TEST_OBJS = $(BUILD_DIR)/test_uart_integration.o

# Targets
MAIN_TARGET = $(BIN_DIR)/icd3_simulator
TEST_TARGET = $(BIN_DIR)/test_interface_layer
UART_TEST_TARGET = $(BIN_DIR)/test_uart_integration

.PHONY: all clean test directories

all: directories $(MAIN_TARGET) $(TEST_TARGET) $(UART_TEST_TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Main application
$(MAIN_TARGET): $(INTERFACE_OBJS) $(DRIVER_OBJS) $(APP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Test executable
$(TEST_TARGET): $(INTERFACE_OBJS) $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# UART test executable
$(UART_TEST_TARGET): $(INTERFACE_OBJS) $(UART_OBJS) $(UART_TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Object files
$(BUILD_DIR)/driver_interface.o: $(INTERFACE_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/device_driver.o: $(DRIVER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/uart_driver.o: $(UART_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/main.o: $(APP_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_interface_layer.o: $(TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_uart_integration.o: $(UART_TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test targets
test: $(TEST_TARGET)
	@echo "Running interface layer tests..."
	@$(TEST_TARGET)

uart-test: $(UART_TEST_TARGET)
	@echo "Running UART integration tests..."
	@$(UART_TEST_TARGET)

integration-test: $(MAIN_TARGET)
	@echo "Running integration tests..."
	@$(MAIN_TARGET)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Help
help:
	@echo "NewICD3 Universal IC Simulator Build System"
	@echo "==========================================="
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build all targets"
	@echo "  test             - Run interface layer unit tests"
	@echo "  uart-test        - Run UART integration tests"
	@echo "  integration-test - Run integration tests"
	@echo "  clean            - Clean build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Architecture:"
	@echo "  Application Layer    - src/app_layer/"
	@echo "  Driver Layer         - src/driver_layer/"
	@echo "  Interface Layer      - src/interface_layer/"
	@echo "  Device Models        - src/device_models/ (future)"
	@echo "  Tests                - tests/"