#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "interface_layer.h"
#include "device_driver.h"

/* Test cases */
static int test_driver_initialization(void) {
    printf("\n=== Test: Driver Initialization ===\n");
    
    if (device_init() != DRIVER_OK) {
        printf("FAIL: Device initialization failed\n");
        return -1;
    }
    
    printf("PASS: Device initialized successfully\n");
    return 0;
}

static int test_device_operations(void) {
    printf("\n=== Test: Device Operations ===\n");
    
    /* Enable device */
    if (device_enable() != DRIVER_OK) {
        printf("FAIL: Device enable failed\n");
        return -1;
    }
    
    /* Write data */
    uint32_t test_data = 0x12345678;
    if (device_write_data(test_data) != DRIVER_OK) {
        printf("FAIL: Device write failed\n");
        return -1;
    }
    
    /* Read data */
    uint32_t read_data = 0;
    if (device_read_data(&read_data) != DRIVER_OK) {
        printf("FAIL: Device read failed\n");
        return -1;
    }
    
    printf("Written: 0x%x, Read: 0x%x\n", test_data, read_data);
    
    /* Get status */
    uint32_t status = device_get_status();
    printf("Device status: 0x%x\n", status);
    
    printf("PASS: Device operations completed\n");
    return 0;
}

static int test_interrupt_handling(void) {
    printf("\n=== Test: Interrupt Handling ===\n");
    
    /* Enable interrupts */
    device_irq_enable();
    
    /* Simulate interrupt */
    if (trigger_interrupt(1, 0x10) == 0) {
        printf("PASS: Interrupt triggered successfully\n");
    } else {
        printf("FAIL: Interrupt trigger failed\n");
        return -1;
    }
    
    /* Disable interrupts */
    device_irq_disable();
    
    return 0;
}

static int test_register_access(void) {
    printf("\n=== Test: Direct Register Access ===\n");
    
    /* Test direct register access through interface layer */
    uint32_t value = read_register(DEVICE_BASE_ADDR, 4);
    printf("Read register value: 0x%x\n", value);
    
    if (write_register(DEVICE_BASE_ADDR, 0xAABBCCDD, 4) == 0) {
        printf("PASS: Register write successful\n");
    } else {
        printf("FAIL: Register write failed\n");
        return -1;
    }
    
    return 0;
}

static int run_all_tests(void) {
    printf("Starting NewICD3 Interface Layer Tests...\n");
    
    int failures = 0;
    
    if (test_driver_initialization() != 0) failures++;
    if (test_device_operations() != 0) failures++;
    if (test_interrupt_handling() != 0) failures++;
    if (test_register_access() != 0) failures++;
    
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failures);
        return -1;
    }
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    printf("NewICD3 Universal IC Simulator\n");
    printf("==============================\n");
    
    /* Initialize interface layer */
    if (interface_layer_init() != 0) {
        printf("Failed to initialize interface layer\n");
        return EXIT_FAILURE;
    }
    
    /* Run tests */
    int result = run_all_tests();
    
    /* Cleanup */
    device_deinit();
    interface_layer_deinit();
    
    printf("\nSystem shutdown complete.\n");
    
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}