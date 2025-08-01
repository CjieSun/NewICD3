#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "interface_layer.h"

/* Test function to demonstrate the memset issue */
static int test_memset_access(void) {
    printf("Testing memset access to device memory...\n");
    
    if (interface_layer_init() != 0) {
        printf("Failed to initialize interface layer\n");
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        printf("Failed to register device\n");
        interface_layer_deinit();
        return -1;
    }
    
    /* Create a pointer to the device memory */
    volatile uint8_t *device_mem = (volatile uint8_t *)0x40000000;
    
    printf("Attempting memset to clear 16 bytes at device address 0x40000000...\n");
    
    /* This should trigger SEGV and be handled by the segv_handler */
    /* memset typically generates REP STOS instructions */
    memset((void *)device_mem, 0, 16);
    
    printf("Memset completed successfully!\n");
    
    /* Verify the memory was set by reading back */
    printf("Reading back the memory to verify:\n");
    for (int i = 0; i < 16; i++) {
        uint8_t val = device_mem[i];
        printf("  device_mem[%d] = 0x%02x\n", i, val);
    }
    
    /* Test memset with different value */
    printf("Testing memset with value 0xFF for 8 bytes...\n");
    memset((void *)device_mem, 0xFF, 8);
    
    printf("Reading back after second memset:\n");
    for (int i = 0; i < 16; i++) {
        uint8_t val = device_mem[i];
        printf("  device_mem[%d] = 0x%02x\n", i, val);
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

int main(void) {
    printf("NewICD3 memset Issue Reproduction Test\n");
    printf("======================================\n\n");
    
    int result = test_memset_access();
    
    if (result == 0) {
        printf("\nTest PASSED - memset instructions handled correctly\n");
        return EXIT_SUCCESS;
    } else {
        printf("\nTest FAILED - memset instructions not handled\n");
        return EXIT_FAILURE;
    }
}