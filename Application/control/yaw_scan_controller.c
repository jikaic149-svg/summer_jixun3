#include "yaw_scan_controller.h"

#include <math.h>
#include <stddef.h>

#include "lcd_demo_config.h"

static float limit_symmetric(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float apply_deadband(float error)
{
    if (error > YAW_SCAN_DEADBAND_DEG) {
        return error - YAW_SCAN_DEADBAND_DEG;
    }
    if (error < -YAW_SCAN_DEADBAND_DEG) {
        return error + YAW_SCAN_DEADBAND_DEG;
    }
    return 0.0f;
}

void yaw_scan_controller_init(yaw_scan_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = false;
    controller->endpoint_holding = false;
    controller->direction = 0;
    controller->endpoint_hold_ms = 0U;
    controller->target_degrees = 0.0f;
    controller->integral_current_a = 0.0f;
}

void yaw_scan_controller_start(yaw_scan_controller_t *controller)
{
    if (controller != NULL) {
        controller->active = true;
        controller->endpoint_holding = false;
        controller->direction = 0;
        controller->endpoint_hold_ms = 0U;
        controller->integral_current_a = 0.0f;
    }
}

bool yaw_scan_controller_set_target(yaw_scan_controller_t *controller,
                                    float target_degrees)
{
    if ((controller == NULL) || !isfinite(target_degrees) ||
        (fabsf(target_degrees) > YAW_SCAN_TARGET_LIMIT_DEG)) {
        return false;
    }
    if (controller->target_degrees != target_degrees) {
        controller->target_degrees = target_degrees;
        controller->integral_current_a = 0.0f;
    }
    return true;
}

bool yaw_scan_controller_track_target(yaw_scan_controller_t *controller,
                                      float target_degrees)
{
    if ((controller == NULL) || !isfinite(target_degrees) ||
        (fabsf(target_degrees) > YAW_SCAN_TARGET_LIMIT_DEG)) {
        return false;
    }
    controller->target_degrees = target_degrees;
    return true;
}

void yaw_scan_controller_stop(yaw_scan_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = false;
    controller->integral_current_a = 0.0f;
}

bool yaw_scan_controller_update(yaw_scan_controller_t *controller,
                                float angle_degrees,
                                float speed_rpm,
                                float *current_a,
                                float *target_degrees)
{
    const float dt_seconds = (float)YAW_SCAN_CONTROL_PERIOD_MS / 1000.0f;
    float original_error;
    float controlled_error;
    float effort;

    if ((controller == NULL) || (current_a == NULL) ||
        (target_degrees == NULL) || !controller->active ||
        !isfinite(angle_degrees) || !isfinite(speed_rpm)) {
        return false;
    }
    if (fabsf(angle_degrees) > YAW_SCAN_HARD_LIMIT_DEG) {
        *current_a = 0.0f;
        *target_degrees = controller->target_degrees;
        return false;
    }

    original_error = controller->target_degrees - angle_degrees;
    controlled_error = apply_deadband(limit_symmetric(
        original_error, YAW_SCAN_ERROR_LIMIT_DEG));

    if (fabsf(original_error) <= YAW_SCAN_INTEGRAL_ZONE_DEG) {
        controller->integral_current_a +=
            YAW_SCAN_KI_A_PER_DEG_S * original_error * dt_seconds;
        controller->integral_current_a = limit_symmetric(
            controller->integral_current_a, YAW_SCAN_INTEGRAL_LIMIT_A);
    } else {
        controller->integral_current_a = 0.0f;
    }

    effort = YAW_SCAN_KP_A_PER_DEG * controlled_error +
             controller->integral_current_a -
             YAW_SCAN_KD_A_PER_RPM * speed_rpm;
    effort = limit_symmetric(effort, YAW_SCAN_CURRENT_LIMIT_A);

    if (((angle_degrees >= YAW_SCAN_HARD_LIMIT_DEG) && (effort > 0.0f)) ||
        ((angle_degrees <= -YAW_SCAN_HARD_LIMIT_DEG) && (effort < 0.0f))) {
        effort = 0.0f;
        controller->integral_current_a = 0.0f;
    }

    *current_a = effort * YAW_SCAN_CURRENT_POLARITY;
    *target_degrees = controller->target_degrees;
    return true;
}
