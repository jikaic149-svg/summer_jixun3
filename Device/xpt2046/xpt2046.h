#ifndef DEVICE_XPT2046_H
#define DEVICE_XPT2046_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t raw_x_left;
    int32_t raw_x_right;
    int32_t raw_y_top;
    int32_t raw_y_bottom;
    uint16_t width;
    uint16_t height;
} xpt2046_calibration_t;

typedef struct {
    bool pressed;
    bool just_pressed;
    bool just_released;
    uint16_t x;
    uint16_t y;
    uint16_t raw_x;
    uint16_t raw_y;
} xpt2046_sample_t;

typedef struct {
    xpt2046_calibration_t calibration;
    xpt2046_sample_t sample;
    uint32_t next_sample_ms;
    uint8_t press_count;
    uint8_t release_count;
} xpt2046_t;

void xpt2046_init(xpt2046_t *touch,
                  const xpt2046_calibration_t *calibration,
                  uint32_t now_ms);
bool xpt2046_poll(xpt2046_t *touch,
                  uint32_t now_ms,
                  xpt2046_sample_t *sample);

#endif
