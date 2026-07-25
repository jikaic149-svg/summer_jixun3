#include "pitch_hold_controller.h"

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
    if (error > PITCH_HOLD_DEADBAND_DEG) {
        return error - PITCH_HOLD_DEADBAND_DEG;
    }
    if (error < -PITCH_HOLD_DEADBAND_DEG) {
        return error + PITCH_HOLD_DEADBAND_DEG;
    }
    return 0.0f;
}

static void reset_runtime_state(pitch_hold_controller_t *controller)
{
    controller->integral_current_a = 0.0f;
}

void pitch_hold_controller_init(pitch_hold_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = false;
    controller->target_degrees = 0.0f;
    reset_runtime_state(controller);
}

void pitch_hold_controller_start(pitch_hold_controller_t *controller)
{
    if (controller != NULL) {
        controller->active = true;
        reset_runtime_state(controller);
    }
}

bool pitch_hold_controller_set_target(pitch_hold_controller_t *controller,
                                      float target_degrees)
{
    if ((controller == NULL) || !isfinite(target_degrees) ||
        (fabsf(target_degrees) > PITCH_TARGET_LIMIT_DEG)) {
        return false;
    }
    if (controller->target_degrees != target_degrees) {
        controller->target_degrees = target_degrees;
        controller->integral_current_a = 0.0f;
    }
    return true;
}

void pitch_hold_controller_stop(pitch_hold_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = false;
    reset_runtime_state(controller);
}

bool pitch_hold_controller_update(pitch_hold_controller_t *controller,
                                  float angle_degrees,
                                  float speed_rpm,
                                  float *current_a)
{
    const float dt_seconds = (float)YAW_SCAN_CONTROL_PERIOD_MS / 1000.0f;
    float original_error;
    float controlled_error;
    float effort;

    if ((controller == NULL) || (current_a == NULL) ||
        !controller->active || !isfinite(angle_degrees) ||
        !isfinite(speed_rpm)) {
        return false;
    }
    if (fabsf(angle_degrees) > PITCH_HOLD_HARD_LIMIT_DEG) {
        *current_a = 0.0f;
        return false;
    }

    original_error = controller->target_degrees - angle_degrees;
    controlled_error = apply_deadband(limit_symmetric(
        original_error, PITCH_HOLD_ERROR_LIMIT_DEG));
    if (fabsf(original_error) <= PITCH_HOLD_INTEGRAL_ZONE_DEG) {
        controller->integral_current_a +=
            PITCH_HOLD_KI_A_PER_DEG_S * original_error * dt_seconds;
        controller->integral_current_a = limit_symmetric(
            controller->integral_current_a, PITCH_HOLD_INTEGRAL_LIMIT_A);
    } else {
        controller->integral_current_a = 0.0f;
    }

    effort = PITCH_HOLD_KP_A_PER_DEG * controlled_error +
             controller->integral_current_a -
             PITCH_HOLD_KD_A_PER_RPM * speed_rpm;
    effort = limit_symmetric(effort, PITCH_HOLD_CURRENT_LIMIT_A);
    if (((angle_degrees >= PITCH_HOLD_HARD_LIMIT_DEG) &&
         (effort > 0.0f)) ||
        ((angle_degrees <= -PITCH_HOLD_HARD_LIMIT_DEG) &&
         (effort < 0.0f))) {
        effort = 0.0f;
        controller->integral_current_a = 0.0f;
    }

    *current_a = effort * PITCH_HOLD_CURRENT_POLARITY;
    return true;
}
