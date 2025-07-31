#!/usr/bin/env python3
"""
UART Integration Demo: Python UART Model with C Driver Integration

This demonstrates:
1. Python UART model simulation
2. C driver communication with model
3. Interrupt delivery from model to driver
4. UART-specific functionality (TX, RX, interrupts)
"""

import sys
import os
import time
import threading
import subprocess
import signal

# Add the device models directory to path
sys.path.append('src/device_models')

from uart_model import UartModel

def run_uart_test():
    """Run the C UART integration test"""
    print("Starting C UART integration test...")
    
    try:
        process = subprocess.Popen(['./bin/test_uart_integration'], 
                                  stdout=subprocess.PIPE, 
                                  stderr=subprocess.PIPE,
                                  text=True)
        
        # Wait for completion with timeout
        stdout, stderr = process.communicate(timeout=30)
        
        print("=== C Test Output ===")
        print(stdout)
        if stderr:
            print("=== C Test Errors ===")
            print(stderr)
            
        return process.returncode == 0
        
    except subprocess.TimeoutExpired:
        print("C test timed out")
        process.kill()
        return False
    except Exception as e:
        print(f"Error running C test: {e}")
        return False

def main():
    print("NewICD3 UART Integration Demo")
    print("=============================")
    print("Demonstrating Python UART model working with C driver")
    print("")
    
    # Check if binaries exist
    if not os.path.exists('./bin/test_uart_integration'):
        print("Error: Please run 'make' first to build the C components")
        sys.exit(1)
    
    # Start the Python UART model
    uart_model = UartModel(device_id=1, base_address=0x40000000)
    
    # Start model in background thread
    model_thread = threading.Thread(target=uart_model.start, daemon=True)
    model_thread.start()
    
    print("Python UART model started")
    time.sleep(2)  # Give it time to start and bind socket
    
    # Run the C integration test
    print("\nRunning UART integration test with model...")
    test_success = run_uart_test()
    
    if test_success:
        print("\n✓ UART integration test completed successfully!")
    else:
        print("\n✗ UART integration test failed")
    
    # Demonstrate live UART activity
    print("\nDemonstrating live UART activity...")
    print("(UART model will simulate incoming data and trigger interrupts)")
    
    # Let the model simulate activity for a while
    for i in range(10):
        print(f"  Activity cycle {i+1}/10...")
        time.sleep(1)
    
    print("\nDemo completed.")
    
    # Cleanup
    uart_model.stop()
    print("UART model stopped.")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nDemo interrupted by user")
        sys.exit(0)