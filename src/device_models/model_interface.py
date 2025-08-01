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
import signal

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
DRIVER_PID_FILE = "/tmp/icd3_driver_pid"

class ModelInterface:
    def __init__(self, device_id=1):
        self.device_id = device_id
        self.registers = {}  # Simple register storage
        self.running = False
        self.socket = None
        self.client_sockets = []  # Track connected clients for interrupt delivery
        self.driver_pid = None  # PID of the C driver process for signal-based interrupts
        
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
            
    def get_driver_pid(self):
        """Get the PID of the C driver process for signal-based interrupts"""
        try:
            if os.path.exists(DRIVER_PID_FILE):
                with open(DRIVER_PID_FILE, 'r') as f:
                    pid = int(f.read().strip())
                    self.driver_pid = pid
                    logger.debug(f"Found driver PID: {pid}")
                    return pid
        except (ValueError, IOError) as e:
            logger.warning(f"Failed to read driver PID file: {e}")
        
        # Fallback: try to find the process by name (less reliable)
        try:
            import subprocess
            result = subprocess.run(['pgrep', '-f', 'icd3_simulator'], 
                                  capture_output=True, text=True)
            if result.returncode == 0 and result.stdout.strip():
                pid = int(result.stdout.strip().split()[0])
                self.driver_pid = pid
                logger.debug(f"Found driver PID via pgrep: {pid}")
                return pid
        except Exception as e:
            logger.warning(f"Failed to find driver process: {e}")
        
        return None
            
    def trigger_interrupt_to_driver(self, interrupt_id):
        """Trigger an interrupt to the driver interface using signals"""
        logger.info(f"Model triggering interrupt {interrupt_id} to driver for device {self.device_id}")
        
        # Get the driver process PID
        driver_pid = self.get_driver_pid()
        if not driver_pid:
            logger.error("Cannot send interrupt: driver PID not found")
            return
        
        try:
            # Pack device_id (lower 16 bits) and interrupt_id (upper 16 bits) into signal value
            signal_data = (interrupt_id << 16) | (self.device_id & 0xFFFF)
            
            # Send SIGUSR1 with data using sigqueue (if available) or kill
            try:
                # Try to use os.kill with signal data (Python 3.3+ on some systems)
                # Since Python doesn't directly support sigqueue, we'll use a simpler approach
                # We'll send SIGUSR1 and write interrupt details to a temporary file
                
                # Create a temporary file with interrupt details
                import tempfile
                interrupt_file = f"/tmp/icd3_interrupt_{driver_pid}"
                with open(interrupt_file, 'w') as f:
                    f.write(f"{self.device_id},{interrupt_id}")
                
                # Send SIGUSR1 signal
                os.kill(driver_pid, signal.SIGUSR1)
                logger.debug(f"Sent SIGUSR1 to PID {driver_pid} for device {self.device_id}, interrupt {interrupt_id}")
                
                # Clean up the temporary file after a short delay
                def cleanup_file():
                    time.sleep(0.5)  # Give the C process time to read it
                    try:
                        os.unlink(interrupt_file)
                    except:
                        pass
                
                threading.Thread(target=cleanup_file, daemon=True).start()
                
            except PermissionError:
                logger.error(f"Permission denied when sending signal to PID {driver_pid}")
            except ProcessLookupError:
                logger.error(f"Process {driver_pid} not found")
            except Exception as e:
                logger.error(f"Failed to send signal: {e}")
                
        except Exception as e:
            logger.error(f"Failed to trigger interrupt via signal: {e}")
                    
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

    try:
        logger.info("Starting model interface...")
        model.start()
    except KeyboardInterrupt:
        print("\nShutting down model interface...")
        model.stop()

if __name__ == "__main__":
    main()