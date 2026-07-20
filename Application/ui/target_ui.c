#include "target_ui.h"

#include <stddef.h>

#include "ui_font.h"

#define COLOR_BACKGROUND       0xEF5DU
#define COLOR_HEADER           0x0841U
#define COLOR_PANEL            0xFFFFU
#define COLOR_BORDER           0xBDF7U
#define COLOR_TEXT             0x18C3U
#define COLOR_MUTED            0x6B4DU
#define COLOR_PRIMARY          0x1C9FU
#define COLOR_PRIMARY_PRESSED  0x0B77U
#define COLOR_SUCCESS          0x2606U
#define COLOR_WARNING          0xFD20U
#define COLOR_DANGER           0xE8C4U
#define COLOR_DANGER_PRESSED   0xB800U

#define WIDGET_NONE            0xFFU
#define PAGE_MAIN              0U
#define PAGE_DISTANCE          1U

enum {
    WIDGET_DISTANCE = 0,
    WIDGET_FIRE_DISTANCE,
    WIDGET_STATUS,
    WIDGET_AZIMUTH,
    WIDGET_ELEVATION,
    WIDGET_FIXED_TARGET,
    WIDGET_AUTO_SEARCH,
    WIDGET_SWEEP,
    WIDGET_STOP,
    WIDGET_FOOTER,
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
    WIDGET_DISTANCE_RANGE
};

typedef struct {
    uint8_t id;
    ui_rect_t rect;
} widget_t;

static const widget_t s_main_widgets[] = {
    {WIDGET_DISTANCE,      {8U,   34U, 160U, 42U}},
    {WIDGET_FIRE_DISTANCE, {176U, 34U, 136U, 42U}},
    {WIDGET_FIXED_TARGET,  {8U,  126U,  96U, 48U}},
    {WIDGET_AUTO_SEARCH,   {112U,126U,  96U, 48U}},
    {WIDGET_SWEEP,         {216U,126U,  96U, 48U}},
    {WIDGET_STOP,          {224U,182U,  88U, 50U}}
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

static const ui_rect_t s_status_rect = {8U, 82U, 96U, 38U};
static const ui_rect_t s_azimuth_rect = {112U, 82U, 96U, 38U};
static const ui_rect_t s_elevation_rect = {216U, 82U, 96U, 38U};
static const ui_rect_t s_footer_rect = {8U, 182U, 208U, 50U};
static const ui_rect_t s_distance_display_rect = {176U, 36U, 136U, 48U};
static const ui_rect_t s_distance_range_rect = {176U, 92U, 136U, 28U};

static void invalidate(target_ui_t *ui, uint8_t widget)
{
    if (widget < 32U) {
        ui->dirty_widgets |= 1UL << widget;
    }
}

static bool is_dirty(const target_ui_t *ui, uint8_t widget)
{
    return ui->full_redraw ||
           ((widget < 32U) && ((ui->dirty_widgets & (1UL << widget)) != 0U));
}

static bool point_in_rect(uint16_t x, uint16_t y, const ui_rect_t *rect)
{
    return (x >= rect->x) && (y >= rect->y) &&
           (x < ((uint32_t)rect->x + rect->width)) &&
           (y < ((uint32_t)rect->y + rect->height));
}

static uint8_t find_widget(const target_ui_t *ui, uint16_t x, uint16_t y)
{
    const widget_t *widgets;
    size_t count;
    size_t index;

    if (ui->page == PAGE_MAIN) {
        widgets = s_main_widgets;
        count = sizeof(s_main_widgets) / sizeof(s_main_widgets[0]);
    } else {
        widgets = s_key_widgets;
        count = sizeof(s_key_widgets) / sizeof(s_key_widgets[0]);
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
    } else {
        widgets = s_key_widgets;
        count = sizeof(s_key_widgets) / sizeof(s_key_widgets[0]);
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
    border(canvas, rect, background);
    text_center(canvas, rect, text, COLOR_PANEL, background);
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

static const char *runtime_text(target_ui_runtime_status_t status)
{
    switch (status) {
    case TARGET_UI_RUNNING:
        return "运行";
    case TARGET_UI_COMPLETE:
        return "完成";
    case TARGET_UI_FAULT:
        return "故障";
    case TARGET_UI_IDLE:
    default:
        return "待机";
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
    case TARGET_UI_FEEDBACK_READY:
    default:
        return "触摸屏已就绪";
    }
}

static void draw_main(target_ui_t *ui, const ui_canvas_t *canvas)
{
    char text[12];
    const ui_rect_t header = {0U, 0U, 320U, 28U};
    const ui_rect_t *rect;

    if (ui->full_redraw) {
        const ui_rect_t screen = {0U, 0U, 320U, 240U};
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        text_center(canvas, &header, "电磁炮击靶系统",
                    COLOR_PANEL, COLOR_HEADER);
    }

    if (is_dirty(ui, WIDGET_DISTANCE)) {
        rect = interactive_rect(ui, WIDGET_DISTANCE);
        fill(canvas, rect, COLOR_PANEL);
        border(canvas, rect, ui->active_widget == WIDGET_DISTANCE ?
               COLOR_PRIMARY : COLOR_BORDER);
        canvas->draw_text(canvas->context, 16U, 38U, "距离",
                          COLOR_MUTED, COLOR_PANEL);
        format_distance(ui->distance_cm, text);
        canvas->draw_text(canvas->context, 16U, 56U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_FIRE_DISTANCE)) {
        rect = interactive_rect(ui, WIDGET_FIRE_DISTANCE);
        draw_button(ui, canvas, WIDGET_FIRE_DISTANCE, rect, "距离发射",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }

    if (is_dirty(ui, WIDGET_STATUS)) {
        fill(canvas, &s_status_rect, COLOR_PANEL);
        border(canvas, &s_status_rect, COLOR_BORDER);
        canvas->draw_text(canvas->context, 14U, 84U, "状态",
                          COLOR_MUTED, COLOR_PANEL);
        canvas->draw_text(canvas->context, 14U, 102U,
                          runtime_text(ui->runtime_status),
                          ui->runtime_status == TARGET_UI_FAULT ?
                          COLOR_DANGER : COLOR_SUCCESS,
                          COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_AZIMUTH)) {
        fill(canvas, &s_azimuth_rect, COLOR_PANEL);
        border(canvas, &s_azimuth_rect, COLOR_BORDER);
        canvas->draw_text(canvas->context, 118U, 84U, "方位",
                          COLOR_MUTED, COLOR_PANEL);
        format_angle(ui->azimuth_degrees, text);
        canvas->draw_text(canvas->context, 118U, 102U, text,
                          COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_ELEVATION)) {
        fill(canvas, &s_elevation_rect, COLOR_PANEL);
        border(canvas, &s_elevation_rect, COLOR_BORDER);
        canvas->draw_text(canvas->context, 222U, 84U, "仰角",
                          COLOR_MUTED, COLOR_PANEL);
        format_angle(ui->elevation_degrees, text);
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
        draw_button(ui, canvas, WIDGET_SWEEP, rect, "往复扫描",
                    COLOR_PRIMARY, COLOR_PRIMARY_PRESSED);
    }

    if (is_dirty(ui, WIDGET_FOOTER)) {
        fill(canvas, &s_footer_rect, COLOR_PANEL);
        border(canvas, &s_footer_rect, COLOR_BORDER);
        text_center(canvas, &s_footer_rect, feedback_text(ui->feedback),
                    ui->feedback == TARGET_UI_FEEDBACK_ERROR ?
                    COLOR_DANGER : COLOR_TEXT,
                    COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_STOP)) {
        rect = interactive_rect(ui, WIDGET_STOP);
        draw_button(ui, canvas, WIDGET_STOP, rect, "停止",
                    COLOR_DANGER, COLOR_DANGER_PRESSED);
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

static void draw_distance_page(target_ui_t *ui, const ui_canvas_t *canvas)
{
    const ui_rect_t header = {0U, 0U, 320U, 28U};
    size_t index;

    if (ui->full_redraw) {
        const ui_rect_t screen = {0U, 0U, 320U, 240U};
        fill(canvas, &screen, COLOR_BACKGROUND);
        fill(canvas, &header, COLOR_HEADER);
        text_center(canvas, &header, "设置距离", COLOR_PANEL, COLOR_HEADER);
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
            foreground = COLOR_PANEL;
        } else if (id == WIDGET_KEY_CANCEL) {
            label = "取消";
            normal = COLOR_DANGER;
            pressed = COLOR_DANGER_PRESSED;
            foreground = COLOR_PANEL;
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
        text_center(canvas, &s_distance_display_rect,
                    ui->distance_input_length == 0U ? "--" : ui->distance_input,
                    COLOR_TEXT, COLOR_PANEL);
    }

    if (is_dirty(ui, WIDGET_DISTANCE_RANGE)) {
        fill(canvas, &s_distance_range_rect, COLOR_BACKGROUND);
        text_center(canvas, &s_distance_range_rect,
                    ui->distance_input_valid ? "范围 50-100 cm" :
                                               "请输入 50-100",
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
    ui->distance_input_valid = (ui->distance_input_length > 0U) &&
                               (value >= 50U) && (value <= 100U);
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

static bool activate_widget(target_ui_t *ui,
                            uint8_t widget,
                            uint32_t now_ms,
                            app_command_t *command)
{
    uint16_t value;

    command->timestamp_ms = now_ms;
    command->distance_cm = ui->distance_cm;

    if (ui->page == PAGE_MAIN) {
        switch (widget) {
        case WIDGET_DISTANCE:
            format_uint16(ui->distance_cm, ui->distance_input);
            ui->distance_input_length = 0U;
            while (ui->distance_input[ui->distance_input_length] != '\0') {
                ++ui->distance_input_length;
            }
            ui->distance_input_started = false;
            ui->distance_input_valid = true;
            ui->page = PAGE_DISTANCE;
            ui->full_redraw = true;
            return false;
        case WIDGET_FIRE_DISTANCE:
            command->type = APP_COMMAND_FIRE_DISTANCE;
            return true;
        case WIDGET_FIXED_TARGET:
            command->type = APP_COMMAND_FIXED_TARGET;
            return true;
        case WIDGET_AUTO_SEARCH:
            command->type = APP_COMMAND_AUTO_SEARCH;
            return true;
        case WIDGET_SWEEP:
            command->type = APP_COMMAND_SWEEP;
            return true;
        case WIDGET_STOP:
            command->type = APP_COMMAND_STOP;
            return true;
        default:
            return false;
        }
    }

    if ((widget >= WIDGET_KEY_1) && (widget <= WIDGET_KEY_9)) {
        append_digit(ui, (uint8_t)(widget - WIDGET_KEY_1 + 1U));
    } else if (widget == WIDGET_KEY_0) {
        append_digit(ui, 0U);
    } else if (widget == WIDGET_KEY_CLEAR) {
        ui->distance_input_started = true;
        ui->distance_input_length = 0U;
        ui->distance_input[0] = '\0';
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
            ui->distance_cm = value;
            command->type = APP_COMMAND_SET_DISTANCE;
            command->distance_cm = value;
            ui->page = PAGE_MAIN;
            ui->full_redraw = true;
            return true;
        }
    } else if (widget == WIDGET_KEY_CANCEL) {
        ui->page = PAGE_MAIN;
        ui->full_redraw = true;
    }
    return false;
}

void target_ui_init(target_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    ui->page = PAGE_MAIN;
    ui->active_widget = WIDGET_NONE;
    ui->distance_cm = 75U;
    ui->distance_input[0] = '\0';
    ui->distance_input_length = 0U;
    ui->distance_input_started = false;
    ui->distance_input_valid = true;
    ui->runtime_status = TARGET_UI_IDLE;
    ui->feedback = TARGET_UI_FEEDBACK_READY;
    ui->azimuth_degrees = 0;
    ui->elevation_degrees = 0;
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
    } else {
        draw_distance_page(ui, canvas);
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
    if (ui->page == PAGE_MAIN) {
        invalidate(ui, WIDGET_FOOTER);
    }
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
