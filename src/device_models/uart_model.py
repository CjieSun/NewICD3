#!/usr/bin/env python3
"""
UART Device Model for NewICD3 Interface Layer

This Python model simulates a UART device with typical registers:
- CTRL: Control register
- STATUS: Status register  
- DATA: Data register (TX/RX)
- BAUD: Baud rate register

The model can trigger interrupts for data ready, transmission complete, etc.
"""

import socket
import struct
import threading
import time
import os
import sys
import queue
import random

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

# UART register offsets (matching C driver expectations)
UART_CTRL_OFFSET = 0x00
UART_STATUS_OFFSET = 0x04
UART_DATA_OFFSET = 0x08
UART_BAUD_OFFSET = 0x0C

# UART control register bits
UART_CTRL_ENABLE = 0x01
UART_CTRL_TX_EN = 0x02
UART_CTRL_RX_EN = 0x04
UART_CTRL_IRQ_EN = 0x08

# UART status register bits
UART_STATUS_READY = 0x01
UART_STATUS_TX_EMPTY = 0x02
UART_STATUS_RX_FULL = 0x04
UART_STATUS_TX_COMPLETE = 0x08

class UartModel:
    def __init__(self, device_id=1, base_address=0x40000000):
        self.device_id = device_id
        self.base_address = base_address
        self.running = False
        self.socket = None
        self.client_sockets = []
        
        # UART state
        self.registers = {
            UART_CTRL_OFFSET: 0x00,     # Control register
            UART_STATUS_OFFSET: 0x03,   # Status register (READY | TX_EMPTY)
            UART_DATA_OFFSET: 0x00,     # Data register
            UART_BAUD_OFFSET: 9600,     # Baud rate
        }
        
        # UART buffers
        self.tx_buffer = queue.Queue(maxsize=16)
        self.rx_buffer = queue.Queue(maxsize=16)
        
        # Simulation state
        self.simulation_thread = None
        
    def start(self):
        """Start the UART device model server"""
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
        
        print(f"UART model {self.device_id} started on {SOCKET_PATH}")
        print(f"Base address: 0x{self.base_address:08x}")
        
        # Start simulation thread
        self.simulation_thread = threading.Thread(target=self.simulate_uart_activity, daemon=True)
        self.simulation_thread.start()
        
        while self.running:
            try:
                client, addr = self.socket.accept()
                self.client_sockets.append(client)
                # Handle client in separate thread
                threading.Thread(target=self.handle_client, args=(client,)).start()
            except OSError:
                break
                
    def stop(self):
        """Stop the UART device model server"""
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
        print(f"UART triggering interrupt {interrupt_id} to driver for device {self.device_id}")
        
        # Create interrupt message
        message = struct.pack('<IIIII', self.device_id, CMD_INTERRUPT, 0, interrupt_id, RESULT_SUCCESS)
        message += b'\x00' * 256  # Padding for data field
        
        # Send interrupt to all connected clients (driver interfaces)
        for client in self.client_sockets[:]:
            try:
                client.send(message)
                print(f"UART interrupt {interrupt_id} sent to driver")
            except Exception as e:
                print(f"Failed to send UART interrupt to client: {e}")
                if client in self.client_sockets:
                    self.client_sockets.remove(client)
                try:
                    client.close()
                except:
                    pass
                    
    def simulate_uart_activity(self):
        """Simulate UART device activity"""
        time.sleep(2)  # Wait after startup
        
        while self.running:
            try:
                # Check if UART is enabled
                if not (self.registers[UART_CTRL_OFFSET] & UART_CTRL_ENABLE):
                    time.sleep(1)
                    continue
                
                # Simulate incoming data
                if (self.registers[UART_CTRL_OFFSET] & UART_CTRL_RX_EN) and random.random() < 0.3:
                    if not self.rx_buffer.full():
                        # Simulate receiving some data
                        data = random.choice([0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x0D, 0x0A])  # "Hello\r\n"
                        self.rx_buffer.put(data)
                        self.registers[UART_STATUS_OFFSET] |= UART_STATUS_RX_FULL
                        
                        # Trigger RX interrupt if enabled
                        if self.registers[UART_CTRL_OFFSET] & UART_CTRL_IRQ_EN:
                            self.trigger_interrupt_to_driver(0x01)  # RX interrupt
                
                # Simulate transmission completion
                if (self.registers[UART_CTRL_OFFSET] & UART_CTRL_TX_EN) and not self.tx_buffer.empty():
                    try:
                        data = self.tx_buffer.get_nowait()
                        print(f"UART transmitted: 0x{data:02x} ('{chr(data)}' if printable)")
                        
                        # Update status
                        if self.tx_buffer.empty():
                            self.registers[UART_STATUS_OFFSET] |= UART_STATUS_TX_EMPTY
                            self.registers[UART_STATUS_OFFSET] |= UART_STATUS_TX_COMPLETE
                            
                            # Trigger TX complete interrupt if enabled
                            if self.registers[UART_CTRL_OFFSET] & UART_CTRL_IRQ_EN:
                                self.trigger_interrupt_to_driver(0x02)  # TX complete interrupt
                    except queue.Empty:
                        pass
                
                time.sleep(0.5)  # Simulation tick
                
            except Exception as e:
                print(f"Error in UART simulation: {e}")
                time.sleep(1)
                
    def handle_client(self, client_socket):
        """Handle communication with a client"""
        try:
            while self.running:
                # Receive message
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                # Parse message
                if len(data) >= 20:  # Minimum message size
                    device_id, command, address, length = struct.unpack('<IIII', data[:16])
                    
                    print(f"UART received: device_id={device_id}, cmd={command}, addr=0x{address:x}, len={length}")
                    
                    response = self.process_command(device_id, command, address, length, data[20:])
                    client_socket.send(response)
                    
        except Exception as e:
            print(f"Error handling UART client: {e}")
        finally:
            if client_socket in self.client_sockets:
                self.client_sockets.remove(client_socket)
            client_socket.close()
            
    def process_command(self, device_id, command, address, length, data):
        """Process a command and return response"""
        result = RESULT_SUCCESS
        response_data = b'\x00' * 256
        
        # Calculate register offset
        offset = address - self.base_address
        
        if command == CMD_READ:
            if offset in self.registers:
                if offset == UART_DATA_OFFSET:
                    # Reading data register
                    if not self.rx_buffer.empty():
                        try:
                            value = self.rx_buffer.get_nowait()
                            self.registers[UART_DATA_OFFSET] = value
                            # Update status
                            if self.rx_buffer.empty():
                                self.registers[UART_STATUS_OFFSET] &= ~UART_STATUS_RX_FULL
                        except queue.Empty:
                            value = 0
                    else:
                        value = self.registers[UART_DATA_OFFSET]
                else:
                    value = self.registers[offset]
                    
                response_data = struct.pack('<I', value) + b'\x00' * 252
                print(f"UART read 0x{address:x} (offset 0x{offset:x}) = 0x{value:x}")
            else:
                result = RESULT_INVALID_ADDR
                print(f"UART invalid read address: 0x{address:x}")
                
        elif command == CMD_WRITE:
            if len(data) >= 4 and offset in self.registers:
                value = struct.unpack('<I', data[:4])[0]
                
                if offset == UART_CTRL_OFFSET:
                    # Writing control register
                    self.registers[offset] = value
                    print(f"UART control set to 0x{value:x}")
                    
                    # Update status based on control
                    if value & UART_CTRL_ENABLE:
                        self.registers[UART_STATUS_OFFSET] |= UART_STATUS_READY
                    else:
                        self.registers[UART_STATUS_OFFSET] &= ~UART_STATUS_READY
                        
                elif offset == UART_DATA_OFFSET:
                    # Writing data register (transmit)
                    if not self.tx_buffer.full():
                        self.tx_buffer.put(value & 0xFF)
                        self.registers[UART_STATUS_OFFSET] &= ~UART_STATUS_TX_EMPTY
                        self.registers[UART_STATUS_OFFSET] &= ~UART_STATUS_TX_COMPLETE
                        print(f"UART queued for transmission: 0x{value & 0xFF:x}")
                    else:
                        print("UART TX buffer full, dropping data")
                        
                else:
                    # Other registers
                    self.registers[offset] = value
                    
                print(f"UART write 0x{address:x} (offset 0x{offset:x}) = 0x{value:x}")
            else:
                result = RESULT_ERROR
                print(f"UART invalid write to address: 0x{address:x}")
                
        elif command == CMD_INIT:
            print(f"UART device {device_id} initialized")
            self.registers[UART_STATUS_OFFSET] = UART_STATUS_READY | UART_STATUS_TX_EMPTY
            
        elif command == CMD_DEINIT:
            print(f"UART device {device_id} deinitialized")
            
        else:
            result = RESULT_ERROR
            print(f"UART unknown command: {command}")
            
        # Build response message
        response = struct.pack('<IIIII', device_id, command, address, length, result)
        response += response_data
        
        return response

def main():
    """Main function for testing"""
    uart = UartModel(device_id=1, base_address=0x40000000)
    
    try:
        print("Starting UART model interface...")
        uart.start()
    except KeyboardInterrupt:
        print("\nShutting down UART model interface...")
        uart.stop()

if __name__ == "__main__":
    main()