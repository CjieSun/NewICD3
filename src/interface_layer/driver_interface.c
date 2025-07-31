#define _GNU_SOURCE
#include "interface_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <ucontext.h>

#define MAX_DEVICES 16
#define SOCKET_PATH "/tmp/icd3_interface"

/* Global state */
static device_info_t devices[MAX_DEVICES];
static int device_count = 0;
static int server_socket = -1;
static interrupt_handler_t interrupt_handlers[MAX_DEVICES];

/* Signal handler for memory access violations */
static void segv_handler(int sig, siginfo_t *info, void *context) {
    uintptr_t fault_addr = (uintptr_t)info->si_addr;
    ucontext_t *uctx = (ucontext_t *)context;
    
    /* Get the RIP (instruction pointer) to examine the instruction */
    uintptr_t rip = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
    uint8_t *instruction = (uint8_t *)rip;
    
    /* Determine if it's a read or write instruction */
    int is_write = 0;
    uint32_t access_size = 4; /* Default to 4 bytes */
    
    /* Parse x86 instruction to determine read/write */
    if (*instruction == 0x8B) {
        /* mov reg, [mem] → read instruction */
        is_write = 0;
        printf("Detected READ instruction (0x8B) at RIP 0x%lx\n", rip);
    } else if (*instruction == 0x89) {
        /* mov [mem], reg → write instruction */
        is_write = 1;
        printf("Detected WRITE instruction (0x89) at RIP 0x%lx\n", rip);
    } else if (*instruction == 0xC7) {
        /* mov [mem], imm32 → write instruction (immediate) */
        is_write = 1;
        printf("Detected WRITE instruction with immediate (0xC7) at RIP 0x%lx\n", rip);
    } else {
        /* Unknown instruction or unsupported - treat as read for safety */
        is_write = 0;
        printf("Unknown instruction 0x%02X at RIP 0x%lx, treating as READ\n", *instruction, rip);
    }
    
    /* Find which device this address belongs to */
    for (int i = 0; i < device_count; i++) {
        uintptr_t base = (uintptr_t)devices[i].mapped_memory;
        if (fault_addr >= base && fault_addr < base + devices[i].size) {
            uint32_t offset = fault_addr - base;
            uint32_t device_addr = devices[i].base_address + offset;
            
            printf("Memory access violation at device %d, address 0x%x (%s)\n", 
                   devices[i].device_id, device_addr, is_write ? "WRITE" : "READ");
            
            /* Handle the actual read/write operation by sending it to the device model */
            protocol_message_t message = {0};
            message.device_id = devices[i].device_id;
            message.command = is_write ? CMD_WRITE : CMD_READ;
            message.address = device_addr;
            message.length = access_size;
            
            if (is_write) {
                /* For writes, we need to extract the data from registers 
                 * This is simplified - in a real implementation we'd need to 
                 * fully decode the instruction to get the source operand */
                uint32_t write_data = 0; /* Placeholder - would extract from instruction/registers */
                memcpy(message.data, &write_data, access_size);
            }
            
            protocol_message_t response = {0};
            if (send_message_to_model(&message, &response) == 0) {
                if (!is_write && response.result == RESULT_SUCCESS) {
                    /* For reads, we would need to store the result back to the destination register
                     * This is simplified - in a real implementation we'd need to fully decode 
                     * the instruction and update the appropriate register */
                    printf("Read completed, data: 0x%x\n", *(uint32_t*)response.data);
                }
            }
            return;
        }
    }
    
    /* If we get here, it's an actual segmentation fault */
    printf("Actual segmentation fault at address %p\n", info->si_addr);
    exit(1);
}

int interface_layer_init(void) {
    /* Install signal handler for SIGSEGV */
    struct sigaction sa;
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("Failed to install SIGSEGV handler");
        return -1;
    }
    
    /* Initialize socket for communication with device models */
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Failed to create socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    /* Remove existing socket file */
    unlink(SOCKET_PATH);
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Failed to bind socket");
        close(server_socket);
        return -1;
    }
    
    if (listen(server_socket, 5) == -1) {
        perror("Failed to listen on socket");
        close(server_socket);
        return -1;
    }
    
    printf("Driver interface initialized successfully\n");
    return 0;
}

int interface_layer_deinit(void) {
    /* Unmap all device memory */
    for (int i = 0; i < device_count; i++) {
        if (devices[i].mapped_memory) {
            munmap(devices[i].mapped_memory, devices[i].size);
        }
        if (devices[i].socket_fd != -1) {
            close(devices[i].socket_fd);
        }
    }
    
    if (server_socket != -1) {
        close(server_socket);
        unlink(SOCKET_PATH);
    }
    
    device_count = 0;
    printf("Driver interface deinitialized\n");
    return 0;
}

int register_device(uint32_t device_id, uint32_t base_address, uint32_t size) {
    if (device_count >= MAX_DEVICES) {
        printf("Maximum number of devices reached\n");
        return -1;
    }
    
    /* Allocate memory with no access permissions */
    void *mapped_mem = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped_mem == MAP_FAILED) {
        perror("Failed to map memory");
        return -1;
    }
    
    devices[device_count].device_id = device_id;
    devices[device_count].base_address = base_address;
    devices[device_count].size = size;
    devices[device_count].mapped_memory = mapped_mem;
    devices[device_count].socket_fd = -1;
    
    printf("Registered device %d at base address 0x%x, size %d bytes\n", 
           device_id, base_address, size);
    
    device_count++;
    return 0;
}

int unregister_device(uint32_t device_id) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].device_id == device_id) {
            if (devices[i].mapped_memory) {
                munmap(devices[i].mapped_memory, devices[i].size);
            }
            if (devices[i].socket_fd != -1) {
                close(devices[i].socket_fd);
            }
            
            /* Move last device to this position */
            if (i < device_count - 1) {
                devices[i] = devices[device_count - 1];
            }
            device_count--;
            
            printf("Unregistered device %d\n", device_id);
            return 0;
        }
    }
    
    printf("Device %d not found\n", device_id);
    return -1;
}

uint32_t read_register(uint32_t address, uint32_t size) {
    /* Find device for this address */
    for (int i = 0; i < device_count; i++) {
        if (address >= devices[i].base_address && 
            address < devices[i].base_address + devices[i].size) {
            
            protocol_message_t message = {0};
            message.device_id = devices[i].device_id;
            message.command = CMD_READ;
            message.address = address;
            message.length = size;
            
            protocol_message_t response = {0};
            if (send_message_to_model(&message, &response) == 0) {
                return *(uint32_t*)response.data;
            }
        }
    }
    
    printf("Read from unmapped address 0x%x\n", address);
    return 0;
}

int write_register(uint32_t address, uint32_t data, uint32_t size) {
    /* Find device for this address */
    for (int i = 0; i < device_count; i++) {
        if (address >= devices[i].base_address && 
            address < devices[i].base_address + devices[i].size) {
            
            protocol_message_t message = {0};
            message.device_id = devices[i].device_id;
            message.command = CMD_WRITE;
            message.address = address;
            message.length = size;
            memcpy(message.data, &data, size);
            
            protocol_message_t response = {0};
            return send_message_to_model(&message, &response);
        }
    }
    
    printf("Write to unmapped address 0x%x\n", address);
    return -1;
}

int register_interrupt_handler(uint32_t device_id, interrupt_handler_t handler) {
    if (device_id < MAX_DEVICES) {
        interrupt_handlers[device_id] = handler;
        return 0;
    }
    return -1;
}

int trigger_interrupt(uint32_t device_id, uint32_t interrupt_id) {
    if (device_id < MAX_DEVICES && interrupt_handlers[device_id]) {
        interrupt_handlers[device_id](device_id, interrupt_id);
        return 0;
    }
    return -1;
}

int send_message_to_model(const protocol_message_t *message, protocol_message_t *response) {
    /* For now, simulate the communication */
    printf("Sending to model: device_id=%d, cmd=%d, addr=0x%x, len=%d\n",
           message->device_id, message->command, message->address, message->length);
    
    /* Simulate response */
    if (response) {
        memcpy(response, message, sizeof(protocol_message_t));
        response->result = RESULT_SUCCESS;
        
        if (message->command == CMD_READ) {
            /* Simulate reading some data based on address */
            uint32_t simulated_data = 0xDEADBEEF;
            
            /* Simulate STATUS register with READY bit set */
            if ((message->address & 0xFF) == 0x04) {  /* STATUS register offset */
                simulated_data = 0x00000001;  /* READY bit set */
            }
            
            memcpy(response->data, &simulated_data, sizeof(simulated_data));
        }
    }
    
    return 0;
}

/* Function to receive and handle interrupts from Python model */
int handle_model_interrupts(void) {
    /* This would typically be called in a separate thread to listen for 
     * incoming interrupts from the Python model via socket */
    
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    
    /* Non-blocking check for incoming connections */
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;  /* 100ms timeout */
    
    int activity = select(server_socket + 1, &readfds, NULL, NULL, &timeout);
    
    if (activity > 0 && FD_ISSET(server_socket, &readfds)) {
        /* Accept incoming connection from model */
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket >= 0) {
            printf("Model connected for interrupt delivery\n");
            
            /* Read interrupt message */
            protocol_message_t interrupt_msg;
            ssize_t bytes_read = recv(client_socket, &interrupt_msg, sizeof(interrupt_msg), 0);
            
            if (bytes_read == sizeof(interrupt_msg) && interrupt_msg.command == CMD_INTERRUPT) {
                printf("Received interrupt from model: device_id=%d, interrupt_id=%d\n",
                       interrupt_msg.device_id, interrupt_msg.length);
                
                /* Trigger the interrupt handler */
                if (trigger_interrupt(interrupt_msg.device_id, interrupt_msg.length) == 0) {
                    printf("Interrupt from model processed successfully\n");
                } else {
                    printf("Failed to process interrupt from model\n");
                }
            }
            
            close(client_socket);
        }
    }
    
    return 0;
}