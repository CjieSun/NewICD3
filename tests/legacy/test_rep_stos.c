#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "interface_layer.h"

/* Custom memset implementation that uses REP STOS instructions */
static void custom_memset_rep_stosb(void *dest, int value, size_t count) {
    __asm__ __volatile__(
        "rep stosb"
        : "=D" (dest), "=c" (count)
        : "0" (dest), "1" (count), "a" (value)
        : "memory"
    );
}

static void custom_memset_rep_stosd(void *dest, uint32_t value, size_t dword_count) {
    __asm__ __volatile__(
        "rep stosl"
        : "=D" (dest), "=c" (dword_count)
        : "0" (dest), "1" (dword_count), "a" (value)
        : "memory"
    );
}

/* Test function to demonstrate memset-style instructions */
static int test_memset_instructions(void) {
    printf("Testing memset-style instructions (REP STOS)...\n");
    
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
    
    printf("Testing REP STOSB (byte-wise memset)...\n");
    printf("Attempting to clear 16 bytes using REP STOSB...\n");
    
    /* This will generate REP STOSB instruction */
    custom_memset_rep_stosb((void *)device_mem, 0, 16);
    
    printf("REP STOSB completed successfully!\n");
    
    /* Verify the memory was set by reading back */
    printf("Reading back the memory to verify:\n");
    for (int i = 0; i < 16; i++) {
        uint8_t val = device_mem[i];
        printf("  device_mem[%d] = 0x%02x\n", i, val);
    }
    
    printf("\nTesting REP STOSD (dword-wise memset)...\n");
    printf("Attempting to set 4 dwords (16 bytes) using REP STOSD...\n");
    
    /* Test dword-aligned access using REP STOSD */
    volatile uint32_t *device_mem32 = (volatile uint32_t *)0x40000000;
    custom_memset_rep_stosd((void *)device_mem32, 0x12345678, 4);
    
    printf("REP STOSD completed successfully!\n");
    
    /* Read back as dwords */
    printf("Reading back as dwords:\n");
    for (int i = 0; i < 4; i++) {
        uint32_t val = device_mem32[i];
        printf("  device_mem32[%d] = 0x%08x\n", i, val);
    }
    
    /* Also test different values */
    printf("\nTesting REP STOSB with 0xFF...\n");
    custom_memset_rep_stosb((void *)device_mem, 0xFF, 8);
    
    printf("Reading back after 0xFF fill:\n");
    for (int i = 0; i < 16; i++) {
        uint8_t val = device_mem[i];
        printf("  device_mem[%d] = 0x%02x\n", i, val);
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

int main(void) {
    printf("NewICD3 REP STOS Instructions Test\n");
    printf("==================================\n\n");
    
    int result = test_memset_instructions();
    
    if (result == 0) {
        printf("\nTest PASSED - REP STOS instructions handled correctly\n");
        return EXIT_SUCCESS;
    } else {
        printf("\nTest FAILED - REP STOS instructions not handled\n");
        return EXIT_FAILURE;
    }
}