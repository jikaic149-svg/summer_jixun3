#include "balance_control_helpers.h"

static float balance_helpers_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float balance_helpers_limit_abs(float value, float limit)
{
    float abs_limit = balance_helpers_absf(limit);

    if (value > abs_limit) {
        return abs_limit;
    }
    if (value < -abs_limit) {
        return -abs_limit;
    }
    return value;
}

static float balance_helpers_step_towards(float value,
                                          float target,
                                          float step)
{
    float abs_step = balance_helpers_absf(step);

    if (value < target) {
        value += abs_step;
        return (value > target) ? target : value;
    }
    if (value > target) {
        value -= abs_step;
        return (value < target) ? target : value;
    }
    return value;
}

uint8_t BalanceControl_StartExitConfirmed(float measured_velocity_mps,
                                          float target_velocity_mps,
                                          float exit_ratio,
                                          float exit_max_mps,
                                          float dt_sec,
                                          float confirm_sec,
                                          float *confirm_timer_sec)
{
    float abs_target_mps;
    float direction_velocity_mps;
    float exit_velocity_mps;

    if (confirm_timer_sec == 0) {
        return 0U;
    }

    abs_target_mps = balance_helpers_absf(target_velocity_mps);
    if (abs_target_mps <= 0.001f) {
        *confirm_timer_sec = 0.0f;
        return 0U;
    }

    if (exit_ratio < 0.0f) {
        exit_ratio = 0.0f;
    } else if (exit_ratio > 1.0f) {
        exit_ratio = 1.0f;
    }
    exit_velocity_mps = abs_target_mps * exit_ratio;
    if ((exit_max_mps > 0.0f) &&
        (exit_velocity_mps > exit_max_mps)) {
        exit_velocity_mps = exit_max_mps;
    }

    direction_velocity_mps =
        (target_velocity_mps >= 0.0f) ?
        measured_velocity_mps : -measured_velocity_mps;
    if (direction_velocity_mps < exit_velocity_mps) {
        *confirm_timer_sec = 0.0f;
        return 0U;
    }

    if (confirm_sec <= 0.0f) {
        *confirm_timer_sec = 0.0f;
        return 1U;
    }
    if (dt_sec > 0.0f) {
        if (*confirm_timer_sec >= (confirm_sec - dt_sec)) {
            *confirm_timer_sec = confirm_sec;
        } else {
            *confirm_timer_sec += dt_sec;
        }
    }
    return (*confirm_timer_sec >= confirm_sec) ? 1U : 0U;
}

BalanceControlIntegralAction_e BalanceControl_SelectIntegralAction(
    uint8_t safety_decay_required,
    uint8_t integration_eligible)
{
    if (safety_decay_required != 0U) {
        return BALANCE_CONTROL_INTEGRAL_DECAY;
    }
    if (integration_eligible != 0U) {
        return BALANCE_CONTROL_INTEGRAL_INTEGRATE;
    }
    return BALANCE_CONTROL_INTEGRAL_HOLD;
}

float BalanceControl_ApplyIntegralAction(
    float integral,
    float error,
    float dt_sec,
    float integral_limit,
    float decay_per_sec,
    BalanceControlIntegralAction_e action)
{
    if (dt_sec <= 0.0f) {
        return integral;
    }

    if (action == BALANCE_CONTROL_INTEGRAL_INTEGRATE) {
        integral += error * dt_sec;
        return balance_helpers_limit_abs(integral, integral_limit);
    }
    if (action == BALANCE_CONTROL_INTEGRAL_DECAY) {
        return balance_helpers_step_towards(
            integral, 0.0f,
            balance_helpers_absf(decay_per_sec) * dt_sec);
    }
    return integral;
}

float BalanceControl_ApplyContinuousDeadband(float value, float deadband)
{
    float abs_deadband = balance_helpers_absf(deadband);

    if (value > abs_deadband) {
        return value - abs_deadband;
    }
    if (value < -abs_deadband) {
        return value + abs_deadband;
    }
    return 0.0f;
}

uint8_t BalanceControl_QuietConfirmed(uint8_t quiet,
                                      float dt_sec,
                                      float confirm_sec,
                                      float *confirm_timer_sec)
{
    if (confirm_timer_sec == 0) {
        return 0U;
    }
    if (quiet == 0U) {
        *confirm_timer_sec = 0.0f;
        return 0U;
    }
    if (confirm_sec <= 0.0f) {
        *confirm_timer_sec = 0.0f;
        return 1U;
    }
    if (dt_sec > 0.0f) {
        if (*confirm_timer_sec >= (confirm_sec - dt_sec)) {
            *confirm_timer_sec = confirm_sec;
        } else {
            *confirm_timer_sec += dt_sec;
        }
    }
    return (*confirm_timer_sec >= confirm_sec) ? 1U : 0U;
}

float BalanceControl_UpdateActualBaseBlend(float blend,
                                           float dt_sec,
                                           float blend_per_sec,
                                           float maximum_blend,
                                           uint8_t running)
{
    float target_blend = balance_helpers_absf(maximum_blend);

    if (running == 0U) {
        return 0.0f;
    }
    if (target_blend > 1.0f) {
        target_blend = 1.0f;
    }
    if (blend < 0.0f) {
        blend = 0.0f;
    } else if (blend > target_blend) {
        blend = target_blend;
    }
    if (dt_sec <= 0.0f) {
        return blend;
    }
    return balance_helpers_step_towards(
        blend, target_blend,
        balance_helpers_absf(blend_per_sec) * dt_sec);
}

float BalanceControl_UpdateLowPass(float filtered,
                                   float input,
                                   float dt_sec,
                                   float tau_sec)
{
    float alpha;

    if (dt_sec <= 0.0f) {
        return filtered;
    }
    if (tau_sec <= 0.0f) {
        return input;
    }

    alpha = dt_sec / (tau_sec + dt_sec);
    return filtered + alpha * (input - filtered);
}

float BalanceControl_TransferAssistToIntegral(float integral,
                                              float assist_before_deg,
                                              float assist_after_deg,
                                              float ki_deg_per_m_s,
                                              float integral_limit_m_s)
{
    if (balance_helpers_absf(ki_deg_per_m_s) > 0.000001f) {
        integral += (assist_before_deg - assist_after_deg) /
                    ki_deg_per_m_s;
    }

    return balance_helpers_limit_abs(integral, integral_limit_m_s);
}

float BalanceControl_TransferAssistBumpless(
    float integral_before,
    float assist_before_deg,
    float assist_requested_after_deg,
    float ki_deg_per_m_s,
    float integral_limit_m_s,
    uint8_t preserve_total,
    float *integral_after)
{
    float integral = integral_before;
    float assist_after_deg = assist_requested_after_deg;

    if (preserve_total != 0U) {
        integral = BalanceControl_TransferAssistToIntegral(
            integral_before,
            assist_before_deg,
            assist_requested_after_deg,
            ki_deg_per_m_s,
            integral_limit_m_s);
        assist_after_deg = assist_before_deg -
                           ki_deg_per_m_s *
                           (integral - integral_before);
    }
    if (integral_after != 0) {
        *integral_after = integral;
    }
    return assist_after_deg;
}

float BalanceControl_SeedAssistFromIntegral(float assist_deg,
                                            float integral,
                                            float ki_deg_per_m_s,
                                            float drive_sign,
                                            float assist_limit_deg)
{
    float direction = (drive_sign >= 0.0f) ? 1.0f : -1.0f;
    float directed_assist = direction *
                            (assist_deg + ki_deg_per_m_s * integral);

    if (directed_assist < 0.0f) {
        directed_assist = 0.0f;
    }
    if (directed_assist > balance_helpers_absf(assist_limit_deg)) {
        directed_assist = balance_helpers_absf(assist_limit_deg);
    }
    return direction * directed_assist;
}

uint8_t BalanceControl_StopReleaseConfirmed(float signed_velocity_mps,
                                            float dt_sec,
                                            float release_velocity_mps,
                                            float confirm_sec,
                                            float *confirm_timer_sec)
{
    if (confirm_timer_sec == 0) {
        return 0U;
    }
    if (balance_helpers_absf(signed_velocity_mps) >
        balance_helpers_absf(release_velocity_mps)) {
        *confirm_timer_sec = 0.0f;
        return 0U;
    }
    if (confirm_sec <= 0.0f) {
        *confirm_timer_sec = 0.0f;
        return 1U;
    }
    if (dt_sec > 0.0f) {
        if (*confirm_timer_sec >= (confirm_sec - dt_sec)) {
            *confirm_timer_sec = confirm_sec;
        } else {
            *confirm_timer_sec += dt_sec;
        }
    }
    return (*confirm_timer_sec >= confirm_sec) ? 1U : 0U;
}

uint8_t BalanceControl_StopZeroCrossed(float velocity_mps,
                                       float entry_direction,
                                       float max_cross_velocity_mps)
{
    float direction;

    if (entry_direction > 0.0f) {
        direction = 1.0f;
    } else if (entry_direction < 0.0f) {
        direction = -1.0f;
    } else {
        return 0U;
    }
    if (balance_helpers_absf(velocity_mps) >
        balance_helpers_absf(max_cross_velocity_mps)) {
        return 0U;
    }
    return ((velocity_mps * direction) <= 0.0f) ? 1U : 0U;
}

float BalanceControl_StopCaptureDirection(float velocity_mps,
                                          float minimum_velocity_mps)
{
    if (balance_helpers_absf(velocity_mps) <
        balance_helpers_absf(minimum_velocity_mps)) {
        return 0.0f;
    }
    return (velocity_mps >= 0.0f) ? 1.0f : -1.0f;
}

uint8_t BalanceControl_StopReboundConfirmed(float velocity_mps,
                                            float dt_sec,
                                            float rebound_velocity_mps,
                                            float confirm_sec,
                                            float *confirm_timer_sec,
                                            float *candidate_direction)
{
    float direction;

    if ((confirm_timer_sec == 0) || (candidate_direction == 0)) {
        return 0U;
    }
    if (balance_helpers_absf(velocity_mps) <
        balance_helpers_absf(rebound_velocity_mps)) {
        *confirm_timer_sec = 0.0f;
        *candidate_direction = 0.0f;
        return 0U;
    }
    direction = (velocity_mps >= 0.0f) ? 1.0f : -1.0f;
    if ((*candidate_direction * direction) <= 0.0f) {
        *candidate_direction = direction;
        *confirm_timer_sec = 0.0f;
    }
    if (confirm_sec <= 0.0f) {
        *confirm_timer_sec = 0.0f;
        return 1U;
    }
    if (dt_sec > 0.0f) {
        if (*confirm_timer_sec >= (confirm_sec - dt_sec)) {
            *confirm_timer_sec = confirm_sec;
        } else {
            *confirm_timer_sec += dt_sec;
        }
    }
    return (*confirm_timer_sec >= confirm_sec) ? 1U : 0U;
}

float BalanceControl_CalcViscousBrakeTheta(float velocity_mps,
                                           float gain_deg_per_mps,
                                           float limit_deg)
{
    return balance_helpers_limit_abs(-gain_deg_per_mps * velocity_mps,
                                     limit_deg);
}
