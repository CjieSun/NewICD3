#include "interface_layer.h"
#include "logging.h"
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
#define MAX_IRQS 16

#define SOCKET_PATH "/tmp/icd3_interface"
#define DRIVER_SOCKET_PATH "/tmp/icd3_driver_interface"
#define DRIVER_PID_FILE "/tmp/icd3_driver_pid"

/* Global state */
static device_info_t devices[MAX_DEVICES];
static int device_count = 0;
static int server_socket = -1;
static interrupt_handler_t interrupt_handlers[MAX_IRQS];

/* Signal-based interrupt handling */
static volatile uint32_t pending_device_interrupt = 0;
static volatile uint32_t pending_interrupt_id = 0;
static volatile sig_atomic_t interrupt_pending = 0;

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

/* Helper function to extract destination register from ModR/M byte for read operations */
static int get_destination_register_from_modrm(uint8_t *instruction, int is_two_byte_opcode, uint8_t opcode) {
    (void)opcode; /* Mark parameter as used - may be needed for future instruction-specific handling */
    uint8_t *inst_ptr = instruction;
    
    /* Skip prefixes */
    while (*inst_ptr == 0xF0 || *inst_ptr == 0xF2 || *inst_ptr == 0xF3 ||
           *inst_ptr == 0x2E || *inst_ptr == 0x36 || *inst_ptr == 0x3E ||
           *inst_ptr == 0x26 || *inst_ptr == 0x64 || *inst_ptr == 0x65 ||
           *inst_ptr == 0x66 || *inst_ptr == 0x67) {
        inst_ptr++;
    }
    
    /* Skip REX prefix if present */
    uint8_t rex_prefix = 0;
    if (*inst_ptr >= 0x40 && *inst_ptr <= 0x4F) {
        rex_prefix = *inst_ptr;
        inst_ptr++;
    }
    
    /* Skip opcode(s) */
    inst_ptr++; /* Skip first opcode byte */
    if (is_two_byte_opcode) {
        inst_ptr++; /* Skip second opcode byte for 0x0F prefixed instructions */
    }
    
    /* Get ModR/M byte */
    uint8_t modrm = *inst_ptr;
    uint8_t reg_field = (modrm >> 3) & 0x07; /* Bits 5-3 contain register field */
    
    /* Apply REX.R extension if present */
    if (rex_prefix & 0x04) { /* REX.R bit set */
        reg_field |= 0x08;
    }
    
    /* Map register field to x86-64 register constants */
    /* For most read instructions, the reg field specifies the destination register */
    switch (reg_field) {
        case 0: return REG_RAX;
        case 1: return REG_RCX;
        case 2: return REG_RDX;
        case 3: return REG_RBX;
        case 4: return REG_RSP;
        case 5: return REG_RBP;
        case 6: return REG_RSI;
        case 7: return REG_RDI;
        case 8: return REG_R8;
        case 9: return REG_R9;
        case 10: return REG_R10;
        case 11: return REG_R11;
        case 12: return REG_R12;
        case 13: return REG_R13;
        case 14: return REG_R14;
        case 15: return REG_R15;
        default: return REG_RAX; /* Default fallback */
    }
}

/* Signal handler for interrupt delivery from models */
static void interrupt_signal_handler(int sig, siginfo_t *info, void *context) {
    (void)sig;      /* Mark parameter as used */
    (void)context;  /* Mark parameter as used */
    (void)info;     /* Mark parameter as used */
    
    /* Read interrupt details from temporary file */
    char interrupt_file[256];
    snprintf(interrupt_file, sizeof(interrupt_file), "/tmp/icd3_interrupt_%d", getpid());
    
    FILE *f = fopen(interrupt_file, "r");
    if (f) {
        uint32_t device_id, interrupt_id;
        if (fscanf(f, "%u,%u", &device_id, &interrupt_id) == 2) {
            pending_device_interrupt = device_id;
            pending_interrupt_id = interrupt_id;
            interrupt_pending = 1;
            
            printf("Signal interrupt received: device_id=%d, interrupt_id=0x%x\n", 
                   device_id, interrupt_id);
            interrupt_handlers[interrupt_id](interrupt_id);
        }
        fclose(f);
        /* File will be cleaned up by Python model */
    }
}
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
    
    /* Check for REP STOS instructions first (memset-style bulk operations) */
    uint8_t *rep_inst_ptr = instruction;
    int is_rep_stos = 0;
    int stos_size = 1; /* Default to byte size */
    
    /* Check for REP prefix (0xF3) */
    if (*rep_inst_ptr == 0xF3) {
        rep_inst_ptr++;
        
        /* Check for 16-bit operand size prefix before STOS */
        if (*rep_inst_ptr == 0x66) {
            stos_size = 2; /* 16-bit word */
            rep_inst_ptr++;
        }
        
        /* Check for STOS opcodes */
        if (*rep_inst_ptr == 0xAA) {
            /* REP STOSB - repeat store byte */
            is_rep_stos = 1;
            stos_size = 1;
            printf("Detected REP STOSB instruction (bulk byte write)\n");
        } else if (*rep_inst_ptr == 0xAB) {
            /* REP STOSD or REP STOSW - repeat store dword/word */
            is_rep_stos = 1;
            if (stos_size != 2) {
                stos_size = 4; /* Default to 32-bit dword if no 0x66 prefix */
            }
            printf("Detected REP STOS%c instruction (bulk %d-byte write)\n", 
                   stos_size == 2 ? 'W' : 'D', stos_size);
        }
    }
    
    /* Check for AVX/SIMD memset patterns (VEX prefix 0xC5) */
    uint8_t *avx_inst_ptr = instruction;
    int is_avx_memset = 0;
    
    if (*avx_inst_ptr == 0xC5) {
        /* This is a VEX-encoded AVX instruction, common in optimized memset */
        printf("Detected VEX-encoded AVX instruction (likely optimized memset)\n");
        
        /* For AVX memset, we'll simulate it as a series of writes based on the fault address */
        /* We can't easily decode the full AVX instruction, but we can infer the operation */
        /* from the context and handle it as a bulk write operation */
        
        /* Try to determine the operation size from the second VEX byte */
        avx_inst_ptr++; /* Skip 0xC5 */
        uint8_t vex_byte2 = *avx_inst_ptr;
        (void)vex_byte2; /* Mark as used to avoid compiler warning */
        
        /* For now, treat AVX memset as a conservative bulk operation */
        /* We'll use heuristics: if RCX contains a reasonable count, use it */
        uint64_t rcx_val = uctx->uc_mcontext.gregs[REG_RCX];
        uint64_t rdi_val = uctx->uc_mcontext.gregs[REG_RDI];
        
        /* If RDI points to our device memory and RCX looks like a count, handle as bulk write */
        for (int i = 0; i < device_count; i++) {
            uint32_t device_base = devices[i].base_address;
            uint32_t device_size = devices[i].size;
            
            if (fault_addr >= device_base && fault_addr < device_base + device_size) {
                /* This AVX instruction is targeting our device memory */
                if (rcx_val > 0 && rcx_val <= 1024 && rdi_val == fault_addr) {
                    /* Looks like a memset pattern: RDI=destination, RCX=count */
                    is_avx_memset = 1;
                    printf("AVX memset pattern detected: dest=0x%lx, count=%lu\n", rdi_val, rcx_val);
                    
                    /* Perform conservative byte-wise memset simulation */
                    uint64_t value = uctx->uc_mcontext.gregs[REG_RAX] & 0xFF; /* Use AL for value */
                    
                    printf("Simulating AVX memset: %lu bytes of 0x%02lx at 0x%lx\n", 
                           rcx_val, value, rdi_val);
                    
                    for (uint64_t j = 0; j < rcx_val; j++) {
                        uint32_t write_addr = (uint32_t)(rdi_val + j);
                        
                        /* Ensure we stay within device bounds */
                        if (write_addr >= device_base + device_size) {
                            break;
                        }
                        
                        protocol_message_t message = {0};
                        message.device_id = devices[i].device_id;
                        message.command = CMD_WRITE;
                        message.address = write_addr;
                        message.length = 1;
                        uint8_t write_val = (uint8_t)value;
                        memcpy(message.data, &write_val, 1);
                        
                        protocol_message_t response = {0};
                        if (send_message_to_model(&message, &response) != 0) {
                            printf("Failed to send AVX memset write\n");
                            break;
                        }
                    }
                    
                    /* Update registers to simulate completion */
                    uctx->uc_mcontext.gregs[REG_RCX] = 0;
                    uctx->uc_mcontext.gregs[REG_RDI] = rdi_val + rcx_val;
                    
                    /* Skip the AVX instruction (conservative approach) */
                    uctx->uc_mcontext.gregs[REG_RIP] += inst_length;
                    printf("AVX memset simulation completed, advanced RIP by %d bytes\n", inst_length);
                    
                    return;
                } else {
                    /* AVX instruction but doesn't look like memset, fall back to single operation */
                    printf("AVX instruction targeting device but not memset pattern, treating as single write\n");
                    is_avx_memset = 0;
                }
                break;
            }
        }
        
        if (!is_avx_memset) {
            /* AVX instruction but not targeting our device or not memset pattern */
            printf("AVX instruction not targeting device memory or not memset, treating as regular instruction\n");
        }
    }
    
    /* Handle REP STOS or AVX memset instructions */
    if (is_rep_stos || is_avx_memset) {
        if (is_avx_memset) {
            /* AVX memset was already handled above */
            return;
        }
        
        /* Handle REP STOS instructions */
        /* Extract registers for REP STOS operation */
        uint64_t count = uctx->uc_mcontext.gregs[REG_RCX];     /* Count of operations */
        uint64_t dest_addr = uctx->uc_mcontext.gregs[REG_RDI]; /* Destination address */
        uint64_t value = uctx->uc_mcontext.gregs[REG_RAX];     /* Value to store */
        
        printf("REP STOS: count=%lu, dest=0x%lx, value=0x%lx, size=%d\n",
               count, dest_addr, value, stos_size);
        
        /* Find which device this address range belongs to */
        for (int i = 0; i < device_count; i++) {
            uint32_t device_base = devices[i].base_address;
            uint32_t device_size = devices[i].size;
            
            /* Check if the start address falls within this device's range */
            if (dest_addr >= device_base && dest_addr < device_base + device_size) {
                printf("REP STOS operation targeting device %d (base=0x%x, size=0x%x)\n",
                       devices[i].device_id, device_base, device_size);
                
                /* Ensure the entire operation stays within device bounds */
                uint64_t end_addr = dest_addr + (count * stos_size);
                if (end_addr > device_base + device_size) {
                    printf("WARNING: REP STOS operation extends beyond device bounds, truncating\n");
                    count = (device_base + device_size - dest_addr) / stos_size;
                }
                
                /* Perform bulk write operation */
                printf("Performing bulk write: %lu x %d-byte writes starting at 0x%lx\n",
                       count, stos_size, dest_addr);
                
                for (uint64_t j = 0; j < count; j++) {
                    uint32_t write_addr = (uint32_t)(dest_addr + j * stos_size);
                    uint64_t write_val = value;
                    
                    /* Mask value according to size */
                    if (stos_size == 1) {
                        write_val &= 0xFF;
                    } else if (stos_size == 2) {
                        write_val &= 0xFFFF;
                    } else {
                        write_val &= 0xFFFFFFFF;
                    }
                    
                    /* Send write command to device model */
                    protocol_message_t message = {0};
                    message.device_id = devices[i].device_id;
                    message.command = CMD_WRITE;
                    message.address = write_addr;
                    message.length = stos_size;
                    memcpy(message.data, &write_val, stos_size);
                    
                    protocol_message_t response = {0};
                    if (send_message_to_model(&message, &response) != 0) {
                        printf("Failed to send write command for REP STOS operation\n");
                        break;
                    }
                    
                    if (response.result != RESULT_SUCCESS) {
                        printf("Device model returned error for REP STOS write\n");
                        break;
                    }
                }
                
                /* Update CPU registers to reflect completed REP STOS operation */
                uctx->uc_mcontext.gregs[REG_RCX] = 0; /* Count decrements to 0 */
                uctx->uc_mcontext.gregs[REG_RDI] = dest_addr + (count * stos_size); /* Final destination */
                
                printf("REP STOS completed: RCX=0, RDI=0x%lx\n", 
                       (uint64_t)uctx->uc_mcontext.gregs[REG_RDI]);
                
                /* Skip the entire REP STOS instruction */
                uctx->uc_mcontext.gregs[REG_RIP] += inst_length;
                printf("Advanced RIP by %d bytes (from 0x%lx to 0x%lx)\n",
                       inst_length, rip, rip + inst_length);
                
                return; /* REP STOS handling complete */
            }
        }
        
        /* If we get here, the address is not in any registered device */
        printf("REP STOS targeting unmapped address 0x%lx\n", dest_addr);
        exit(1);
    }
    
    /* Determine if it's a read or write instruction and access size */
    int is_write = 0;
    uint32_t access_size = 4; /* Default to 4 bytes */
    uint64_t write_data = 0;
    
    /* Enhanced x86-64 instruction parsing */
    uint8_t *inst_ptr = instruction;
    int has_66_prefix = 0;  /* Track 16-bit operand size prefix */
    int is_two_byte_opcode = 0;  /* Track two-byte opcodes (0x0F prefix) */
    uint8_t first_opcode = 0;
    uint8_t second_opcode = 0;
    
    /* Skip prefixes and find the actual opcode */
    while (*inst_ptr == 0xF0 || *inst_ptr == 0xF2 || *inst_ptr == 0xF3 ||
           *inst_ptr == 0x2E || *inst_ptr == 0x36 || *inst_ptr == 0x3E ||
           *inst_ptr == 0x26 || *inst_ptr == 0x64 || *inst_ptr == 0x65 ||
           *inst_ptr == 0x67) {
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
                    /* For reads, backfill the destination register with the read data */
                    uint64_t read_data = 0;
                    memcpy(&read_data, response.data, access_size);
                    printf("Read completed, %d-byte data: 0x%lx\n", access_size, read_data);
                    
                    /* Determine destination register from ModR/M byte and update it */
                    int dest_reg = get_destination_register_from_modrm(instruction, is_two_byte_opcode, 
                                                                     is_two_byte_opcode ? second_opcode : first_opcode);
                    
                    /* Update the destination register based on access size */
                    if (access_size == 1) {
                        /* 8-bit read: only update low byte, preserve high bits */
                        uctx->uc_mcontext.gregs[dest_reg] = (uctx->uc_mcontext.gregs[dest_reg] & 0xFFFFFFFFFFFFFF00ULL) | (read_data & 0xFF);
                    } else if (access_size == 2) {
                        /* 16-bit read: only update low 16 bits, preserve high bits */
                        uctx->uc_mcontext.gregs[dest_reg] = (uctx->uc_mcontext.gregs[dest_reg] & 0xFFFFFFFFFFFF0000ULL) | (read_data & 0xFFFF);
                    } else if (access_size == 4) {
                        /* 32-bit read: clear high 32 bits, set low 32 bits (x86-64 convention) */
                        uctx->uc_mcontext.gregs[dest_reg] = read_data & 0xFFFFFFFF;
                    } else {
                        /* 64-bit read: set entire register */
                        uctx->uc_mcontext.gregs[dest_reg] = read_data;
                    }
                    
                    printf("Updated register %d with read data: 0x%llx\n", dest_reg, (unsigned long long)uctx->uc_mcontext.gregs[dest_reg]);
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
    struct sigaction sa_segv;
    sa_segv.sa_sigaction = segv_handler;
    sigemptyset(&sa_segv.sa_mask);
    sa_segv.sa_flags = SA_SIGINFO;
    
    if (sigaction(SIGSEGV, &sa_segv, NULL) == -1) {
        perror("Failed to install SIGSEGV handler");
        return -1;
    }
    
    /* Install signal handler for SIGUSR1 (interrupt delivery) */
    struct sigaction sa_usr1;
    sa_usr1.sa_sigaction = interrupt_signal_handler;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = SA_SIGINFO;
    
    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("Failed to install SIGUSR1 handler for interrupts");
        return -1;
    }
    
    /* Initialize signal-based interrupt state */
    pending_device_interrupt = 0;
    pending_interrupt_id = 0;
    interrupt_pending = 0;
    
    /* Write PID to file for Python models to use */
    FILE *pid_file = fopen(DRIVER_PID_FILE, "w");
    if (pid_file) {
        fprintf(pid_file, "%d", getpid());
        fclose(pid_file);
        printf("Driver PID %d written to %s\n", getpid(), DRIVER_PID_FILE);
    } else {
        printf("Warning: Failed to write PID file\n");
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
    
    printf("Driver interface initialized successfully with signal-based interrupts\n");
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
    
    /* Clean up PID file */
    unlink(DRIVER_PID_FILE);

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

int register_interrupt_handler(uint32_t interrupt_id, interrupt_handler_t handler) {
    if (interrupt_id < MAX_IRQS) {
        interrupt_handlers[interrupt_id] = handler;
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

/* Function to get the current process PID for signal-based interrupts */
pid_t get_interface_process_pid(void) {
    return getpid();
}
