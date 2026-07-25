#include "lcd_demo_app.h"

#include <math.h>
#include <stddef.h>

#include "app_command_bus.h"
#include "bsp_board_f407ve.h"
#include "bsp_can1.h"
#include "bsp_cannon_relay.h"
#include "bsp_lcd_fsmc.h"
#include "bsp_uart_bridge.h"
#include "bsp_xpt2046_io.h"
#include "cannon_aim_model.h"
#include "control_port.h"
#include "esp8266.h"
#include "gimbal_link_protocol.h"
#include "ili9341.h"
#include "lcd_demo_config.h"
#include "pitch_hold_controller.h"
#include "qd4310.h"
#include "tft_theme_assets.h"
#include "ui_font.h"
#include "xpt2046.h"
#include "yaw_scan_controller.h"

static ili9341_t s_display;
static xpt2046_t s_touch;
static target_ui_t s_ui;
static app_command_bus_t s_command_bus;
static uint16_t s_glyph_pixels[16U * 16U];
static bool s_hardware_started;
static bool s_ui_started;
static bool s_clock_failed;
static qd4310_enable_test_t s_qd4310_yaw_enable;
static qd4310_enable_test_t s_qd4310_pitch_enable;
static qd4310_enable_state_t s_reported_yaw_state;
static qd4310_enable_state_t s_reported_pitch_state;
static bool s_reported_yaw_expected_enabled;
static bool s_reported_pitch_expected_enabled;
static bool s_reported_scan_active;
static bool s_reported_scan_fault;
static bool s_reported_user_stopped;
static yaw_scan_controller_t s_yaw_scan;
static pitch_hold_controller_t s_pitch_hold;
static bool s_yaw_scan_start_pending;
static bool s_enable_zero_pending;
static bool s_enable_zero_valid;
static bool s_user_stopped;
static bool s_yaw_scan_fault;
static bool s_yaw_direction_check_pending;
static uint8_t s_yaw_scan_fault_code;
static uint16_t s_yaw_zero_raw;
static uint16_t s_pitch_zero_raw;
static uint32_t s_yaw_enable_zero_response_baseline;
static uint32_t s_pitch_enable_zero_response_baseline;
static uint32_t s_yaw_display_response_count;
static uint32_t s_pitch_display_response_count;
static uint32_t s_angle_monitor_next_ms;
static uint32_t s_yaw_scan_next_control_ms;
static uint32_t s_yaw_feedback_deadline_ms;
static uint32_t s_yaw_seen_response_count;
static uint32_t s_pitch_feedback_deadline_ms;
static uint32_t s_pitch_seen_response_count;
static uint32_t s_pitch_control_next_ms;
static int16_t s_yaw_relative_degrees;
static int16_t s_pitch_relative_degrees;
static float s_yaw_scan_target_degrees;
static float s_yaw_scan_current_a;
static float s_pitch_target_degrees;
static float s_pitch_hold_current_a;
static float s_yaw_scan_min_degrees;
static float s_yaw_scan_max_degrees;
static float s_yaw_scan_peak_current_a;
static uint32_t s_yaw_control_command_count;
static esp8266_t s_esp8266;
static gimbal_link_decoder_t s_gimbal_link_decoder;
static uint16_t s_gimbal_telemetry_sequence;
static uint32_t s_gimbal_telemetry_deadline_ms;
static bool s_gimbal_debug_pending;
static gimbal_link_vision_t s_latest_vision;
static uint32_t s_vision_last_rx_ms;
static bool s_vision_online;
static bool s_fire_relay_active;
static uint32_t s_fire_relay_deadline_ms;
static bool s_charge_relay_active;
static bool s_charge_cycle_started;
static bool s_charge_cycle_complete;
static uint32_t s_charge_elapsed_ms;
static uint32_t s_charge_last_step_ms;
static uint32_t s_charge_pause_started_ms;
static bool s_task_auto_fire_pending;

typedef enum {
    AUTO_AIM_IDLE = 0,
    AUTO_AIM_SEARCH,
    AUTO_AIM_CALIBRATE_A,
    AUTO_AIM_CALIBRATE_B,
    AUTO_AIM_ALIGN,
    AUTO_AIM_SETTLE,
    AUTO_AIM_FIRING
} auto_aim_state_t;

static auto_aim_state_t s_auto_aim_state;
static uint32_t s_auto_aim_deadline_ms;
static uint32_t s_auto_aim_target_seen_ms;
static uint32_t s_auto_aim_settle_started_ms;
static uint16_t s_auto_aim_vision_sequence;
static uint16_t s_auto_aim_settle_sequence;
static int8_t s_auto_aim_search_direction;
static bool s_auto_aim_coarse_locked;
static uint8_t s_auto_aim_release_count;
static int8_t s_auto_aim_release_direction;
static bool s_auto_aim_motion_arrived;
static uint32_t s_auto_aim_motion_last_ms;
static float s_auto_aim_coarse_yaw_degrees;
static float s_auto_aim_calibration_a_degrees;
static float s_auto_aim_calibration_b_degrees;
static float s_auto_aim_calibration_x_a;
static float s_auto_aim_calibration_x_b;
static float s_auto_aim_slope_px_per_degree;

static void abort_fire_pulse(void);
static void abort_charge(void);
static void auto_aim_cancel(void);

#define GIMBAL_ACK_QUEUE_CAPACITY  8U

typedef struct {
    uint16_t sequence;
    gimbal_link_command_type_t command;
    gimbal_link_ack_result_t result;
} gimbal_ack_t;

static gimbal_ack_t s_gimbal_ack_queue[GIMBAL_ACK_QUEUE_CAPACITY];
static uint8_t s_gimbal_ack_read;
static uint8_t s_gimbal_ack_write;
static uint8_t s_gimbal_ack_count;

_Static_assert((int)APP_COMMAND_SET_DISTANCE ==
               (int)GIMBAL_LINK_COMMAND_SET_DISTANCE,
               "gimbal command enum order changed");
_Static_assert((int)APP_COMMAND_STOP == (int)GIMBAL_LINK_COMMAND_STOP,
               "gimbal command enum order changed");
_Static_assert((int)APP_COMMAND_DISABLE ==
               (int)GIMBAL_LINK_COMMAND_DISABLE,
               "gimbal command enum order changed");
_Static_assert((int)APP_COMMAND_SET_PITCH_TARGET ==
               (int)GIMBAL_LINK_COMMAND_SET_PITCH_TARGET,
               "gimbal command enum order changed");

typedef struct {
    uint32_t timestamp_ms;
    uint32_t response_count;
    float angle_degrees;
    float feedback_current_a;
    float raw_speed_rpm;
    float control_speed_rpm;
    float target_degrees;
    float speed_command_dps;
    float command_current_a;
    float integral_current_a;
    uint32_t flags;
} yaw_scan_trace_sample_t;

_Static_assert(sizeof(yaw_scan_trace_sample_t) == 44U,
               "yaw trace binary layout changed");

volatile yaw_scan_trace_sample_t
    g_yaw_scan_trace[YAW_SCAN_TRACE_CAPACITY];
volatile uint32_t g_yaw_scan_trace_write_count;
volatile uint32_t g_yaw_scan_trace_valid_count;

static bool qd4310_state_failed(qd4310_enable_state_t state)
{
    return (state == QD4310_ENABLE_REJECTED) ||
           (state == QD4310_ENABLE_TIMEOUT) ||
           (state == QD4310_ENABLE_CAN_ERROR);
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static float limit_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool auto_aim_active(void)
{
    return s_auto_aim_state != AUTO_AIM_IDLE;
}

static bool both_axes_enabled(void)
{
    return (s_qd4310_yaw_enable.state == QD4310_ENABLE_CONFIRMED) &&
           (s_qd4310_pitch_enable.state == QD4310_ENABLE_CONFIRMED) &&
           s_qd4310_yaw_enable.expected_enabled &&
           s_qd4310_pitch_enable.expected_enabled;
}

static float yaw_feedback_angle_degrees(void)
{
    const int16_t raw_delta =
        (int16_t)(s_qd4310_yaw_enable.last_angle_raw - s_yaw_zero_raw);
    return (float)raw_delta * 360.0f / 65536.0f;
}

static float yaw_feedback_speed_rpm(void)
{
    return (float)s_qd4310_yaw_enable.last_speed_raw *
           1000.0f / 32767.0f;
}

static float yaw_feedback_current_a(void)
{
    return (float)s_qd4310_yaw_enable.last_current_raw *
           10.0f / 32767.0f;
}

static float pitch_feedback_angle_degrees(void)
{
    const int16_t raw_delta =
        (int16_t)(s_qd4310_pitch_enable.last_angle_raw - s_pitch_zero_raw);
    return (float)raw_delta * 360.0f / 65536.0f;
}

static float pitch_feedback_speed_rpm(void)
{
    return (float)s_qd4310_pitch_enable.last_speed_raw *
           1000.0f / 32767.0f;
}

static float pitch_feedback_current_a(void)
{
    return (float)s_qd4310_pitch_enable.last_current_raw *
           10.0f / 32767.0f;
}

static void record_yaw_scan_trace(uint32_t now_ms,
                                  float angle_degrees,
                                  float speed_rpm)
{
    const uint32_t index = g_yaw_scan_trace_write_count %
                           YAW_SCAN_TRACE_CAPACITY;
    volatile yaw_scan_trace_sample_t *sample = &g_yaw_scan_trace[index];

    sample->timestamp_ms = now_ms;
    sample->response_count = s_qd4310_yaw_enable.response_count;
    sample->angle_degrees = angle_degrees;
    sample->feedback_current_a = yaw_feedback_current_a();
    sample->raw_speed_rpm = speed_rpm;
    sample->control_speed_rpm = speed_rpm;
    sample->target_degrees = s_yaw_scan_target_degrees;
    sample->speed_command_dps = s_yaw_scan.endpoint_holding ? 0.0f :
        (float)s_yaw_scan.direction * YAW_SCAN_TARGET_SPEED_DPS;
    sample->command_current_a = s_yaw_scan_current_a;
    sample->integral_current_a = s_yaw_scan.integral_current_a;
    sample->flags = (s_yaw_scan.endpoint_holding ? 1UL : 0UL) |
                    ((s_yaw_scan.direction > 0) ? 2UL : 0UL);
    ++g_yaw_scan_trace_write_count;
    if (g_yaw_scan_trace_valid_count < YAW_SCAN_TRACE_CAPACITY) {
        ++g_yaw_scan_trace_valid_count;
    }
}

static int16_t rounded_degrees(float degrees)
{
    return (int16_t)((degrees >= 0.0f) ? degrees + 0.5f : degrees - 0.5f);
}

static int32_t rounded_milli(float value)
{
    const float scaled = value * 1000.0f;
    return (int32_t)((scaled >= 0.0f) ? scaled + 0.5f : scaled - 0.5f);
}

static const char *runtime_name(target_ui_runtime_status_t status)
{
    switch (status) {
    case TARGET_UI_ENABLING:
        return "ENABLING";
    case TARGET_UI_DISABLING:
        return "DISABLING";
    case TARGET_UI_RUNNING:
        return "RUNNING";
    case TARGET_UI_COMPLETE:
        return "COMPLETE";
    case TARGET_UI_READY:
        return "READY";
    case TARGET_UI_FAULT:
        return "FAULT";
    case TARGET_UI_UNKNOWN:
        return "UNKNOWN";
    case TARGET_UI_IDLE:
    default:
        return "IDLE";
    }
}

static const char *feedback_name(target_ui_feedback_t feedback)
{
    static const char *const names[] = {
        "READY", "COMMAND_SENT", "CONTROLLER_MISSING", "QUEUE_FULL",
        "UI_ERROR", "CAN_WAIT", "CAN_DISABLE_WAIT", "GIMBAL_RUNNING",
        "CAN_ENABLED", "CAN_DISABLED", "GIMBAL_CONTROL_ERROR",
        "CAN_YAW_ERROR", "CAN_PITCH_ERROR", "CAN_ERROR",
        "MOTOR_STOPPED", "SCAN_REQUIRES_ENABLE", "STATE_UNKNOWN",
        "ACTION_UNAVAILABLE", "VISION_DISTANCE_INVALID",
        "VISION_TARGET_INVALID", "FIRE_ACTIVE", "FIRE_COMPLETE"
    };
    return ((unsigned int)feedback <
            (sizeof(names) / sizeof(names[0]))) ? names[feedback] :
            "UNKNOWN";
}

static const char *qd4310_state_name(qd4310_enable_state_t state)
{
    static const char *const names[] = {
        "IDLE", "WAITING", "CONFIRMED", "REJECTED", "TIMEOUT",
        "CAN_ERROR"
    };
    return ((unsigned int)state <
            (sizeof(names) / sizeof(names[0]))) ? names[state] :
            "UNKNOWN";
}

static bool queue_gimbal_ack(uint16_t sequence,
                             gimbal_link_command_type_t command,
                             gimbal_link_ack_result_t result)
{
    gimbal_ack_t *ack;

    if (s_gimbal_ack_count >= GIMBAL_ACK_QUEUE_CAPACITY) {
        return false;
    }
    ack = &s_gimbal_ack_queue[s_gimbal_ack_write];
    ack->sequence = sequence;
    ack->command = command;
    ack->result = result;
    s_gimbal_ack_write = (uint8_t)((s_gimbal_ack_write + 1U) %
                                  GIMBAL_ACK_QUEUE_CAPACITY);
    ++s_gimbal_ack_count;
    return true;
}

static void accept_gimbal_command(const gimbal_link_command_t *link_command,
                                  uint32_t now_ms)
{
    app_command_t command;
    app_publish_result_t publish_result;
    gimbal_link_ack_result_t ack_result = GIMBAL_LINK_ACK_INVALID;

    if ((link_command == NULL) ||
        ((unsigned int)link_command->type >
         GIMBAL_LINK_COMMAND_SET_PITCH_TARGET)) {
        return;
    }
    if ((GIMBAL_SCAN_ENABLED == 0U) &&
        (link_command->type == GIMBAL_LINK_COMMAND_SWEEP)) {
        (void)queue_gimbal_ack(link_command->sequence, link_command->type,
                               GIMBAL_LINK_ACK_INVALID);
        return;
    }
    command.type = (app_command_type_t)link_command->type;
    command.timestamp_ms = now_ms;
    command.angle_degrees = 0;
    command.distance_cm = 0U;
    if (link_command->type == GIMBAL_LINK_COMMAND_SET_DISTANCE) {
        command.distance_cm = (uint16_t)link_command->value;
    } else if (s_ui_started &&
               (link_command->type !=
                GIMBAL_LINK_COMMAND_SET_YAW_TARGET) &&
               (link_command->type !=
                GIMBAL_LINK_COMMAND_SET_PITCH_TARGET)) {
        command.distance_cm = s_ui.distance_cm;
    } else if ((link_command->type !=
                GIMBAL_LINK_COMMAND_SET_YAW_TARGET) &&
               (link_command->type !=
                GIMBAL_LINK_COMMAND_SET_PITCH_TARGET) &&
               (link_command->value >= 0)) {
        command.distance_cm = (uint16_t)link_command->value;
    }

    if ((link_command->type == GIMBAL_LINK_COMMAND_SET_DISTANCE) &&
        ((command.distance_cm < DISTANCE_INPUT_MIN_CM) ||
         (command.distance_cm > DISTANCE_INPUT_MAX_CM))) {
        (void)queue_gimbal_ack(link_command->sequence, link_command->type,
                              GIMBAL_LINK_ACK_INVALID);
        return;
    }
    if (((link_command->type == GIMBAL_LINK_COMMAND_SET_YAW_TARGET) &&
         ((link_command->value < -(int16_t)YAW_SCAN_TARGET_LIMIT_DEG) ||
          (link_command->value > (int16_t)YAW_SCAN_TARGET_LIMIT_DEG))) ||
        ((link_command->type == GIMBAL_LINK_COMMAND_SET_PITCH_TARGET) &&
         ((link_command->value <
           -(int16_t)PITCH_MANUAL_TARGET_LIMIT_DEG) ||
          (link_command->value >
           (int16_t)PITCH_MANUAL_TARGET_LIMIT_DEG)))) {
        (void)queue_gimbal_ack(link_command->sequence, link_command->type,
                               GIMBAL_LINK_ACK_INVALID);
        return;
    }
    if ((link_command->type == GIMBAL_LINK_COMMAND_SET_YAW_TARGET) ||
        (link_command->type == GIMBAL_LINK_COMMAND_SET_PITCH_TARGET)) {
        command.angle_degrees = link_command->value;
    }

    if ((link_command->type == GIMBAL_LINK_COMMAND_STOP) ||
        (link_command->type == GIMBAL_LINK_COMMAND_DISABLE)) {
        auto_aim_cancel();
        abort_fire_pulse();
        abort_charge();
    }

    publish_result = app_command_bus_publish(&s_command_bus, &command);
    if (publish_result == APP_PUBLISH_OK) {
        ack_result = GIMBAL_LINK_ACK_ACCEPTED;
        if ((link_command->type == GIMBAL_LINK_COMMAND_SET_DISTANCE) &&
            s_ui_started) {
            (void)target_ui_set_distance(&s_ui, link_command->value);
        }
    } else if (publish_result == APP_PUBLISH_FULL) {
        ack_result = GIMBAL_LINK_ACK_FULL;
    }
    (void)queue_gimbal_ack(link_command->sequence, link_command->type,
                           ack_result);
}

static void accept_gimbal_vision(const gimbal_link_vision_t *vision,
                                 uint32_t now_ms)
{
    bool distance_valid;
    uint16_t target_center_distance_mm;

    if (vision == NULL) {
        return;
    }
    distance_valid =
        ((vision->flags & GIMBAL_LINK_VISION_DISTANCE_VALID) != 0U) &&
        (vision->distance_mm <
         (UINT16_MAX - CANNON_BOX_FRONT_TO_TARGET_CENTER_MM));
    s_latest_vision = *vision;
    target_center_distance_mm = vision->distance_mm;
    if (distance_valid) {
        target_center_distance_mm =
            (uint16_t)(vision->distance_mm +
                       CANNON_BOX_FRONT_TO_TARGET_CENTER_MM);
    } else {
        s_latest_vision.flags &=
            (uint8_t)~GIMBAL_LINK_VISION_DISTANCE_VALID;
    }
    s_vision_last_rx_ms = now_ms;
    s_vision_online = true;
    if (s_ui_started) {
        target_ui_set_vision_distance(&s_ui,
                                      target_center_distance_mm,
                                      distance_valid);
    }
}

static void fill_gimbal_telemetry(uint32_t now_ms,
                                  gimbal_link_telemetry_t *telemetry)
{
    bsp_uart_bridge_stats_t uart_stats;

    bsp_uart_bridge_get_stats(&uart_stats);
    telemetry->timestamp_ms = now_ms;
    telemetry->status = runtime_name(s_ui.runtime_status);
    telemetry->feedback = feedback_name(s_ui.feedback);
    telemetry->distance_cm = s_ui.distance_cm;
    telemetry->yaw_encoder_raw = s_qd4310_yaw_enable.last_angle_raw;
    telemetry->pitch_encoder_raw = s_qd4310_pitch_enable.last_angle_raw;
    telemetry->yaw_feedback_valid = s_qd4310_yaw_enable.feedback_valid;
    telemetry->pitch_feedback_valid = s_qd4310_pitch_enable.feedback_valid;
    telemetry->yaw_mdeg = s_qd4310_yaw_enable.feedback_valid ?
                          rounded_milli(yaw_feedback_angle_degrees()) : 0;
    telemetry->pitch_mdeg = s_qd4310_pitch_enable.feedback_valid ?
                            rounded_milli(pitch_feedback_angle_degrees()) : 0;
    telemetry->target_mdeg = rounded_milli(s_yaw_scan_target_degrees);
    telemetry->pitch_target_mdeg = rounded_milli(s_pitch_target_degrees);
    telemetry->command_current_ma = rounded_milli(s_yaw_scan_current_a);
    telemetry->feedback_current_ma = rounded_milli(yaw_feedback_current_a());
    telemetry->pitch_command_current_ma =
        rounded_milli(s_pitch_hold_current_a);
    telemetry->pitch_feedback_current_ma =
        rounded_milli(pitch_feedback_current_a());
    telemetry->speed_mrpm = rounded_milli(yaw_feedback_speed_rpm());
    telemetry->yaw_state = qd4310_state_name(s_qd4310_yaw_enable.state);
    telemetry->pitch_state = qd4310_state_name(s_qd4310_pitch_enable.state);
    telemetry->scan_active = s_yaw_scan.active;
    telemetry->scan_fault = s_yaw_scan_fault;
    telemetry->fault_code = s_yaw_scan_fault_code;
    telemetry->response_count = s_qd4310_yaw_enable.response_count;
    telemetry->pitch_response_count =
        s_qd4310_pitch_enable.response_count;
    telemetry->command_count = s_yaw_control_command_count;
    telemetry->minimum_mdeg = rounded_milli(s_yaw_scan_min_degrees);
    telemetry->maximum_mdeg = rounded_milli(s_yaw_scan_max_degrees);
    telemetry->peak_current_ma = rounded_milli(s_yaw_scan_peak_current_a);
    telemetry->esp_rx_dropped = s_esp8266.rx_dropped;
    telemetry->esp_tx_frames = s_esp8266.tx_frames;
    telemetry->uart_rx_dropped = uart_stats.esp_to_app_dropped;
    telemetry->protocol_errors = s_gimbal_link_decoder.error_count;
}

static void gimbal_link_poll(uint32_t now_ms)
{
    uint8_t value;
    uint8_t frame[ESP8266_MAX_TCP_PAYLOAD];
    size_t frame_length;
    gimbal_link_command_t command;
    gimbal_link_vision_t vision;
    gimbal_link_telemetry_t telemetry;

    esp8266_poll(&s_esp8266, now_ms);
    while (esp8266_read(&s_esp8266, &value)) {
        if (gimbal_link_decoder_feed(&s_gimbal_link_decoder, value,
                                     &command)) {
            accept_gimbal_command(&command, now_ms);
        }
        if (gimbal_link_decoder_take_vision(&s_gimbal_link_decoder,
                                            &vision)) {
            accept_gimbal_vision(&vision, now_ms);
        }
    }
    if (s_vision_online &&
        time_reached(now_ms,
                     s_vision_last_rx_ms + GIMBAL_VISION_TIMEOUT_MS)) {
        s_vision_online = false;
        if (s_ui_started) {
            target_ui_set_vision_distance(&s_ui, 0U, false);
        }
    }
    if ((esp8266_status(&s_esp8266) != ESP8266_SESSION_READY) ||
        !s_ui_started) {
        return;
    }

    if (s_gimbal_ack_count > 0U) {
        const gimbal_ack_t *ack =
            &s_gimbal_ack_queue[s_gimbal_ack_read];
        frame_length = gimbal_link_encode_ack(frame, sizeof(frame),
                                              ack->sequence, ack->command,
                                              ack->result);
        if ((frame_length > 0U) &&
            esp8266_send(&s_esp8266, frame, frame_length)) {
            s_gimbal_ack_read = (uint8_t)((s_gimbal_ack_read + 1U) %
                                         GIMBAL_ACK_QUEUE_CAPACITY);
            --s_gimbal_ack_count;
        }
        return;
    }

    fill_gimbal_telemetry(now_ms, &telemetry);
    if (s_gimbal_debug_pending) {
        frame_length = gimbal_link_encode_debug(
            frame, sizeof(frame), s_gimbal_telemetry_sequence,
            &telemetry);
        if ((frame_length > 0U) &&
            esp8266_send(&s_esp8266, frame, frame_length)) {
            ++s_gimbal_telemetry_sequence;
            s_gimbal_debug_pending = false;
        }
    } else if (time_reached(now_ms, s_gimbal_telemetry_deadline_ms)) {
        frame_length = gimbal_link_encode_telemetry(
            frame, sizeof(frame), s_gimbal_telemetry_sequence,
            &telemetry);
        if ((frame_length > 0U) &&
            esp8266_send(&s_esp8266, frame, frame_length)) {
            ++s_gimbal_telemetry_sequence;
            s_gimbal_telemetry_deadline_ms = now_ms +
                                             GIMBAL_TELEMETRY_PERIOD_MS;
            s_gimbal_debug_pending = true;
        }
    }
}

static void report_qd4310_state(void)
{
    target_ui_feedback_t feedback;
    target_ui_runtime_status_t status;
    bool yaw_failed;
    bool pitch_failed;

    if (!s_ui_started ||
        ((s_reported_yaw_state == s_qd4310_yaw_enable.state) &&
         (s_reported_pitch_state == s_qd4310_pitch_enable.state) &&
         (s_reported_yaw_expected_enabled ==
          s_qd4310_yaw_enable.expected_enabled) &&
         (s_reported_pitch_expected_enabled ==
          s_qd4310_pitch_enable.expected_enabled) &&
         (s_reported_scan_active == s_yaw_scan.active) &&
         (s_reported_scan_fault == s_yaw_scan_fault) &&
         (s_reported_user_stopped == s_user_stopped))) {
        return;
    }

    status = (!s_qd4310_yaw_enable.expected_enabled &&
              !s_qd4310_pitch_enable.expected_enabled) ?
             TARGET_UI_DISABLING : TARGET_UI_ENABLING;
    feedback = (!s_qd4310_yaw_enable.expected_enabled &&
                !s_qd4310_pitch_enable.expected_enabled) ?
               TARGET_UI_FEEDBACK_CAN_DISABLE_WAIT :
               TARGET_UI_FEEDBACK_CAN_WAIT;
    yaw_failed = qd4310_state_failed(s_qd4310_yaw_enable.state);
    pitch_failed = qd4310_state_failed(s_qd4310_pitch_enable.state);
    if (s_yaw_scan_fault) {
        status = TARGET_UI_FAULT;
        feedback = TARGET_UI_FEEDBACK_YAW_SCAN_ERROR;
    } else if (s_yaw_scan.active || s_pitch_hold.active) {
        status = TARGET_UI_RUNNING;
        feedback = TARGET_UI_FEEDBACK_YAW_SCANNING;
    } else if ((s_qd4310_yaw_enable.state == QD4310_ENABLE_CONFIRMED) &&
        (s_qd4310_pitch_enable.state == QD4310_ENABLE_CONFIRMED)) {
        if (s_qd4310_yaw_enable.expected_enabled &&
            s_qd4310_pitch_enable.expected_enabled) {
            if (s_user_stopped) {
                status = TARGET_UI_COMPLETE;
                feedback = TARGET_UI_FEEDBACK_MOTOR_STOPPED;
            } else {
                status = TARGET_UI_READY;
                feedback = TARGET_UI_FEEDBACK_CAN_ENABLED;
            }
        } else {
            status = TARGET_UI_IDLE;
            feedback = TARGET_UI_FEEDBACK_CAN_DISABLED;
        }
    } else if (yaw_failed || pitch_failed) {
        status = TARGET_UI_FAULT;
        if (yaw_failed && !pitch_failed) {
            feedback = TARGET_UI_FEEDBACK_CAN_YAW_ERROR;
        } else if (pitch_failed && !yaw_failed) {
            feedback = TARGET_UI_FEEDBACK_CAN_PITCH_ERROR;
        } else {
            feedback = TARGET_UI_FEEDBACK_CAN_ERROR;
        }
    }

    target_ui_set_runtime(&s_ui, status, s_yaw_relative_degrees,
                          s_pitch_relative_degrees);
    target_ui_set_feedback(&s_ui, feedback);
    s_reported_yaw_state = s_qd4310_yaw_enable.state;
    s_reported_pitch_state = s_qd4310_pitch_enable.state;
    s_reported_yaw_expected_enabled =
        s_qd4310_yaw_enable.expected_enabled;
    s_reported_pitch_expected_enabled =
        s_qd4310_pitch_enable.expected_enabled;
    s_reported_scan_active = s_yaw_scan.active;
    s_reported_scan_fault = s_yaw_scan_fault;
    s_reported_user_stopped = s_user_stopped;
}

static void stop_yaw_scan_on_fault(uint8_t fault_code)
{
    auto_aim_cancel();
    yaw_scan_controller_stop(&s_yaw_scan);
    pitch_hold_controller_stop(&s_pitch_hold);
    s_yaw_scan_start_pending = false;
    s_yaw_direction_check_pending = false;
    s_yaw_scan_fault = true;
    s_yaw_scan_fault_code = fault_code;
    s_yaw_scan_current_a = 0.0f;
    s_pitch_hold_current_a = 0.0f;
    s_user_stopped = false;
    (void)qd4310_set_test_current(QD4310_YAW_MOTOR_ID, 0.0f);
    (void)qd4310_set_test_current(QD4310_PITCH_MOTOR_ID, 0.0f);
}

static void stop_scan_state(void)
{
    auto_aim_cancel();
    yaw_scan_controller_stop(&s_yaw_scan);
    pitch_hold_controller_stop(&s_pitch_hold);
    s_yaw_scan_start_pending = false;
    s_yaw_direction_check_pending = false;
    s_yaw_scan_fault = false;
    s_yaw_scan_fault_code = 0U;
    s_yaw_scan_current_a = 0.0f;
    s_pitch_hold_current_a = 0.0f;
}

static bool request_user_enable(uint32_t now_ms)
{
    stop_scan_state();
    s_yaw_enable_zero_response_baseline =
        s_qd4310_yaw_enable.response_count;
    s_pitch_enable_zero_response_baseline =
        s_qd4310_pitch_enable.response_count;
    s_enable_zero_pending = true;
    s_enable_zero_valid = false;
    s_user_stopped = false;
    qd4310_enable_test_begin(&s_qd4310_yaw_enable, now_ms,
                             QD4310_ENABLE_TIMEOUT_MS);
    qd4310_enable_test_begin(&s_qd4310_pitch_enable, now_ms,
                             QD4310_ENABLE_TIMEOUT_MS);
    return (s_qd4310_yaw_enable.state == QD4310_ENABLE_WAITING) &&
           (s_qd4310_pitch_enable.state == QD4310_ENABLE_WAITING);
}

static bool request_user_scan(void)
{
    if ((GIMBAL_SCAN_ENABLED == 0U) ||
        !both_axes_enabled() || !s_enable_zero_valid) {
        return false;
    }
    stop_scan_state();
    s_yaw_scan_start_pending = true;
    s_yaw_direction_check_pending = true;
    s_user_stopped = false;
    return true;
}

static bool request_user_stop(void)
{
    bool yaw_zero_sent;
    bool pitch_zero_sent;

    if (!both_axes_enabled()) {
        return false;
    }
    stop_scan_state();
    s_user_stopped = true;
    yaw_zero_sent = qd4310_set_test_current(QD4310_YAW_MOTOR_ID, 0.0f);
    pitch_zero_sent = qd4310_set_test_current(QD4310_PITCH_MOTOR_ID, 0.0f);
    return yaw_zero_sent && pitch_zero_sent;
}

static bool request_user_disable(uint32_t now_ms)
{
    stop_scan_state();
    s_enable_zero_pending = false;
    s_enable_zero_valid = false;
    s_user_stopped = false;
    qd4310_disable_test_begin(&s_qd4310_yaw_enable, now_ms,
                              QD4310_ENABLE_TIMEOUT_MS);
    qd4310_disable_test_begin(&s_qd4310_pitch_enable, now_ms,
                              QD4310_ENABLE_TIMEOUT_MS);
    return (s_qd4310_yaw_enable.state == QD4310_ENABLE_WAITING) &&
           (s_qd4310_pitch_enable.state == QD4310_ENABLE_WAITING);
}

static control_submit_result_t request_distance_aim_cm(float distance_cm)
{
    float target_degrees;

    if (!both_axes_enabled() || !s_enable_zero_valid ||
        !s_pitch_hold.active ||
        !cannon_aim_pitch_for_distance(distance_cm, &target_degrees) ||
        !pitch_hold_controller_set_target(&s_pitch_hold,
                                          target_degrees)) {
        return CONTROL_SUBMIT_BUSY;
    }

    s_pitch_target_degrees = target_degrees;
    (void)target_ui_set_targets(
        &s_ui, (int16_t)s_yaw_scan_target_degrees,
        rounded_degrees(target_degrees));
    return CONTROL_SUBMIT_ACCEPTED;
}

static control_submit_result_t request_fixed_target_aim_cm(float distance_cm)
{
    float target_degrees;

    if (!both_axes_enabled() || !s_enable_zero_valid ||
        !s_pitch_hold.active ||
        !cannon_aim_pitch_for_fixed_target(distance_cm, &target_degrees) ||
        !pitch_hold_controller_set_target(&s_pitch_hold,
                                          target_degrees)) {
        return CONTROL_SUBMIT_BUSY;
    }

    s_pitch_target_degrees = target_degrees;
    (void)target_ui_set_targets(
        &s_ui, (int16_t)s_yaw_scan_target_degrees,
        rounded_degrees(target_degrees));
    return CONTROL_SUBMIT_ACCEPTED;
}

static control_submit_result_t request_fire_pulse(uint32_t now_ms)
{
#if CANNON_MANUAL_TEST_MODE != 0U
    if (s_fire_relay_active) {
        return CONTROL_SUBMIT_BUSY;
    }
#else
    if (s_fire_relay_active || !s_charge_cycle_started ||
        !s_charge_cycle_complete) {
        return CONTROL_SUBMIT_BUSY;
    }
#endif

    if (s_charge_relay_active) {
        abort_charge();
    }
    bsp_cannon_relay_set_fire(true);
    s_fire_relay_active = true;
    s_fire_relay_deadline_ms = now_ms + CANNON_FIRE_PULSE_MS;
    target_ui_set_fire_active(&s_ui, true);
    return CONTROL_SUBMIT_ACCEPTED;
}

static void abort_fire_pulse(void)
{
    bsp_cannon_relay_set_fire(false);
    s_fire_relay_active = false;
    s_fire_relay_deadline_ms = 0U;
    target_ui_set_fire_active(&s_ui, false);
}

static bool charge_cycle_paused(void)
{
    return s_charge_cycle_started && !s_charge_cycle_complete &&
           !s_charge_relay_active;
}

static void begin_new_charge_cycle(uint32_t now_ms)
{
    s_charge_cycle_started = true;
    s_charge_cycle_complete = false;
    s_charge_elapsed_ms = 0U;
    s_charge_last_step_ms = now_ms;
    s_charge_pause_started_ms = 0U;
    bsp_cannon_relay_set_charge(true);
    s_charge_relay_active = true;
    if (s_ui_started) {
        target_ui_set_charge_active(&s_ui, true);
    }
}

static control_submit_result_t start_charge(uint32_t now_ms)
{
    const bool resuming = charge_cycle_paused();

    if (s_charge_relay_active || s_fire_relay_active) {
        return CONTROL_SUBMIT_BUSY;
    }

    if (!resuming) {
        begin_new_charge_cycle(now_ms);
        return CONTROL_SUBMIT_ACCEPTED;
    } else if (auto_aim_active()) {
        s_auto_aim_deadline_ms += now_ms - s_charge_pause_started_ms;
    }
    s_charge_last_step_ms = now_ms;
    s_charge_pause_started_ms = 0U;
    bsp_cannon_relay_set_charge(true);
    s_charge_relay_active = true;
    target_ui_set_charge_active(&s_ui, true);
    return CONTROL_SUBMIT_ACCEPTED;
}

static void abort_charge(void)
{
    if (s_charge_relay_active) {
        s_charge_pause_started_ms = s_charge_last_step_ms;
    }
    bsp_cannon_relay_set_charge(false);
    s_charge_relay_active = false;
    target_ui_set_charge_active(&s_ui, false);
}

static void auto_aim_cancel(void)
{
    const bool was_active = auto_aim_active();
    const bool was_task_fire_pending = s_task_auto_fire_pending;

    s_task_auto_fire_pending = false;
    s_auto_aim_state = AUTO_AIM_IDLE;
    s_auto_aim_deadline_ms = 0U;
    s_auto_aim_target_seen_ms = 0U;
    s_auto_aim_settle_started_ms = 0U;
    s_auto_aim_settle_sequence = 0U;
    s_auto_aim_search_direction = 0;
    s_auto_aim_coarse_locked = false;
    s_auto_aim_release_count = 0U;
    s_auto_aim_release_direction = 0;
    s_auto_aim_motion_arrived = false;
    s_auto_aim_motion_last_ms = 0U;
    s_auto_aim_slope_px_per_degree = 0.0f;
    if (s_fire_relay_active) {
        abort_fire_pulse();
    }
    if ((was_active || was_task_fire_pending) && s_charge_relay_active) {
        abort_charge();
    }
    if (s_ui_started) {
        target_ui_set_auto_aim_active(&s_ui, false);
    }
}

static void auto_aim_fail(void)
{
    auto_aim_cancel();
    if (s_ui_started) {
        target_ui_set_feedback(&s_ui,
                               TARGET_UI_FEEDBACK_VISION_TARGET_INVALID);
    }
}

static bool auto_aim_set_yaw_target(float target_degrees)
{
    target_degrees = limit_float(target_degrees,
                                 -AUTO_AIM_SEARCH_LIMIT_DEG,
                                 AUTO_AIM_SEARCH_LIMIT_DEG);
    if (!yaw_scan_controller_track_target(&s_yaw_scan, target_degrees)) {
        return false;
    }
    s_yaw_scan_target_degrees = target_degrees;
    if (s_ui_started) {
        (void)target_ui_set_targets(
            &s_ui, rounded_degrees(target_degrees),
            rounded_degrees(s_pitch_target_degrees));
    }
    return true;
}

static bool auto_aim_start_slow_yaw_move(float goal_degrees,
                                         uint32_t now_ms)
{
    const float angle_degrees = yaw_feedback_angle_degrees();

    goal_degrees = limit_float(goal_degrees,
                               -AUTO_AIM_SEARCH_LIMIT_DEG,
                               AUTO_AIM_SEARCH_LIMIT_DEG);
    s_auto_aim_motion_last_ms = now_ms;
    s_auto_aim_motion_arrived = false;
    s_yaw_scan.direction = (goal_degrees > angle_degrees) ? 1 :
                           ((goal_degrees < angle_degrees) ? -1 : 0);
    s_yaw_scan.endpoint_holding = false;
    return true;
}

static bool auto_aim_apply_relative_yaw_error(float pixel_error,
                                              uint32_t now_ms)
{
    const uint32_t elapsed_ms = now_ms - s_auto_aim_motion_last_ms;
    const float speed_limited_step =
        AUTO_AIM_CENTERING_TARGET_SPEED_DPS * (float)elapsed_ms / 1000.0f;
    float correction_degrees =
        pixel_error * AUTO_AIM_PIXEL_ERROR_YAW_POLARITY *
        AUTO_AIM_YAW_DEGREES_PER_PIXEL;
    float maximum_step = AUTO_AIM_YAW_CORRECTION_STEP_MAX_DEG;

    if (speed_limited_step < maximum_step) {
        maximum_step = speed_limited_step;
    }
    correction_degrees = limit_float(correction_degrees,
                                     -maximum_step,
                                     maximum_step);
    s_auto_aim_motion_last_ms = now_ms;
    s_yaw_scan.direction = (correction_degrees > 0.0f) ? 1 :
                           ((correction_degrees < 0.0f) ? -1 : 0);
    s_yaw_scan.endpoint_holding = false;
    return auto_aim_set_yaw_target(s_yaw_scan_target_degrees +
                                   correction_degrees);
}

static bool auto_aim_locked_target_should_release(float pixel_error)
{
    const int8_t correction_direction =
        (pixel_error * AUTO_AIM_PIXEL_ERROR_YAW_POLARITY > 0.0f) ? 1 : -1;

    if (fabsf(pixel_error) <= AUTO_AIM_X_RELEASE_TOLERANCE_PX) {
        s_auto_aim_release_count = 0U;
        s_auto_aim_release_direction = 0;
        return false;
    }
    if (correction_direction != s_auto_aim_release_direction) {
        s_auto_aim_release_direction = correction_direction;
        s_auto_aim_release_count = 1U;
    } else if (s_auto_aim_release_count <
               AUTO_AIM_X_RELEASE_CONFIRM_SAMPLES) {
        ++s_auto_aim_release_count;
    }
    return s_auto_aim_release_count >=
           AUTO_AIM_X_RELEASE_CONFIRM_SAMPLES;
}

static bool auto_aim_slow_yaw_step(float goal_degrees,
                                   float reached_degrees,
                                   uint32_t now_ms,
                                   bool *reached)
{
    const uint32_t elapsed_ms = now_ms - s_auto_aim_motion_last_ms;
    const float maximum_step = YAW_SCAN_TARGET_SPEED_DPS *
                               (float)elapsed_ms / 1000.0f;
    float command_degrees = s_yaw_scan_target_degrees;

    if (reached == NULL) {
        return false;
    }
    *reached = false;
    goal_degrees = limit_float(goal_degrees,
                               -AUTO_AIM_SEARCH_LIMIT_DEG,
                               AUTO_AIM_SEARCH_LIMIT_DEG);
    if (command_degrees < goal_degrees) {
        command_degrees += maximum_step;
        if (command_degrees > goal_degrees) {
            command_degrees = goal_degrees;
        }
    } else if (command_degrees > goal_degrees) {
        command_degrees -= maximum_step;
        if (command_degrees < goal_degrees) {
            command_degrees = goal_degrees;
        }
    }
    s_auto_aim_motion_last_ms = now_ms;
    if (!auto_aim_set_yaw_target(command_degrees)) {
        return false;
    }
    if ((fabsf(command_degrees - goal_degrees) <= 0.001f) &&
        (fabsf(yaw_feedback_angle_degrees() - goal_degrees) <=
         reached_degrees)) {
        s_yaw_scan.direction = 0;
        s_yaw_scan.endpoint_holding = true;
        *reached = true;
    }
    return true;
}

static bool auto_aim_set_fixed_target_pitch(void)
{
    const uint16_t distance_mm = s_latest_vision.distance_mm;

    if (!s_vision_online ||
        ((s_latest_vision.flags &
          GIMBAL_LINK_VISION_DISTANCE_VALID) == 0U) ||
        (distance_mm < DISTANCE_INPUT_MIN_CM * 10U) ||
        (distance_mm > DISTANCE_INPUT_MAX_CM * 10U)) {
        return false;
    }
    return request_fixed_target_aim_cm((float)distance_mm / 10.0f) ==
           CONTROL_SUBMIT_ACCEPTED;
}

static bool auto_aim_axes_reached(void)
{
    return both_axes_enabled() && s_enable_zero_valid &&
           s_qd4310_yaw_enable.feedback_valid &&
           s_qd4310_pitch_enable.feedback_valid &&
           (fabsf(yaw_feedback_angle_degrees() -
                  s_yaw_scan_target_degrees) <=
            AUTO_AIM_AXIS_TOLERANCE_DEG) &&
           (fabsf(pitch_feedback_angle_degrees() -
                  s_pitch_target_degrees) <=
            AUTO_AIM_AXIS_TOLERANCE_DEG);
}

static bool auto_aim_vision_x_valid(bool final_required)
{
    const uint8_t required = final_required ?
        (GIMBAL_LINK_VISION_X_VALID | GIMBAL_LINK_VISION_X_FINAL) :
        GIMBAL_LINK_VISION_X_VALID;

    return s_vision_online &&
           ((s_latest_vision.flags & required) == required) &&
           (s_latest_vision.frame_width > 0U);
}

static bool auto_aim_set_search_endpoint(uint32_t now_ms)
{
    const float angle_degrees = yaw_feedback_angle_degrees();

    s_auto_aim_search_direction = (angle_degrees >= 0.0f) ? -1 : 1;
    return auto_aim_start_slow_yaw_move(
        (float)s_auto_aim_search_direction * AUTO_AIM_SEARCH_LIMIT_DEG,
        now_ms);
}

static bool auto_aim_begin_search(uint32_t now_ms)
{
    s_auto_aim_state = AUTO_AIM_SEARCH;
    s_auto_aim_target_seen_ms = now_ms - AUTO_AIM_TARGET_LOSS_MS;
    s_auto_aim_settle_started_ms = 0U;
    s_auto_aim_slope_px_per_degree = 0.0f;
    s_auto_aim_coarse_locked = false;
    s_auto_aim_release_count = 0U;
    s_auto_aim_release_direction = 0;
    return auto_aim_set_search_endpoint(now_ms);
}

static bool auto_aim_begin_calibration(uint32_t now_ms)
{
    const float minimum = -AUTO_AIM_SEARCH_LIMIT_DEG;
    const float maximum = AUTO_AIM_SEARCH_LIMIT_DEG;
    float lower_degrees;
    float upper_degrees;

    lower_degrees =
        s_auto_aim_coarse_yaw_degrees - AUTO_AIM_CALIBRATION_OFFSET_DEG;
    upper_degrees =
        s_auto_aim_coarse_yaw_degrees + AUTO_AIM_CALIBRATION_OFFSET_DEG;
    if (lower_degrees < minimum) {
        upper_degrees += minimum - lower_degrees;
        lower_degrees = minimum;
    }
    if (upper_degrees > maximum) {
        lower_degrees -= upper_degrees - maximum;
        upper_degrees = maximum;
    }
    lower_degrees = limit_float(lower_degrees, minimum, maximum);
    upper_degrees = limit_float(upper_degrees, minimum, maximum);
    if ((upper_degrees - lower_degrees) <
        AUTO_AIM_CALIBRATION_OFFSET_DEG) {
        return false;
    }
    if (s_auto_aim_search_direction > 0) {
        s_auto_aim_calibration_a_degrees = upper_degrees;
        s_auto_aim_calibration_b_degrees = lower_degrees;
    } else {
        s_auto_aim_calibration_a_degrees = lower_degrees;
        s_auto_aim_calibration_b_degrees = upper_degrees;
    }
    s_auto_aim_state = AUTO_AIM_CALIBRATE_A;
    s_auto_aim_target_seen_ms = now_ms;
    return auto_aim_start_slow_yaw_move(
        s_auto_aim_calibration_a_degrees, now_ms);
}

static control_submit_result_t request_auto_aim(uint32_t now_ms)
{
    if (auto_aim_active() || s_fire_relay_active ||
        !both_axes_enabled() || !s_enable_zero_valid ||
        !s_yaw_scan.active || !s_pitch_hold.active || s_yaw_scan_fault) {
        return CONTROL_SUBMIT_BUSY;
    }

    s_auto_aim_deadline_ms = now_ms + AUTO_AIM_TIMEOUT_MS;
    s_auto_aim_vision_sequence = s_latest_vision.sequence;
    if (!auto_aim_begin_search(now_ms)) {
        auto_aim_cancel();
        return CONTROL_SUBMIT_BUSY;
    }
#if CANNON_MANUAL_TEST_MODE == 0U
    s_task_auto_fire_pending = true;
    begin_new_charge_cycle(now_ms);
#endif
    if (s_ui_started) {
        target_ui_set_auto_aim_active(&s_ui, true);
    }
    return CONTROL_SUBMIT_ACCEPTED;
}

static void auto_aim_step(uint32_t now_ms)
{
    bool fresh_vision;
    bool motion_reached;
    float pixel_error;

    if (!auto_aim_active()) {
        return;
    }
    if (s_auto_aim_state == AUTO_AIM_FIRING) {
        if (!s_fire_relay_active) {
            s_auto_aim_state = AUTO_AIM_IDLE;
            if (s_ui_started) {
                target_ui_set_auto_aim_active(&s_ui, false);
            }
        } else if (time_reached(now_ms, s_auto_aim_deadline_ms)) {
            auto_aim_fail();
        }
        return;
    }
    if (!charge_cycle_paused() &&
        time_reached(now_ms, s_auto_aim_deadline_ms)) {
        auto_aim_fail();
        return;
    }
    if (!both_axes_enabled() || !s_enable_zero_valid ||
        !s_yaw_scan.active || !s_pitch_hold.active || s_yaw_scan_fault) {
        auto_aim_cancel();
        return;
    }

    fresh_vision = s_latest_vision.sequence !=
                   s_auto_aim_vision_sequence;
    if (fresh_vision) {
        s_auto_aim_vision_sequence = s_latest_vision.sequence;
        if (auto_aim_vision_x_valid(false)) {
            s_auto_aim_target_seen_ms = now_ms;
        }
    }

    if (s_auto_aim_state == AUTO_AIM_SEARCH) {
        if (AUTO_AIM_SINGLE_PASS_LOCK != 0U) {
            if (fresh_vision && auto_aim_vision_x_valid(false)) {
                const float center_x =
                    (float)s_latest_vision.frame_width * 0.5f;

                pixel_error = (float)s_latest_vision.x - center_x;
                if (s_auto_aim_coarse_locked &&
                    !auto_aim_locked_target_should_release(pixel_error)) {
                    if (fabsf(pixel_error) >
                        AUTO_AIM_X_RELEASE_TOLERANCE_PX) {
                        return;
                    }
                    s_yaw_scan.direction = 0;
                    s_yaw_scan.endpoint_holding = true;
                    if (!auto_aim_set_fixed_target_pitch()) {
                        return;
                    }
                    if (auto_aim_axes_reached()) {
                        s_auto_aim_state = AUTO_AIM_SETTLE;
                        s_auto_aim_settle_started_ms = now_ms;
                        s_auto_aim_settle_sequence =
                            s_latest_vision.sequence;
                    }
                    return;
                }
                if (s_auto_aim_coarse_locked) {
                    s_auto_aim_coarse_locked = false;
                    s_auto_aim_release_count = 0U;
                    s_auto_aim_release_direction = 0;
                }
                if (fabsf(pixel_error) <= AUTO_AIM_X_TOLERANCE_PX) {
                    s_auto_aim_coarse_locked = true;
                    s_yaw_scan.direction = 0;
                    s_yaw_scan.endpoint_holding = true;
                    if (!auto_aim_set_fixed_target_pitch()) {
                        return;
                    }
                    if (auto_aim_axes_reached()) {
                        s_auto_aim_state = AUTO_AIM_SETTLE;
                        s_auto_aim_settle_started_ms = now_ms;
                        s_auto_aim_settle_sequence =
                            s_latest_vision.sequence;
                    }
                    return;
                }
                if (!auto_aim_apply_relative_yaw_error(pixel_error,
                                                       now_ms)) {
                    auto_aim_fail();
                }
                return;
            }
            if (!time_reached(now_ms, s_auto_aim_target_seen_ms +
                              AUTO_AIM_TARGET_LOSS_MS)) {
                return;
            }
            s_auto_aim_coarse_locked = false;
            s_auto_aim_release_count = 0U;
            s_auto_aim_release_direction = 0;
            if (!auto_aim_slow_yaw_step(
                    (float)s_auto_aim_search_direction *
                        AUTO_AIM_SEARCH_LIMIT_DEG,
                    AUTO_AIM_SEARCH_REACHED_DEG, now_ms,
                    &motion_reached)) {
                auto_aim_fail();
                return;
            }
            if (motion_reached) {
                s_auto_aim_search_direction =
                    (int8_t)-s_auto_aim_search_direction;
                if (!auto_aim_start_slow_yaw_move(
                        (float)s_auto_aim_search_direction *
                            AUTO_AIM_SEARCH_LIMIT_DEG,
                        now_ms)) {
                    auto_aim_fail();
                }
            }
            return;
        }
        if (!s_auto_aim_coarse_locked && fresh_vision &&
            auto_aim_vision_x_valid(false)) {
            s_auto_aim_coarse_yaw_degrees =
                yaw_feedback_angle_degrees();
            s_auto_aim_coarse_locked = true;
        }
        if (!auto_aim_slow_yaw_step(
                (float)s_auto_aim_search_direction *
                    AUTO_AIM_SEARCH_LIMIT_DEG,
                AUTO_AIM_SEARCH_REACHED_DEG, now_ms,
                &motion_reached)) {
            auto_aim_fail();
            return;
        }
        if (motion_reached) {
            if (s_auto_aim_coarse_locked) {
                if (!auto_aim_begin_calibration(now_ms)) {
                    auto_aim_fail();
                }
                return;
            }
            s_auto_aim_search_direction =
                (int8_t)-s_auto_aim_search_direction;
            if (!auto_aim_start_slow_yaw_move(
                    (float)s_auto_aim_search_direction *
                        AUTO_AIM_SEARCH_LIMIT_DEG,
                    now_ms)) {
                auto_aim_fail();
            }
        }
        return;
    }

    if ((s_auto_aim_state != AUTO_AIM_CALIBRATE_A) &&
        (s_auto_aim_state != AUTO_AIM_CALIBRATE_B) &&
        time_reached(now_ms, s_auto_aim_target_seen_ms +
                     AUTO_AIM_TARGET_LOSS_MS)) {
        if (!auto_aim_begin_search(now_ms)) {
            auto_aim_fail();
        }
        return;
    }

    if (s_auto_aim_state == AUTO_AIM_CALIBRATE_A) {
        if (!s_auto_aim_motion_arrived) {
            if (!auto_aim_slow_yaw_step(
                    s_auto_aim_calibration_a_degrees,
                    AUTO_AIM_CALIBRATION_REACHED_DEG,
                    now_ms, &motion_reached)) {
                auto_aim_fail();
                return;
            }
            if (motion_reached) {
                s_auto_aim_motion_arrived = true;
                s_auto_aim_target_seen_ms = now_ms;
            }
            return;
        }
        if (time_reached(now_ms, s_auto_aim_target_seen_ms +
                         AUTO_AIM_TARGET_LOSS_MS)) {
            if (!auto_aim_begin_search(now_ms)) {
                auto_aim_fail();
            }
            return;
        }
        if (fresh_vision && auto_aim_vision_x_valid(true) &&
            (fabsf(yaw_feedback_angle_degrees() -
                   s_auto_aim_calibration_a_degrees) <=
             AUTO_AIM_CALIBRATION_REACHED_DEG)) {
            s_auto_aim_calibration_x_a = (float)s_latest_vision.x;
            s_auto_aim_calibration_a_degrees =
                yaw_feedback_angle_degrees();
            s_auto_aim_state = AUTO_AIM_CALIBRATE_B;
            if (!auto_aim_start_slow_yaw_move(
                    s_auto_aim_calibration_b_degrees, now_ms)) {
                auto_aim_fail();
            }
        }
        return;
    }

    if (s_auto_aim_state == AUTO_AIM_CALIBRATE_B) {
        if (!s_auto_aim_motion_arrived) {
            if (!auto_aim_slow_yaw_step(
                    s_auto_aim_calibration_b_degrees,
                    AUTO_AIM_CALIBRATION_REACHED_DEG,
                    now_ms, &motion_reached)) {
                auto_aim_fail();
                return;
            }
            if (motion_reached) {
                s_auto_aim_motion_arrived = true;
                s_auto_aim_target_seen_ms = now_ms;
            }
            return;
        }
        if (time_reached(now_ms, s_auto_aim_target_seen_ms +
                         AUTO_AIM_TARGET_LOSS_MS)) {
            if (!auto_aim_begin_search(now_ms)) {
                auto_aim_fail();
            }
            return;
        }
        if (fresh_vision && auto_aim_vision_x_valid(true) &&
            (fabsf(yaw_feedback_angle_degrees() -
                   s_auto_aim_calibration_b_degrees) <=
             AUTO_AIM_CALIBRATION_REACHED_DEG)) {
            const float yaw_delta = yaw_feedback_angle_degrees() -
                                    s_auto_aim_calibration_a_degrees;
            s_auto_aim_calibration_x_b = (float)s_latest_vision.x;
            s_auto_aim_calibration_b_degrees =
                yaw_feedback_angle_degrees();
            if (fabsf(yaw_delta) < AUTO_AIM_CALIBRATION_OFFSET_DEG) {
                if (!auto_aim_begin_search(now_ms)) {
                    auto_aim_fail();
                }
                return;
            }
            s_auto_aim_slope_px_per_degree =
                (s_auto_aim_calibration_x_b -
                 s_auto_aim_calibration_x_a) / yaw_delta;
            if (!isfinite(s_auto_aim_slope_px_per_degree) ||
                (fabsf(s_auto_aim_slope_px_per_degree) <
                 AUTO_AIM_CALIBRATION_MIN_PX_PER_DEG)) {
                if (!auto_aim_begin_search(now_ms)) {
                    auto_aim_fail();
                }
                return;
            }
            s_auto_aim_state = AUTO_AIM_ALIGN;
        }
        return;
    }

    if (s_auto_aim_state == AUTO_AIM_ALIGN) {
        if (!fresh_vision || !auto_aim_vision_x_valid(true)) {
            return;
        }
        pixel_error = (float)s_latest_vision.x -
                      (float)s_latest_vision.frame_width * 0.5f;
        if (fabsf(pixel_error) > AUTO_AIM_X_TOLERANCE_PX) {
            float correction_degrees =
                -pixel_error / s_auto_aim_slope_px_per_degree;
            correction_degrees = limit_float(
                correction_degrees,
                -AUTO_AIM_YAW_CORRECTION_LIMIT_DEG,
                AUTO_AIM_YAW_CORRECTION_LIMIT_DEG);
            if (!auto_aim_set_yaw_target(
                    yaw_feedback_angle_degrees() + correction_degrees)) {
                auto_aim_fail();
            }
            return;
        }
        if (!auto_aim_set_fixed_target_pitch()) {
            return;
        }
        if (((s_latest_vision.flags & GIMBAL_LINK_VISION_AIM_STABLE) != 0U) &&
            auto_aim_axes_reached()) {
            s_auto_aim_state = AUTO_AIM_SETTLE;
            s_auto_aim_settle_started_ms = now_ms;
            s_auto_aim_settle_sequence = s_latest_vision.sequence;
        }
        return;
    }

    if (s_auto_aim_state == AUTO_AIM_SETTLE) {
        if (fresh_vision) {
            if (AUTO_AIM_SINGLE_PASS_LOCK != 0U) {
                if (!auto_aim_vision_x_valid(false)) {
                    return;
                }
                pixel_error = (float)s_latest_vision.x -
                              (float)s_latest_vision.frame_width * 0.5f;
                if (auto_aim_locked_target_should_release(pixel_error)) {
                    s_auto_aim_state = AUTO_AIM_SEARCH;
                    s_auto_aim_settle_started_ms = 0U;
                    s_auto_aim_coarse_locked = false;
                    s_auto_aim_release_count = 0U;
                    s_auto_aim_release_direction = 0;
                    if (!auto_aim_apply_relative_yaw_error(pixel_error,
                                                           now_ms)) {
                        auto_aim_fail();
                    }
                    return;
                }
                if (!auto_aim_set_fixed_target_pitch()) {
                    return;
                }
            } else {
                pixel_error = (float)s_latest_vision.x -
                              (float)s_latest_vision.frame_width * 0.5f;
                if (!auto_aim_vision_x_valid(true) ||
                    ((s_latest_vision.flags &
                      GIMBAL_LINK_VISION_AIM_STABLE) == 0U) ||
                    (fabsf(pixel_error) > AUTO_AIM_X_TOLERANCE_PX) ||
                    !auto_aim_set_fixed_target_pitch()) {
                    s_auto_aim_state = AUTO_AIM_ALIGN;
                    s_auto_aim_settle_started_ms = 0U;
                    return;
                }
            }
        }
        if (!auto_aim_axes_reached()) {
            if (AUTO_AIM_SINGLE_PASS_LOCK != 0U) {
                s_auto_aim_settle_started_ms = now_ms;
                s_auto_aim_settle_sequence =
                    s_latest_vision.sequence;
                return;
            }
            s_auto_aim_state = AUTO_AIM_ALIGN;
            s_auto_aim_settle_started_ms = 0U;
            return;
        }
#if CANNON_MANUAL_TEST_MODE == 0U
        if (time_reached(now_ms, s_auto_aim_settle_started_ms +
                         AUTO_AIM_SETTLE_MS) &&
            (s_latest_vision.sequence != s_auto_aim_settle_sequence) &&
            s_charge_cycle_complete &&
            !time_reached(now_ms + CANNON_FIRE_PULSE_MS,
                          s_auto_aim_deadline_ms + 1U)) {
            if (request_fire_pulse(now_ms) != CONTROL_SUBMIT_ACCEPTED) {
                auto_aim_fail();
                return;
            }
            s_task_auto_fire_pending = false;
            s_auto_aim_state = AUTO_AIM_FIRING;
            if (s_ui_started) {
                target_ui_set_feedback(&s_ui,
                                       TARGET_UI_FEEDBACK_FIRE_ACTIVE);
            }
        }
#endif
    }
}

static void fire_relay_step(uint32_t now_ms)
{
    if (s_fire_relay_active &&
        time_reached(now_ms, s_fire_relay_deadline_ms)) {
        abort_fire_pulse();
        if (s_ui_started) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_FIRE_COMPLETE);
        }
    }
}

static void charge_relay_step(uint32_t now_ms)
{
    uint32_t elapsed_ms;
#if CANNON_MANUAL_TEST_MODE == 0U
    uint32_t remaining_ms;
#endif

    if (!s_charge_relay_active || s_charge_cycle_complete) {
        return;
    }
    elapsed_ms = now_ms - s_charge_last_step_ms;
    s_charge_last_step_ms = now_ms;
#if CANNON_MANUAL_TEST_MODE != 0U
    if (elapsed_ms > (UINT32_MAX - s_charge_elapsed_ms)) {
        s_charge_elapsed_ms = UINT32_MAX;
    } else {
        s_charge_elapsed_ms += elapsed_ms;
    }
#else
    remaining_ms = CANNON_CHARGE_DURATION_MS - s_charge_elapsed_ms;
    if (elapsed_ms < remaining_ms) {
        s_charge_elapsed_ms += elapsed_ms;
        return;
    }

    s_charge_elapsed_ms = CANNON_CHARGE_DURATION_MS;
    s_charge_cycle_complete = true;
    s_charge_pause_started_ms = 0U;
    bsp_cannon_relay_set_charge(false);
    s_charge_relay_active = false;
    if (s_ui_started) {
        target_ui_set_charge_active(&s_ui, false);
        target_ui_set_feedback(&s_ui,
                               TARGET_UI_FEEDBACK_CHARGE_COMPLETE);
    }
#endif
}

static void position_auto_fire_step(uint32_t now_ms)
{
#if CANNON_MANUAL_TEST_MODE != 0U
    (void)now_ms;
    return;
#else
    /* Charge completion is the automatic fire gate for all three tasks. */
    if (!s_task_auto_fire_pending || !s_charge_cycle_complete ||
        s_fire_relay_active) {
        return;
    }
    if (request_fire_pulse(now_ms) == CONTROL_SUBMIT_ACCEPTED) {
        s_task_auto_fire_pending = false;
        if (auto_aim_active()) {
            s_auto_aim_state = AUTO_AIM_FIRING;
        }
        if (s_ui_started) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_FIRE_ACTIVE);
        }
    }
#endif
}

static void cannon_ui_timing_step(uint32_t now_ms)
{
    uint32_t fire_remaining_ms = 0U;
    uint32_t charge_seconds;
    uint8_t fire_remaining_tenths;
    target_ui_cannon_state_t state = TARGET_UI_CANNON_IDLE;

    if (!s_ui_started) {
        return;
    }
    if (s_fire_relay_active &&
        !time_reached(now_ms, s_fire_relay_deadline_ms)) {
        fire_remaining_ms = s_fire_relay_deadline_ms - now_ms;
    }
    fire_remaining_tenths =
        (uint8_t)((fire_remaining_ms + 99U) / 100U);

    if (s_fire_relay_active) {
        state = TARGET_UI_CANNON_FIRING;
    } else if (s_charge_relay_active) {
        state = TARGET_UI_CANNON_CHARGING;
    } else if (s_charge_cycle_started && !s_charge_cycle_complete) {
        state = TARGET_UI_CANNON_PAUSED;
    } else if (s_charge_cycle_complete &&
               (s_task_auto_fire_pending || auto_aim_active())) {
        state = TARGET_UI_CANNON_WAIT_AIM;
    } else if (s_charge_cycle_complete) {
        state = TARGET_UI_CANNON_READY;
    }
    charge_seconds = s_charge_elapsed_ms / 1000U;
    if (charge_seconds > (99U * 60U + 59U)) {
        charge_seconds = 99U * 60U + 59U;
    }
    target_ui_set_cannon_timing(
        &s_ui, (uint16_t)charge_seconds,
        fire_remaining_tenths, state);
}

static void capture_enable_zero_step(uint32_t now_ms)
{
    if (!s_enable_zero_pending) {
        return;
    }
    if (both_axes_enabled() &&
        s_qd4310_yaw_enable.feedback_valid &&
        s_qd4310_pitch_enable.feedback_valid &&
        (s_qd4310_yaw_enable.response_count !=
         s_yaw_enable_zero_response_baseline) &&
        (s_qd4310_pitch_enable.response_count !=
         s_pitch_enable_zero_response_baseline)) {
        s_yaw_zero_raw = s_qd4310_yaw_enable.last_angle_raw;
        s_pitch_zero_raw = PITCH_HORIZONTAL_ZERO_RAW;
        s_yaw_display_response_count =
            s_qd4310_yaw_enable.response_count;
        s_pitch_display_response_count =
            s_qd4310_pitch_enable.response_count;
        s_yaw_relative_degrees = 0;
        s_pitch_relative_degrees =
            rounded_degrees(pitch_feedback_angle_degrees());
        s_yaw_seen_response_count =
            s_qd4310_yaw_enable.response_count;
        s_yaw_feedback_deadline_ms = now_ms +
                                     YAW_SCAN_FEEDBACK_TIMEOUT_MS;
        s_yaw_scan_next_control_ms = now_ms;
        s_pitch_hold_current_a = 0.0f;
        s_pitch_seen_response_count =
            s_qd4310_pitch_enable.response_count;
        s_pitch_feedback_deadline_ms = now_ms +
                                       PITCH_HOLD_FEEDBACK_TIMEOUT_MS;
        s_pitch_control_next_ms = now_ms;
        (void)yaw_scan_controller_set_target(
            &s_yaw_scan, s_yaw_scan_target_degrees);
        (void)pitch_hold_controller_set_target(
            &s_pitch_hold, s_pitch_target_degrees);
        yaw_scan_controller_start(&s_yaw_scan);
        pitch_hold_controller_start(&s_pitch_hold);
        s_yaw_scan_min_degrees = 0.0f;
        s_yaw_scan_max_degrees = 0.0f;
        s_yaw_scan_peak_current_a = 0.0f;
        s_yaw_control_command_count = 0U;
        g_yaw_scan_trace_write_count = 0U;
        g_yaw_scan_trace_valid_count = 0U;
        s_enable_zero_valid = true;
        s_enable_zero_pending = false;
    } else if (qd4310_state_failed(s_qd4310_yaw_enable.state) ||
               qd4310_state_failed(s_qd4310_pitch_enable.state)) {
        s_enable_zero_valid = false;
        s_enable_zero_pending = false;
    }
}

static void enabled_angle_monitor_step(uint32_t now_ms)
{
    bool angle_changed = false;

    if (!s_enable_zero_valid || !both_axes_enabled()) {
        return;
    }

    if (s_qd4310_yaw_enable.response_count !=
        s_yaw_display_response_count) {
        s_yaw_display_response_count =
            s_qd4310_yaw_enable.response_count;
        s_yaw_relative_degrees =
            rounded_degrees(yaw_feedback_angle_degrees());
        angle_changed = true;
    }
    if (s_qd4310_pitch_enable.response_count !=
        s_pitch_display_response_count) {
        s_pitch_display_response_count =
            s_qd4310_pitch_enable.response_count;
        s_pitch_relative_degrees =
            rounded_degrees(pitch_feedback_angle_degrees());
        angle_changed = true;
    }
    if (angle_changed && s_ui_started) {
        target_ui_set_runtime(&s_ui, s_ui.runtime_status,
                              s_yaw_relative_degrees,
                              s_pitch_relative_degrees);
    }

    if (!s_yaw_scan.active && !s_yaw_scan_start_pending &&
        time_reached(now_ms, s_angle_monitor_next_ms)) {
        s_angle_monitor_next_ms = now_ms +
                                  QD4310_ANGLE_MONITOR_PERIOD_MS;
        (void)qd4310_set_test_current(QD4310_YAW_MOTOR_ID, 0.0f);
        if (!s_pitch_hold.active) {
            (void)qd4310_set_test_current(QD4310_PITCH_MOTOR_ID, 0.0f);
        }
    }
}

static void pitch_hold_control_step(uint32_t now_ms)
{
    float angle_degrees;
    float speed_rpm;

    if (!s_pitch_hold.active) {
        return;
    }
    if (!both_axes_enabled()) {
        stop_yaw_scan_on_fault(1U);
        return;
    }
    if (s_qd4310_pitch_enable.response_count !=
        s_pitch_seen_response_count) {
        s_pitch_seen_response_count = s_qd4310_pitch_enable.response_count;
        s_pitch_feedback_deadline_ms = now_ms +
                                       PITCH_HOLD_FEEDBACK_TIMEOUT_MS;
        s_pitch_relative_degrees =
            rounded_degrees(pitch_feedback_angle_degrees());
        if (s_ui_started) {
            target_ui_set_runtime(&s_ui, s_ui.runtime_status,
                                  s_yaw_relative_degrees,
                                  s_pitch_relative_degrees);
        }
    } else if (time_reached(now_ms, s_pitch_feedback_deadline_ms)) {
        stop_yaw_scan_on_fault(6U);
        return;
    }
    if (!time_reached(now_ms, s_pitch_control_next_ms)) {
        return;
    }
    s_pitch_control_next_ms = now_ms + YAW_SCAN_CONTROL_PERIOD_MS;
    angle_degrees = pitch_feedback_angle_degrees();
    speed_rpm = pitch_feedback_speed_rpm();
    if (!pitch_hold_controller_update(&s_pitch_hold,
                                      angle_degrees,
                                      speed_rpm,
                                      &s_pitch_hold_current_a)) {
        stop_yaw_scan_on_fault(7U);
        return;
    }
    if (!qd4310_set_test_current(QD4310_PITCH_MOTOR_ID,
                                 s_pitch_hold_current_a)) {
        stop_yaw_scan_on_fault(8U);
    }
}

static void yaw_scan_control_step(uint32_t now_ms)
{
    float angle_degrees;
    float speed_rpm;

    if (!s_yaw_scan.active) {
        return;
    }
    if (!both_axes_enabled()) {
        stop_yaw_scan_on_fault(1U);
        return;
    }

    if (s_qd4310_yaw_enable.response_count !=
        s_yaw_seen_response_count) {
        s_yaw_seen_response_count = s_qd4310_yaw_enable.response_count;
        s_yaw_feedback_deadline_ms = now_ms +
                                     YAW_SCAN_FEEDBACK_TIMEOUT_MS;
        angle_degrees = yaw_feedback_angle_degrees();
        if (angle_degrees < s_yaw_scan_min_degrees) {
            s_yaw_scan_min_degrees = angle_degrees;
        }
        if (angle_degrees > s_yaw_scan_max_degrees) {
            s_yaw_scan_max_degrees = angle_degrees;
        }
        s_yaw_relative_degrees = rounded_degrees(angle_degrees);
    } else if (time_reached(now_ms, s_yaw_feedback_deadline_ms)) {
        stop_yaw_scan_on_fault(2U);
        return;
    }

    if (s_ui_started) {
        target_ui_set_runtime(&s_ui, TARGET_UI_RUNNING,
                              s_yaw_relative_degrees,
                              s_pitch_relative_degrees);
    }

    if (!time_reached(now_ms, s_yaw_scan_next_control_ms)) {
        return;
    }
    s_yaw_scan_next_control_ms = now_ms + YAW_SCAN_CONTROL_PERIOD_MS;
    angle_degrees = yaw_feedback_angle_degrees();
    speed_rpm = yaw_feedback_speed_rpm();
    if (!yaw_scan_controller_update(&s_yaw_scan,
                                    angle_degrees,
                                    speed_rpm,
                                    &s_yaw_scan_current_a,
                                    &s_yaw_scan_target_degrees)) {
        stop_yaw_scan_on_fault(4U);
        return;
    }
    if (fabsf(s_yaw_scan_current_a) > s_yaw_scan_peak_current_a) {
        s_yaw_scan_peak_current_a = fabsf(s_yaw_scan_current_a);
    }
    record_yaw_scan_trace(now_ms, angle_degrees, speed_rpm);
    if (!qd4310_set_test_current(QD4310_YAW_MOTOR_ID,
                                 s_yaw_scan_current_a)) {
        stop_yaw_scan_on_fault(5U);
        return;
    }
    ++s_yaw_control_command_count;
}

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

static bool canvas_draw_theme(void *context, uint8_t theme_index)
{
    (void)context;
    if (theme_index >= TFT_THEME_COUNT) {
        return false;
    }
    return ili9341_blit_rgb565(0U, 0U, TFT_THEME_WIDTH,
                               TFT_THEME_HEIGHT,
                               g_tft_themes[theme_index]);
}

static const ui_canvas_t s_canvas = {
    .context = NULL,
    .fill_rect = canvas_fill_rect,
    .draw_text = canvas_draw_text,
    .draw_theme = canvas_draw_theme
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

    bsp_uart_bridge_init();
    gimbal_link_decoder_init(&s_gimbal_link_decoder);
    esp8266_init(&s_esp8266, now_ms);
    s_gimbal_telemetry_deadline_ms = now_ms +
                                     GIMBAL_TELEMETRY_PERIOD_MS;
    bsp_lcd_fsmc_init();
    qd4310_enable_test_init(&s_qd4310_yaw_enable,
                            QD4310_YAW_MOTOR_ID);
    qd4310_enable_test_init(&s_qd4310_pitch_enable,
                            QD4310_PITCH_MOTOR_ID);
    if (!bsp_can1_init()) {
        s_qd4310_yaw_enable.state = QD4310_ENABLE_CAN_ERROR;
        s_qd4310_pitch_enable.state = QD4310_ENABLE_CAN_ERROR;
    } else {
        (void)request_user_enable(now_ms);
    }
    bsp_xpt2046_io_init();
    xpt2046_init(&s_touch, &calibration, now_ms);
    ili9341_begin(&s_display, now_ms);
    s_hardware_started = true;
}

static void publish_ui_command(const app_command_t *command)
{
    if (command != NULL) {
        if ((command->type == APP_COMMAND_STOP) ||
            (command->type == APP_COMMAND_DISABLE)) {
            auto_aim_cancel();
            abort_fire_pulse();
            abort_charge();
        } else if (command->type == APP_COMMAND_FIRE_ABORT) {
            auto_aim_cancel();
            abort_fire_pulse();
        } else if (command->type == APP_COMMAND_CHARGE_ABORT) {
            abort_charge();
        }
    }
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

static void dispatch_one_command(uint32_t now_ms)
{
    app_command_t command;
    control_submit_result_t result;

    if (!app_command_bus_take(&s_command_bus, &command)) {
        return;
    }

    if (auto_aim_active() &&
        (command.type != APP_COMMAND_STOP) &&
        (command.type != APP_COMMAND_DISABLE) &&
        (command.type != APP_COMMAND_FIRE_ABORT) &&
        (command.type != APP_COMMAND_CHARGE_START) &&
        (command.type != APP_COMMAND_CHARGE_ABORT) &&
        ((CANNON_MANUAL_TEST_MODE == 0U) ||
         (command.type != APP_COMMAND_FIRE_PULSE))) {
        target_ui_set_feedback(&s_ui,
                               TARGET_UI_FEEDBACK_VISION_TARGET_INVALID);
        return;
    }

    if (command.type == APP_COMMAND_START) {
        result = request_user_enable(now_ms) ?
                 CONTROL_SUBMIT_ACCEPTED : CONTROL_SUBMIT_BUSY;
    } else if (command.type == APP_COMMAND_SWEEP) {
        if (GIMBAL_SCAN_ENABLED == 0U) {
            target_ui_set_feedback(
                &s_ui, TARGET_UI_FEEDBACK_ACTION_UNAVAILABLE);
            return;
        }
        if (!request_user_scan()) {
            target_ui_set_feedback(
                &s_ui, TARGET_UI_FEEDBACK_SCAN_REQUIRES_ENABLE);
            return;
        }
        result = CONTROL_SUBMIT_ACCEPTED;
    } else if (command.type == APP_COMMAND_FIRE_DISTANCE) {
        if (s_fire_relay_active) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_FIRE_ACTIVE);
            return;
        }
        if ((command.distance_cm < DISTANCE_INPUT_MIN_CM) ||
            (command.distance_cm > DISTANCE_INPUT_MAX_CM)) {
            target_ui_set_feedback(
                &s_ui, TARGET_UI_FEEDBACK_VISION_DISTANCE_INVALID);
            return;
        }
        result = request_distance_aim_cm((float)command.distance_cm);
#if CANNON_MANUAL_TEST_MODE == 0U
        if (result == CONTROL_SUBMIT_ACCEPTED) {
            s_task_auto_fire_pending = true;
            begin_new_charge_cycle(now_ms);
        }
#endif
    } else if (command.type == APP_COMMAND_FIXED_TARGET) {
        const uint16_t distance_mm = s_latest_vision.distance_mm;
        if (s_fire_relay_active) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_FIRE_ACTIVE);
            return;
        }
        if (!s_vision_online ||
            ((s_latest_vision.flags &
              GIMBAL_LINK_VISION_DISTANCE_VALID) == 0U) ||
            (distance_mm < DISTANCE_INPUT_MIN_CM * 10U) ||
            (distance_mm > DISTANCE_INPUT_MAX_CM * 10U)) {
            target_ui_set_feedback(
                &s_ui, TARGET_UI_FEEDBACK_VISION_DISTANCE_INVALID);
            return;
        }
        result = request_fixed_target_aim_cm(
            (float)distance_mm / 10.0f);
#if CANNON_MANUAL_TEST_MODE == 0U
        if (result == CONTROL_SUBMIT_ACCEPTED) {
            s_task_auto_fire_pending = true;
            begin_new_charge_cycle(now_ms);
        }
#endif
    } else if (command.type == APP_COMMAND_AUTO_SEARCH) {
        result = request_auto_aim(now_ms);
    } else if (command.type == APP_COMMAND_SET_YAW_TARGET) {
        if (!yaw_scan_controller_set_target(
                &s_yaw_scan, (float)command.angle_degrees)) {
            result = CONTROL_SUBMIT_BUSY;
        } else {
            s_yaw_scan_target_degrees = (float)command.angle_degrees;
            (void)target_ui_set_targets(
                &s_ui, command.angle_degrees,
                (int16_t)s_pitch_target_degrees);
            result = CONTROL_SUBMIT_ACCEPTED;
        }
    } else if (command.type == APP_COMMAND_SET_PITCH_TARGET) {
        if (!pitch_hold_controller_set_target(
                &s_pitch_hold, (float)command.angle_degrees)) {
            result = CONTROL_SUBMIT_BUSY;
        } else {
            s_pitch_target_degrees = (float)command.angle_degrees;
            (void)target_ui_set_targets(
                &s_ui, (int16_t)s_yaw_scan_target_degrees,
                command.angle_degrees);
            result = CONTROL_SUBMIT_ACCEPTED;
        }
    } else if (command.type == APP_COMMAND_STOP) {
        auto_aim_cancel();
        abort_fire_pulse();
        abort_charge();
        result = request_user_stop() ?
                 CONTROL_SUBMIT_ACCEPTED : CONTROL_SUBMIT_BUSY;
    } else if (command.type == APP_COMMAND_DISABLE) {
        auto_aim_cancel();
        abort_fire_pulse();
        abort_charge();
        result = request_user_disable(now_ms) ?
                 CONTROL_SUBMIT_ACCEPTED : CONTROL_SUBMIT_BUSY;
    } else if (command.type == APP_COMMAND_FIRE_PULSE) {
#if CANNON_MANUAL_TEST_MODE != 0U
        auto_aim_cancel();
        result = request_fire_pulse(now_ms);
#else
        if (!s_charge_cycle_complete) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_CHARGE_ACTIVE);
            return;
        }
        result = auto_aim_active() ? CONTROL_SUBMIT_BUSY :
                 request_fire_pulse(now_ms);
        if (result == CONTROL_SUBMIT_ACCEPTED) {
            s_task_auto_fire_pending = false;
        }
#endif
    } else if (command.type == APP_COMMAND_FIRE_ABORT) {
        auto_aim_cancel();
        abort_fire_pulse();
        result = CONTROL_SUBMIT_ACCEPTED;
    } else if (command.type == APP_COMMAND_CHARGE_START) {
        result = start_charge(now_ms);
    } else if (command.type == APP_COMMAND_CHARGE_ABORT) {
        abort_charge();
        result = CONTROL_SUBMIT_ACCEPTED;
    } else {
        result = control_port_submit(&command);
    }
    if (result == CONTROL_SUBMIT_ACCEPTED) {
        if (command.type == APP_COMMAND_FIRE_PULSE) {
            target_ui_set_feedback(&s_ui, TARGET_UI_FEEDBACK_FIRE_ACTIVE);
        } else if (command.type == APP_COMMAND_FIRE_ABORT) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_FIRE_COMPLETE);
        } else if (command.type == APP_COMMAND_CHARGE_START) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_CHARGE_ACTIVE);
        } else if (command.type == APP_COMMAND_CHARGE_ABORT) {
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_CHARGE_COMPLETE);
        } else {
            if ((command.type == APP_COMMAND_FIRE_DISTANCE) ||
                (command.type == APP_COMMAND_FIXED_TARGET) ||
                (command.type == APP_COMMAND_AUTO_SEARCH)) {
                target_ui_open_fire_control(&s_ui);
            }
            target_ui_set_feedback(&s_ui,
                                   TARGET_UI_FEEDBACK_COMMAND_SENT);
        }
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
    yaw_scan_controller_init(&s_yaw_scan);
    pitch_hold_controller_init(&s_pitch_hold);
    s_hardware_started = false;
    s_ui_started = false;
    s_clock_failed = false;
    s_reported_yaw_state = QD4310_ENABLE_IDLE;
    s_reported_pitch_state = QD4310_ENABLE_IDLE;
    s_reported_yaw_expected_enabled = false;
    s_reported_pitch_expected_enabled = false;
    s_reported_scan_active = false;
    s_reported_scan_fault = false;
    s_reported_user_stopped = false;
    s_yaw_scan_start_pending = false;
    s_enable_zero_pending = false;
    s_enable_zero_valid = false;
    s_user_stopped = false;
    s_yaw_scan_fault = false;
    s_yaw_direction_check_pending = false;
    s_yaw_scan_fault_code = 0U;
    s_yaw_zero_raw = 0U;
    s_pitch_zero_raw = 0U;
    s_yaw_enable_zero_response_baseline = 0U;
    s_pitch_enable_zero_response_baseline = 0U;
    s_yaw_display_response_count = 0U;
    s_pitch_display_response_count = 0U;
    s_angle_monitor_next_ms = 0U;
    s_yaw_scan_next_control_ms = 0U;
    s_yaw_feedback_deadline_ms = 0U;
    s_yaw_seen_response_count = 0U;
    s_yaw_relative_degrees = 0;
    s_pitch_feedback_deadline_ms = 0U;
    s_pitch_seen_response_count = 0U;
    s_pitch_relative_degrees = 0;
    s_yaw_scan_target_degrees = 0.0f;
    s_yaw_scan_current_a = 0.0f;
    s_pitch_target_degrees = 0.0f;
    s_pitch_hold_current_a = 0.0f;
    s_pitch_control_next_ms = 0U;
    s_yaw_scan_min_degrees = 0.0f;
    s_yaw_scan_max_degrees = 0.0f;
    s_yaw_scan_peak_current_a = 0.0f;
    s_yaw_control_command_count = 0U;
    s_gimbal_telemetry_sequence = 0U;
    s_gimbal_telemetry_deadline_ms = 0U;
    s_gimbal_debug_pending = false;
    s_latest_vision.sequence = 0U;
    s_latest_vision.x = -1;
    s_latest_vision.distance_mm = GIMBAL_LINK_VISION_DISTANCE_INVALID;
    s_latest_vision.frame_width = 0U;
    s_latest_vision.flags = 0U;
    s_vision_last_rx_ms = 0U;
    s_vision_online = false;
    s_fire_relay_active = false;
    s_fire_relay_deadline_ms = 0U;
    s_charge_relay_active = false;
    s_charge_cycle_started = false;
    s_charge_cycle_complete = false;
    s_charge_elapsed_ms = 0U;
    s_charge_last_step_ms = 0U;
    s_charge_pause_started_ms = 0U;
    s_task_auto_fire_pending = false;
    s_auto_aim_state = AUTO_AIM_IDLE;
    s_auto_aim_deadline_ms = 0U;
    s_auto_aim_target_seen_ms = 0U;
    s_auto_aim_settle_started_ms = 0U;
    s_auto_aim_vision_sequence = 0U;
    s_auto_aim_settle_sequence = 0U;
    s_auto_aim_search_direction = 0;
    s_auto_aim_coarse_locked = false;
    s_auto_aim_release_count = 0U;
    s_auto_aim_release_direction = 0;
    s_auto_aim_motion_arrived = false;
    s_auto_aim_motion_last_ms = 0U;
    s_auto_aim_coarse_yaw_degrees = 0.0f;
    s_auto_aim_calibration_a_degrees = 0.0f;
    s_auto_aim_calibration_b_degrees = 0.0f;
    s_auto_aim_calibration_x_a = 0.0f;
    s_auto_aim_calibration_x_b = 0.0f;
    s_auto_aim_slope_px_per_degree = 0.0f;
    s_gimbal_ack_read = 0U;
    s_gimbal_ack_write = 0U;
    s_gimbal_ack_count = 0U;
    g_yaw_scan_trace_write_count = 0U;
    g_yaw_scan_trace_valid_count = 0U;
}

void lcd_demo_app_step(uint32_t now_ms)
{
    xpt2046_sample_t touch_sample;
    ui_touch_t ui_touch;
    app_command_t command;

    fire_relay_step(now_ms);
    charge_relay_step(now_ms);

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

    {
        bsp_can1_frame_t frame;
        while (bsp_can1_receive(&frame)) {
            qd4310_enable_test_accept(&s_qd4310_yaw_enable,
                                      frame.standard_id,
                                      frame.length,
                                      frame.data);
            qd4310_enable_test_accept(&s_qd4310_pitch_enable,
                                      frame.standard_id,
                                      frame.length,
                                      frame.data);
        }
    }
    qd4310_enable_test_step(&s_qd4310_yaw_enable, now_ms);
    qd4310_enable_test_step(&s_qd4310_pitch_enable, now_ms);
    capture_enable_zero_step(now_ms);
    enabled_angle_monitor_step(now_ms);
    pitch_hold_control_step(now_ms);
    yaw_scan_control_step(now_ms);
    gimbal_link_poll(now_ms);

    ili9341_poll(&s_display, now_ms);
    if (ili9341_status(&s_display) != ILI9341_READY) {
        return;
    }

    if (!s_ui_started) {
        target_ui_init(&s_ui);
        s_reported_yaw_state = QD4310_ENABLE_IDLE;
        s_reported_pitch_state = QD4310_ENABLE_IDLE;
        s_ui_started = true;
        report_qd4310_state();
        target_ui_render(&s_ui, &s_canvas);
        ili9341_set_backlight(true);
        return;
    }

    report_qd4310_state();

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

    dispatch_one_command(now_ms);
    position_auto_fire_step(now_ms);
    auto_aim_step(now_ms);
    cannon_ui_timing_step(now_ms);
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
