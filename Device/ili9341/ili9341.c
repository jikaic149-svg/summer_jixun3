#include "ili9341.h"

#include "bsp_lcd_fsmc.h"
#include "lcd_demo_config.h"

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[15];
} init_command_t;

enum {
    INIT_WAIT_POWER_ON = 1,
    INIT_WAIT_SOFTWARE_RESET = 2,
    INIT_WAIT_SLEEP_OUT = 3
};

static const init_command_t s_init_commands[] = {
    {0xCFU, 3U, {0x00U, 0xC1U, 0x30U}},
    {0xEDU, 4U, {0x64U, 0x03U, 0x12U, 0x81U}},
    {0xE8U, 3U, {0x85U, 0x10U, 0x7AU}},
    {0xCBU, 5U, {0x39U, 0x2CU, 0x00U, 0x34U, 0x02U}},
    {0xF7U, 1U, {0x20U}},
    {0xEAU, 2U, {0x00U, 0x00U}},
    {0xC0U, 1U, {0x1BU}},
    {0xC1U, 1U, {0x01U}},
    {0xC5U, 2U, {0x30U, 0x30U}},
    {0xC7U, 1U, {0xB7U}},
    {0x36U, 1U, {LCD_DEMO_MADCTL_LANDSCAPE}},
    {0x3AU, 1U, {0x55U}},
    {0xB1U, 2U, {0x00U, 0x1AU}},
    {0xB6U, 2U, {0x0AU, 0xA2U}},
    {0xF2U, 1U, {0x00U}},
    {0x26U, 1U, {0x01U}},
    {0xE0U, 15U, {0x0FU, 0x2AU, 0x28U, 0x08U, 0x0EU,
                   0x08U, 0x54U, 0xA9U, 0x43U, 0x0AU,
                   0x0FU, 0x00U, 0x00U, 0x00U, 0x00U}},
    {0xE1U, 15U, {0x00U, 0x15U, 0x17U, 0x07U, 0x11U,
                   0x06U, 0x2BU, 0x56U, 0x3CU, 0x05U,
                   0x10U, 0x0FU, 0x3FU, 0x3FU, 0x0FU}}
};

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void send_command_data(const init_command_t *entry)
{
    uint8_t index;

    bsp_lcd_fsmc_write_command(entry->command);
    for (index = 0U; index < entry->length; ++index) {
        bsp_lcd_fsmc_write_data8(entry->data[index]);
    }
}

static uint16_t read_controller_id(void)
{
    uint8_t id_high;
    uint8_t id_low;

    bsp_lcd_fsmc_write_command(0xD3U);
    (void)bsp_lcd_fsmc_read_data16();
    (void)bsp_lcd_fsmc_read_data16();
    id_high = (uint8_t)bsp_lcd_fsmc_read_data16();
    id_low = (uint8_t)bsp_lcd_fsmc_read_data16();
    return (uint16_t)(((uint16_t)id_high << 8U) | id_low);
}

static void set_window(uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height)
{
    const uint16_t x_end = (uint16_t)(x + width - 1U);
    const uint16_t y_end = (uint16_t)(y + height - 1U);

    bsp_lcd_fsmc_write_command(0x2AU);
    bsp_lcd_fsmc_write_data8((uint8_t)(x >> 8U));
    bsp_lcd_fsmc_write_data8((uint8_t)x);
    bsp_lcd_fsmc_write_data8((uint8_t)(x_end >> 8U));
    bsp_lcd_fsmc_write_data8((uint8_t)x_end);

    bsp_lcd_fsmc_write_command(0x2BU);
    bsp_lcd_fsmc_write_data8((uint8_t)(y >> 8U));
    bsp_lcd_fsmc_write_data8((uint8_t)y);
    bsp_lcd_fsmc_write_data8((uint8_t)(y_end >> 8U));
    bsp_lcd_fsmc_write_data8((uint8_t)y_end);

    bsp_lcd_fsmc_write_command(0x2CU);
}

void ili9341_begin(ili9341_t *display, uint32_t now_ms)
{
    if ((display == NULL) || !bsp_lcd_fsmc_is_initialized()) {
        if (display != NULL) {
            display->status = ILI9341_ERROR;
        }
        return;
    }

    display->status = ILI9341_INITIALIZING;
    display->init_phase = INIT_WAIT_POWER_ON;
    display->controller_id = 0U;
    display->controller_id_verified = false;
    display->deadline_ms = now_ms + 10U;

    bsp_lcd_fsmc_backlight(false);
}

void ili9341_poll(ili9341_t *display, uint32_t now_ms)
{
    size_t index;

    if ((display == NULL) || (display->status != ILI9341_INITIALIZING) ||
        !deadline_reached(now_ms, display->deadline_ms)) {
        return;
    }

    if (display->init_phase == INIT_WAIT_POWER_ON) {
        bsp_lcd_fsmc_write_command(0x01U);
        display->init_phase = INIT_WAIT_SOFTWARE_RESET;
        display->deadline_ms = now_ms + 10U;
        return;
    }

    if (display->init_phase == INIT_WAIT_SOFTWARE_RESET) {
        display->controller_id = read_controller_id();
        display->controller_id_verified = display->controller_id == 0x9341U;

        for (index = 0U;
             index < (sizeof(s_init_commands) / sizeof(s_init_commands[0]));
             ++index) {
            send_command_data(&s_init_commands[index]);
        }

        bsp_lcd_fsmc_write_command(0x11U);
        display->init_phase = INIT_WAIT_SLEEP_OUT;
        display->deadline_ms = now_ms + 120U;
        return;
    }

    if (display->init_phase == INIT_WAIT_SLEEP_OUT) {
        bsp_lcd_fsmc_write_command(0x29U);
        display->status = ILI9341_READY;
    }
}

ili9341_status_t ili9341_status(const ili9341_t *display)
{
    return (display == NULL) ? ILI9341_ERROR : display->status;
}

uint16_t ili9341_controller_id(const ili9341_t *display)
{
    return (display == NULL) ? 0U : display->controller_id;
}

bool ili9341_controller_id_verified(const ili9341_t *display)
{
    return (display != NULL) && display->controller_id_verified;
}

void ili9341_set_backlight(bool enabled)
{
    bsp_lcd_fsmc_backlight(enabled);
}

void ili9341_fill_rect(uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height,
                       uint16_t color)
{
    uint32_t x_end;
    uint32_t y_end;

    if ((width == 0U) || (height == 0U) ||
        (x >= LCD_DEMO_WIDTH) || (y >= LCD_DEMO_HEIGHT)) {
        return;
    }

    x_end = (uint32_t)x + width;
    y_end = (uint32_t)y + height;
    if (x_end > LCD_DEMO_WIDTH) {
        width = (uint16_t)(LCD_DEMO_WIDTH - x);
    }
    if (y_end > LCD_DEMO_HEIGHT) {
        height = (uint16_t)(LCD_DEMO_HEIGHT - y);
    }

    set_window(x, y, width, height);
    bsp_lcd_fsmc_fill_pixels(color, (size_t)width * height);
}

bool ili9341_blit_rgb565(uint16_t x, uint16_t y,
                         uint16_t width, uint16_t height,
                         const uint16_t *pixels)
{
    const uint32_t x_end = (uint32_t)x + width;
    const uint32_t y_end = (uint32_t)y + height;

    if ((pixels == NULL) || (width == 0U) || (height == 0U) ||
        (x_end > LCD_DEMO_WIDTH) || (y_end > LCD_DEMO_HEIGHT)) {
        return false;
    }

    set_window(x, y, width, height);
    bsp_lcd_fsmc_write_pixels(pixels, (size_t)width * height);
    return true;
}
