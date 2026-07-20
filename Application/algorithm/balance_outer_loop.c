#include "balance_outer_loop.h"

#include <stddef.h>

static float bol_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float bol_limit_abs(float value, float abs_limit)
{
    float limit = bol_absf(abs_limit);

    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

void BalanceOuterLoop_Init(BalanceOuterLoop_t *loop)
{
    BalanceOuterLoop_Reset(loop);
}

void BalanceOuterLoop_Reset(BalanceOuterLoop_t *loop)
{
    if (loop == NULL) {
        return;
    }

    loop->position_integral_m_s = 0.0f;
    loop->theta_ref_deg = 0.0f;
}

float BalanceOuterLoop_Update(BalanceOuterLoop_t *loop,
                              const BalanceOuterLoop_Config_t *config,
                              const BalanceOuterLoop_Input_t *input)
{
    float theta_ref_deg = 0.0f;

    if ((loop == NULL) || (config == NULL) || (input == NULL)) {
        return 0.0f;
    }

    if ((config->enable == 0U) ||
        (input->fault_ok == 0U) ||
        (bol_absf(input->theta_deg) > config->active_theta_deg)) {
        BalanceOuterLoop_Reset(loop);
        return 0.0f;
    }

    if (config->velocity_enable != 0U) {
        theta_ref_deg += config->k_v_deg_per_mps * input->velocity_mps;
    }

    if (config->position_enable != 0U) {
        theta_ref_deg += config->k_x_deg_per_m * input->position_m;
    }

    if (config->integral_enable != 0U) {
        uint8_t allow_integral = 1U;

        if ((config->active_current_a > 0.0f) &&
            (input->current_abs_a > config->active_current_a)) {
            allow_integral = 0U;
        }

        if ((allow_integral != 0U) && (input->dt_sec > 0.0f)) {
            loop->position_integral_m_s += input->position_m * input->dt_sec;
            loop->position_integral_m_s =
                bol_limit_abs(loop->position_integral_m_s,
                              config->integral_limit_m_s);
        }
        theta_ref_deg += config->k_i_deg_per_m_s *
                         loop->position_integral_m_s;
    } else {
        loop->position_integral_m_s = 0.0f;
    }

    loop->theta_ref_deg = bol_limit_abs(theta_ref_deg,
                                        config->theta_ref_limit_deg);
    return loop->theta_ref_deg;
}
