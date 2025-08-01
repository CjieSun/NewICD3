#!/bin/bash
# NewICD3 Logging System Demonstration
# This script shows the logging optimization features

echo "======================================"
echo "NewICD3 Logging System Demonstration"
echo "======================================"
echo

echo "1. Testing C logging with different levels:"
echo "-------------------------------------------"

echo
echo "Running with INFO level (default):"
ICD3_LOG_LEVEL=INFO ./bin/icd3_simulator | head -10

echo
echo "Running with ERROR level (minimal output):"
ICD3_LOG_LEVEL=ERROR ./bin/icd3_simulator | grep "\[ERROR\]" | head -5

echo
echo "2. Testing Python logging with different levels:"
echo "------------------------------------------------"

echo
echo "Running Python model with INFO level:"
ICD3_LOG_LEVEL=INFO python3 -c "
import sys
sys.path.append('src/device_models')
from model_interface import ModelInterface

# Demonstrate different log levels in Python
model = ModelInterface(device_id=42)
model.trigger_interrupt_to_driver(0xFF)
"

echo
echo "Running Python model with ERROR level (minimal output):"
ICD3_LOG_LEVEL=ERROR python3 -c "
import sys
sys.path.append('src/device_models')
from model_interface import ModelInterface

# Demonstrate ERROR level filtering
model = ModelInterface(device_id=42)
model.trigger_interrupt_to_driver(0xFF)
print('INFO level messages are filtered out at ERROR level')
"

echo
echo "======================================"
echo "Logging System Features Demonstrated:"
echo "======================================"
echo "✓ Timestamp with millisecond precision"
echo "✓ Log level control (DEBUG, INFO, WARN, ERROR)"
echo "✓ File name and function name in logs"
echo "✓ Runtime level control via ICD3_LOG_LEVEL"
echo "✓ Consistent format between C and Python"
echo "✓ Proper level filtering"
echo
echo "Usage: Set ICD3_LOG_LEVEL=DEBUG|INFO|WARN|ERROR before running any NewICD3 component"