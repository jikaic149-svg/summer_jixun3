#include "lcd_demo_app.h"

#include <stddef.h>

#include "app_command_bus.h"
#include "bsp_board_f407ve.h"
#include "bsp_lcd_fsmc.h"
#include "bsp_xpt2046_io.h"
#include "control_port.h"
#include "ili9341.h"
#include "lcd_demo_config.h"
#include "ui_font.h"
#include "xpt2046.h"

static ili9341_t s_display;
static xpt2046_t s_touch;
static target_ui_t s_ui;
static app_command_bus_t s_command_bus;
static uint16_t s_glyph_pixels[16U * 16U];
static bool s_hardware_started;
static bool s_ui_started;
static bool s_clock_failed;

static void canvas_fill_rect(void *context,
                             const ui_rect_t *rect,
                             uint16_t color)
{
    (void)context;
    ili9341_fill_rect(rect->x, rect->y, rect->width, rect->height, color);
}

static void draw_glyph(uint16_t x,
                       uint16_t y,
                       const ui_font_glyph_t *glyph,
                       uint16_t foreground,
                       uint16_t background)
{
    uint8_t row;
    uint8_t column;

    for (row = 0U; row < UI_FONT_HEIGHT; ++row) {
        for (column = 0U; column < glyph->width; ++column) {
            const uint16_t mask = (uint16_t)(1UL <<
                                  (glyph->width - column - 1U));
            s_glyph_pixels[(size_t)row * glyph->width + column] =
                ((glyph->rows[row] & mask) != 0U) ? foreground : background;
        }
    }

    (void)ili9341_blit_rgb565(x, y, glyph->width, UI_FONT_HEIGHT,
                              s_glyph_pixels);
}

static void canvas_draw_text(void *context,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             uint16_t foreground,
                             uint16_t background)
{
    uint32_t codepoint;

    (void)context;
    while (ui_font_utf8_next(&text, &codepoint)) {
        const ui_font_glyph_t *glyph = ui_font_find(codepoint);
        if (glyph == NULL) {
            continue;
        }
        if (((uint32_t)x + glyph->width > LCD_DEMO_WIDTH) ||
            ((uint32_t)y + UI_FONT_HEIGHT > LCD_DEMO_HEIGHT)) {
            break;
        }
        draw_glyph(x, y, glyph, foreground, background);
        x = (uint16_t)(x + glyph->width);
    }
}

static const ui_canvas_t s_canvas = {
    .context = NULL,
    .fill_rect = canvas_fill_rect,
    .draw_text = canvas_draw_text
};

static void start_hardware(uint32_t now_ms)
{
    static const xpt2046_calibration_t calibration = {
        .raw_x_left = TOUCH_RAW_X_LEFT,
        .raw_x_right = TOUCH_RAW_X_RIGHT,
        .raw_y_top = TOUCH_RAW_Y_TOP,
        .raw_y_bottom = TOUCH_RAW_Y_BOTTOM,
        .width = LCD_DEMO_WIDTH,
        .height = LCD_DEMO_HEIGHT
    };

    bsp_lcd_fsmc_init();
    bsp_xpt2046_io_init();
    xpt2046_init(&s_touch, &calibration, now_ms);
    ili9341_begin(&s_display, now_ms);
    s_hardware_started = true;
}

static void publish_ui_command(const app_command_t *command)
{
    const app_publish_result_t result =
        app_command_bus_publish(&s_command_bus, command);

    if (result == APP_PUBLISH_OK) {
        target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_COMMAND_SENT);
    } else if (result == APP_PUBLISH_FULL) {
        target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_QUEUE_FULL);
    } else {
        target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_ERROR);
    }
}

static void dispatch_one_command(void)
{
    app_command_t command;
    control_submit_result_t result;

    if (!app_command_bus_take(&s_command_bus, &command)) {
        return;
    }

    result = control_port_submit(&command);
    if (result == CONTROL_SUBMIT_ACCEPTED) {
        target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_COMMAND_SENT);
    } else if (result == CONTROL_SUBMIT_UNAVAILABLE) {
        target_ui_set_feedback(&s_ui,
                               TARGET_UI_FEEDBACK_CONTROLLER_MISSING);
    } else {
        target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_QUEUE_FULL);
    }
}

void lcd_demo_app_init(void)
{
    app_command_bus_init(&s_command_bus);
    s_hardware_started = false;
    s_ui_started = false;
    s_clock_failed = false;
}

void lcd_demo_app_step(uint32_t now_ms)
{
    xpt2046_sample_t touch_sample;
    ui_touch_t ui_touch;
    app_command_t command;

    if (s_clock_failed) {
        return;
    }

    if (!s_hardware_started) {
        const bsp_board_clock_status_t clock_status = bsp_board_clock_status();
        if (clock_status == BSP_BOARD_CLOCK_READY) {
            start_hardware(now_ms);
        } else if (clock_status == BSP_BOARD_CLOCK_ERROR) {
            s_clock_failed = true;
        }
        return;
    }

    ili9341_poll(&s_display, now_ms);
    if (ili9341_status(&s_display) != ILI9341_READY) {
        return;
    }

    if (!s_ui_started) {
        target_ui_init(&s_ui);
        target_ui_render(&s_ui, &s_canvas);
        ili9341_set_backlight(true);
        s_ui_started = true;
        return;
    }

    if (xpt2046_poll(&s_touch, now_ms, &touch_sample)) {
        ui_touch.pressed = touch_sample.pressed;
        ui_touch.just_pressed = touch_sample.just_pressed;
        ui_touch.just_released = touch_sample.just_released;
        ui_touch.x = touch_sample.x;
        ui_touch.y = touch_sample.y;

        if (target_ui_step(&s_ui, &ui_touch, now_ms, &command)) {
            publish_ui_command(&command);
        }
    }

    dispatch_one_command();
    target_ui_render(&s_ui, &s_canvas);
}

bool lcd_demo_app_ready(void)
{
    return s_ui_started;
}

void lcd_demo_app_set_runtime(target_ui_runtime_status_t status,
                              int16_t azimuth_degrees,
                              int16_t elevation_degrees)
{
    if (s_ui_started) {
        target_ui_set_runtime(&s_ui, status, azimuth_degrees,
                              elevation_degrees);
    }
}
