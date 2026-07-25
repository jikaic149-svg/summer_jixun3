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
    bool (*draw_theme)(void *context, uint8_t theme_index);
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
    TARGET_UI_READY,
    TARGET_UI_FAULT,
    TARGET_UI_UNKNOWN,
    TARGET_UI_ENABLING,
    TARGET_UI_DISABLING
} target_ui_runtime_status_t;

typedef enum {
    TARGET_UI_FEEDBACK_READY = 0,
    TARGET_UI_FEEDBACK_COMMAND_SENT,
    TARGET_UI_FEEDBACK_CONTROLLER_MISSING,
    TARGET_UI_FEEDBACK_QUEUE_FULL,
    TARGET_UI_FEEDBACK_ERROR,
    TARGET_UI_FEEDBACK_CAN_WAIT,
    TARGET_UI_FEEDBACK_CAN_DISABLE_WAIT,
    TARGET_UI_FEEDBACK_YAW_SCANNING,
    TARGET_UI_FEEDBACK_CAN_ENABLED,
    TARGET_UI_FEEDBACK_CAN_DISABLED,
    TARGET_UI_FEEDBACK_YAW_SCAN_ERROR,
    TARGET_UI_FEEDBACK_CAN_YAW_ERROR,
    TARGET_UI_FEEDBACK_CAN_PITCH_ERROR,
    TARGET_UI_FEEDBACK_CAN_ERROR,
    TARGET_UI_FEEDBACK_MOTOR_STOPPED,
    TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE,
    TARGET_UI_FEEDBACK_STATE_UNKNOWN,
    TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE,
    TARGET_UI_FEEDBACK_VISION_DISTANCE_INVALID,
    TARGET_UI_FEEDBACK_VISION_TARGET_INVALID,
    TARGET_UI_FEEDBACK_FIRE_ACTIVE,
    TARGET_UI_FEEDBACK_FIRE_COMPLETE,
    TARGET_UI_FEEDBACK_CHARGE_ACTIVE,
    TARGET_UI_FEEDBACK_CHARGE_COMPLETE
} target_ui_feedback_t;

typedef enum {
    TARGET_UI_CANNON_IDLE = 0,
    TARGET_UI_CANNON_CHARGING,
    TARGET_UI_CANNON_PAUSED,
    TARGET_UI_CANNON_WAIT_AIM,
    TARGET_UI_CANNON_FIRING,
    TARGET_UI_CANNON_READY
} target_ui_cannon_state_t;

typedef struct {
    uint8_t page;
    uint8_t active_widget;
    uint16_t distance_cm;
    uint16_t vision_distance_mm;
    bool vision_distance_valid;
    char distance_input[4];
    uint8_t distance_input_length;
    bool distance_input_started;
    bool distance_input_valid;
    target_ui_runtime_status_t runtime_status;
    target_ui_feedback_t feedback;
    int16_t azimuth_degrees;
    int16_t elevation_degrees;
    int16_t yaw_target_degrees;
    int16_t pitch_target_degrees;
    uint8_t angle_axis;
    bool angle_negative;
    bool fire_active;
    bool auto_aim_active;
    bool charge_active;
    uint16_t cannon_charge_seconds;
    uint8_t cannon_fire_remaining_tenths;
    target_ui_cannon_state_t cannon_state;
    uint8_t theme_index;
    uint64_t dirty_widgets;
    bool full_redraw;
} target_ui_t;

void target_ui_init(target_ui_t *ui);
bool target_ui_step(target_ui_t *ui,
                    const ui_touch_t *touch,
                    uint32_t now_ms,
                    app_command_t *command);
void target_ui_render(target_ui_t *ui, const ui_canvas_t *canvas);
void target_ui_set_feedback(target_ui_t *ui, target_ui_feedback_t feedback);
bool target_ui_set_distance(target_ui_t *ui, uint16_t distance_cm);
void target_ui_set_vision_distance(target_ui_t *ui,
                                   uint16_t distance_mm,
                                   bool valid);
bool target_ui_set_targets(target_ui_t *ui,
                           int16_t yaw_target_degrees,
                           int16_t pitch_target_degrees);
void target_ui_open_fire_control(target_ui_t *ui);
void target_ui_set_fire_active(target_ui_t *ui, bool active);
void target_ui_set_auto_aim_active(target_ui_t *ui, bool active);
void target_ui_set_charge_active(target_ui_t *ui, bool active);
void target_ui_set_cannon_timing(target_ui_t *ui,
                                 uint16_t charge_seconds,
                                 uint8_t fire_remaining_tenths,
                                 target_ui_cannon_state_t state);
void target_ui_set_runtime(target_ui_t *ui,
                           target_ui_runtime_status_t status,
                           int16_t azimuth_degrees,
                           int16_t elevation_degrees);

#endif
