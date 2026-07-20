#ifndef APPLICATION_TARGET_UI_H
#define APPLICATION_TARGET_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "app_command_bus.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} ui_rect_t;

typedef struct {
    void *context;
    void (*fill_rect)(void *context, const ui_rect_t *rect, uint16_t color);
    void (*draw_text)(void *context, uint16_t x, uint16_t y,
                      const char *text, uint16_t foreground,
                      uint16_t background);
} ui_canvas_t;

typedef struct {
    bool pressed;
    bool just_pressed;
    bool just_released;
    uint16_t x;
    uint16_t y;
} ui_touch_t;

typedef enum {
    TARGET_UI_IDLE = 0,
    TARGET_UI_RUNNING,
    TARGET_UI_COMPLETE,
    TARGET_UI_FAULT
} target_ui_runtime_status_t;

typedef enum {
    TARGET_UI_FEEDBACK_READY = 0,
    TARGET_UI_FEEDBACK_COMMAND_SENT,
    TARGET_UI_FEEDBACK_CONTROLLER_MISSING,
    TARGET_UI_FEEDBACK_QUEUE_FULL,
    TARGET_UI_FEEDBACK_ERROR
} target_ui_feedback_t;

typedef struct {
    uint8_t page;
    uint8_t active_widget;
    uint16_t distance_cm;
    char distance_input[4];
    uint8_t distance_input_length;
    bool distance_input_started;
    bool distance_input_valid;
    target_ui_runtime_status_t runtime_status;
    target_ui_feedback_t feedback;
    int16_t azimuth_degrees;
    int16_t elevation_degrees;
    uint32_t dirty_widgets;
    bool full_redraw;
} target_ui_t;

void target_ui_init(target_ui_t *ui);
bool target_ui_step(target_ui_t *ui,
                    const ui_touch_t *touch,
                    uint32_t now_ms,
                    app_command_t *command);
void target_ui_render(target_ui_t *ui, const ui_canvas_t *canvas);
void target_ui_set_feedback(target_ui_t *ui, target_ui_feedback_t feedback);
void target_ui_set_runtime(target_ui_t *ui,
                           target_ui_runtime_status_t status,
                           int16_t azimuth_degrees,
                           int16_t elevation_degrees);

#endif
