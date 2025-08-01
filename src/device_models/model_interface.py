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
            print(f"Removed existing socket: {SOCKET_PATH}")
        except OSError:
            print(f"No existing socket to remove: {SOCKET_PATH}")
            
        # Create and bind socket
        try:
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.bind(SOCKET_PATH)
            self.socket.listen(5)
            print(f"Device model {self.device_id} started on {SOCKET_PATH}")
            print(f"Socket file exists: {os.path.exists(SOCKET_PATH)}")
        except Exception as e:
            print(f"Failed to create socket: {e}")
            return
        
        while self.running:
            try:
                print("Waiting for client connection...")
                client, addr = self.socket.accept()
                print(f"Client connected: {client}")
                self.client_sockets.append(client)
                # Handle client in separate thread
                threading.Thread(target=self.handle_client, args=(client,)).start()
            except OSError as e:
                if self.running:  # Only print error if we're still supposed to be running
                    print(f"Socket error: {e}")
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
        
        # Create interrupt message - device_id, command, address, length, result + data[256]
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
            
    def handle_client(self, client_socket):
        """Handle communication with a client - one message per connection"""
        try:
            # Receive full protocol message (5 uint32_t + 256 bytes data = 276 bytes total)
            expected_size = 5 * 4 + 256  # 5 uint32_t fields + 256 byte data array
            print(f"Waiting to receive {expected_size} bytes from new client...")
            
            data = client_socket.recv(expected_size)
            if not data:
                print("No data received, client disconnected")
                return
            
            print(f"Received {len(data)} bytes of data")
            
            if len(data) < expected_size:
                # Try to receive remaining data
                remaining = expected_size - len(data)
                print(f"Need {remaining} more bytes...")
                more_data = client_socket.recv(remaining)
                if more_data:
                    data += more_data
                    print(f"Received additional {len(more_data)} bytes, total: {len(data)}")
                
            # Parse message according to C protocol_message_t structure
            if len(data) >= expected_size:
                # Unpack: device_id, command, address, length, result
                device_id, command, address, length, result = struct.unpack('<IIIII', data[:20])
                message_data = data[20:20+256]  # Extract the 256-byte data array
                
                print(f"Parsed: device_id={device_id}, cmd={command}, addr=0x{address:x}, len={length}, result={result}")
                
                response = self.process_command(device_id, command, address, length, message_data)
                print(f"Sending response of {len(response)} bytes...")
                bytes_sent = client_socket.send(response)
                print(f"Response sent: {bytes_sent} bytes")
            else:
                print(f"Insufficient data received: {len(data)} < {expected_size}")
                
        except Exception as e:
            print(f"Error handling client: {e}")
        finally:
            # Always close the client connection after handling one message
            print("Closing client connection")
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
            
        # Build response message with correct protocol_message_t structure
        # device_id, command, address, length, result + data[256]
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