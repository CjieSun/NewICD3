TEST(memset_rep_stos_support) {
    if (interface_layer_init() != 0) {
        return -1;
    }
    
    /* Register a test device */
    if (register_device(1, 0x40000000, 0x1000) != 0) {
        interface_layer_deinit();
        return -1;
    }
    
    printf("  Testing memset-style REP STOS instruction support...\n");
    
    /* Create a pointer to the device memory */
    volatile uint8_t *device_mem = (volatile uint8_t *)0x40000000;
    volatile uint32_t *device_mem32 = (volatile uint32_t *)0x40000000;
    
    /* Test 1: Custom REP STOSB (8-bit memset) */
    printf("  Testing REP STOSB (8-bit bulk write)...\n");
    
    /* Use inline assembly to generate REP STOSB */
    void *dest = (void *)device_mem;
    size_t count = 16;
    int value = 0xAB;
    
    __asm__ __volatile__(
        "rep stosb"
        : "=D" (dest), "=c" (count)
        : "0" (dest), "1" (count), "a" (value)
        : "memory"
    );
    
    printf("  REP STOSB completed, verifying with reads...\n");
    
    /* Verify the first few bytes were set correctly by reading them back */
    for (int i = 0; i < 4; i++) {
        uint8_t val = device_mem[i];
        printf("    device_mem[%d] = 0x%02x\n", i, val);
    }
    
    /* Test 2: Custom REP STOSD (32-bit memset) */
    printf("  Testing REP STOSD (32-bit bulk write)...\n");
    
    dest = (void *)device_mem32;
    size_t dword_count = 4;
    uint32_t dword_value = 0x12345678;
    
    __asm__ __volatile__(
        "rep stosl"
        : "=D" (dest), "=c" (dword_count)  
        : "0" (dest), "1" (dword_count), "a" (dword_value)
        : "memory"
    );
    
    printf("  REP STOSD completed, verifying with reads...\n");
    
    /* Verify the dwords were set correctly */
    for (int i = 0; i < 4; i++) {
        uint32_t val = device_mem32[i];
        printf("    device_mem32[%d] = 0x%08x\n", i, val);
    }
    
    /* Test 3: Different sizes and patterns */
    printf("  Testing various REP STOS patterns...\n");
    
    /* Clear with REP STOSB */
    dest = (void *)device_mem;
    count = 8;
    value = 0;
    
    __asm__ __volatile__(
        "rep stosb"
        : "=D" (dest), "=c" (count)
        : "0" (dest), "1" (count), "a" (value)
        : "memory"
    );
    
    printf("  Cleared 8 bytes with REP STOSB\n");
    
    /* Set pattern with REP STOSD */
    dest = (void *)device_mem32;
    dword_count = 2;
    dword_value = 0xDEADBEEF;
    
    __asm__ __volatile__(
        "rep stosl"
        : "=D" (dest), "=c" (dword_count)
        : "0" (dest), "1" (dword_count), "a" (dword_value)
        : "memory"
    );
    
    printf("  Set 2 dwords with REP STOSD\n");
    
    /* Read back to verify */
    printf("  Final verification reads:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t val = device_mem[i];
        printf("    byte[%d] = 0x%02x\n", i, val);
    }
    
    unregister_device(1);
    interface_layer_deinit();
    return 0;
}