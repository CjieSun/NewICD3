#include "device_driver.h"
#include "interface_layer.h"
#include <stdio.h>

/* Static state */
static int driver_initialized = 0;

/* Helper macros for register access through interface layer */
#define REG_READ(addr)           read_register((addr), 4)
#define REG_WRITE(addr, value)   write_register((addr), (value), 4)

/* Register addresses */
#define DEVICE_CTRL_ADDR    (DEVICE_BASE_ADDR + 0x00)
#define DEVICE_STATUS_ADDR  (DEVICE_BASE_ADDR + 0x04)
#define DEVICE_DATA_ADDR    (DEVICE_BASE_ADDR + 0x08)
#define DEVICE_IRQ_ADDR     (DEVICE_BASE_ADDR + 0x0C)

/* Interrupt handler callback */
static void device_interrupt_callback(uint32_t device_id, uint32_t interrupt_id) {
    printf("Device %d interrupt received: ID=%d\n", device_id, interrupt_id);
    device_irq_handler();
}

driver_status_t device_init(void) {
    if (driver_initialized) {
        return DRIVER_OK;
    }
    
    /* Register device with interface layer */
    if (register_device(1, DEVICE_BASE_ADDR, DEVICE_SIZE) != 0) {
        printf("Failed to register device\n");
        return DRIVER_ERROR;
    }
    
    /* Register interrupt handler */
    if (register_interrupt_handler(1, device_interrupt_callback) != 0) {
        printf("Failed to register interrupt handler\n");
        return DRIVER_ERROR;
    }
    
    /* Initialize device registers */
    REG_WRITE(DEVICE_CTRL_ADDR, 0);
    REG_WRITE(DEVICE_STATUS_ADDR, 0);
    REG_WRITE(DEVICE_DATA_ADDR, 0);
    REG_WRITE(DEVICE_IRQ_ADDR, 0);
    
    driver_initialized = 1;
    printf("Device driver initialized\n");
    return DRIVER_OK;
}

driver_status_t device_deinit(void) {
    if (!driver_initialized) {
        return DRIVER_OK;
    }
    
    /* Disable device */
    uint32_t ctrl = REG_READ(DEVICE_CTRL_ADDR);
    ctrl &= ~DEVICE_CTRL_ENABLE_Msk;
    REG_WRITE(DEVICE_CTRL_ADDR, ctrl);
    
    /* Unregister device */
    unregister_device(1);
    
    driver_initialized = 0;
    printf("Device driver deinitialized\n");
    return DRIVER_OK;
}

driver_status_t device_enable(void) {
    if (!driver_initialized) {
        return DRIVER_ERROR;
    }
    
    uint32_t ctrl = REG_READ(DEVICE_CTRL_ADDR);
    ctrl |= DEVICE_CTRL_ENABLE_Msk;
    REG_WRITE(DEVICE_CTRL_ADDR, ctrl);
    printf("Device enabled\n");
    return DRIVER_OK;
}

driver_status_t device_disable(void) {
    if (!driver_initialized) {
        return DRIVER_ERROR;
    }
    
    uint32_t ctrl = REG_READ(DEVICE_CTRL_ADDR);
    ctrl &= ~DEVICE_CTRL_ENABLE_Msk;
    REG_WRITE(DEVICE_CTRL_ADDR, ctrl);
    printf("Device disabled\n");
    return DRIVER_OK;
}

driver_status_t device_write_data(uint32_t data) {
    if (!driver_initialized) {
        return DRIVER_ERROR;
    }
    
    /* Check if device is ready */
    uint32_t status = REG_READ(DEVICE_STATUS_ADDR);
    if (!(status & DEVICE_STATUS_READY_Msk)) {
        printf("Device not ready for write\n");
        return DRIVER_ERROR;
    }
    
    REG_WRITE(DEVICE_DATA_ADDR, data);
    printf("Wrote data: 0x%x\n", data);
    return DRIVER_OK;
}

driver_status_t device_read_data(uint32_t *data) {
    if (!driver_initialized || !data) {
        return DRIVER_ERROR;
    }
    
    /* Check if device is ready */
    uint32_t status = REG_READ(DEVICE_STATUS_ADDR);
    if (!(status & DEVICE_STATUS_READY_Msk)) {
        printf("Device not ready for read\n");
        return DRIVER_ERROR;
    }
    
    *data = REG_READ(DEVICE_DATA_ADDR);
    printf("Read data: 0x%x\n", *data);
    return DRIVER_OK;
}

uint32_t device_get_status(void) {
    if (!driver_initialized) {
        return 0;
    }
    
    return REG_READ(DEVICE_STATUS_ADDR);
}

void device_irq_handler(void) {
    printf("Device IRQ handler called\n");
    
    /* Read interrupt status */
    uint32_t irq_status = REG_READ(DEVICE_IRQ_ADDR);
    
    /* Clear interrupt */
    REG_WRITE(DEVICE_IRQ_ADDR, 0);
    
    printf("IRQ status: 0x%x\n", irq_status);
}

void device_irq_enable(void) {
    if (!driver_initialized) {
        return;
    }
    
    uint32_t irq = REG_READ(DEVICE_IRQ_ADDR);
    irq |= DEVICE_IRQ_ENABLE_Msk;
    REG_WRITE(DEVICE_IRQ_ADDR, irq);
    printf("Device interrupts enabled\n");
}

void device_irq_disable(void) {
    if (!driver_initialized) {
        return;
    }
    
    uint32_t irq = REG_READ(DEVICE_IRQ_ADDR);
    irq &= ~DEVICE_IRQ_ENABLE_Msk;
    REG_WRITE(DEVICE_IRQ_ADDR, irq);
    printf("Device interrupts disabled\n");
}