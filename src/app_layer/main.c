#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "interface_layer.h"
#include "device_driver.h"
#include "logging.h"

/* Test cases */
static int test_driver_initialization(void) {
    LOG_INFO("=== Test: Driver Initialization ===");
    
    if (device_init() != DRIVER_OK) {
        LOG_ERROR("Device initialization failed");
        return -1;
    }
    
    LOG_INFO("PASS: Device initialized successfully");
    return 0;
}

static int test_device_operations(void) {
    LOG_INFO("=== Test: Device Operations ===");
    
    /* Enable device */
    if (device_enable() != DRIVER_OK) {
        LOG_ERROR("Device enable failed");
        return -1;
    }
    
    /* Write data */
    uint32_t test_data = 0x12345678;
    if (device_write_data(test_data) != DRIVER_OK) {
        LOG_ERROR("Device write failed");
        return -1;
    }
    
    /* Read data */
    uint32_t read_data = 0;
    if (device_read_data(&read_data) != DRIVER_OK) {
        LOG_ERROR("Device read failed");
        return -1;
    }
    
    LOG_INFO("Written: 0x%x, Read: 0x%x", test_data, read_data);
    
    /* Get status */
    uint32_t status = device_get_status();
    LOG_INFO("Device status: 0x%x", status);
    
    LOG_INFO("PASS: Device operations completed");
    return 0;
}

static int test_interrupt_handling(void) {
    LOG_INFO("=== Test: Interrupt Handling ===");
    
    /* Enable interrupts */
    device_irq_enable();
    
    /* Simulate interrupt */
    if (trigger_interrupt(1, 0x10) == 0) {
        LOG_INFO("PASS: Interrupt triggered successfully");
    } else {
        LOG_ERROR("Interrupt trigger failed");
        return -1;
    }
    
    /* Disable interrupts */
    device_irq_disable();
    
    return 0;
}

static int test_register_access(void) {
    LOG_INFO("=== Test: Direct Register Access ===");
    
    /* Test direct register access through interface layer */
    uint32_t value = read_register(DEVICE_BASE_ADDR, 4);
    LOG_INFO("Read register value: 0x%x", value);
    
    if (write_register(DEVICE_BASE_ADDR, 0xAABBCCDD, 4) == 0) {
        LOG_INFO("PASS: Register write successful");
    } else {
        LOG_ERROR("Register write failed");
        return -1;
    }
    
    return 0;
}

static int run_all_tests(void) {
    LOG_INFO("Starting NewICD3 Interface Layer Tests...");
    
    int failures = 0;
    
    if (test_driver_initialization() != 0) failures++;
    if (test_device_operations() != 0) failures++;
    if (test_interrupt_handling() != 0) failures++;
    if (test_register_access() != 0) failures++;
    
    LOG_INFO("=== Test Summary ===");
    if (failures == 0) {
        LOG_INFO("All tests PASSED");
        return 0;
    } else {
        LOG_ERROR("%d test(s) FAILED", failures);
        return -1;
    }
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    LOG_INFO("NewICD3 Universal IC Simulator");
    LOG_INFO("==============================");
    
    /* Initialize interface layer */
    if (interface_layer_init() != 0) {
        LOG_ERROR("Failed to initialize interface layer");
        return EXIT_FAILURE;
    }
    
    /* Run tests */
    int result = run_all_tests();
    
    /* Cleanup */
    device_deinit();
    interface_layer_deinit();
    
    LOG_INFO("System shutdown complete.");
    
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}