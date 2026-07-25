#include "target_ui.h"

#include <stddef.h>

#include "lcd_demo_config.h"
#include "ui_font.h"

#define COLOR_BACKGROUND       0x0883U
#define COLOR_HEADER           0x0042U
#define COLOR_PANEL            0x1126U
#define COLOR_PANEL_ALT        0x1989U
#define COLOR_BORDER           0x2A2BU
#define COLOR_TEXT             0xEF9FU
#define COLOR_MUTED            0x8D16U
#define COLOR_PRIMARY          0x2C1DU
#define COLOR_PRIMARY_PRESSED  0x1AF6U
#define COLOR_THEME            0x8AFEU
#define COLOR_THEME_PRESSED    0x6158U
#define COLOR_SUCCESS          0x2E4EU
#define COLOR_WARNING          0xF525U
#define COLOR_WARNING_PRESSED  0xBB82U
#define COLOR_DANGER           0xEA4BU
#define COLOR_DANGER_PRESSED   0xB986U
#define COLOR_DISABLED         0x21A8U
#define COLOR_DISABLED_TEXT    0x63B0U

#define WIDGET_NONE            0xFFU
#define PAGE_MAIN              0U
#define PAGE_DISTANCE          1U
#define PAGE_MOTOR             2U
#define PAGE_THEME             3U
#define PAGE_ANGLE             4U
#define PAGE_CANNON            5U
#define PAGE_CHARGE            6U
#define TARGET_UI_THEME_COUNT  2U
#define ANGLE_AXIS_YAW         0U
#define ANGLE_AXIS_PITCH       1U

#if CANNON_MANUAL_TEST_MODE != 0U
#define HOME_PAGE              PAGE_CANNON
#else
#define HOME_PAGE              PAGE_MAIN
#endif

enum {
    WIDGET_DISTANCE = 0,
    WIDGET_FIRE_DISTANCE,
    WIDGET_STATUS,
    WIDGET_AZIMUTH,
    WIDGET_ELEVATION,
    WIDGET_FIXED_TARGET,
    WIDGET_AUTO_SEARCH,
    WIDGET_SWEEP,
    WIDGET_START,
    WIDGET_STOP,
    WIDGET_FOOTER,
    WIDGET_VISION_DISTANCE,
    WIDGET_KEY_1 = 12,
    WIDGET_KEY_2,
    WIDGET_KEY_3,
    WIDGET_KEY_4,
    WIDGET_KEY_5,
    WIDGET_KEY_6,
    WIDGET_KEY_7,
    WIDGET_KEY_8,
    WIDGET_KEY_9,
    WIDGET_KEY_CLEAR,
    WIDGET_KEY_0,
    WIDGET_KEY_BACK,
    WIDGET_KEY_CONFIRM,
    WIDGET_KEY_CANCEL,
    WIDGET_DISTANCE_DISPLAY,
    WIDGET_DISTANCE_RANGE,
    WIDGET_MOTOR_ENABLE,
    WIDGET_MOTOR_SCAN,
    WIDGET_MOTOR_STOP,
    WIDGET_MOTOR_DISABLE,
    WIDGET_MOTOR_BACK,
    WIDGET_THEME_BACK,
    WIDGET_THEME_NEXT,
    WIDGET_CANNON_BACK,
    WIDGET_CANNON_FIRE,
    WIDGET_CANNON_ABORT,
    WIDGET_CHARGE,
    WIDGET_CHARGE_BACK,
    WIDGET_CHARGE_START,
    WIDGET_CHARGE_ABORT
};

typedef struct {
    uint8_t id;
    ui_rect_t rect;
} widget_t;

static const widget_t s_main_widgets[] = {
    {WIDGET_DISTANCE,      {8U,   36U,  96U, 42U}},
    {WIDGET_FIRE_DISTANCE, {216U, 36U,  96U, 42U}},
    {WIDGET_AZIMUTH,       {112U, 84U,  96U, 36U}},
    {WIDGET_ELEVATION,     {216U, 84U,  96U, 36U}},
    {WIDGET_FIXED_TARGET,  {8U,  126U,  96U, 44U}},
    {WIDGET_AUTO_SEARCH,   {112U,126U,  96U, 44U}},
    {WIDGET_SWEEP,         {216U,126U,  96U, 44U}},
    {WIDGET_START,         {8U,  176U,  96U, 40U}},
    {WIDGET_CHARGE,        {112U,176U,  96U, 40U}},
    {WIDGET_STOP,          {216U,176U,  96U, 40U}}
};

static const widget_t s_key_widgets[] = {
    {WIDGET_KEY_1,       {8U,   36U, 44U, 42U}},
    {WIDGET_KEY_2,       {60U,  36U, 44U, 42U}},
    {WIDGET_KEY_3,       {112U, 36U, 44U, 42U}},
    {WIDGET_KEY_4,       {8U,   81U, 44U, 42U}},
    {WIDGET_KEY_5,       {60U,  81U, 44U, 42U}},
    {WIDGET_KEY_6,       {112U, 81U, 44U, 42U}},
    {WIDGET_KEY_7,       {8U,  126U, 44U, 42U}},
    {WIDGET_KEY_8,       {60U, 126U, 44U, 42U}},
    {WIDGET_KEY_9,       {112U,126U, 44U, 42U}},
    {WIDGET_KEY_CLEAR,   {8U,  171U, 44U, 42U}},
    {WIDGET_KEY_0,       {60U, 171U, 44U, 42U}},
    {WIDGET_KEY_BACK,    {112U,171U, 44U, 42U}},
    {WIDGET_KEY_CONFIRM, {176U,128U,136U, 42U}},
    {WIDGET_KEY_CANCEL,  {176U,176U,136U, 42U}}
};

static const widget_t s_motor_widgets[] = {
    {WIDGET_MOTOR_BACK,    {4U,   2U,  80U, 26U}},
    {WIDGET_MOTOR_ENABLE,  {8U, 130U, 148U, 42U}},
    {WIDGET_MOTOR_SCAN,    {164U,130U, 148U, 42U}},
    {WIDGET_MOTOR_STOP,    {8U, 176U, 148U, 42U}},
    {WIDGET_MOTOR_DISABLE, {164U,176U, 148U, 42U}}
};

/* Touch targets fill the gaps around the visible buttons for finger use. */
static const widget_t s_motor_touch_widgets[] = {
    {WIDGET_MOTOR_BACK,    {0U,   0U,  92U, 36U}},
    {WIDGET_MOTOR_ENABLE,  {0U, 124U, 160U, 50U}},
    {WIDGET_MOTOR_SCAN,    {160U,124U, 160U, 50U}},
    {WIDGET_MOTOR_STOP,    {0U, 174U, 160U, 46U}},
    {WIDGET_MOTOR_DISABLE, {160U,174U, 160U, 46U}}
};

static const widget_t s_theme_widgets[] = {
    {WIDGET_THEME_BACK, {0U,   0U, 96U, 40U}},
    {WIDGET_THEME_NEXT, {224U, 0U, 96U, 40U}}
};

#if CANNON_MANUAL_TEST_MODE != 0U
static const widget_t s_cannon_widgets[] = {
    {WIDGET_ELEVATION,    {8U,   36U, 148U, 58U}},
    {WIDGET_CHARGE_START, {8U,  106U, 148U, 48U}},
    {WIDGET_CHARGE_ABORT, {164U,106U, 148U, 48U}},
    {WIDGET_CANNON_FIRE,  {8U,  162U, 148U, 48U}},
    {WIDGET_CANNON_ABORT, {164U,162U, 148U, 48U}}
};

static const widget_t s_cannon_touch_widgets[] = {
    {WIDGET_ELEVATION,    {0U,   32U, 160U, 66U}},
    {WIDGET_CHARGE_START, {0U,  100U, 160U, 58U}},
    {WIDGET_CHARGE_ABORT, {160U,100U, 160U, 58U}},
    {WIDGET_CANNON_FIRE,  {0U,  158U, 160U, 56U}},
    {WIDGET_CANNON_ABORT, {160U,158U, 160U, 56U}}
};
#else
static const widget_t s_cannon_widgets[] = {
    {WIDGET_CANNON_BACK,  {4U,   2U,  80U, 26U}},
    {WIDGET_CANNON_FIRE,  {8U, 130U, 304U, 42U}},
    {WIDGET_CANNON_ABORT, {8U, 176U, 304U, 42U}}
};

static const widget_t s_cannon_touch_widgets[] = {
    {WIDGET_CANNON_BACK,  {0U,   0U,  92U, 36U}},
    {WIDGET_CANNON_FIRE,  {0U, 124U, 320U, 50U}},
    {WIDGET_CANNON_ABORT, {0U, 174U, 320U, 46U}}
};
#endif

static const widget_t s_charge_widgets[] = {
    {WIDGET_CHARGE_BACK,  {4U,   2U,  80U, 26U}},
    {WIDGET_CHARGE_START, {8U, 130U, 304U, 42U}},
    {WIDGET_CHARGE_ABORT, {8U, 176U, 304U, 42U}}
};

static const widget_t s_charge_touch_widgets[] = {
    {WIDGET_CHARGE_BACK,  {0U,   0U,  92U, 36U}},
    {WIDGET_CHARGE_START, {0U, 124U, 320U, 50U}},
    {WIDGET_CHARGE_ABORT, {0U, 174U, 320U, 46U}}
};

static const ui_rect_t s_status_rect = {8U, 84U, 96U, 36U};
static const ui_rect_t s_azimuth_rect = {112U, 84U, 96U, 36U};
static const ui_rect_t s_elevation_rect = {216U, 84U, 96U, 36U};
static const ui_rect_t s_vision_distance_rect = {112U, 36U, 96U, 42U};
static const ui_rect_t s_footer_rect = {8U, 220U, 304U, 18U};
static const ui_rect_t s_distance_display_rect = {176U, 36U, 136U, 48U};
static const ui_rect_t s_distance_range_rect = {176U, 92U, 136U, 28U};

static void invalidate(target_ui_t *ui, uint8_t widget)
{
    if (widget < 64U) {
        ui->dirty_widgets |= 1ULL << widget;
    }
}

static bool is_dirty(const target_ui_t *ui, uint8_t widget)
{
    return ui->full_redraw ||
           ((widget < 64U) &&
            ((ui->dirty_widgets & (1ULL << widget)) != 0U));
}

static bool point_in_rect(uint16_t x, uint16_t y, const ui_rect_t *rect)
{
    return (x >= rect->x) && (y >= rect->y) &&
           (x < ((uint32_t)rect->x + rect->width)) &&
           (y < ((uint32_t)rect->y + rect->height));
}

static bool scan_start_allowed(const target_ui_t *ui)
{
    return (GIMBAL_SCAN_ENABLED != 0U) &&
           ((ui->runtime_status == TARGET_UI_READY) ||
            (ui->runtime_status == TARGET_UI_COMPLETE));
}

static bool motor_widget_enabled(const target_ui_t *ui, uint8_t id)
{
    if (id == WIDGET_MOTOR_BACK) {
        return true;
    }

    switch (id) {
    case WIDGET_MOTOR_ENABLE:
        return (ui->runtime_status != TARGET_UI_ENABLING) &&
               (ui->runtime_status != TARGET_UI_DISABLING) &&
               (ui->runtime_status != TARGET_UI_RUNNING);
    case WIDGET_MOTOR_SCAN:
        return scan_start_allowed(ui);
    case WIDGET_MOTOR_STOP:
        return (ui->runtime_status == TARGET_UI_READY) ||
               (ui->runtime_status == TARGET_UI_RUNNING) ||
               (ui->runtime_status == TARGET_UI_COMPLETE);
    case WIDGET_MOTOR_DISABLE:
        return ui->runtime_status != TARGET_UI_DISABLING;
    default:
        return false;
    }
}

static uint8_t find_widget(const target_ui_t *ui, uint16_t x, uint16_t y)
{
    const widget_t *widgets;
    size_t count;
    size_t index;

    if (ui->page == PAGE_MAIN) {
        widgets = s_main_widgets;
        count = sizeof(s_main_widgets) / sizeof(s_main_widgets[0]);
    } else if ((ui->page == PAGE_DISTANCE) ||
               (ui->page == PAGE_ANGLE)) {
        widgets = s_key_widgets;
        count = sizeof(s_key_widgets) / sizeof(s_key_widgets[0]);
    } else if (ui->page == PAGE_MOTOR) {
        widgets = s_motor_touch_widgets;
        count = sizeof(s_motor_touch_widgets) /
                sizeof(s_motor_touch_widgets[0]);
    } else if (ui->page == PAGE_CANNON) {
        widgets = s_cannon_touch_widgets;
        count = sizeof(s_cannon_touch_widgets) /
                sizeof(s_cannon_touch_widgets[0]);
    } else if (ui->page == PAGE_CHARGE) {
        widgets = s_charge_touch_widgets;
        count = sizeof(s_charge_touch_widgets) /
                sizeof(s_charge_touch_widgets[0]);
    } else {
        widgets = s_theme_widgets;
        count = sizeof(s_theme_widgets) / sizeof(s_theme_widgets[0]);
    }

    for (index = 0U; index < count; ++index) {
        if (point_in_rect(x, y, &widgets[index].rect)) {
            return widgets[index].id;
        }
    }
    return WIDGET_NONE;
}

static const ui_rect_t *interactive_rect(const target_ui_t *ui, uint8_t id)
{
    const widget_t *widgets;
    size_t count;
    size_t index;

    if (ui->page == PAGE_MAIN) {
        widgets = s_main_widgets;
        count = sizeof(s_main_widgets) / sizeof(s_main_widgets[0]);
    } else if ((ui->page == PAGE_DISTANCE) ||
               (ui->page == PAGE_ANGLE)) {
        widgets = s_key_widgets;
        count = sizeof(s_key_widgets) / sizeof(s_key_widgets[0]);
    } else if (ui->page == PAGE_MOTOR) {
        widgets = s_motor_widgets;
        count = sizeof(s_motor_widgets) / sizeof(s_motor_widgets[0]);
    } else if (ui->page == PAGE_CANNON) {
        widgets = s_cannon_widgets;
        count = sizeof(s_cannon_widgets) / sizeof(s_cannon_widgets[0]);
    } else if (ui->page == PAGE_CHARGE) {
        widgets = s_charge_widgets;
        count = sizeof(s_charge_widgets) / sizeof(s_charge_widgets[0]);
    } else {
        widgets = s_theme_widgets;
        count = sizeof(s_theme_widgets) / sizeof(s_theme_widgets[0]);
    }

    for (index = 0U; index < count; ++index) {
        if (widgets[index].id == id) {
            return &widgets[index].rect;
        }
    }
    return NULL;
}

static void fill(const ui_canvas_t *canvas,
                 const ui_rect_t *rect,
                 uint16_t color)
{
    canvas->fill_rect(canvas->context, rect, color);
}

static void border(const ui_canvas_t *canvas,
                   const ui_rect_t *rect,
                   uint16_t color)
{
    const ui_rect_t top = {rect->x, rect->y, rect->width, 1U};
    const ui_rect_t bottom = {rect->x,
                              (uint16_t)(rect->y + rect->height - 1U),
                              rect->width, 1U};
    const ui_rect_t left = {rect->x, rect->y, 1U, rect->height};
    const ui_rect_t right = {(uint16_t)(rect->x + rect->width - 1U),
                             rect->y, 1U, rect->height};
    fill(canvas, &top, color);
    fill(canvas, &bottom, color);
    fill(canvas, &left, color);
    fill(canvas, &right, color);
}

static void text_center(const ui_canvas_t *canvas,
                        const ui_rect_t *rect,
                        const char *text,
                        uint16_t foreground,
                        uint16_t background)
{
    const uint16_t text_width = ui_font_text_width(text);
    uint16_t x = rect->x;
    uint16_t y = rect->y;

    if (text_width < rect->width) {
        x = (uint16_t)(x + (rect->width - text_width) / 2U);
    }
    if (UI_FONT_HEIGHT < rect->height) {
        y = (uint16_t)(y + (rect->height - UI_FONT_HEIGHT) / 2U);
    }
    canvas->draw_text(canvas->context, x, y, text, foreground, background);
}

static void draw_button(const target_ui_t *ui,
                        const ui_canvas_t *canvas,
                        uint8_t id,
                        const ui_rect_t *rect,
                        const char *text,
                        uint16_t normal_color,
                        uint16_t pressed_color)
{
    const bool pressed = ui->active_widget == id;
    const uint16_t background = pressed ? pressed_color : normal_color;
    fill(canvas, rect, background);
    border(canvas, rect, pressed ? COLOR_TEXT : normal_color);
    text_center(canvas, rect, text, COLOR_TEXT, background);
}

static void draw_motor_button(const target_ui_t *ui,
                              const ui_canvas_t *canvas,
                              uint8_t id,
                              const ui_rect_t *rect,
                              const char *text,
                              uint16_t normal_color,
                              uint16_t pressed_color)
{
    if (motor_widget_enabled(ui, id)) {
        draw_button(ui, canvas, id, rect, text, normal_color, pressed_color);
        return;
    }

    fill(canvas, rect, COLOR_DISABLED);
    border(canvas, rect, COLOR_BORDER);
    text_center(canvas, rect, text, COLOR_DISABLED_TEXT, COLOR_DISABLED);
}

static void draw_disabled_button(const ui_canvas_t *canvas,
                                 const ui_rect_t *rect,
                                 const char *text)
{
    fill(canvas, rect, COLOR_DISABLED);
    border(canvas, rect, COLOR_BORDER);
    text_center(canvas, rect, text, COLOR_DISABLED_TEXT, COLOR_DISABLED);
}

static void format_uint16(uint16_t value, char *text)
{
    char reverse[5];
    uint8_t count = 0U;
    uint8_t index;

    do {
        reverse[count] = (char)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
        ++count;
    } while ((value > 0U) && (count < sizeof(reverse)));

    for (index = 0U; index < count; ++index) {
        text[index] = reverse[count - index - 1U];
    }
    text[count] = '\0';
}

static void format_clock(uint16_t seconds, char *text)
{
    uint16_t minutes = (uint16_t)(seconds / 60U);

    if (minutes > 99U) {
        minutes = 99U;
        seconds = 59U;
    } else {
        seconds = (uint16_t)(seconds % 60U);
    }
    text[0] = (char)('0' + (minutes / 10U));
    text[1] = (char)('0' + (minutes % 10U));
    text[2] = ':';
    text[3] = (char)('0' + (seconds / 10U));
    text[4] = (char)('0' + (seconds % 10U));
    text[5] = '\0';
}

static void format_charge_timer(uint16_t elapsed_seconds, char *text)
{
    char elapsed[6];
#if CANNON_MANUAL_TEST_MODE != 0U
    uint8_t index;

    format_clock(elapsed_seconds, elapsed);
    text[0] = 'T';
    text[1] = 'I';
    text[2] = 'M';
    text[3] = 'E';
    text[4] = ' ';
    for (index = 0U; index < 5U; ++index) {
        text[5U + index] = elapsed[index];
    }
    text[10] = '\0';
#else
    char total[6];
    const uint16_t total_seconds =
        (uint16_t)((CANNON_CHARGE_DURATION_MS + 999U) / 1000U);
    uint8_t index;

    if (elapsed_seconds > total_seconds) {
        elapsed_seconds = total_seconds;
    }
    format_clock(elapsed_seconds, elapsed);
    format_clock(total_seconds, total);
    text[0] = 'C';
    text[1] = 'H';
    text[2] = 'G';
    text[3] = ' ';
    for (index = 0U; index < 5U; ++index) {
        text[4U + index] = elapsed[index];
    }
    text[9] = '/';
    for (index = 0U; index < 5U; ++index) {
        text[10U + index] = total[index];
    }
    text[15] = '\0';
#endif
}

static void format_fire_timer(uint8_t remaining_tenths, char *text)
{
    if (remaining_tenths > 9U) {
        remaining_tenths = 9U;
    }
    text[0] = 'F';
    text[1] = 'I';
    text[2] = 'R';
    text[3] = 'E';
    text[4] = ' ';
    text[5] = '0';
    text[6] = '.';
    text[7] = (char)('0' + remaining_tenths);
    text[8] = 's';
    text[9] = '\0';
}

#if CANNON_MANUAL_TEST_MODE == 0U
static const char *cannon_state_text(target_ui_cannon_state_t state)
{
    switch (state) {
    case TARGET_UI_CANNON_CHARGING:
        return "CHARGING";
    case TARGET_UI_CANNON_PAUSED:
        return "PAUSED";
    case TARGET_UI_CANNON_WAIT_AIM:
        return "WAIT AIM";
    case TARGET_UI_CANNON_FIRING:
        return "FIRING";
    case TARGET_UI_CANNON_READY:
        return "READY";
    case TARGET_UI_CANNON_IDLE:
    default:
        return "IDLE";
    }
}
#endif

static uint16_t cannon_state_color(target_ui_cannon_state_t state)
{
    switch (state) {
    case TARGET_UI_CANNON_CHARGING:
        return COLOR_WARNING;
    case TARGET_UI_CANNON_WAIT_AIM:
        return COLOR_PRIMARY;
    case TARGET_UI_CANNON_FIRING:
        return COLOR_DANGER;
    case TARGET_UI_CANNON_READY:
        return COLOR_SUCCESS;
    case TARGET_UI_CANNON_PAUSED:
    case TARGET_UI_CANNON_IDLE:
    default:
        return COLOR_MUTED;
    }
}

static void format_distance(uint16_t value, char *text)
{
    char digits[6];
    uint8_t length = 0U;

    format_uint16(value, digits);
    while (digits[length] != '\0') {
        text[length] = digits[length];
        ++length;
    }
    text[length++] = ' ';
    text[length++] = 'c';
    text[length++] = 'm';
    text[length] = '\0';
}

static void format_vision_distance(const target_ui_t *ui, char *text)
{
    char digits[6];
    uint16_t tenths_cm;
    uint8_t length = 0U;

    if (!ui->vision_distance_valid) {
        text[0] = '-';
        text[1] = '-';
        text[2] = ' ';
        text[3] = 'c';
        text[4] = 'm';
        text[5] = '\0';
        return;
    }

    /* One millimetre is exactly one tenth of a centimetre. */
    tenths_cm = ui->vision_distance_mm;
    format_uint16((uint16_t)(tenths_cm / 10U), digits);
    while (digits[length] != '\0') {
        text[length] = digits[length];
        ++length;
    }
    text[length++] = '.';
    text[length++] = (char)('0' + (tenths_cm % 10U));
    text[length++] = 'c';
    text[length++] = 'm';
    text[length] = '\0';
}

static void format_angle(int16_t degrees, char *text)
{
    uint16_t magnitude;
    char digits[6];
    uint8_t length = 0U;

    if (degrees < 0) {
        text[length++] = '-';
        magnitude = (uint16_t)(-(int32_t)degrees);
    } else {
        magnitude = (uint16_t)degrees;
    }
    format_uint16(magnitude, digits);
    {
        uint8_t index = 0U;
        while (digits[index] != '\0') {
            text[length++] = digits[index++];
        }
    }
    text[length++] = (char)0xC2;
    text[length++] = (char)0xB0;
    text[length] = '\0';
}

static uint8_t append_signed_value(char *text, uint8_t offset, int16_t value)
{
    char digits[6];
    uint16_t magnitude;
    uint8_t index = 0U;

    if (value < 0) {
        text[offset++] = '-';
        magnitude = (uint16_t)(-(int32_t)value);
    } else {
        magnitude = (uint16_t)value;
    }
    format_uint16(magnitude, digits);
    while (digits[index] != '\0') {
        text[offset++] = digits[index++];
    }
    return offset;
}

static void format_axis_summary(int16_t actual,
                                int16_t target,
                                char *text)
{
    uint8_t length = 0U;

    text[length++] = 'A';
    length = append_signed_value(text, length, actual);
    text[length++] = ' ';
    text[length++] = 'T';
    length = append_signed_value(text, length, target);
    text[length] = '\0';
}

static const char *runtime_text(target_ui_runtime_status_t status)
{
    switch (status) {
    case TARGET_UI_ENABLING:
        return "ENABLING";
    case TARGET_UI_DISABLING:
        return "DISABLING";
    case TARGET_UI_RUNNING:
        return "ANGLE HOLD";
    case TARGET_UI_COMPLETE:
        return "STOPPED";
    case TARGET_UI_READY:
        return "ENABLED";
    case TARGET_UI_FAULT:
        return "FAULT";
    case TARGET_UI_UNKNOWN:
        return "UNKNOWN";
    case TARGET_UI_IDLE:
    default:
        return "DISABLED";
    }
}

static uint16_t runtime_color(target_ui_runtime_status_t status)
{
    switch (status) {
    case TARGET_UI_READY:
        return COLOR_SUCCESS;
    case TARGET_UI_RUNNING:
        return COLOR_PRIMARY;
    case TARGET_UI_COMPLETE:
    case TARGET_UI_ENABLING:
    case TARGET_UI_DISABLING:
        return COLOR_WARNING;
    case TARGET_UI_FAULT:
        return COLOR_DANGER;
    case TARGET_UI_UNKNOWN:
    case TARGET_UI_IDLE:
    default:
        return COLOR_MUTED;
    }
}

static const char *feedback_text(target_ui_feedback_t feedback)
{
    switch (feedback) {
    case TARGET_UI_FEEDBACK_COMMAND_SENT:
        return "命令已发送";
    case TARGET_UI_FEEDBACK_CONTROLLER_MISSING:
        return "控制器未接入";
    case TARGET_UI_FEEDBACK_QUEUE_FULL:
        return "命令队列已满";
    case TARGET_UI_FEEDBACK_ERROR:
        return "显示或触摸故障";
    case TARGET_UI_FEEDBACK_CAN_WAIT:
        return "CAN WAIT";
    case TARGET_UI_FEEDBACK_CAN_DISABLE_WAIT:
        return "CAN DISABLE WAIT";
    case TARGET_UI_FEEDBACK_YAW_SCANNING:
        return "ANGLE PID HOLD";
    case TARGET_UI_FEEDBACK_CAN_ENABLED:
        return "YAW/PITCH OK";
    case TARGET_UI_FEEDBACK_CAN_DISABLED:
        return "YAW/PITCH OFF";
    case TARGET_UI_FEEDBACK_YAW_SCAN_ERROR:
        return "GIMBAL CONTROL ERROR";
    case TARGET_UI_FEEDBACK_CAN_YAW_ERROR:
        return "CAN YAW ERROR";
    case TARGET_UI_FEEDBACK_CAN_PITCH_ERROR:
        return "CAN PITCH ERROR";
    case TARGET_UI_FEEDBACK_CAN_ERROR:
        return "CAN NO RESPONSE";
    case TARGET_UI_FEEDBACK_MOTOR_STOPPED:
        return "HOLD STOPPED / MOTOR ON";
    case TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE:
        return "ENABLE MOTOR FIRST";
    case TARGET_UI_FEEDBACK_STATE_UNKNOWN:
        return "SELECT ENABLE OR DISABLE";
    case TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE:
        return "ACTION NOT AVAILABLE";
    case TARGET_UI_FEEDBACK_VISION_DISTANCE_INVALID:
        return "WAIT DIST 50-100CM";
    case TARGET_UI_FEEDBACK_VISION_TARGET_INVALID:
        return "WAIT TARGET LOCK";
    case TARGET_UI_FEEDBACK_FIRE_ACTIVE:
        return "发射脉冲中";
    case TARGET_UI_FEEDBACK_FIRE_COMPLETE:
        return "发射继电器已断开";
    case TARGET_UI_FEEDBACK_CHARGE_ACTIVE:
        return "充电继电器已接通";
    case TARGET_UI_FEEDBACK_CHARGE_COMPLETE:
        return "充电继电器已断开";
    case TARGET_UI_FEEDBACK_READY:
    default:
        return "触摸屏已就绪";
    }
}

static uint16_t feedback_color(target_ui_feedback_t feedback)
{
    switch (feedback) {
    case TARGET_UI_FEEDBACK_CAN_ENABLED:
    case TARGET_UI_FEEDBACK_CAN_DISABLED:
    case TARGET_UI_FEEDBACK_MOTOR_STOPPED:
        return COLOR_SUCCESS;
    case TARGET_UI_FEEDBACK_CAN_WAIT:
    case TARGET_UI_FEEDBACK_CAN_DISABLE_WAIT:
    case TARGET_UI_FEEDBACK_COMMAND_SENT:
    case TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE:
    case TARGET_UI_FEEDBACK_STATE_UNKNOWN:
    case TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE:
    case TARGET_UI_FEEDBACK_VISION_DISTANCE_INVALID:
    case TARGET_UI_FEEDBACK_VISION_TARGET_INVALID:
    case TARGET_UI_FEEDBACK_CHARGE_ACTIVE:
        return COLOR_WARNING;
    case TARGET_UI_FEEDBACK_FIRE_COMPLETE:
    case TARGET_UI_FEEDBACK_CHARGE_COMPLETE:
        return COLOR_SUCCESS;
    case TARGET_UI_FEEDBACK_CONTROLLER_MISSING:
    case TARGET_UI_FEEDBACK_QUEUE_FULL:
    case TARGET_UI_FEEDBACK_ERROR:
    case TARGET_UI_FEEDBACK_YAW_SCAN_ERROR:
    case TARGET_UI_FEEDBACK_CAN_YAW_ERROR:
    case TARGET_UI_FEEDBACK_CAN_PITCH_ERROR:
    case TARGET_UI_FEEDBACK_CAN_ERROR:
    case TARGET_UI_FEEDBACK_FIRE_ACTIVE:
        return COLOR_DANGER;
    case TARGET_UI_FEEDBACK_YAW_SCANNING:
        return COLOR_PRIMARY;
    case TARGET_UI_FEEDBACK_READY:
    default:
        return COLOR_MUTED;
    }
}

static void draw_main(target_ui_t *ui, const ui_canvas_t *canvas)
{
    char text[16];
    const ui_rect_t header = {0U, 0U, 320U, 30U};
    const ui_rect_t header_line = {0U, 28U, 320U, 2U};
    const ui_rect_t status_accent = {8U, 84U, 4U, 36U};
    const ui_rect_t *rect;

    if (ui->full_redraw) {
        const ui_rect_t screen = {0U, 0U, 320U, 240U};
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        fill(canvas, &header_line, COLOR_PRIMARY);
        text_center(canvas, &header, "电磁炮击靶系统",
                    COLOR_TEXT, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_DISTANCE)) {
        rect = interactive_rect(ui, WIDGET_DISTANCE);
        fill(canvas, rect, COLOR_PANEL);
        border(canvas, rect, ui->active_widget == WIDGET_DISTANCE ?
               COLOR_PRIMARY : COLOR_BORDER);
        canvas->draw_text(canvas->context, 16U, 40U, "设定",
                          COLOR_MUTED, COLOR_PANEL);
        format_distance(ui->distance_cm, text);
        canvas->draw_text(canvas->context, 16U, 58U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_VISION_DISTANCE)) {
        fill(canvas, &s_vision_distance_rect, COLOR_PANEL);
        border(canvas, &s_vision_distance_rect,
               ui->vision_distance_valid ? COLOR_SUCCESS : COLOR_BORDER);
        canvas->draw_text(canvas->context, 118U, 40U, "DIST",
                          COLOR_MUTED, COLOR_PANEL);
        format_vision_distance(ui, text);
        canvas->draw_text(canvas->context, 118U, 58U, text,
                          ui->vision_distance_valid ? COLOR_TEXT :
                          COLOR_DISABLED_TEXT,
                          COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_FIRE_DISTANCE)) {
        rect = interactive_rect(ui, WIDGET_FIRE_DISTANCE);
        draw_button(ui, canvas, WIDGET_FIRE_DISTANCE, rect, "距离定位",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }

    if (is_dirty(ui, WIDGET_STATUS)) {
        fill(canvas, &s_status_rect, COLOR_PANEL);
        border(canvas, &s_status_rect, COLOR_BORDER);
        fill(canvas, &status_accent, runtime_color(ui->runtime_status));
        canvas->draw_text(canvas->context, 16U, 86U, "状态",
                          COLOR_MUTED, COLOR_PANEL);
        canvas->draw_text(canvas->context, 16U, 102U,
                          runtime_text(ui->runtime_status),
                          runtime_color(ui->runtime_status),
                          COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_AZIMUTH)) {
        fill(canvas, &s_azimuth_rect, COLOR_PANEL);
        border(canvas, &s_azimuth_rect,
               ui->active_widget == WIDGET_AZIMUTH ?
               COLOR_PRIMARY : COLOR_BORDER);
        canvas->draw_text(canvas->context, 118U, 86U, "YAW",
                          COLOR_MUTED, COLOR_PANEL);
        format_axis_summary(ui->azimuth_degrees,
                            ui->yaw_target_degrees, text);
        canvas->draw_text(canvas->context, 118U, 102U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_ELEVATION)) {
        fill(canvas, &s_elevation_rect, COLOR_PANEL);
        border(canvas, &s_elevation_rect,
               ui->active_widget == WIDGET_ELEVATION ?
               COLOR_PRIMARY : COLOR_BORDER);
        canvas->draw_text(canvas->context, 222U, 86U, "PITCH",
                          COLOR_MUTED, COLOR_PANEL);
        format_axis_summary(ui->elevation_degrees,
                            ui->pitch_target_degrees, text);
        canvas->draw_text(canvas->context, 222U, 102U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_FIXED_TARGET)) {
        rect = interactive_rect(ui, WIDGET_FIXED_TARGET);
        draw_button(ui, canvas, WIDGET_FIXED_TARGET, rect, "固定靶",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }
    if (is_dirty(ui, WIDGET_AUTO_SEARCH)) {
        rect = interactive_rect(ui, WIDGET_AUTO_SEARCH);
        draw_button(ui, canvas, WIDGET_AUTO_SEARCH, rect, "自动搜索",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }
    if (is_dirty(ui, WIDGET_SWEEP)) {
        rect = interactive_rect(ui, WIDGET_SWEEP);
        if (scan_start_allowed(ui)) {
            draw_button(ui, canvas, WIDGET_SWEEP, rect, "往复扫描",
                        COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
        } else {
            draw_disabled_button(
                canvas, rect,
                (GIMBAL_SCAN_ENABLED != 0U) ? "往复扫描" : "SCAN OFF");
        }
    }

    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &s_footer_rect, COLOR_PANEL);
        text_center(canvas, &s_footer_rect, feedback_text(ui->feedback),
                    feedback_color(ui->feedback),
                    COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_START)) {
        rect = interactive_rect(ui, WIDGET_START);
        draw_button(ui, canvas, WIDGET_START, rect, "MOTOR",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }

    if (is_dirty(ui, WIDGET_CHARGE)) {
        rect = interactive_rect(ui, WIDGET_CHARGE);
        draw_button(ui, canvas, WIDGET_CHARGE, rect, "充电",
                    COLOR_WARNING, COLOR_WARNING_PRESSED);
    }

    if (is_dirty(ui, WIDGET_STOP)) {
        rect = interactive_rect(ui, WIDGET_STOP);
        draw_button(ui, canvas, WIDGET_STOP, rect, "THEME",
                    COLOR_THEME, COLOR_THEME_PRESSED);
    }
}

static void draw_charge_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
    const ui_rect_t screen = {0U, 0U, 320U, 240U};
    const ui_rect_t header = {0U, 0U, 320U, 30U};
    const ui_rect_t header_line = {0U, 28U, 320U, 2U};
    const ui_rect_t state = {8U, 36U, 304U, 58U};
    const ui_rect_t state_accent = {8U, 36U, 5U, 58U};
    const ui_rect_t level_hint = {174U, 48U, 130U, 24U};
    const ui_rect_t warning = {8U, 100U, 304U, 24U};
    const ui_rect_t footer = {8U, 220U, 304U, 18U};
    const ui_rect_t *rect;

    if (ui->full_redraw) {
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        fill(canvas, &header_line, COLOR_WARNING);
        text_center(canvas, &header, "充电控制", COLOR_TEXT, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_CHARGE_BACK)) {
        rect = interactive_rect(ui, WIDGET_CHARGE_BACK);
        draw_button(ui, canvas, WIDGET_CHARGE_BACK, rect, "< BACK",
                    COLOR_HEADER, COLOR_WARNING_PRESSED);
    }
    if (is_dirty(ui, WIDGET_STATUS)) {
        fill(canvas, &state, COLOR_PANEL);
        border(canvas, &state, COLOR_BORDER);
        fill(canvas, &state_accent,
             ui->charge_active ? COLOR_WARNING : COLOR_SUCCESS);
        canvas->draw_text(canvas->context, 20U, 40U, "充电继电器",
                          COLOR_MUTED, COLOR_PANEL);
        canvas->draw_text(canvas->context, 20U, 62U,
                          ui->charge_active ? "充电中" : "已断开",
                          ui->charge_active ? COLOR_WARNING : COLOR_SUCCESS,
                          COLOR_PANEL);
        text_center(canvas, &level_hint, "PB1 / LOW",
                    COLOR_MUTED, COLOR_PANEL);
    }
    if (ui->full_redraw) {
        fill(canvas, &warning, COLOR_BACKGROUND);
        text_center(canvas, &warning, "充电期间禁止发射",
                    COLOR_WARNING, COLOR_BACKGROUND);
    }

    rect = interactive_rect(ui, WIDGET_CHARGE_START);
    if (is_dirty(ui, WIDGET_CHARGE_START)) {
        if (ui->charge_active) {
            draw_disabled_button(canvas, rect, "充电中");
        } else {
            draw_button(ui, canvas, WIDGET_CHARGE_START, rect, "开始充电",
                        COLOR_WARNING, COLOR_WARNING_PRESSED);
        }
    }
    if (is_dirty(ui, WIDGET_CHARGE_ABORT)) {
        rect = interactive_rect(ui, WIDGET_CHARGE_ABORT);
        draw_button(ui, canvas, WIDGET_CHARGE_ABORT, rect,
                    "立即断开充电", COLOR_DANGER,
                    COLOR_DANGER_PRESSED);
    }
    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &footer, COLOR_PANEL);
        text_center(canvas, &footer, feedback_text(ui->feedback),
                    feedback_color(ui->feedback), COLOR_PANEL);
    }
}

static void draw_cannon_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
#if CANNON_MANUAL_TEST_MODE != 0U
    char pitch_text[16];
    char charge_timer[16];
    char fire_timer[10];
    const ui_rect_t screen = {0U, 0U, 320U, 240U};
    const ui_rect_t header = {0U, 0U, 320U, 30U};
    const ui_rect_t header_line = {0U, 28U, 320U, 2U};
    const ui_rect_t timer = {164U, 36U, 148U, 58U};
    const ui_rect_t timer_accent = {164U, 36U, 5U, 58U};
    const ui_rect_t footer = {8U, 220U, 304U, 18U};
    const ui_rect_t *rect;

    if (ui->full_redraw) {
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        fill(canvas, &header_line, COLOR_WARNING);
        text_center(canvas, &header, "手动测试",
                    COLOR_TEXT, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_ELEVATION)) {
        rect = interactive_rect(ui, WIDGET_ELEVATION);
        fill(canvas, rect, COLOR_PANEL);
        border(canvas, rect, ui->active_widget == WIDGET_ELEVATION ?
               COLOR_PRIMARY : COLOR_BORDER);
        canvas->draw_text(canvas->context, 18U, 40U, "PITCH",
                          COLOR_MUTED, COLOR_PANEL);
        format_axis_summary(ui->elevation_degrees,
                            ui->pitch_target_degrees, pitch_text);
        canvas->draw_text(canvas->context, 18U, 62U, pitch_text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_STATUS)) {
        const uint16_t state_color = cannon_state_color(ui->cannon_state);

        fill(canvas, &timer, COLOR_PANEL);
        border(canvas, &timer, COLOR_BORDER);
        fill(canvas, &timer_accent, state_color);
        format_charge_timer(ui->cannon_charge_seconds, charge_timer);
        format_fire_timer(ui->cannon_fire_remaining_tenths, fire_timer);
        canvas->draw_text(canvas->context, 176U, 40U, charge_timer,
                          COLOR_TEXT, COLOR_PANEL);
        canvas->draw_text(canvas->context, 176U, 62U, fire_timer,
                          ui->fire_active ? COLOR_DANGER : COLOR_MUTED,
                          COLOR_PANEL);
    }

    rect = interactive_rect(ui, WIDGET_CHARGE_START);
    if (is_dirty(ui, WIDGET_CHARGE_START)) {
        if (ui->charge_active) {
            draw_disabled_button(canvas, rect, "充电中");
        } else {
            draw_button(ui, canvas, WIDGET_CHARGE_START, rect,
                        ui->cannon_charge_seconds > 0U ?
                        "继续充电" : "开始充电",
                        COLOR_WARNING, COLOR_WARNING_PRESSED);
        }
    }
    if (is_dirty(ui, WIDGET_CHARGE_ABORT)) {
        rect = interactive_rect(ui, WIDGET_CHARGE_ABORT);
        draw_button(ui, canvas, WIDGET_CHARGE_ABORT, rect,
                    "停止充电", COLOR_DANGER, COLOR_DANGER_PRESSED);
    }
    rect = interactive_rect(ui, WIDGET_CANNON_FIRE);
    if (is_dirty(ui, WIDGET_CANNON_FIRE)) {
        if (ui->fire_active) {
            draw_disabled_button(canvas, rect, "发射中");
        } else {
            draw_button(ui, canvas, WIDGET_CANNON_FIRE, rect,
                        "手动发射", COLOR_DANGER, COLOR_DANGER_PRESSED);
        }
    }
    if (is_dirty(ui, WIDGET_CANNON_ABORT)) {
        rect = interactive_rect(ui, WIDGET_CANNON_ABORT);
        draw_button(ui, canvas, WIDGET_CANNON_ABORT, rect,
                    "停止发射", COLOR_WARNING, COLOR_WARNING_PRESSED);
    }
    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &footer, COLOR_PANEL);
        text_center(canvas, &footer, feedback_text(ui->feedback),
                    feedback_color(ui->feedback), COLOR_PANEL);
    }
#else
    char charge_timer[16];
    char fire_timer[10];
    const ui_rect_t screen = {0U, 0U, 320U, 240U};
    const ui_rect_t header = {0U, 0U, 320U, 30U};
    const ui_rect_t header_line = {0U, 28U, 320U, 2U};
    const ui_rect_t state = {8U, 36U, 304U, 58U};
    const ui_rect_t state_accent = {8U, 36U, 5U, 58U};
    const ui_rect_t pulse_hint = {174U, 48U, 130U, 24U};
    const ui_rect_t warning = {8U, 100U, 304U, 24U};
    const ui_rect_t footer = {8U, 220U, 304U, 18U};
    const ui_rect_t *rect;

    if (ui->full_redraw) {
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        fill(canvas, &header_line, COLOR_DANGER);
        text_center(canvas, &header, "发射控制",
                    COLOR_TEXT, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_CANNON_BACK)) {
        rect = interactive_rect(ui, WIDGET_CANNON_BACK);
        draw_button(ui, canvas, WIDGET_CANNON_BACK, rect, "< BACK",
                    COLOR_HEADER, COLOR_DANGER_PRESSED);
    }
    if (is_dirty(ui, WIDGET_STATUS)) {
        const uint16_t state_color = cannon_state_color(ui->cannon_state);

        fill(canvas, &state, COLOR_PANEL);
        border(canvas, &state, COLOR_BORDER);
        fill(canvas, &state_accent, state_color);
        format_charge_timer(ui->cannon_charge_seconds, charge_timer);
        format_fire_timer(ui->cannon_fire_remaining_tenths, fire_timer);
        canvas->draw_text(canvas->context, 20U, 40U, charge_timer,
                          COLOR_TEXT, COLOR_PANEL);
        canvas->draw_text(canvas->context, 20U, 62U, fire_timer,
                          ui->fire_active ? COLOR_DANGER : COLOR_MUTED,
                          COLOR_PANEL);
        text_center(canvas, &pulse_hint,
                    cannon_state_text(ui->cannon_state),
                    state_color, COLOR_PANEL);
    }
    if (ui->full_redraw) {
        fill(canvas, &warning, COLOR_BACKGROUND);
        text_center(canvas, &warning, "确认炮口方向与人员安全",
                    COLOR_WARNING, COLOR_BACKGROUND);
    }

    rect = interactive_rect(ui, WIDGET_CANNON_FIRE);
    if (is_dirty(ui, WIDGET_CANNON_FIRE)) {
        if (ui->fire_active) {
            draw_disabled_button(canvas, rect, "发射中");
        } else if (ui->auto_aim_active) {
            draw_disabled_button(canvas, rect, "自动瞄准");
        } else {
            draw_button(ui, canvas, WIDGET_CANNON_FIRE, rect, "发射",
                        COLOR_DANGER, COLOR_DANGER_PRESSED);
        }
    }
    if (is_dirty(ui, WIDGET_CANNON_ABORT)) {
        rect = interactive_rect(ui, WIDGET_CANNON_ABORT);
        draw_button(ui, canvas, WIDGET_CANNON_ABORT, rect,
                    "立即断开发射", COLOR_WARNING,
                    COLOR_WARNING_PRESSED);
    }
    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &footer, COLOR_PANEL);
        text_center(canvas, &footer, feedback_text(ui->feedback),
                    feedback_color(ui->feedback), COLOR_PANEL);
    }
#endif
}

static void draw_motor_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
    char text[12];
    const ui_rect_t screen = {0U, 0U, 320U, 240U};
    const ui_rect_t header = {0U, 0U, 320U, 30U};
    const ui_rect_t header_line = {0U, 28U, 320U, 2U};
    const ui_rect_t state = {8U, 36U, 304U, 46U};
    const ui_rect_t state_accent = {8U, 36U, 5U, 46U};
    const ui_rect_t state_hint = {152U, 47U, 152U, 24U};
    const ui_rect_t yaw = {8U, 88U, 148U, 36U};
    const ui_rect_t pitch = {164U, 88U, 148U, 36U};
    const ui_rect_t yaw_accent = {8U, 88U, 4U, 36U};
    const ui_rect_t pitch_accent = {164U, 88U, 4U, 36U};
    const ui_rect_t footer = {8U, 220U, 304U, 18U};
    const ui_rect_t *rect;
    const char *enable_label = "ENABLE";
    const char *scan_label = (GIMBAL_SCAN_ENABLED != 0U) ?
                             "START SCAN" : "SCAN OFF";
    const char *stop_label = (GIMBAL_SCAN_ENABLED != 0U) ?
                             "STOP SCAN" : "ZERO CURRENT";
    const char *disable_label = "DISABLE";

    if ((ui->runtime_status == TARGET_UI_READY) ||
        (ui->runtime_status == TARGET_UI_COMPLETE)) {
        enable_label = "ENABLE / HOME";
    } else if (ui->runtime_status == TARGET_UI_RUNNING) {
        enable_label = "ENABLED";
    } else if (ui->runtime_status == TARGET_UI_ENABLING) {
        enable_label = "ENABLING";
    }
    if (ui->runtime_status == TARGET_UI_RUNNING) {
        scan_label = "ANGLE HOLD";
    }
    if (ui->runtime_status == TARGET_UI_DISABLING) {
        disable_label = "DISABLING";
    }

    if (ui->full_redraw) {
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        fill(canvas, &header_line, COLOR_PRIMARY);
        text_center(canvas, &header, "GIMBAL MOTOR",
                    COLOR_TEXT, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_MOTOR_BACK)) {
        rect = interactive_rect(ui, WIDGET_MOTOR_BACK);
        draw_button(ui, canvas, WIDGET_MOTOR_BACK, rect, "< BACK",
                    COLOR_HEADER, COLOR_PRIMARY_PRESSED);
    }
    if (is_dirty(ui, WIDGET_STATUS)) {
        fill(canvas, &state, COLOR_PANEL);
        border(canvas, &state, COLOR_BORDER);
        fill(canvas, &state_accent, runtime_color(ui->runtime_status));
        canvas->draw_text(canvas->context, 20U, 39U, "MOTOR STATE",
                          COLOR_MUTED, COLOR_PANEL);
        canvas->draw_text(canvas->context, 20U, 59U,
                          runtime_text(ui->runtime_status),
                          runtime_color(ui->runtime_status), COLOR_PANEL);
        text_center(canvas, &state_hint, "AUTO HOLD @ POWER",
                    COLOR_MUTED, COLOR_PANEL);
    }
    if (is_dirty(ui, WIDGET_AZIMUTH)) {
        fill(canvas, &yaw, COLOR_PANEL);
        border(canvas, &yaw, COLOR_BORDER);
        fill(canvas, &yaw_accent, COLOR_PRIMARY);
        canvas->draw_text(canvas->context, 18U, 90U, "YAW",
                          COLOR_MUTED, COLOR_PANEL);
        format_angle(ui->azimuth_degrees, text);
        canvas->draw_text(canvas->context, 82U, 98U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }
    if (is_dirty(ui, WIDGET_ELEVATION)) {
        fill(canvas, &pitch, COLOR_PANEL);
        border(canvas, &pitch, COLOR_BORDER);
        fill(canvas, &pitch_accent, COLOR_SUCCESS);
        canvas->draw_text(canvas->context, 174U, 90U, "PITCH",
                          COLOR_MUTED, COLOR_PANEL);
        format_angle(ui->elevation_degrees, text);
        canvas->draw_text(canvas->context, 250U, 98U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    rect = interactive_rect(ui, WIDGET_MOTOR_ENABLE);
    if (is_dirty(ui, WIDGET_MOTOR_ENABLE)) {
        draw_motor_button(ui, canvas, WIDGET_MOTOR_ENABLE, rect,
                          enable_label, COLOR_SUCCESS,
                          COLOR_PRIMARY_PRESSED);
    }
    rect = interactive_rect(ui, WIDGET_MOTOR_SCAN);
    if (is_dirty(ui, WIDGET_MOTOR_SCAN)) {
        draw_motor_button(ui, canvas, WIDGET_MOTOR_SCAN, rect,
                          scan_label, COLOR_PRIMARY,
                          COLOR_PRIMARY_PRESSED);
    }
    rect = interactive_rect(ui, WIDGET_MOTOR_STOP);
    if (is_dirty(ui, WIDGET_MOTOR_STOP)) {
        draw_motor_button(ui, canvas, WIDGET_MOTOR_STOP, rect,
                          stop_label, COLOR_WARNING,
                          COLOR_WARNING_PRESSED);
    }
    rect = interactive_rect(ui, WIDGET_MOTOR_DISABLE);
    if (is_dirty(ui, WIDGET_MOTOR_DISABLE)) {
        draw_motor_button(ui, canvas, WIDGET_MOTOR_DISABLE, rect,
                          disable_label, COLOR_DANGER,
                          COLOR_DANGER_PRESSED);
    }
    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &footer, COLOR_PANEL);
        text_center(canvas, &footer, feedback_text(ui->feedback),
                    feedback_color(ui->feedback), COLOR_PANEL);
    }
}

static void draw_theme_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
    const ui_rect_t screen = {0U, 0U, 320U, 240U};
    const ui_rect_t toolbar = {0U, 0U, 320U, 40U};
    const ui_rect_t toolbar_line = {0U, 38U, 320U, 2U};
    const ui_rect_t title = {96U, 0U, 128U, 40U};
    static const char *const titles[TARGET_UI_THEME_COUNT] = {
        "THEME 1 / 2", "THEME 2 / 2"
    };
    const ui_rect_t *rect;
    bool drawn = false;

    if (ui->full_redraw && (canvas->draw_theme != NULL)) {
        drawn = canvas->draw_theme(canvas->context, ui->theme_index);
    }
    if (ui->full_redraw && !drawn) {
        fill(canvas, &screen, COLOR_HEADER);
        text_center(canvas, &screen, "THEME NOT AVAILABLE",
                    COLOR_TEXT, COLOR_HEADER);
    }
    if (ui->full_redraw) {
        fill(canvas, &toolbar, COLOR_HEADER);
        fill(canvas, &toolbar_line, COLOR_THEME);
        text_center(canvas, &title, titles[ui->theme_index],
                    COLOR_TEXT, COLOR_HEADER);
    }
    if (is_dirty(ui, WIDGET_THEME_BACK)) {
        rect = interactive_rect(ui, WIDGET_THEME_BACK);
        draw_button(ui, canvas, WIDGET_THEME_BACK, rect, "< BACK",
                    COLOR_HEADER, COLOR_PRIMARY_PRESSED);
    }
    if (is_dirty(ui, WIDGET_THEME_NEXT)) {
        rect = interactive_rect(ui, WIDGET_THEME_NEXT);
        draw_button(ui, canvas, WIDGET_THEME_NEXT, rect, "NEXT >",
                    COLOR_THEME, COLOR_THEME_PRESSED);
    }
}

static const char *key_text(uint8_t id)
{
    static const char *const labels[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };

    if ((id >= WIDGET_KEY_1) && (id <= WIDGET_KEY_9)) {
        return labels[id - WIDGET_KEY_1];
    }
    if (id == WIDGET_KEY_0) {
        return "0";
    }
    if (id == WIDGET_KEY_CLEAR) {
        return "清";
    }
    return "退";
}

static uint16_t input_value(const target_ui_t *ui);

static void draw_input_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
    const ui_rect_t header = {0U, 0U, 320U, 28U};
    char signed_input[6];
    size_t index;

    if (ui->full_redraw) {
        const ui_rect_t screen = {0U, 0U, 320U, 240U};
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        text_center(canvas, &header,
                    (ui->page == PAGE_DISTANCE) ? "设置距离" :
                    ((ui->angle_axis == ANGLE_AXIS_YAW) ?
                     "SET YAW TARGET" : "SET PITCH TARGET"),
                    COLOR_TEXT, COLOR_HEADER);
    }

    for (index = 0U;
         index < (sizeof(s_key_widgets) / sizeof(s_key_widgets[0]));
         ++index) {
        const uint8_t id = s_key_widgets[index].id;
        const ui_rect_t *rect = &s_key_widgets[index].rect;
        const char *label;
        uint16_t normal = COLOR_PANEL;
        uint16_t pressed = COLOR_BORDER;
        uint16_t foreground = COLOR_TEXT;

        if (!is_dirty(ui, id)) {
            continue;
        }

        if (id == WIDGET_KEY_CONFIRM) {
            label = "确认";
            normal = COLOR_SUCCESS;
            pressed = COLOR_PRIMARY_PRESSED;
            foreground = COLOR_TEXT;
        } else if (id == WIDGET_KEY_CANCEL) {
            label = "取消";
            normal = COLOR_DANGER;
            pressed = COLOR_DANGER_PRESSED;
            foreground = COLOR_TEXT;
        } else if ((id == WIDGET_KEY_CLEAR) &&
                   (ui->page == PAGE_ANGLE)) {
            label = "+/-";
        } else {
            label = key_text(id);
        }

        fill(canvas, rect, ui->active_widget == id ? pressed : normal);
        border(canvas, rect, COLOR_BORDER);
        text_center(canvas, rect, label, foreground,
                    ui->active_widget == id ? pressed : normal);
    }

    if (is_dirty(ui, WIDGET_DISTANCE_DISPLAY)) {
        fill(canvas, &s_distance_display_rect, COLOR_PANEL);
        border(canvas, &s_distance_display_rect,
               ui->distance_input_valid ? COLOR_PRIMARY : COLOR_DANGER);
        if ((ui->page == PAGE_ANGLE) && ui->angle_negative &&
            (ui->distance_input_length > 0U) &&
            (input_value(ui) != 0U)) {
            signed_input[0] = '-';
            for (index = 0U; index <= ui->distance_input_length; ++index) {
                signed_input[index + 1U] = ui->distance_input[index];
            }
        } else {
            for (index = 0U; index <= ui->distance_input_length; ++index) {
                signed_input[index] = ui->distance_input[index];
            }
        }
        text_center(canvas, &s_distance_display_rect,
                    ui->distance_input_length == 0U ? "--" : signed_input,
                    COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_DISTANCE_RANGE)) {
        fill(canvas, &s_distance_range_rect, COLOR_BACKGROUND);
        text_center(canvas, &s_distance_range_rect,
                    (ui->page == PAGE_DISTANCE) ?
                    (ui->distance_input_valid ? "范围 50-100 cm" :
                                                "请输入 50-100") :
                    ((ui->angle_axis == ANGLE_AXIS_YAW) ?
                     "YAW -50..50 DEG" : "PITCH -50..50 DEG"),
                    ui->distance_input_valid ? COLOR_MUTED : COLOR_DANGER,
                    COLOR_BACKGROUND);
    }
}

static uint16_t input_value(const target_ui_t *ui)
{
    uint16_t value = 0U;
    uint8_t index;

    for (index = 0U; index < ui->distance_input_length; ++index) {
        value = (uint16_t)(value * 10U +
                           (uint16_t)(ui->distance_input[index] - '0'));
    }
    return value;
}

static void update_input_validity(target_ui_t *ui)
{
    const uint16_t value = input_value(ui);
    if (ui->page == PAGE_DISTANCE) {
        ui->distance_input_valid = (ui->distance_input_length > 0U) &&
                                   (value >= DISTANCE_INPUT_MIN_CM) &&
                                   (value <= DISTANCE_INPUT_MAX_CM);
    } else {
        const uint16_t limit = (ui->angle_axis == ANGLE_AXIS_YAW) ?
                               (uint16_t)YAW_SCAN_TARGET_LIMIT_DEG :
                               (uint16_t)PITCH_MANUAL_TARGET_LIMIT_DEG;
        ui->distance_input_valid = (ui->distance_input_length > 0U) &&
                                   (value <= limit);
    }
    invalidate(ui, WIDGET_DISTANCE_DISPLAY);
    invalidate(ui, WIDGET_DISTANCE_RANGE);
}

static void append_digit(target_ui_t *ui, uint8_t digit)
{
    if (!ui->distance_input_started) {
        ui->distance_input_length = 0U;
        ui->distance_input[0] = '\0';
        ui->distance_input_started = true;
    }

    if (ui->distance_input_length < 3U) {
        ui->distance_input[ui->distance_input_length] = (char)('0' + digit);
        ++ui->distance_input_length;
        ui->distance_input[ui->distance_input_length] = '\0';
    }
    update_input_validity(ui);
}

static void open_distance_page(target_ui_t *ui)
{
    format_uint16(ui->distance_cm, ui->distance_input);
    ui->distance_input_length = 0U;
    while (ui->distance_input[ui->distance_input_length] != '\0') {
        ++ui->distance_input_length;
    }
    ui->distance_input_started = false;
    ui->distance_input_valid = true;
    ui->page = PAGE_DISTANCE;
    ui->full_redraw = true;
}

static void open_angle_page(target_ui_t *ui, uint8_t axis)
{
    const int16_t target = (axis == ANGLE_AXIS_YAW) ?
                           ui->yaw_target_degrees :
                           ui->pitch_target_degrees;
    const uint16_t magnitude = (target < 0) ?
                               (uint16_t)(-(int32_t)target) :
                               (uint16_t)target;

    format_uint16(magnitude, ui->distance_input);
    ui->distance_input_length = 0U;
    while (ui->distance_input[ui->distance_input_length] != '\0') {
        ++ui->distance_input_length;
    }
    ui->distance_input_started = false;
    ui->distance_input_valid = true;
    ui->angle_axis = axis;
    ui->angle_negative = target < 0;
    ui->page = PAGE_ANGLE;
    ui->full_redraw = true;
}

static bool activate_widget(target_ui_t *ui,
                            uint8_t widget,
                            uint32_t now_ms,
                            app_command_t *command)
{
    uint16_t value;

    command->timestamp_ms = now_ms;
    command->distance_cm = ui->distance_cm;
    command->angle_degrees = 0;

    if (ui->page == PAGE_MAIN) {
        switch (widget) {
        case WIDGET_DISTANCE:
            open_distance_page(ui);
            return false;
        case WIDGET_FIRE_DISTANCE:
            command->type = APP_COMMAND_FIRE_DISTANCE;
            return true;
        case WIDGET_AZIMUTH:
            open_angle_page(ui, ANGLE_AXIS_YAW);
            return false;
        case WIDGET_ELEVATION:
            open_angle_page(ui, ANGLE_AXIS_PITCH);
            return false;
        case WIDGET_FIXED_TARGET:
            command->type = APP_COMMAND_FIXED_TARGET;
            return true;
        case WIDGET_AUTO_SEARCH:
            command->type = APP_COMMAND_AUTO_SEARCH;
            return true;
        case WIDGET_SWEEP:
            if (!scan_start_allowed(ui)) {
                target_ui_set_feedback(
                    ui, (GIMBAL_SCAN_ENABLED != 0U) ?
                        TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE :
                        TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE);
                return false;
            }
            command->type = APP_COMMAND_SWEEP;
            return true;
        case WIDGET_START:
            ui->page = PAGE_MOTOR;
            ui->full_redraw = true;
            return false;
        case WIDGET_CHARGE:
            ui->page = PAGE_CHARGE;
            ui->full_redraw = true;
            return false;
        case WIDGET_STOP:
            ui->page = PAGE_THEME;
            ui->full_redraw = true;
            return false;
        default:
            return false;
        }
    }

    if (ui->page == PAGE_MOTOR) {
        if (!motor_widget_enabled(ui, widget)) {
            if ((widget == WIDGET_MOTOR_SCAN) &&
                (GIMBAL_SCAN_ENABLED == 0U)) {
                target_ui_set_feedback(
                    ui, TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE);
            } else if (ui->runtime_status == TARGET_UI_ENABLING) {
                target_ui_set_feedback(ui, TARGET_UI_FEEDBACK_CAN_WAIT);
            } else if (ui->runtime_status == TARGET_UI_DISABLING) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_CAN_DISABLE_WAIT);
            } else if (ui->runtime_status == TARGET_UI_UNKNOWN) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_STATE_UNKNOWN);
            } else if (ui->runtime_status == TARGET_UI_RUNNING) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_YAW_SCANNING);
            } else if ((widget == WIDGET_MOTOR_SCAN) &&
                       ((ui->runtime_status == TARGET_UI_IDLE) ||
                        (ui->runtime_status == TARGET_UI_FAULT))) {
                target_ui_set_feedback(
                    ui, TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE);
            } else if (ui->runtime_status == TARGET_UI_IDLE) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_CAN_DISABLED);
            } else if (ui->runtime_status == TARGET_UI_COMPLETE) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_MOTOR_STOPPED);
            } else if (ui->runtime_status == TARGET_UI_READY) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_CAN_ENABLED);
            } else {
                target_ui_set_feedback(
                    ui, TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE);
            }
            return false;
        }
        switch (widget) {
        case WIDGET_MOTOR_BACK:
            ui->page = PAGE_MAIN;
            ui->full_redraw = true;
            return false;
        case WIDGET_MOTOR_ENABLE:
            command->type = APP_COMMAND_START;
            return true;
        case WIDGET_MOTOR_SCAN:
            command->type = APP_COMMAND_SWEEP;
            return true;
        case WIDGET_MOTOR_STOP:
            command->type = APP_COMMAND_STOP;
            return true;
        case WIDGET_MOTOR_DISABLE:
            command->type = APP_COMMAND_DISABLE;
            return true;
        default:
            return false;
        }
    }

    if (ui->page == PAGE_CANNON) {
#if CANNON_MANUAL_TEST_MODE != 0U
        switch (widget) {
        case WIDGET_ELEVATION:
            open_angle_page(ui, ANGLE_AXIS_PITCH);
            return false;
        case WIDGET_CHARGE_START:
            if (ui->charge_active) {
                target_ui_set_feedback(
                    ui, TARGET_UI_FEEDBACK_CHARGE_ACTIVE);
                return false;
            }
            command->type = APP_COMMAND_CHARGE_START;
            return true;
        case WIDGET_CHARGE_ABORT:
            command->type = APP_COMMAND_CHARGE_ABORT;
            return true;
        case WIDGET_CANNON_FIRE:
            if (ui->fire_active) {
                target_ui_set_feedback(ui,
                                       TARGET_UI_FEEDBACK_FIRE_ACTIVE);
                return false;
            }
            command->type = APP_COMMAND_FIRE_PULSE;
            return true;
        case WIDGET_CANNON_ABORT:
            command->type = APP_COMMAND_FIRE_ABORT;
            return true;
        default:
            return false;
        }
#else
        switch (widget) {
        case WIDGET_CANNON_BACK:
            ui->page = PAGE_MAIN;
            ui->full_redraw = true;
            command->type = APP_COMMAND_CHARGE_ABORT;
            return true;
        case WIDGET_CANNON_FIRE:
            if (ui->fire_active || ui->auto_aim_active) {
                target_ui_set_feedback(ui,
                    ui->fire_active ? TARGET_UI_FEEDBACK_FIRE_ACTIVE :
                    TARGET_UI_FEEDBACK_VISION_TARGET_INVALID);
                return false;
            }
            command->type = APP_COMMAND_FIRE_PULSE;
            return true;
        case WIDGET_CANNON_ABORT:
            command->type = APP_COMMAND_FIRE_ABORT;
            return true;
        default:
            return false;
        }
#endif
    }

    if (ui->page == PAGE_CHARGE) {
        switch (widget) {
        case WIDGET_CHARGE_BACK:
            ui->page = PAGE_MAIN;
            ui->full_redraw = true;
            command->type = APP_COMMAND_CHARGE_ABORT;
            return true;
        case WIDGET_CHARGE_START:
            if (ui->charge_active) {
                target_ui_set_feedback(
                    ui, TARGET_UI_FEEDBACK_CHARGE_ACTIVE);
                return false;
            }
            command->type = APP_COMMAND_CHARGE_START;
            return true;
        case WIDGET_CHARGE_ABORT:
            command->type = APP_COMMAND_CHARGE_ABORT;
            return true;
        default:
            return false;
        }
    }

    if (ui->page == PAGE_THEME) {
        if (widget == WIDGET_THEME_BACK) {
            ui->page = PAGE_MAIN;
            ui->full_redraw = true;
        } else if (widget == WIDGET_THEME_NEXT) {
            ui->theme_index = (uint8_t)((ui->theme_index + 1U) %
                                        TARGET_UI_THEME_COUNT);
            ui->full_redraw = true;
        }
        return false;
    }

    if ((widget >= WIDGET_KEY_1) && (widget <= WIDGET_KEY_9)) {
        append_digit(ui, (uint8_t)(widget - WIDGET_KEY_1 + 1U));
    } else if (widget == WIDGET_KEY_0) {
        append_digit(ui, 0U);
    } else if (widget == WIDGET_KEY_CLEAR) {
        if (ui->page == PAGE_ANGLE) {
            ui->angle_negative = !ui->angle_negative;
        } else {
            ui->distance_input_started = true;
            ui->distance_input_length = 0U;
            ui->distance_input[0] = '\0';
        }
        update_input_validity(ui);
    } else if (widget == WIDGET_KEY_BACK) {
        ui->distance_input_started = true;
        if (ui->distance_input_length > 0U) {
            --ui->distance_input_length;
            ui->distance_input[ui->distance_input_length] = '\0';
        }
        update_input_validity(ui);
    } else if (widget == WIDGET_KEY_CONFIRM) {
        value = input_value(ui);
        update_input_validity(ui);
        if (ui->distance_input_valid) {
            if (ui->page == PAGE_DISTANCE) {
                ui->distance_cm = value;
                command->type = APP_COMMAND_SET_DISTANCE;
                command->distance_cm = value;
            } else {
                const int16_t angle = ui->angle_negative && (value != 0U) ?
                                       -(int16_t)value : (int16_t)value;
                command->angle_degrees = angle;
                if (ui->angle_axis == ANGLE_AXIS_YAW) {
                    command->type = APP_COMMAND_SET_YAW_TARGET;
                } else {
                    command->type = APP_COMMAND_SET_PITCH_TARGET;
                }
            }
            ui->page = HOME_PAGE;
            ui->full_redraw = true;
            return true;
        }
    } else if (widget == WIDGET_KEY_CANCEL) {
        ui->page = HOME_PAGE;
        ui->full_redraw = true;
    }
    return false;
}

void target_ui_init(target_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    ui->page = HOME_PAGE;
    ui->active_widget = WIDGET_NONE;
    ui->distance_cm = 75U;
    ui->vision_distance_mm = 0U;
    ui->vision_distance_valid = false;
    ui->distance_input[0] = '\0';
    ui->distance_input_length = 0U;
    ui->distance_input_started = false;
    ui->distance_input_valid = true;
    ui->runtime_status = TARGET_UI_UNKNOWN;
    ui->feedback = TARGET_UI_FEEDBACK_STATE_UNKNOWN;
    ui->azimuth_degrees = 0;
    ui->elevation_degrees = 0;
    ui->yaw_target_degrees = 0;
    ui->pitch_target_degrees = 0;
    ui->angle_axis = ANGLE_AXIS_YAW;
    ui->angle_negative = false;
    ui->fire_active = false;
    ui->auto_aim_active = false;
    ui->charge_active = false;
    ui->cannon_charge_seconds = 0U;
    ui->cannon_fire_remaining_tenths = 0U;
    ui->cannon_state = TARGET_UI_CANNON_IDLE;
    ui->theme_index = 0U;
    ui->dirty_widgets = 0U;
    ui->full_redraw = true;
}

bool target_ui_step(target_ui_t *ui,
                    const ui_touch_t *touch,
                    uint32_t now_ms,
                    app_command_t *command)
{
    uint8_t released_widget;
    uint8_t active;

    if ((ui == NULL) || (touch == NULL) || (command == NULL)) {
        return false;
    }

    if (touch->just_pressed) {
        ui->active_widget = find_widget(ui, touch->x, touch->y);
        if (((ui->page == PAGE_MOTOR) &&
             (ui->active_widget == WIDGET_MOTOR_BACK)) ||
            ((ui->page == PAGE_THEME) &&
             (ui->active_widget == WIDGET_THEME_BACK))) {
            ui->page = PAGE_MAIN;
            ui->active_widget = WIDGET_NONE;
            ui->full_redraw = true;
            return false;
        }
        invalidate(ui, ui->active_widget);
    }

    if (!touch->just_released) {
        return false;
    }

    active = ui->active_widget;
    released_widget = find_widget(ui, touch->x, touch->y);
    ui->active_widget = WIDGET_NONE;
    invalidate(ui, active);
    if ((active == WIDGET_NONE) || (active != released_widget)) {
        return false;
    }

    return activate_widget(ui, active, now_ms, command);
}

void target_ui_render(target_ui_t *ui, const ui_canvas_t *canvas)
{
    if ((ui == NULL) || (canvas == NULL) ||
        (canvas->fill_rect == NULL) || (canvas->draw_text == NULL)) {
        return;
    }

    if (ui->page == PAGE_MAIN) {
        draw_main(ui, canvas);
    } else if ((ui->page == PAGE_DISTANCE) ||
               (ui->page == PAGE_ANGLE)) {
        draw_input_page(ui, canvas);
    } else if (ui->page == PAGE_MOTOR) {
        draw_motor_page(ui, canvas);
    } else if (ui->page == PAGE_CANNON) {
        draw_cannon_page(ui, canvas);
    } else if (ui->page == PAGE_CHARGE) {
        draw_charge_page(ui, canvas);
    } else {
        draw_theme_page(ui, canvas);
    }

    ui->dirty_widgets = 0U;
    ui->full_redraw = false;
}

void target_ui_set_feedback(target_ui_t *ui, target_ui_feedback_t feedback)
{
    if ((ui == NULL) || (ui->feedback == feedback)) {
        return;
    }

    ui->feedback = feedback;
    if ((ui->page == PAGE_MAIN) || (ui->page == PAGE_MOTOR) ||
        (ui->page == PAGE_CANNON) || (ui->page == PAGE_CHARGE)) {
        invalidate(ui, WIDGET_FOOTER);
    }
}

bool target_ui_set_distance(target_ui_t *ui, uint16_t distance_cm)
{
    if ((ui == NULL) || (distance_cm < DISTANCE_INPUT_MIN_CM) ||
        (distance_cm > DISTANCE_INPUT_MAX_CM)) {
        return false;
    }
    if (ui->distance_cm != distance_cm) {
        ui->distance_cm = distance_cm;
        invalidate(ui, WIDGET_DISTANCE);
    }
    return true;
}

void target_ui_set_vision_distance(target_ui_t *ui,
                                   uint16_t distance_mm,
                                   bool valid)
{
    if (ui == NULL) {
        return;
    }
    if ((ui->vision_distance_valid != valid) ||
        (valid && (ui->vision_distance_mm != distance_mm))) {
        ui->vision_distance_mm = distance_mm;
        ui->vision_distance_valid = valid;
        if (ui->page == PAGE_MAIN) {
            invalidate(ui, WIDGET_VISION_DISTANCE);
        }
    }
}

bool target_ui_set_targets(target_ui_t *ui,
                           int16_t yaw_target_degrees,
                           int16_t pitch_target_degrees)
{
    if ((ui == NULL) ||
        (yaw_target_degrees < -(int16_t)YAW_SCAN_TARGET_LIMIT_DEG) ||
        (yaw_target_degrees > (int16_t)YAW_SCAN_TARGET_LIMIT_DEG) ||
        (pitch_target_degrees < -(int16_t)PITCH_TARGET_LIMIT_DEG) ||
        (pitch_target_degrees > (int16_t)PITCH_TARGET_LIMIT_DEG)) {
        return false;
    }
    if (ui->yaw_target_degrees != yaw_target_degrees) {
        ui->yaw_target_degrees = yaw_target_degrees;
        invalidate(ui, WIDGET_AZIMUTH);
    }
    if (ui->pitch_target_degrees != pitch_target_degrees) {
        ui->pitch_target_degrees = pitch_target_degrees;
        invalidate(ui, WIDGET_ELEVATION);
    }
    return true;
}

void target_ui_set_fire_active(target_ui_t *ui, bool active)
{
    if ((ui == NULL) || (ui->fire_active == active)) {
        return;
    }

    ui->fire_active = active;
    if (ui->page == PAGE_CANNON) {
        invalidate(ui, WIDGET_STATUS);
        invalidate(ui, WIDGET_CANNON_FIRE);
    }
}

void target_ui_set_auto_aim_active(target_ui_t *ui, bool active)
{
    if ((ui == NULL) || (ui->auto_aim_active == active)) {
        return;
    }

    ui->auto_aim_active = active;
    if (ui->page == PAGE_CANNON) {
        invalidate(ui, WIDGET_STATUS);
        invalidate(ui, WIDGET_CANNON_FIRE);
    }
}

void target_ui_set_charge_active(target_ui_t *ui, bool active)
{
    if ((ui == NULL) || (ui->charge_active == active)) {
        return;
    }

    ui->charge_active = active;
    if ((ui->page == PAGE_CHARGE) ||
        ((CANNON_MANUAL_TEST_MODE != 0U) &&
         (ui->page == PAGE_CANNON))) {
        invalidate(ui, WIDGET_STATUS);
        invalidate(ui, WIDGET_CHARGE_START);
    }
}

void target_ui_set_cannon_timing(target_ui_t *ui,
                                 uint16_t charge_seconds,
                                 uint8_t fire_remaining_tenths,
                                 target_ui_cannon_state_t state)
{
    if (ui == NULL) {
        return;
    }
    if ((ui->cannon_charge_seconds == charge_seconds) &&
        (ui->cannon_fire_remaining_tenths ==
         fire_remaining_tenths) &&
        (ui->cannon_state == state)) {
        return;
    }

    ui->cannon_charge_seconds = charge_seconds;
    ui->cannon_fire_remaining_tenths = fire_remaining_tenths;
    ui->cannon_state = state;
    if (ui->page == PAGE_CANNON) {
        invalidate(ui, WIDGET_STATUS);
    }
}

void target_ui_open_fire_control(target_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    ui->active_widget = WIDGET_NONE;
    ui->page = PAGE_CANNON;
    ui->full_redraw = true;
}

void target_ui_set_runtime(target_ui_t *ui,
                           target_ui_runtime_status_t status,
                           int16_t azimuth_degrees,
                           int16_t elevation_degrees)
{
    if (ui == NULL) {
        return;
    }

    if (ui->runtime_status != status) {
        ui->runtime_status = status;
        invalidate(ui, WIDGET_STATUS);
        invalidate(ui, WIDGET_MOTOR_ENABLE);
        invalidate(ui, WIDGET_MOTOR_SCAN);
        invalidate(ui, WIDGET_MOTOR_STOP);
        invalidate(ui, WIDGET_MOTOR_DISABLE);
        invalidate(ui, WIDGET_SWEEP);
    }
    if (ui->azimuth_degrees != azimuth_degrees) {
        ui->azimuth_degrees = azimuth_degrees;
        invalidate(ui, WIDGET_AZIMUTH);
    }
    if (ui->elevation_degrees != elevation_degrees) {
        ui->elevation_degrees = elevation_degrees;
        invalidate(ui, WIDGET_ELEVATION);
    }
}
