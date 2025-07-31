#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UART device definitions */
#define UART_BASE_ADDR      0x40000000UL
#define UART_SIZE           0x1000UL

/* UART register definitions */
typedef struct {
    volatile uint32_t CTRL;     /* Control Register */
    volatile uint32_t STATUS;   /* Status Register */
    volatile uint32_t DATA;     /* Data Register */
    volatile uint32_t BAUD;     /* Baud Rate Register */
} UART_TypeDef;

#define UART    ((UART_TypeDef *) UART_BASE_ADDR)

/* UART Control register bit definitions */
#define UART_CTRL_ENABLE_Pos    0
#define UART_CTRL_ENABLE_Msk    (1UL << UART_CTRL_ENABLE_Pos)

#define UART_CTRL_TX_EN_Pos     1
#define UART_CTRL_TX_EN_Msk     (1UL << UART_CTRL_TX_EN_Pos)

#define UART_CTRL_RX_EN_Pos     2
#define UART_CTRL_RX_EN_Msk     (1UL << UART_CTRL_RX_EN_Pos)

#define UART_CTRL_IRQ_EN_Pos    3
#define UART_CTRL_IRQ_EN_Msk    (1UL << UART_CTRL_IRQ_EN_Pos)

/* UART Status register bit definitions */
#define UART_STATUS_READY_Pos   0
#define UART_STATUS_READY_Msk   (1UL << UART_STATUS_READY_Pos)

#define UART_STATUS_TX_EMPTY_Pos   1
#define UART_STATUS_TX_EMPTY_Msk   (1UL << UART_STATUS_TX_EMPTY_Pos)

#define UART_STATUS_RX_FULL_Pos    2
#define UART_STATUS_RX_FULL_Msk    (1UL << UART_STATUS_RX_FULL_Pos)

#define UART_STATUS_TX_COMPLETE_Pos    3
#define UART_STATUS_TX_COMPLETE_Msk    (1UL << UART_STATUS_TX_COMPLETE_Pos)

/* Driver API */
typedef enum {
    UART_OK = 0,
    UART_ERROR = 1,
    UART_TIMEOUT = 2,
    UART_BUSY = 3
} uart_status_t;

uart_status_t uart_init(void);
uart_status_t uart_deinit(void);
uart_status_t uart_enable(void);
uart_status_t uart_disable(void);
uart_status_t uart_configure(uint32_t baud_rate);
uart_status_t uart_transmit(uint8_t data);
uart_status_t uart_receive(uint8_t *data);
uart_status_t uart_transmit_string(const char *str);
uint32_t uart_get_status(void);

/* Interrupt handling */
void uart_irq_handler(void);
void uart_irq_enable(void);
void uart_irq_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_DRIVER_H */