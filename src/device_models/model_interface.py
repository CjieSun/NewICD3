#!/usr/bin/env python3
"""
NewICD3 Device Model Interface

This module provides the Python-based device model interface for the NewICD3
universal IC simulator. It implements the server side of the socket-based
communication protocol with the C interface layer.

Key Features:
- Unix domain socket server for communication with C driver interface
- Register read/write simulation with configurable behavior
- Interrupt generation and delivery to C drivers  
- Protocol-compliant message handling
- Multi-client support for concurrent device simulation

Architecture:
    C Driver Layer ←→ C Interface Layer ←→ Python Device Models
    
Communication Protocol:
    - Commands: READ, WRITE, INTERRUPT, INIT, DEINIT
    - Results: SUCCESS, ERROR, TIMEOUT, INVALID_ADDR
    - Message structure: device_id, command, address, length, result, data

Usage:
    model = ModelInterface(device_id=1)
    model.start()  # Starts socket server
    model.trigger_interrupt_to_driver(irq_id)  # Send interrupt
    model.stop()   # Cleanup

@author NewICD3 Team
@version 1.0
@date 2024
"""

import socket
import struct
import threading
import time
import os
import sys
import logging

# Setup logging for Python model interface
def setup_logging():
    """Configure logging with file/function information and level control"""
    # Get log level from environment variable
    log_level_str = os.getenv('ICD3_LOG_LEVEL', 'INFO').upper()
    log_level = getattr(logging, log_level_str, logging.INFO)
    
    # Configure logging format to match C logging
    formatter = logging.Formatter(
        '[%(asctime)s.%(msecs)03d] [%(levelname)s] [%(filename)s:%(funcName)s] %(message)s',
        datefmt='%H:%M:%S'
    )
    
    # Setup console handler
    handler = logging.StreamHandler()
    handler.setFormatter(formatter)
    
    # Get logger and configure
    logger = logging.getLogger('NewICD3')
    logger.setLevel(log_level)
    logger.addHandler(handler)
    
    return logger

# Initialize logger
logger = setup_logging()

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
            logger.info(f"Removed existing socket: {SOCKET_PATH}")
        except OSError:
            logger.info(f"No existing socket to remove: {SOCKET_PATH}")
            
        # Create and bind socket
        try:
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.bind(SOCKET_PATH)
            self.socket.listen(5)
            logger.info(f"Device model {self.device_id} started on {SOCKET_PATH}")
            logger.debug(f"Socket file exists: {os.path.exists(SOCKET_PATH)}")
        except Exception as e:
            logger.error(f"Failed to create socket: {e}")
            return
        
        while self.running:
            try:
                logger.debug("Waiting for client connection...")
                client, addr = self.socket.accept()
                logger.info(f"Client connected")
                self.client_sockets.append(client)
                # Handle client in separate thread
                threading.Thread(target=self.handle_client, args=(client,)).start()
            except OSError as e:
                if self.running:  # Only print error if we're still supposed to be running
                    logger.error(f"Socket error: {e}")
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
        logger.info(f"Model triggering interrupt {interrupt_id} to driver for device {self.device_id}")
        
        # Create interrupt message - device_id, command, address, length, result + data[256]
        message = struct.pack('<IIIII', self.device_id, CMD_INTERRUPT, 0, interrupt_id, RESULT_SUCCESS)
        message += b'\x00' * 256  # Padding for data field
        
        # Send interrupt to all connected clients (driver interfaces)
        for client in self.client_sockets[:]:  # Use slice to avoid modification during iteration
            try:
                client.send(message)
                logger.debug(f"Interrupt {interrupt_id} sent to driver")
            except Exception as e:
                logger.error(f"Failed to send interrupt to client: {e}")
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
            
            data = client_socket.recv(expected_size)
            if not data:
                return
            
            if len(data) < expected_size:
                # Try to receive remaining data
                remaining = expected_size - len(data)
                more_data = client_socket.recv(remaining)
                if more_data:
                    data += more_data
                
            # Parse message according to C protocol_message_t structure
            if len(data) >= expected_size:
                # Unpack: device_id, command, address, length, result
                device_id, command, address, length, result = struct.unpack('<IIIII', data[:20])
                message_data = data[20:20+256]  # Extract the 256-byte data array
                
                logger.debug(f"Received: device_id={device_id}, cmd={command}, addr=0x{address:x}, len={length}")
                
                response = self.process_command(device_id, command, address, length, message_data)
                client_socket.send(response)
                
        except Exception as e:
            logger.error(f"Error handling client: {e}")
        finally:
            # Always close the client connection after handling one message
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
            logger.debug(f"Read 0x{address:x} = 0x{value:x}")
            
        elif command == CMD_WRITE:
            # Write to register
            if len(data) >= 4:
                value = struct.unpack('<I', data[:4])[0]
                self.registers[address] = value
                logger.debug(f"Write 0x{address:x} = 0x{value:x}")
            else:
                result = RESULT_ERROR
                
        elif command == CMD_INIT:
            logger.info(f"Device {device_id} initialized")
            
        elif command == CMD_DEINIT:
            logger.info(f"Device {device_id} deinitialized")
            
        else:
            result = RESULT_ERROR
            logger.error(f"Unknown command: {command}")
            
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
        logger.info("Starting model interface...")
        activity_thread.start()
        model.start()
    except KeyboardInterrupt:
        print("\nShutting down model interface...")
        model.stop()

if __name__ == "__main__":
    main()