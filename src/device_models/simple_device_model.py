#!/usr/bin/env python3
"""
Simple Device Model Example for NewICD3 Interface Layer

This is a basic example of how a Python device model can communicate
with the C interface layer via socket protocol.
"""

import socket
import struct
import threading
import time
import os

# Protocol definitions (must match C definitions)
CMD_READ = 0x01
CMD_WRITE = 0x02
CMD_INTERRUPT = 0x03
CMD_INIT = 0x04
CMD_DEINIT = 0x05

RESULT_SUCCESS = 0x00
RESULT_ERROR = 0x01
RESULT_TIMEOUT = 0x02
RESULT_INVALID_ADDR = 0x03

SOCKET_PATH = "/tmp/icd3_interface"

class SimpleDeviceModel:
    def __init__(self, device_id=1):
        self.device_id = device_id
        self.registers = {}  # Simple register storage
        self.running = False
        self.socket = None
        
    def start(self):
        """Start the device model server"""
        self.running = True
        
        # Remove existing socket
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
            
        # Create and bind socket
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.bind(SOCKET_PATH)
        self.socket.listen(5)
        
        print(f"Device model {self.device_id} started on {SOCKET_PATH}")
        
        while self.running:
            try:
                client, addr = self.socket.accept()
                # Handle client in separate thread
                threading.Thread(target=self.handle_client, args=(client,)).start()
            except OSError:
                break
                
    def stop(self):
        """Stop the device model server"""
        self.running = False
        if self.socket:
            self.socket.close()
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
            
    def handle_client(self, client_socket):
        """Handle communication with a client"""
        try:
            while True:
                # Receive message (simplified protocol)
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                # Parse message (this is a simplified version)
                if len(data) >= 20:  # Minimum message size
                    device_id, command, address, length = struct.unpack('<IIII', data[:16])
                    
                    print(f"Received: device_id={device_id}, cmd={command}, addr=0x{address:x}, len={length}")
                    
                    response = self.process_command(device_id, command, address, length, data[20:])
                    client_socket.send(response)
                    
        except Exception as e:
            print(f"Error handling client: {e}")
        finally:
            client_socket.close()
            
    def process_command(self, device_id, command, address, length, data):
        """Process a command and return response"""
        result = RESULT_SUCCESS
        response_data = b'\x00' * 256  # Initialize response data
        
        if command == CMD_READ:
            # Read from register
            value = self.registers.get(address, 0xDEADBEEF)  # Default value
            response_data = struct.pack('<I', value) + b'\x00' * 252
            print(f"Read 0x{address:x} = 0x{value:x}")
            
        elif command == CMD_WRITE:
            # Write to register
            if len(data) >= 4:
                value = struct.unpack('<I', data[:4])[0]
                self.registers[address] = value
                print(f"Write 0x{address:x} = 0x{value:x}")
            else:
                result = RESULT_ERROR
                
        elif command == CMD_INIT:
            print(f"Device {device_id} initialized")
            
        elif command == CMD_DEINIT:
            print(f"Device {device_id} deinitialized")
            
        else:
            result = RESULT_ERROR
            print(f"Unknown command: {command}")
            
        # Build response message
        response = struct.pack('<IIIII', device_id, command, address, length, result)
        response += response_data
        
        return response

def main():
    """Main function for testing"""
    model = SimpleDeviceModel(1)
    
    try:
        print("Starting simple device model...")
        model.start()
    except KeyboardInterrupt:
        print("\nShutting down device model...")
        model.stop()

if __name__ == "__main__":
    main()