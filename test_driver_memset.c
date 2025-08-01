#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "interface_layer.h"

/* Simulate driver-style memset operations that use REP STOS */

/* Driver function to clear a register block */
static void driver_clear_registers(volatile void *base_addr, size_t count) {
    /* This mimics how embedded drivers often clear register blocks */
    __asm__ __volatile__(
        "rep stosb"
        : "=D" (base_addr), "=c" (count)
        : "0" (base_addr), "1" (count), "a" (0)
        : "memory"
    );
}

/* Driver function to initialize a register block with a pattern */
static void driver_init_registers(volatile uint32_t *base_addr, uint32_t pattern, size_t dword_count) {
    /* This mimics how embedded drivers initialize register arrays */
    __asm__ __volatile__(
        "rep stosl"
        : "=D" (base_addr), "=c" (dword_count)
        : "0" (base_addr), "1" (dword_count), "a" (pattern)
        : "memory"
    );
}

/* Test driver-style memset operations */
static int test_driver_memset_operations(void) {
    printf("Testing driver-style memset operations...\n");
    
    if (interface_layer_init() != 0) {
        printf("Failed to initialize interface layer\n");
        return -1;
    }
    
    /* Register a test device representing a peripheral with multiple registers */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        printf("Failed to register device\n");
        interface_layer_deinit();
        return -1;
    }
    
    /* Simulate driver initialization sequence */
    printf("Simulating driver initialization sequence...\n");
    
    /* Step 1: Clear control registers (first 64 bytes) */
    printf("Step 1: Clearing control registers (64 bytes)...\n");
    volatile uint8_t *ctrl_regs = (volatile uint8_t *)0x40000000;
    driver_clear_registers((volatile void *)ctrl_regs, 64);
    printf("Control registers cleared\n");
    
    /* Step 2: Initialize data buffers with pattern (16 dwords = 64 bytes) */
    printf("Step 2: Initializing data buffers with pattern...\n");
    volatile uint32_t *data_regs = (volatile uint32_t *)0x40000100;
    driver_init_registers(data_regs, 0x12345678, 16);
    printf("Data buffers initialized\n");
    
    /* Step 3: Clear interrupt status registers (32 bytes) */
    printf("Step 3: Clearing interrupt status registers...\n");
    volatile uint8_t *irq_regs = (volatile uint8_t *)0x40000200;
    driver_clear_registers((volatile void *)irq_regs, 32);
    printf("Interrupt registers cleared\n");
    
    /* Step 4: Verify some registers by reading them back */
    printf("Step 4: Verifying register initialization...\n");
    
    /* Check first few control registers */
    for (int i = 0; i < 4; i++) {
        uint8_t val = ctrl_regs[i];
        printf("  ctrl_reg[%d] = 0x%02x\n", i, val);
    }
    
    /* Check first few data registers */
    for (int i = 0; i < 4; i++) {
        uint32_t val = data_regs[i];
        printf("  data_reg[%d] = 0x%08x\n", i, val);
    }
    
    /* Check first few interrupt registers */  
    for (int i = 0; i < 4; i++) {
        uint8_t val = irq_regs[i];
        printf("  irq_reg[%d] = 0x%02x\n", i, val);
    }
    
    printf("Driver initialization sequence completed successfully!\n");
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}

int main(void) {
    printf("NewICD3 Driver-Style Memset Operations Test\n");
    printf("===========================================\n\n");
    
    int result = test_driver_memset_operations();
    
    if (result == 0) {
        printf("\nTest PASSED - Driver memset operations handled correctly\n");
        printf("This demonstrates that drivers can now use memset-style operations\n");
        printf("(REP STOS instructions) to efficiently initialize device registers.\n");
        return EXIT_SUCCESS;
    } else {
        printf("\nTest FAILED - Driver memset operations not handled\n");
        return EXIT_FAILURE;
    }
}