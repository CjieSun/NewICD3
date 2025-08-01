/**
 * @file interface_layer.h
 * @brief NewICD3 Interface Layer - Driver Transparency Implementation
 * 
 * This header defines the interface layer API that provides driver transparency
 * through memory protection and signal handling. The interface layer acts as
 * a bridge between CMSIS-compliant drivers and Python device models.
 * 
 * Key Features:
 * - Memory protection using mmap + PROT_NONE
 * - SIGSEGV signal handling for register access interception
 * - x86-64 instruction parsing for read/write detection
 * - Socket-based communication with Python device models
 * - Interrupt delivery from models to drivers
 * 
 * @author NewICD3 Team
 * @version 1.0
 * @date 2024
 */

#ifndef INTERFACE_LAYER_H
#define INTERFACE_LAYER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Communication protocol definitions */
typedef enum {
    CMD_READ = 0x01,
    CMD_WRITE = 0x02,
    CMD_INTERRUPT = 0x03,
    CMD_INIT = 0x04,
    CMD_DEINIT = 0x05
} protocol_command_t;

typedef enum {
    RESULT_SUCCESS = 0x00,
    RESULT_ERROR = 0x01,
    RESULT_TIMEOUT = 0x02,
    RESULT_INVALID_ADDR = 0x03
} protocol_result_t;

typedef struct {
    uint32_t device_id;
    protocol_command_t command;
    uint32_t address;
    uint32_t length;
    protocol_result_t result;
    uint8_t data[256];  // Maximum data payload
} protocol_message_t;

/* Device registration structure */
typedef struct {
    uint32_t device_id;
    uint32_t base_address;
    uint32_t size;
    void *mapped_memory;
    int socket_fd;
} device_info_t;

/* Interface layer API */
int interface_layer_init(void);
int interface_layer_deinit(void);

/* Device management */
int register_device(uint32_t device_id, uint32_t base_address, uint32_t size);
int unregister_device(uint32_t device_id);

/* Memory access handling */
uint32_t read_register(uint32_t address, uint32_t size);
int write_register(uint32_t address, uint32_t data, uint32_t size);

/* Interrupt handling */
typedef void (*interrupt_handler_t)(uint32_t interrupt_id);
int register_interrupt_handler(uint32_t device_id, interrupt_handler_t handler);

/* Socket communication */
int send_message_to_model(const protocol_message_t *message, protocol_message_t *response);

/* Signal-based interrupt support */
pid_t get_interface_process_pid(void);

#ifdef __cplusplus
}
#endif

#endif /* INTERFACE_LAYER_H */