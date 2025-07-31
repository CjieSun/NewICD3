#!/usr/bin/env python3
"""
Integration Demo: Python Model Interface triggering interrupts to C Driver Interface

This demonstrates the corrected interrupt flow:
- Python model triggers interrupts to C driver (not the other way around)
"""

import sys
import os
import time
import threading
import subprocess

# Add the device models directory to path
sys.path.append('src/device_models')

from model_interface import ModelInterface

def run_driver_interface():
    """Run the C driver interface in background"""
    print("Starting C driver interface...")
    process = subprocess.Popen(['./bin/icd3_simulator'], 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE,
                              text=True)
    return process

def main():
    print("NewICD3 Integration Demo")
    print("========================")
    print("Demonstrating Python model triggering interrupts to C driver")
    print("")
    
    # Start the Python model interface
    model = ModelInterface(device_id=1)
    
    # Start model in background thread
    model_thread = threading.Thread(target=model.start, daemon=True)
    model_thread.start()
    
    print("Python model interface started")
    time.sleep(1)  # Give it time to start
    
    # Demonstrate interrupt triggering
    print("\nTriggering interrupts from Python model to C driver...")
    
    for i in range(3):
        print(f"Triggering interrupt {i+1}...")
        model.trigger_interrupt_to_driver(0x10 + i)
        time.sleep(2)
    
    print("\nDemo completed. Model interface continues running...")
    print("(In a real scenario, the C driver would be listening for these interrupts)")
    
    # Keep running for a bit to show continued operation
    time.sleep(5)
    
    # Cleanup
    model.stop()
    print("Model interface stopped.")

if __name__ == "__main__":
    # Check if the binary exists
    if not os.path.exists('./bin/icd3_simulator'):
        print("Error: Please run 'make' first to build the C components")
        sys.exit(1)
        
    main()