#ifndef BSP_XPT2046_IO_H
#define BSP_XPT2046_IO_H

#include <stdbool.h>
#include <stdint.h>

void bsp_xpt2046_io_init(void);
bool bsp_xpt2046_pen_is_active(void);
void bsp_xpt2046_select(bool selected);
uint8_t bsp_xpt2046_transfer(uint8_t output);

#endif
