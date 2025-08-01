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
#define DRIVER_SOCKET_PATH "/tmp/icd3_driver_interface"

/* Global state */
static device_info_t devices[MAX_DEVICES];
static int device_count = 0;
static int server_socket = -1;
static interrupt_handler_t interrupt_handlers[MAX_DEVICES];

/* Function to calculate x86-64 instruction length */
static int get_instruction_length(uint8_t *instruction) {
    uint8_t *inst_ptr = instruction;
    int length = 0;
    
    /* Legacy prefixes (can have multiple) */
    while (*inst_ptr == 0xF0 || *inst_ptr == 0xF2 || *inst_ptr == 0xF3 ||  /* LOCK, REPNE, REP */
           *inst_ptr == 0x2E || *inst_ptr == 0x36 || *inst_ptr == 0x3E ||  /* Segment overrides */
           *inst_ptr == 0x26 || *inst_ptr == 0x64 || *inst_ptr == 0x65 ||
           *inst_ptr == 0x66 || *inst_ptr == 0x67) {                       /* Operand/Address size */
        inst_ptr++;
        length++;
    }
    
    /* REX prefix (only in 64-bit mode) */
    if (*inst_ptr >= 0x40 && *inst_ptr <= 0x4F) {
        inst_ptr++;
        length++;
    }
    
    /* Opcode (1-3 bytes) */
    uint8_t opcode = *inst_ptr++;
    length++;
    
    /* Handle two-byte opcodes */
    if (opcode == 0x0F) {
        opcode = *inst_ptr++;
        length++;
        
        /* Handle three-byte opcodes */
        if (opcode == 0x38 || opcode == 0x3A) {
            inst_ptr++;
            length++;
        }
    }
    
    /* ModR/M byte analysis */
    uint8_t modrm = *inst_ptr;
    uint8_t mod = (modrm >> 6) & 0x03;
    uint8_t rm = modrm & 0x07;
    
    /* Check if this instruction has ModR/M byte */
    int has_modrm = 0;
    
    /* Single-byte opcodes that have ModR/M */
    if (opcode == 0x88 || opcode == 0x89 || opcode == 0x8A || opcode == 0x8B ||
        opcode == 0xC6 || opcode == 0xC7) {
        has_modrm = 1;
    }
    /* String operations (STOS) don't use ModR/M but have implicit operands */
    else if (opcode == 0xAA || opcode == 0xAB) {
        has_modrm = 0;  /* STOSB/STOSW/STOSD use implicit [RDI] addressing */
    }
    /* Two-byte opcodes that have ModR/M (0x0F prefix already consumed) */
    else if (opcode == 0xB6 || opcode == 0xB7 || opcode == 0xBE || opcode == 0xBF) {
        has_modrm = 1;
    }
    
    if (has_modrm) {
        inst_ptr++;
        length++;
        
        /* SIB byte (if needed) */
        if (mod != 0x03 && rm == 0x04) {
            inst_ptr++;
            length++;
        }
        
        /* Displacement */
        if (mod == 0x01) {
            length += 1;  /* 8-bit displacement */
        } else if (mod == 0x02 || (mod == 0x00 && rm == 0x05)) {
            length += 4;  /* 32-bit displacement */
        }
        
        /* Immediate data */
        if (opcode == 0xC6) {
            length += 1;  /* 8-bit immediate */
        } else if (opcode == 0xC7) {
            /* Check for 16-bit operand size prefix */
            uint8_t *check_ptr = instruction;
            int has_66_prefix = 0;
            while (check_ptr < inst_ptr - 1) {
                if (*check_ptr == 0x66) {
                    has_66_prefix = 1;
                    break;
                }
                check_ptr++;
            }
            length += has_66_prefix ? 2 : 4;  /* 16-bit or 32-bit immediate */
        }
    }
    
    return length;
}

/* Signal handler for memory access violations */
static void segv_handler(int sig, siginfo_t *info, void *context) {
    (void)sig; /* Mark parameter as used */
    uintptr_t fault_addr = (uintptr_t)info->si_addr;
    ucontext_t *uctx = (ucontext_t *)context;
    
    /* Get the RIP (instruction pointer) to examine the instruction */
    uintptr_t rip = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
    uint8_t *instruction = (uint8_t *)rip;
    
    /* Calculate instruction length */
    int inst_length = get_instruction_length(instruction);
    printf("Instruction at RIP 0x%lx, length: %d bytes\n", rip, inst_length);
    
    /* Print the instruction bytes in hex */
    printf("Instruction bytes: ");
    for (int i = 0; i < inst_length && i < 15; i++) {  /* Limit to 15 bytes max for safety */
        printf("%02X ", instruction[i]);
    }
    printf("\n");
    
    /* Determine if it's a read or write instruction and access size */
    int is_write = 0;
    uint32_t access_size = 4; /* Default to 4 bytes */
    uint64_t write_data = 0;
    int is_rep_operation = 0; /* Track REP prefix for bulk operations like memset */
    uint64_t rep_count = 0;   /* Count for REP operations */
    
    /* Enhanced x86-64 instruction parsing */
    uint8_t *inst_ptr = instruction;
    int has_66_prefix = 0;  /* Track 16-bit operand size prefix */
    int has_rep_prefix = 0; /* Track REP/REPE/REPNE prefix */
    int is_two_byte_opcode = 0;  /* Track two-byte opcodes (0x0F prefix) */
    uint8_t first_opcode = 0;
    uint8_t second_opcode = 0;
    
    /* Skip prefixes and find the actual opcode */
    while (*inst_ptr == 0xF0 || *inst_ptr == 0xF2 || *inst_ptr == 0xF3 ||
           *inst_ptr == 0x2E || *inst_ptr == 0x36 || *inst_ptr == 0x3E ||
           *inst_ptr == 0x26 || *inst_ptr == 0x64 || *inst_ptr == 0x65 ||
           *inst_ptr == 0x67) {
        /* Check for REP/REPE/REPNE prefixes (used by memset) */
        if (*inst_ptr == 0xF2 || *inst_ptr == 0xF3) {
            has_rep_prefix = 1;
            printf("Detected REP prefix: 0x%02X\n", *inst_ptr);
        }
        inst_ptr++;
    }
    
    /* Check for 16-bit operand size prefix */
    if (*inst_ptr == 0x66) {
        has_66_prefix = 1;
        inst_ptr++;
    }
    
    /* Skip REX prefix if present (0x40-0x4F) */
    if (*inst_ptr >= 0x40 && *inst_ptr <= 0x4F) {
        inst_ptr++;
    }

    /* Get the opcode(s) */
    first_opcode = *inst_ptr++;
    if (first_opcode == 0x0F) {
        is_two_byte_opcode = 1;
        second_opcode = *inst_ptr;
    }

    /* Parse instruction opcode and determine size/operation */
    if (is_two_byte_opcode) {
        /* Handle two-byte opcodes (0x0F prefix) */
        switch (second_opcode) {
            case 0xB6: /* movzbl [mem], r32 - zero-extend 8-bit read */
                is_write = 0;
                access_size = 1;
                printf("Detected MOVZBL (0x0F 0xB6) 8-bit zero-extend READ instruction at RIP 0x%lx\n", rip);
                break;
            case 0xB7: /* movzwl [mem], r32 - zero-extend 16-bit read */
                is_write = 0;
                access_size = 2;
                printf("Detected MOVZWL (0x0F 0xB7) 16-bit zero-extend READ instruction at RIP 0x%lx\n", rip);
                break;
            case 0xBE: /* movsbl [mem], r32 - sign-extend 8-bit read */
                is_write = 0;
                access_size = 1;
                printf("Detected MOVSBL (0x0F 0xBE) 8-bit sign-extend READ instruction at RIP 0x%lx\n", rip);
                break;
            case 0xBF: /* movswl [mem], r32 - sign-extend 16-bit read */
                is_write = 0;
                access_size = 2;
                printf("Detected MOVSWL (0x0F 0xBF) 16-bit sign-extend READ instruction at RIP 0x%lx\n", rip);
                break;
            default:
                /* Unknown two-byte instruction - treat as 32-bit read for safety */
                is_write = 0;
                access_size = 4;
                printf("Unknown two-byte instruction 0x0F 0x%02X at RIP 0x%lx, treating as 32-bit READ\n", second_opcode, rip);
                break;
        }
    } else {
        /* Handle single-byte opcodes */
        switch (first_opcode) {
            case 0xAA: /* STOSB - store AL to [RDI], used by memset for 8-bit */
                if (has_rep_prefix) {
                    is_rep_operation = 1;
                    is_write = 1;
                    access_size = 1;
                    write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFF; /* AL register */
                    rep_count = uctx->uc_mcontext.gregs[REG_RCX] & 0xFFFFFFFF; /* ECX/RCX register */
                    printf("Detected REP STOSB (memset 8-bit) at RIP 0x%lx: value=0x%02lx, count=%lu\n", 
                           rip, write_data, rep_count);
                } else {
                    is_write = 1;
                    access_size = 1;
                    write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFF;
                    printf("Detected STOSB (single 8-bit store) at RIP 0x%lx\n", rip);
                }
                break;
            case 0xAB: /* STOSW/STOSD/STOSQ - store AX/EAX/RAX to [RDI], used by memset for 16/32/64-bit */
                if (has_rep_prefix) {
                    is_rep_operation = 1;
                    is_write = 1;
                    /* Determine access size based on prefixes and REX */
                    if (has_66_prefix) {
                        access_size = 2;
                        write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFF; /* AX register */
                        printf("Detected REP STOSW (memset 16-bit) at RIP 0x%lx: value=0x%04lx\n", 
                               rip, write_data);
                    } else {
                        /* Check if this is 64-bit mode (REX.W prefix would indicate STOSQ) */
                        uint8_t *rex_check = instruction;
                        int is_64bit = 0;
                        while (rex_check < inst_ptr) {
                            if (*rex_check >= 0x48 && *rex_check <= 0x4F && (*rex_check & 0x08)) {
                                is_64bit = 1; /* REX.W prefix present */
                                break;
                            }
                            rex_check++;
                        }
                        if (is_64bit) {
                            access_size = 8;
                            write_data = uctx->uc_mcontext.gregs[REG_RAX]; /* RAX register */
                            printf("Detected REP STOSQ (memset 64-bit) at RIP 0x%lx: value=0x%016lx\n", 
                                   rip, write_data);
                        } else {
                            access_size = 4;
                            write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFFFFFF; /* EAX register */
                            printf("Detected REP STOSD (memset 32-bit) at RIP 0x%lx: value=0x%08lx\n", 
                                   rip, write_data);
                        }
                    }
                    rep_count = uctx->uc_mcontext.gregs[REG_RCX] & 0xFFFFFFFF; /* ECX/RCX register */
                    printf("REP count: %lu\n", rep_count);
                } else {
                    is_write = 1;
                    access_size = has_66_prefix ? 2 : 4;
                    if (has_66_prefix) {
                        write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFF;
                        printf("Detected STOSW (single 16-bit store) at RIP 0x%lx\n", rip);
                    } else {
                        write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFFFFFF;
                        printf("Detected STOSD (single 32-bit store) at RIP 0x%lx\n", rip);
                    }
                }
                break;
            case 0x8A: /* mov r8, [mem] - 8-bit read */
                is_write = 0;
                access_size = 1;
                printf("Detected 8-bit READ instruction (0x8A) at RIP 0x%lx\n", rip);
                break;
            case 0x8B: /* mov r32/r16, [mem] - 32/16-bit read */
                is_write = 0;
                access_size = has_66_prefix ? 2 : 4;
                printf("Detected %d-bit READ instruction (0x8B) at RIP 0x%lx\n", access_size * 8, rip);
                break;
            case 0x88: /* mov [mem], r8 - 8-bit write */
                is_write = 1;
                access_size = 1;
                printf("Detected 8-bit WRITE instruction (0x88) at RIP 0x%lx\n", rip);
                /* Extract 8-bit data from low byte of register */
                write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFF; /* Simplified: assume AL register */
                break;
            case 0x89: /* mov [mem], r32/r16 - 32/16-bit write */
                is_write = 1;
                access_size = has_66_prefix ? 2 : 4;
                printf("Detected %d-bit WRITE instruction (0x89) at RIP 0x%lx\n", access_size * 8, rip);
                /* Extract data from register */
                if (has_66_prefix) {
                    write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFF; /* 16-bit */
                } else {
                    write_data = uctx->uc_mcontext.gregs[REG_RAX] & 0xFFFFFFFF; /* 32-bit */
                }
                break;
            case 0xC6: /* mov [mem], imm8 - 8-bit immediate write */
                is_write = 1;
                access_size = 1;
                printf("Detected 8-bit immediate WRITE instruction (0xC6) at RIP 0x%lx\n", rip);
                /* Extract immediate value from instruction stream */
                uint8_t *imm_ptr = instruction + inst_length - 1; /* Last byte is immediate */
                write_data = *imm_ptr;
                break;
            case 0xC7: /* mov [mem], imm32/imm16 - 32/16-bit immediate write */
                is_write = 1;
                access_size = has_66_prefix ? 2 : 4;
                printf("Detected %d-bit immediate WRITE instruction (0xC7) at RIP 0x%lx\n", access_size * 8, rip);
                /* Extract immediate value from instruction stream */
                if (has_66_prefix) {
                    uint8_t *imm_ptr = instruction + inst_length - 2; /* Last 2 bytes are immediate */
                    write_data = *(uint16_t *)imm_ptr;
                } else {
                    uint8_t *imm_ptr = instruction + inst_length - 4; /* Last 4 bytes are immediate */
                    write_data = *(uint32_t *)imm_ptr;
                }
                break;
            default:
                /* Unknown instruction - treat as 32-bit read for safety */
                is_write = 0;
                access_size = 4;
                printf("Unknown instruction 0x%02X at RIP 0x%lx, treating as 32-bit READ\n", first_opcode, rip);
                break;
        }
    }
    
    /* Handle REP operations (like memset) by performing multiple individual operations */
    if (is_rep_operation && rep_count > 0) {
        printf("Processing REP operation: %lu iterations of %d-byte writes\n", rep_count, access_size);
        
        /* Get destination address from RDI register */
        uintptr_t dest_addr = (uintptr_t)uctx->uc_mcontext.gregs[REG_RDI];
        printf("REP destination address: 0x%lx\n", dest_addr);
        
        /* Perform the bulk write operation as individual writes to device model */
        for (uint64_t i = 0; i < rep_count; i++) {
            uintptr_t current_addr = dest_addr + (i * access_size);
            
            /* Find which device this address belongs to */
            int device_found = 0;
            for (int j = 0; j < device_count; j++) {
                uint32_t device_base = devices[j].base_address;
                uint32_t device_size = devices[j].size;
                
                if (current_addr >= device_base && current_addr < device_base + device_size) {
                    device_found = 1;
                    
                    /* Send write operation to device model */
                    protocol_message_t message = {0};
                    message.device_id = devices[j].device_id;
                    message.command = CMD_WRITE;
                    message.address = (uint32_t)current_addr;
                    message.length = access_size;
                    memcpy(message.data, &write_data, access_size);
                    
                    protocol_message_t response = {0};
                    if (send_message_to_model(&message, &response) != 0) {
                        printf("Failed to send REP write operation %lu to device model\n", i);
                        break;
                    }
                    break;
                }
            }
            
            if (!device_found) {
                printf("REP operation address 0x%lx not in any registered device\n", current_addr);
                /* For addresses outside device ranges, this would be an actual segfault */
                printf("Actual segmentation fault during REP operation at address 0x%lx\n", current_addr);
                exit(1);
            }
        }
        
        /* Update registers after REP operation */
        /* RDI = RDI + (count * access_size) */
        uctx->uc_mcontext.gregs[REG_RDI] += (rep_count * access_size);
        /* RCX = 0 (count register is decremented to 0) */
        uctx->uc_mcontext.gregs[REG_RCX] = 0;
        
        printf("REP operation completed: wrote %lu x %d bytes, updated RDI to 0x%lx, RCX to 0\n", 
               rep_count, access_size, (uintptr_t)uctx->uc_mcontext.gregs[REG_RDI]);
        
        /* Advance RIP to skip the REP+STOS instruction */
        printf("Advancing RIP by %d bytes (from 0x%lx to 0x%lx)\n", 
               inst_length, rip, rip + inst_length);
        uctx->uc_mcontext.gregs[REG_RIP] += inst_length;
        return;
    }
    
    /* Find which device this address belongs to by comparing with device base addresses */
    printf("Looking for device containing fault address 0x%lx\n", fault_addr);
    printf("Current device count: %d\n", device_count);
    
    for (int i = 0; i < device_count; i++) {
        uint32_t device_base = devices[i].base_address;
        uint32_t device_size = devices[i].size;
        printf("Device %d: base_address=0x%x, size=0x%x, range=0x%x-0x%x\n", 
               devices[i].device_id, device_base, device_size, device_base, device_base + device_size - 1);
        
        /* Check if fault address falls within this device's address range */
        if (fault_addr >= device_base && fault_addr < device_base + device_size) {
            printf("Memory access violation at device %d, address 0x%lx (%s, %d bytes)\n", 
                   devices[i].device_id, fault_addr, is_write ? "WRITE" : "READ", access_size);
            
            /* Handle the actual read/write operation by sending it to the device model */
            protocol_message_t message = {0};
            message.device_id = devices[i].device_id;
            message.command = is_write ? CMD_WRITE : CMD_READ;
            message.address = (uint32_t)fault_addr;
            message.length = access_size;
            
            if (is_write) {
                /* Copy write data to message based on access size */
                memcpy(message.data, &write_data, access_size);
                printf("Writing %d-byte value: 0x%lx\n", access_size, write_data);
            }
            
            protocol_message_t response = {0};
            if (send_message_to_model(&message, &response) == 0) {
                if (!is_write && response.result == RESULT_SUCCESS) {
                    /* For reads, simulate storing result back to memory location */
                    uint64_t read_data = 0;
                    memcpy(&read_data, response.data, access_size);
                    printf("Read completed, %d-byte data: 0x%lx\n", access_size, read_data);
                    
                    /* In a real implementation, we would update the destination register 
                     * based on the instruction's ModR/M byte. For now, just complete the operation. */
                }
                
                /* Advance RIP to skip the faulting instruction using calculated length */
                printf("Advancing RIP by %d bytes (from 0x%lx to 0x%lx)\n", 
                       inst_length, rip, rip + inst_length);
                uctx->uc_mcontext.gregs[REG_RIP] += inst_length;
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
    strncpy(addr.sun_path, DRIVER_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    /* Remove existing socket file */
    unlink(DRIVER_SOCKET_PATH);
    
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
        unlink(DRIVER_SOCKET_PATH);
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
    printf("Sending to model: device_id=%d, cmd=%d, addr=0x%x, len=%d\n",
           message->device_id, message->command, message->address, message->length);
    
    /* Try to connect to Python model via socket */
    int model_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (model_socket == -1) {
        printf("Failed to create socket for model communication\n");
        return -1;
    }
    
    struct sockaddr_un model_addr;
    memset(&model_addr, 0, sizeof(model_addr));
    model_addr.sun_family = AF_UNIX;
    strncpy(model_addr.sun_path, SOCKET_PATH, sizeof(model_addr.sun_path) - 1);
    
    /* Attempt to connect to model - Unix sockets usually connect immediately */
    int connect_result = connect(model_socket, (struct sockaddr*)&model_addr, sizeof(model_addr));
    if (connect_result == -1) {
        /* Model not available, fall back to simulation */
        printf("Model not available (connect failed: %s), using simulation\n", strerror(errno));
        close(model_socket);
        goto simulation_fallback;
    }
    
    printf("Connected to model successfully\n");
    
    /* Send message to model */
    ssize_t bytes_sent = send(model_socket, message, sizeof(protocol_message_t), 0);
    if (bytes_sent != sizeof(protocol_message_t)) {
        printf("Failed to send complete message to model (%zd/%zu bytes), using simulation\n", 
               bytes_sent, sizeof(protocol_message_t));
        if (bytes_sent == -1) {
            printf("send() error: %s\n", strerror(errno));
        }
        close(model_socket);
        goto simulation_fallback;
    }
    
    printf("Message sent to model (%zd bytes)\n", bytes_sent);
    
    /* Receive response from model */
    if (response) {
        ssize_t bytes_received = recv(model_socket, response, sizeof(protocol_message_t), 0);
        if (bytes_received != sizeof(protocol_message_t)) {
            printf("Failed to receive complete response from model (%zd/%zu bytes), using simulation\n", 
                   bytes_received, sizeof(protocol_message_t));
            close(model_socket);
            goto simulation_fallback;
        }
        printf("Received response from model: result=%d (%zd bytes)\n", response->result, bytes_received);
    }
    
    close(model_socket);
    return 0;

simulation_fallback:
    /* Fallback simulation logic */
    if (response) {
        memcpy(response, message, sizeof(protocol_message_t));
        response->result = RESULT_SUCCESS;
        
        if (message->command == CMD_READ) {
            uint32_t simulated_data = 0xDEADBEEF;
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
