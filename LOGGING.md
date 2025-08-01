# NewICD3 Logging System

This document describes the optimized logging system implemented for the NewICD3 Universal IC Simulator.

## Features

The logging system provides the following optimizations:

1. **Log Level Control**: Support for DEBUG, INFO, WARN, and ERROR levels
2. **Source Location**: Includes filename and function name in log messages
3. **Timestamp**: Precise millisecond timestamps for debugging
4. **Runtime Control**: Log level can be set via environment variable
5. **Unified Format**: Consistent logging format across C and Python components

## Usage

### Setting Log Level

Control the logging verbosity using the `ICD3_LOG_LEVEL` environment variable:

```bash
# Show only errors
ICD3_LOG_LEVEL=ERROR ./bin/icd3_simulator

# Show warnings and errors
ICD3_LOG_LEVEL=WARN ./bin/icd3_simulator

# Show info, warnings, and errors (default)
ICD3_LOG_LEVEL=INFO ./bin/icd3_simulator

# Show all log messages including debug
ICD3_LOG_LEVEL=DEBUG ./bin/icd3_simulator
```

### C Code Usage

Include the logging header and use the macros:

```c
#include "logging.h"

void my_function() {
    LOG_DEBUG("Debug information: value=%d", 42);
    LOG_INFO("Operation completed successfully");
    LOG_WARN("Warning: configuration issue detected");
    LOG_ERROR("Error: operation failed");
}
```

### Python Code Usage

The Python model interface automatically uses the logging system:

```python
# Logger is automatically configured in model_interface.py
# Log level is controlled by ICD3_LOG_LEVEL environment variable
logger.debug("Debug information")
logger.info("Operation completed")
logger.warning("Warning message")
logger.error("Error occurred")
```

## Log Format

All log messages follow this format:

```
[HH:MM:SS.mmm] [LEVEL] [filename:function] message
```

Example:
```
[07:15:10.247] [INFO] [main.c:main] NewICD3 Universal IC Simulator
[07:15:10.247] [WARN] [device_driver.c:device_write_data] Device not ready for write
[07:15:10.248] [ERROR] [main.c:test_device_operations] Device write failed
```

## Integration

The logging system is integrated throughout the NewICD3 codebase:

- **Application Layer** (`src/app_layer/main.c`): Test execution and results
- **Driver Layer** (`src/driver_layer/device_driver.c`): Device operations and status
- **Interface Layer** (`src/interface_layer/driver_interface.c`): Protocol communication
- **Device Models** (`src/device_models/model_interface.py`): Python model operations

## Benefits

1. **Debugging**: Precise timing and source location help identify issues
2. **Performance**: Log level filtering reduces overhead in production
3. **Maintenance**: Consistent format makes log analysis easier
4. **Development**: Debug level provides detailed operation traces
5. **Production**: Error level provides minimal, focused output

## Demonstration

Run the logging demonstration script to see all features in action:

```bash
./demo_logging.sh
```

This script shows:
- Different log levels in C and Python
- Source location information
- Timestamp precision
- Level filtering functionality