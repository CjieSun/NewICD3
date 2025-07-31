#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "interface_layer.h"
#include "uart_driver.h"

/* Simple test framework */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static int test_##name(void); \
    static void run_test_##name(void) { \
        printf("Running test: %s\n", #name); \
        tests_run++; \
        if (test_##name() == 0) { \
            tests_passed++; \
            printf("  PASS\n"); \
        } else { \
            printf("  FAIL\n"); \
        } \
    } \
    static int test_##name(void)

#define RUN_TEST(name) run_test_##name()

TEST(uart_initialization) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    printf("  UART initialized successfully\n");
    
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

TEST(uart_configuration) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    /* Configure UART */
    if (uart_configure(115200) != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Enable UART */
    if (uart_enable() != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Check status */
    uint32_t status = uart_get_status();
    printf("  UART status after enable: 0x%x\n", status);
    
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

TEST(uart_transmission) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    if (uart_enable() != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Test single byte transmission */
    printf("  Testing single byte transmission...\n");
    if (uart_transmit(0x48) != UART_OK) {  /* 'H' */
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Test string transmission */
    printf("  Testing string transmission...\n");
    if (uart_transmit_string("Hello") != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

TEST(uart_interrupt_handling) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    if (uart_enable() != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Enable interrupts */
    uart_irq_enable();
    
    /* Simulate interrupt trigger (normally would come from model) */
    printf("  Simulating UART interrupt from model...\n");
    if (trigger_interrupt(1, 0x01) == 0) {
        printf("  UART RX interrupt processed\n");
    }
    
    if (trigger_interrupt(1, 0x02) == 0) {
        printf("  UART TX interrupt processed\n");
    }
    
    uart_irq_disable();
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

TEST(uart_bare_address_access) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    if (uart_enable() != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    printf("  Testing direct UART register access (triggers segv_handler)...\n");
    
    /* Get the mapped memory address for direct access */
    volatile uint32_t *mapped_addr = (volatile uint32_t *)get_device_mapped_memory(1);
    
    if (mapped_addr) {
        /* Direct access to UART control register */
        printf("  Performing direct write to UART CTRL register...\n");
        *(volatile uint32_t *)mapped_addr = 0x0F;  /* Enable all UART features */
        
        printf("  Performing direct read from UART STATUS register...\n");
        volatile uint32_t status = *(volatile uint32_t *)(mapped_addr + 1);  /* STATUS register offset */
        printf("  Direct read status: 0x%x\n", status);
        
        printf("  Direct UART register access completed\n");
    } else {
        printf("  Could not get mapped address for UART device\n");
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

TEST(uart_model_integration) {
    /* This test checks the integration infrastructure */
    /* The actual Python model should be started separately */
    
    printf("  Testing UART model integration infrastructure...\n");
    
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (uart_init() != UART_OK) {
        interface_layer_deinit();
        return -1;
    }
    
    if (uart_enable() != UART_OK) {
        uart_deinit();
        interface_layer_deinit();
        return -1;
    }
    
    /* Test communication with model (will timeout if model not running) */
    printf("  Attempting to communicate with UART model...\n");
    uint32_t status = uart_get_status();
    printf("  UART status from model: 0x%x\n", status);
    
    /* Test model interrupt handling */
    printf("  Testing model interrupt handling...\n");
    if (handle_model_interrupts() == 0) {
        printf("  Model interrupt handling available\n");
    }
    
    uart_deinit();
    interface_layer_deinit();
    return 0;
}

int main(void) {
    printf("NewICD3 UART Integration Tests\n");
    printf("=============================\n\n");
    
    RUN_TEST(uart_initialization);
    RUN_TEST(uart_configuration);
    RUN_TEST(uart_transmission);
    RUN_TEST(uart_interrupt_handling);
    RUN_TEST(uart_bare_address_access);
    RUN_TEST(uart_model_integration);
    
    printf("\nTest Results:\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("\nAll UART tests PASSED!\n");
        printf("\nTo test with Python UART model:\n");
        printf("1. Start the UART model: python3 src/device_models/uart_model.py\n");
        printf("2. Run this test again to see full integration\n");
        return EXIT_SUCCESS;
    } else {
        printf("\nSome UART tests FAILED!\n");
        return EXIT_FAILURE;
    }
}