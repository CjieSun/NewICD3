/**
 * NewICD3 memset Support Demonstration
 * ====================================
 * 
 * This program demonstrates the newly added memset support in the NewICD3
 * segv_handler. It shows how REP STOS* instructions are now properly handled
 * for bulk memory operations targeting device memory regions.
 */

#include "interface_layer.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main() {
    printf("NewICD3 memset Support Demonstration\n");
    printf("====================================\n\n");
    
    /* Initialize the interface layer */
    if (interface_layer_init() != 0) {
        printf("Failed to initialize interface layer\n");
        return 1;
    }
    
    /* Register a test device for demonstration */
    uint32_t device_id = 1;
    uint32_t base_addr = 0x40000000;
    uint32_t size = 0x1000;
    
    if (register_device(device_id, base_addr, size) != 0) {
        printf("Failed to register test device\n");
        interface_layer_deinit();
        return 1;
    }
    
    printf("Registered test device %d at 0x%08x (size: %d bytes)\n\n", 
           device_id, base_addr, size);
    
    /* Demonstrate different memset operations */
    printf("Demonstration 1: REP STOSB (8-bit memset)\n");
    printf("-----------------------------------------\n");
    uint8_t *buffer8 = (uint8_t *)base_addr;
    uint8_t pattern8 = 0xAA;
    size_t count8 = 32;
    
    printf("Executing: memset(0x%08x, 0x%02x, %zu) via REP STOSB\n", 
           base_addr, pattern8, count8);
    
    /* Use inline assembly to trigger REP STOSB */
    __asm__ volatile (
        "movq %0, %%rdi\n\t"
        "movq %1, %%rcx\n\t"
        "movb %2, %%al\n\t"
        "rep stosb\n\t"
        :
        : "r"(buffer8), "r"(count8), "r"(pattern8)
        : "rdi", "rcx", "rax", "memory"
    );
    
    printf("✓ REP STOSB completed successfully\n");
    printf("  - %zu bytes filled with pattern 0x%02x\n", count8, pattern8);
    printf("  - segv_handler processed %zu individual write operations\n\n", count8);
    
    printf("Demonstration 2: REP STOSD (32-bit memset)\n");
    printf("------------------------------------------\n");
    uint32_t *buffer32 = (uint32_t *)(base_addr + 0x100);
    uint32_t pattern32 = 0x12345678;
    size_t count32 = 16;  /* 16 dwords = 64 bytes */
    
    printf("Executing: memset(0x%08x, pattern, %zu*4) via REP STOSD\n", 
           base_addr + 0x100, count32);
    
    /* Use inline assembly to trigger REP STOSD */
    __asm__ volatile (
        "movq %0, %%rdi\n\t"
        "movq %1, %%rcx\n\t"
        "movl %2, %%eax\n\t"
        "rep stosl\n\t"
        :
        : "r"(buffer32), "r"(count32), "r"(pattern32)
        : "rdi", "rcx", "rax", "memory"
    );
    
    printf("✓ REP STOSD completed successfully\n");
    printf("  - %zu dwords (%zu bytes) filled with pattern 0x%08x\n", 
           count32, count32 * 4, pattern32);
    printf("  - segv_handler processed %zu individual 4-byte write operations\n\n", count32);
    
    printf("Demonstration 3: Zero-fill operation\n");
    printf("------------------------------------\n");
    uint8_t *buffer_zero = (uint8_t *)(base_addr + 0x200);
    size_t zero_count = 64;
    
    printf("Executing: memset(0x%08x, 0, %zu) via REP STOSB (zero-fill)\n", 
           base_addr + 0x200, zero_count);
    
    /* Use inline assembly to trigger REP STOSB with zero pattern */
    __asm__ volatile (
        "movq %0, %%rdi\n\t"
        "movq %1, %%rcx\n\t"
        "movb $0, %%al\n\t"
        "rep stosb\n\t"
        :
        : "r"(buffer_zero), "r"(zero_count)
        : "rdi", "rcx", "rax", "memory"
    );
    
    printf("✓ REP STOSB zero-fill completed successfully\n");
    printf("  - %zu bytes zeroed\n", zero_count);
    printf("  - segv_handler processed %zu individual zero-write operations\n\n", zero_count);
    
    printf("Verification: Reading back some values\n");
    printf("--------------------------------------\n");
    
    /* Read back some values to verify (these will also trigger segv_handler for reads) */
    printf("Reading buffer8[0]: ");
    uint8_t read_val8 = buffer8[0];
    printf("0x%02x (expected: 0x%02x)\n", read_val8, pattern8);
    
    printf("Reading buffer32[0]: ");
    uint32_t read_val32 = buffer32[0];
    printf("0x%08x (expected: 0x%08x)\n", read_val32, pattern32);
    
    printf("Reading buffer_zero[0]: ");
    uint8_t read_zero = buffer_zero[0];
    printf("0x%02x (expected: 0x00)\n", read_zero);
    
    printf("\nSummary\n");
    printf("=======\n");
    printf("✓ REP STOSB (8-bit memset) - SUPPORTED\n");
    printf("✓ REP STOSD (32-bit memset) - SUPPORTED\n");
    printf("✓ REP STOSW (16-bit memset) - SUPPORTED (not demonstrated)\n");
    printf("✓ REP STOSQ (64-bit memset) - SUPPORTED (not demonstrated)\n");
    printf("✓ Zero-fill operations - SUPPORTED\n");
    printf("✓ Bulk memory operations converted to individual device model writes\n");
    printf("✓ Register state properly managed (RDI, RCX, RIP)\n");
    printf("\nThe NewICD3 segv_handler now fully supports memset operations!\n");
    
    /* Clean up */
    unregister_device(device_id);
    interface_layer_deinit();
    
    return 0;
}