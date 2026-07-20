#ifndef DEVICE_ILI9341_H
#define DEVICE_ILI9341_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ILI9341_OFF = 0,
    ILI9341_INITIALIZING,
    ILI9341_READY,
    ILI9341_ERROR
} ili9341_status_t;

typedef struct {
    ili9341_status_t status;
    uint8_t init_phase;
    uint32_t deadline_ms;
    uint16_t controller_id;
    bool controller_id_verified;
} ili9341_t;

void ili9341_begin(ili9341_t *display, uint32_t now_ms);
void ili9341_poll(ili9341_t *display, uint32_t now_ms);
ili9341_status_t ili9341_status(const ili9341_t *display);
uint16_t ili9341_controller_id(const ili9341_t *display);
bool ili9341_controller_id_verified(const ili9341_t *display);

void ili9341_set_backlight(bool enabled);
void ili9341_fill_rect(uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height,
                       uint16_t color);
bool ili9341_blit_rgb565(uint16_t x, uint16_t y,
                         uint16_t width, uint16_t height,
                         const uint16_t *pixels);

#endif
