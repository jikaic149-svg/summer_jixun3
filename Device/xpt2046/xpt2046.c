#include "xpt2046.h"

#include <stddef.h>

#include "bsp_xpt2046_io.h"
#include "lcd_demo_config.h"

#define XPT2046_LANDSCAPE_X_COMMAND  0x90U
#define XPT2046_LANDSCAPE_Y_COMMAND  0xD0U
#define XPT2046_ADC_MAX_VALUE         0x0FFFU

_Static_assert(TOUCH_MEDIAN_SAMPLE_COUNT == 5U,
               "the fixed median filter requires five samples");
_Static_assert(TOUCH_PRESS_STABLE_SAMPLES > 0U,
               "press debounce must be non-zero");
_Static_assert(TOUCH_RELEASE_STABLE_SAMPLES > 0U,
               "release debounce must be non-zero");

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint16_t read_channel(uint8_t command)
{
    uint16_t value;

    bsp_xpt2046_select(true);
    (void)bsp_xpt2046_transfer(command);
    value = bsp_xpt2046_receive_12bit();
    bsp_xpt2046_select(false);

    return (uint16_t)(value & XPT2046_ADC_MAX_VALUE);
}

static void sort_u16(uint16_t *values, uint8_t count)
{
    uint8_t index;

    for (index = 1U; index < count; ++index) {
        const uint16_t value = values[index];
        uint8_t position = index;

        while ((position > 0U) && (values[position - 1U] > value)) {
            values[position] = values[position - 1U];
            --position;
        }
        values[position] = value;
    }
}

static bool read_filtered(uint16_t *raw_x, uint16_t *raw_y)
{
    uint16_t samples_x[TOUCH_MEDIAN_SAMPLE_COUNT];
    uint16_t samples_y[TOUCH_MEDIAN_SAMPLE_COUNT];
    uint8_t index;

    if (!bsp_xpt2046_pen_is_active()) {
        return false;
    }

    for (index = 0U; index < TOUCH_MEDIAN_SAMPLE_COUNT; ++index) {
        samples_x[index] = read_channel(XPT2046_LANDSCAPE_X_COMMAND);
        samples_y[index] = read_channel(XPT2046_LANDSCAPE_Y_COMMAND);
    }

    sort_u16(samples_x, TOUCH_MEDIAN_SAMPLE_COUNT);
    sort_u16(samples_y, TOUCH_MEDIAN_SAMPLE_COUNT);
    *raw_x = samples_x[TOUCH_MEDIAN_SAMPLE_COUNT / 2U];
    *raw_y = samples_y[TOUCH_MEDIAN_SAMPLE_COUNT / 2U];

    if ((*raw_x == 0U) || (*raw_x == XPT2046_ADC_MAX_VALUE) ||
        (*raw_y == 0U) || (*raw_y == XPT2046_ADC_MAX_VALUE)) {
        return false;
    }
    return true;
}

static uint16_t map_axis(uint16_t raw,
                         int32_t raw_start,
                         int32_t raw_end,
                         uint16_t pixel_count)
{
    const int32_t span = raw_end - raw_start;
    int32_t mapped;

    if ((span == 0) || (pixel_count == 0U)) {
        return 0U;
    }

    mapped = ((int32_t)raw - raw_start) * ((int32_t)pixel_count - 1) / span;
    if (mapped < 0) {
        mapped = 0;
    } else if (mapped >= (int32_t)pixel_count) {
        mapped = (int32_t)pixel_count - 1;
    }
    return (uint16_t)mapped;
}

void xpt2046_init(xpt2046_t *touch,
                  const xpt2046_calibration_t *calibration,
                  uint32_t now_ms)
{
    if ((touch == NULL) || (calibration == NULL)) {
        return;
    }

    touch->calibration = *calibration;
    touch->sample.pressed = false;
    touch->sample.just_pressed = false;
    touch->sample.just_released = false;
    touch->sample.x = 0U;
    touch->sample.y = 0U;
    touch->sample.raw_x = 0U;
    touch->sample.raw_y = 0U;
    touch->press_count = 0U;
    touch->release_count = 0U;
    touch->next_sample_ms = now_ms;

    /*
     * A completed command with PD1:PD0 = 00 explicitly enables PENIRQ.
     * Do not wait for PENIRQ before the controller has received that command.
     */
    (void)read_channel(XPT2046_LANDSCAPE_X_COMMAND);
}

bool xpt2046_poll(xpt2046_t *touch,
                  uint32_t now_ms,
                  xpt2046_sample_t *sample)
{
    bool physical_press;
    uint16_t raw_x = 0U;
    uint16_t raw_y = 0U;

    if ((touch == NULL) || (sample == NULL) ||
        !deadline_reached(now_ms, touch->next_sample_ms)) {
        return false;
    }

    touch->next_sample_ms = now_ms + TOUCH_SAMPLE_PERIOD_MS;
    touch->sample.just_pressed = false;
    touch->sample.just_released = false;

    physical_press = bsp_xpt2046_pen_is_active();
    if (physical_press) {
        physical_press = read_filtered(&raw_x, &raw_y);
    }

    if (physical_press) {
        touch->release_count = 0U;
        if (touch->press_count < TOUCH_PRESS_STABLE_SAMPLES) {
            ++touch->press_count;
        }

        touch->sample.raw_x = raw_x;
        touch->sample.raw_y = raw_y;
        touch->sample.x = map_axis(raw_x,
                                   touch->calibration.raw_x_left,
                                   touch->calibration.raw_x_right,
                                   touch->calibration.width);
        touch->sample.y = map_axis(raw_y,
                                   touch->calibration.raw_y_top,
                                   touch->calibration.raw_y_bottom,
                                   touch->calibration.height);

        if (!touch->sample.pressed &&
            (touch->press_count >= TOUCH_PRESS_STABLE_SAMPLES)) {
            touch->sample.pressed = true;
            touch->sample.just_pressed = true;
        }
    } else {
        touch->press_count = 0U;
        if (touch->release_count < TOUCH_RELEASE_STABLE_SAMPLES) {
            ++touch->release_count;
        }
        if (touch->sample.pressed &&
            (touch->release_count >= TOUCH_RELEASE_STABLE_SAMPLES)) {
            touch->sample.pressed = false;
            touch->sample.just_released = true;
        }
    }

    *sample = touch->sample;
    return true;
}
