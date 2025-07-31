#ifndef DEVICE_DRIVER_H
#define DEVICE_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CMSIS-style device definitions */
#define DEVICE_BASE_ADDR    0x40000000UL
#define DEVICE_SIZE         0x1000UL

/* Register definitions */
typedef struct {
    volatile uint32_t CTRL;     /* Control Register */
    volatile uint32_t STATUS;   /* Status Register */
    volatile uint32_t DATA;     /* Data Register */
    volatile uint32_t IRQ;      /* Interrupt Register */
} DEVICE_TypeDef;

#define DEVICE    ((DEVICE_TypeDef *) DEVICE_BASE_ADDR)

/* Register bit definitions */
#define DEVICE_CTRL_ENABLE_Pos    0
#define DEVICE_CTRL_ENABLE_Msk    (1UL << DEVICE_CTRL_ENABLE_Pos)

#define DEVICE_STATUS_READY_Pos   0
#define DEVICE_STATUS_READY_Msk   (1UL << DEVICE_STATUS_READY_Pos)

#define DEVICE_IRQ_ENABLE_Pos     0
#define DEVICE_IRQ_ENABLE_Msk     (1UL << DEVICE_IRQ_ENABLE_Pos)

/* Driver API */
typedef enum {
    DRIVER_OK = 0,
    DRIVER_ERROR = 1,
    DRIVER_TIMEOUT = 2
} driver_status_t;

driver_status_t device_init(void);
driver_status_t device_deinit(void);
driver_status_t device_enable(void);
driver_status_t device_disable(void);
driver_status_t device_write_data(uint32_t data);
driver_status_t device_read_data(uint32_t *data);
uint32_t device_get_status(void);

/* Interrupt handling */
void device_irq_handler(void);
void device_irq_enable(void);
void device_irq_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_DRIVER_H */