# Legacy Test Files

This directory contains the original standalone test files that were scattered throughout the repository root. These tests have been consolidated here for reference and optional use.

## Available Tests

### test_memset_issue.c
- **Purpose**: Reproduces and validates the original memset instruction handling issue
- **Tests**: Basic memset operations using standard C library `memset()`
- **Coverage**: Demonstrates that memset instructions are properly trapped and handled by the segv_handler

### test_rep_stos.c  
- **Purpose**: Tests explicit REP STOS instruction generation and handling
- **Tests**: Custom inline assembly REP STOSB and REP STOSD instructions
- **Coverage**: Validates bulk memory write operations using x86 string instructions

### test_driver_memset.c
- **Purpose**: Simulates real embedded driver initialization patterns
- **Tests**: Driver-style register clearing and initialization using REP STOS
- **Coverage**: Demonstrates practical use cases for bulk memory operations in device drivers

### memset_test_addition.c
- **Purpose**: Fragment containing additional REP STOS test code
- **Note**: This is not a standalone test but provides example implementations

## Integration Status

All functionality tested by these legacy tests is now covered by the comprehensive test suite in `tests/test_interface_layer.c`. The main test suite includes:

- `test_memset_rep_stos_support()` - Covers REP STOS instruction handling
- `test_bare_metal_*_access()` tests - Cover direct memory access patterns  
- Memory access violation handling for various instruction types

## Building Legacy Tests

To build the legacy tests as standalone executables:

```bash
make legacy-tests
```

This will create:
- `bin/test_memset_issue`
- `bin/test_rep_stos` 
- `bin/test_driver_memset`

## Why Legacy Tests Were Consolidated

1. **Reduced Duplication**: Multiple tests were covering the same core functionality
2. **Improved Organization**: All tests now in proper `tests/` directory structure
3. **Better Maintainability**: Single comprehensive test suite is easier to maintain
4. **Build Integration**: Legacy tests were not part of the main build/test process

The legacy tests remain available for:
- Reference implementation examples
- Debugging specific instruction patterns
- Compatibility with existing workflows that depend on them