#ifndef LCD_DEMO_APP_H
#define LCD_DEMO_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "target_ui.h"

void lcd_demo_app_init(void);
void lcd_demo_app_step(uint32_t now_ms);
bool lcd_demo_app_ready(void);
void lcd_demo_app_set_runtime(target_ui_runtime_status_t status,
                              int16_t azimuth_degrees,
                              int16_t elevation_degrees);

#endif
