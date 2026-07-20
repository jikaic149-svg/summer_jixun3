#ifndef BSP_BOARD_F407VE_H
#define BSP_BOARD_F407VE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_BOARD_CLOCK_STARTING = 0,
    BSP_BOARD_CLOCK_READY,
    BSP_BOARD_CLOCK_ERROR
} bsp_board_clock_status_t;

void bsp_board_init(void);
void bsp_board_poll(void);
uint32_t bsp_board_millis(void);
bsp_board_clock_status_t bsp_board_clock_status(void);
void bsp_board_idle(void);

#endif
