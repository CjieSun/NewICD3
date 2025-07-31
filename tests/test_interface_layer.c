#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface_layer.h"

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

TEST(interface_layer_init_deinit) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    if (interface_layer_deinit() != 0) {
        return -1;
    }
    
    return 0;
}

TEST(device_registration) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    /* Unregister the device */
    if (unregister_device(1) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    interface_layer_deinit();
    return 0;
}

TEST(register_access) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    /* Test register read/write */
    uint32_t test_value = 0x12345678;
    if (write_register(0x40000000, test_value, 4) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    uint32_t read_value = read_register(0x40000000, 4);
    printf("  Wrote: 0x%x, Read: 0x%x\n", test_value, read_value);
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

TEST(interrupt_handling) {
    static int interrupt_received = 0;
    
    /* Simple interrupt handler */
    void test_interrupt_handler(uint32_t device_id, uint32_t interrupt_id) {
        interrupt_received = 1;
        printf("  Interrupt received from MODEL: device=%d, irq=%d\n", device_id, interrupt_id);
    }
    
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register interrupt handler */
    if (register_interrupt_handler(1, test_interrupt_handler) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    /* This simulates what would happen when the Python model triggers an interrupt.
     * In a real scenario, this would be called by handle_model_interrupts() 
     * when it receives an interrupt message from the Python model. */
    printf("  Simulating interrupt from model to driver...\n");
    if (trigger_interrupt(1, 0x10) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    /* Check if interrupt was received */
    if (!interrupt_received) {
        interface_layer_deinit();
        return -1;
    }
    
    interface_layer_deinit();
    return 0;
}

TEST(protocol_message) {
    protocol_message_t message = {0};
    protocol_message_t response = {0};
    
    message.device_id = 1;
    message.command = CMD_READ;
    message.address = 0x40000000;
    message.length = 4;
    
    if (send_message_to_model(&message, &response) != 0) {
        return -1;
    }
    
    if (response.result != RESULT_SUCCESS) {
        return -1;
    }
    
    printf("  Protocol test completed successfully\n");
    return 0;
}

TEST(model_interrupt_handling) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Test the model interrupt handling function */
    printf("  Testing model interrupt handling capability...\n");
    
    /* This tests the infrastructure for receiving interrupts from models */
    if (handle_model_interrupts() == 0) {
        printf("  Model interrupt handling function available\n");
    } else {
        printf("  Model interrupt handling function failed\n");
        interface_layer_deinit();
        return -1;
    }
    
    interface_layer_deinit();
    return 0;
}

TEST(bare_address_access_8bit) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    printf("  Testing 8-bit bare address access (triggers segv_handler)...\n");
    
    /* Get the mapped memory address for direct access */
    volatile uint8_t *mapped_addr = (volatile uint8_t *)get_device_mapped_memory(1);
    
    if (mapped_addr) {
        /* This should trigger the segv_handler for 8-bit read */
        printf("  Performing 8-bit read access at mapped address %p...\n", (void*)mapped_addr);
        volatile uint8_t val = *mapped_addr;
        printf("  8-bit read completed, value: 0x%02x\n", val);
        
        /* This should trigger the segv_handler for 8-bit write */
        printf("  Performing 8-bit write access...\n");
        *mapped_addr = 0x42;
        printf("  8-bit write completed\n");
    } else {
        printf("  Could not get mapped address for direct access\n");
        unregister_device(1);
        interface_layer_deinit();
        return -1;
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

TEST(bare_address_access_16bit) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    printf("  Testing 16-bit bare address access (triggers segv_handler)...\n");
    
    volatile uint16_t *mapped_addr = (volatile uint16_t *)get_device_mapped_memory(1);
    
    if (mapped_addr) {
        /* This should trigger the segv_handler for 16-bit read */
        printf("  Performing 16-bit read access at mapped address %p...\n", (void*)mapped_addr);
        volatile uint16_t val = *mapped_addr;
        printf("  16-bit read completed, value: 0x%04x\n", val);
        
        /* This should trigger the segv_handler for 16-bit write */
        printf("  Performing 16-bit write access...\n");
        *mapped_addr = 0x1234;
        printf("  16-bit write completed\n");
    } else {
        printf("  Could not get mapped address for direct access\n");
        unregister_device(1);
        interface_layer_deinit();
        return -1;
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

TEST(bare_address_access_32bit) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    printf("  Testing 32-bit bare address access (triggers segv_handler)...\n");
    
    volatile uint32_t *mapped_addr = (volatile uint32_t *)get_device_mapped_memory(1);
    
    if (mapped_addr) {
        /* This should trigger the segv_handler for 32-bit read */
        printf("  Performing 32-bit read access at mapped address %p...\n", (void*)mapped_addr);
        volatile uint32_t val = *mapped_addr;
        printf("  32-bit read completed, value: 0x%08x\n", val);
        
        /* This should trigger the segv_handler for 32-bit write */
        printf("  Performing 32-bit write access...\n");
        *mapped_addr = 0x12345678;
        printf("  32-bit write completed\n");
    } else {
        printf("  Could not get mapped address for direct access\n");
        unregister_device(1);
        interface_layer_deinit();
        return -1;
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

int main(void) {
    printf("NewICD3 Interface Layer Unit Tests\n");
    printf("==================================\n\n");
    
    RUN_TEST(interface_layer_init_deinit);
    RUN_TEST(device_registration);
    RUN_TEST(register_access);
    RUN_TEST(interrupt_handling);
    RUN_TEST(model_interrupt_handling);
    RUN_TEST(bare_address_access_8bit);
    RUN_TEST(bare_address_access_16bit);
    RUN_TEST(bare_address_access_32bit);
    RUN_TEST(protocol_message);
    
    printf("\nTest Results:\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("\nAll tests PASSED!\n");
        return EXIT_SUCCESS;
    } else {
        printf("\nSome tests FAILED!\n");
        return EXIT_FAILURE;
    }
}