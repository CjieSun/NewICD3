#include "uart_driver.h"
#include "interface_layer.h"
#include <stdio.h>
#include <string.h>

/* Static state */
static int uart_initialized = 0;

/* Helper macros for register access through interface layer */
#define UART_REG_READ(addr)           read_register((addr), 4)
#define UART_REG_WRITE(addr, value)   write_register((addr), (value), 4)

/* Register addresses */
#define UART_CTRL_ADDR    (UART_BASE_ADDR + 0x00)
#define UART_STATUS_ADDR  (UART_BASE_ADDR + 0x04)
#define UART_DATA_ADDR    (UART_BASE_ADDR + 0x08)
#define UART_BAUD_ADDR    (UART_BASE_ADDR + 0x0C)

/* Interrupt handler callback */
static void uart_interrupt_callback(uint32_t device_id, uint32_t interrupt_id) {
    printf("UART device %d interrupt received: ID=%d\n", device_id, interrupt_id);
    
    switch (interrupt_id) {
        case 0x01:
            printf("  RX data ready interrupt\n");
            break;
        case 0x02:
            printf("  TX complete interrupt\n");
            break;
        default:
            printf("  Unknown UART interrupt: %d\n", interrupt_id);
            break;
    }
    
    uart_irq_handler();
}

uart_status_t uart_init(void) {
    if (uart_initialized) {
        return UART_OK;
    }
    
    /* Register UART device with interface layer */
    if (register_device(1, UART_BASE_ADDR, UART_SIZE) != 0) {
        printf("Failed to register UART device\n");
        return UART_ERROR;
    }
    
    /* Register interrupt handler */
    if (register_interrupt_handler(1, uart_interrupt_callback) != 0) {
        printf("Failed to register UART interrupt handler\n");
        return UART_ERROR;
    }
    
    /* Initialize UART registers */
    UART_REG_WRITE(UART_CTRL_ADDR, 0);
    UART_REG_WRITE(UART_STATUS_ADDR, 0);
    UART_REG_WRITE(UART_DATA_ADDR, 0);
    UART_REG_WRITE(UART_BAUD_ADDR, 9600);  /* Default baud rate */
    
    uart_initialized = 1;
    printf("UART driver initialized\n");
    return UART_OK;
}

uart_status_t uart_deinit(void) {
    if (!uart_initialized) {
        return UART_OK;
    }
    
    /* Disable UART */
    uint32_t ctrl = UART_REG_READ(UART_CTRL_ADDR);
    ctrl &= ~UART_CTRL_ENABLE_Msk;
    UART_REG_WRITE(UART_CTRL_ADDR, ctrl);
    
    /* Unregister device */
    unregister_device(1);
    
    uart_initialized = 0;
    printf("UART driver deinitialized\n");
    return UART_OK;
}

uart_status_t uart_enable(void) {
    if (!uart_initialized) {
        return UART_ERROR;
    }
    
    uint32_t ctrl = UART_REG_READ(UART_CTRL_ADDR);
    ctrl |= UART_CTRL_ENABLE_Msk | UART_CTRL_TX_EN_Msk | UART_CTRL_RX_EN_Msk;
    UART_REG_WRITE(UART_CTRL_ADDR, ctrl);
    printf("UART enabled\n");
    return UART_OK;
}

uart_status_t uart_disable(void) {
    if (!uart_initialized) {
        return UART_ERROR;
    }
    
    uint32_t ctrl = UART_REG_READ(UART_CTRL_ADDR);
    ctrl &= ~(UART_CTRL_ENABLE_Msk | UART_CTRL_TX_EN_Msk | UART_CTRL_RX_EN_Msk);
    UART_REG_WRITE(UART_CTRL_ADDR, ctrl);
    printf("UART disabled\n");
    return UART_OK;
}

uart_status_t uart_configure(uint32_t baud_rate) {
    if (!uart_initialized) {
        return UART_ERROR;
    }
    
    UART_REG_WRITE(UART_BAUD_ADDR, baud_rate);
    printf("UART configured for %d baud\n", baud_rate);
    return UART_OK;
}

uart_status_t uart_transmit(uint8_t data) {
    if (!uart_initialized) {
        return UART_ERROR;
    }
    
    /* Check if UART is enabled and ready */
    uint32_t status = UART_REG_READ(UART_STATUS_ADDR);
    if (!(status & UART_STATUS_READY_Msk)) {
        printf("UART not ready for transmission\n");
        return UART_ERROR;
    }
    
    /* Check if TX buffer is available */
    if (!(status & UART_STATUS_TX_EMPTY_Msk)) {
        printf("UART TX buffer full\n");
        return UART_BUSY;
    }
    
    UART_REG_WRITE(UART_DATA_ADDR, data);
    printf("UART transmitted: 0x%02x\n", data);
    return UART_OK;
}

uart_status_t uart_receive(uint8_t *data) {
    if (!uart_initialized || !data) {
        return UART_ERROR;
    }
    
    /* Check if UART is enabled and ready */
    uint32_t status = UART_REG_READ(UART_STATUS_ADDR);
    if (!(status & UART_STATUS_READY_Msk)) {
        printf("UART not ready for reception\n");
        return UART_ERROR;
    }
    
    /* Check if RX data is available */
    if (!(status & UART_STATUS_RX_FULL_Msk)) {
        printf("UART no RX data available\n");
        return UART_ERROR;
    }
    
    uint32_t rx_data = UART_REG_READ(UART_DATA_ADDR);
    *data = (uint8_t)(rx_data & 0xFF);
    printf("UART received: 0x%02x\n", *data);
    return UART_OK;
}

uart_status_t uart_transmit_string(const char *str) {
    if (!uart_initialized || !str) {
        return UART_ERROR;
    }
    
    while (*str) {
        uart_status_t result = uart_transmit((uint8_t)*str);
        if (result != UART_OK) {
            return result;
        }
        str++;
        
        /* Simple delay to allow transmission */
        for (volatile int i = 0; i < 10000; i++);
    }
    
    return UART_OK;
}

uint32_t uart_get_status(void) {
    if (!uart_initialized) {
        return 0;
    }
    
    return UART_REG_READ(UART_STATUS_ADDR);
}

void uart_irq_handler(void) {
    printf("UART IRQ handler called\n");
    
    /* Read status to determine interrupt cause */
    uint32_t status = UART_REG_READ(UART_STATUS_ADDR);
    
    if (status & UART_STATUS_RX_FULL_Msk) {
        printf("  RX data available\n");
    }
    
    if (status & UART_STATUS_TX_COMPLETE_Msk) {
        printf("  TX transmission complete\n");
    }
    
    printf("UART status: 0x%x\n", status);
}

void uart_irq_enable(void) {
    if (!uart_initialized) {
        return;
    }
    
    uint32_t ctrl = UART_REG_READ(UART_CTRL_ADDR);
    ctrl |= UART_CTRL_IRQ_EN_Msk;
    UART_REG_WRITE(UART_CTRL_ADDR, ctrl);
    printf("UART interrupts enabled\n");
}

void uart_irq_disable(void) {
    if (!uart_initialized) {
        return;
    }
    
    uint32_t ctrl = UART_REG_READ(UART_CTRL_ADDR);
    ctrl &= ~UART_CTRL_IRQ_EN_Msk;
    UART_REG_WRITE(UART_CTRL_ADDR, ctrl);
    printf("UART interrupts disabled\n");
}