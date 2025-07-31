#!/usr/bin/env python3
"""
Model Interface for NewICD3 Interface Layer

This Python interface provides device model functionality and can trigger
interrupts to the C driver interface via socket protocol.
"""

import socket
import struct
import threading
import time
import os
import sys

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

class ModelInterface:
    def __init__(self, device_id=1):
        self.device_id = device_id
        self.registers = {}  # Simple register storage
        self.running = False
        self.socket = None
        self.client_sockets = []  # Track connected clients for interrupt delivery
        
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
        
        print(f"Model interface {self.device_id} started on {SOCKET_PATH}")
        
        while self.running:
            try:
                client, addr = self.socket.accept()
                self.client_sockets.append(client)
                # Handle client in separate thread
                threading.Thread(target=self.handle_client, args=(client,)).start()
            except OSError:
                break
                
    def stop(self):
        """Stop the device model server"""
        self.running = False
        
        # Close all client connections
        for client in self.client_sockets:
            try:
                client.close()
            except:
                pass
        self.client_sockets.clear()
        
        if self.socket:
            self.socket.close()
        try:
            os.unlink(SOCKET_PATH)
        except OSError:
            pass
            
    def trigger_interrupt_to_driver(self, interrupt_id):
        """Trigger an interrupt to the driver interface"""
        print(f"Model triggering interrupt {interrupt_id} to driver for device {self.device_id}")
        
        # Create interrupt message
        message = struct.pack('<IIIII', self.device_id, CMD_INTERRUPT, 0, interrupt_id, RESULT_SUCCESS)
        message += b'\x00' * 256  # Padding for data field
        
        # Send interrupt to all connected clients (driver interfaces)
        for client in self.client_sockets[:]:  # Use slice to avoid modification during iteration
            try:
                client.send(message)
                print(f"Interrupt {interrupt_id} sent to driver")
            except Exception as e:
                print(f"Failed to send interrupt to client: {e}")
                # Remove failed client
                if client in self.client_sockets:
                    self.client_sockets.remove(client)
                try:
                    client.close()
                except:
                    pass
                    
    def simulate_device_activity(self):
        """Simulate device activity that triggers interrupts"""
        if not self.running:
            return
            
        # Simulate some device events that would trigger interrupts
        time.sleep(2)  # Wait a bit after startup
        
        if self.running:
            self.trigger_interrupt_to_driver(0x01)  # Data ready interrupt
            
        time.sleep(3)
        if self.running:
            self.trigger_interrupt_to_driver(0x02)  # Status change interrupt
        
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
            while self.running:
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
            # Remove client from tracking list
            if client_socket in self.client_sockets:
                self.client_sockets.remove(client_socket)
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
    model = ModelInterface(1)
    
    # Start device activity simulation in background
    activity_thread = threading.Thread(target=model.simulate_device_activity, daemon=True)
    
    try:
        print("Starting model interface...")
        activity_thread.start()
        model.start()
    except KeyboardInterrupt:
        print("\nShutting down model interface...")
        model.stop()

if __name__ == "__main__":
    main()