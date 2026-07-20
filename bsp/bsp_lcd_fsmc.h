#ifndef BSP_LCD_FSMC_H
#define BSP_LCD_FSMC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void bsp_lcd_fsmc_init(void);
bool bsp_lcd_fsmc_is_initialized(void);
void bsp_lcd_fsmc_backlight(bool enabled);
void bsp_lcd_fsmc_write_command(uint8_t command);
void bsp_lcd_fsmc_write_data8(uint8_t data);
void bsp_lcd_fsmc_write_data16(uint16_t data);
uint16_t bsp_lcd_fsmc_read_data16(void);
void bsp_lcd_fsmc_write_pixels(const uint16_t *pixels, size_t count);
void bsp_lcd_fsmc_fill_pixels(uint16_t color, size_t count);

#endif
