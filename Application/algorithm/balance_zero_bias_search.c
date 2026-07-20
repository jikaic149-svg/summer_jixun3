#include "balance_zero_bias_search.h"

#include <stddef.h>

#define BALANCE_ZERO_BIAS_PHASE_BASE  0U
#define BALANCE_ZERO_BIAS_PHASE_PLUS  1U
#define BALANCE_ZERO_BIAS_PHASE_MINUS 2U

static float bzbs_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float bzbs_limit_abs(float value, float abs_limit)
{
    float limit = bzbs_absf(abs_limit);

    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float bzbs_squaref(float value)
{
    return value * value;
}

static float bzbs_deadband_abs(float value, float deadband)
{
    float abs_value = bzbs_absf(value);
    float abs_deadband = bzbs_absf(deadband);

    if (abs_value <= abs_deadband) {
        return 0.0f;
    }

    return abs_value - abs_deadband;
}

static uint8_t bzbs_safe(const BalanceZeroBiasSearch_Config_t *config,
                         const BalanceZeroBiasSearch_Input_t *input)
{
    if ((config == NULL) || (input == NULL)) {
        return 0U;
    }
    if (config->enable == 0U) {
        return 0U;
    }
    if (input->fault_ok == 0U) {
        return 0U;
    }
    if (bzbs_absf(input->theta_deg) > config->active_theta_deg) {
        return 0U;
    }
    if (bzbs_absf(input->theta_rate_dps) > config->active_rate_dps) {
        return 0U;
    }
    if (input->current_abs_a > config->active_current_a) {
        return 0U;
    }
    if (bzbs_absf(input->position_m) > config->active_position_m) {
        return 0U;
    }
    if (bzbs_absf(input->velocity_mps) > config->active_velocity_mps) {
        return 0U;
    }

    return 1U;
}

static float bzbs_cost(const BalanceZeroBiasSearch_Config_t *config,
                       const BalanceZeroBiasSearch_Input_t *input)
{
    float v_term = bzbs_deadband_abs(input->velocity_mps,
                                     config->v_deadband_mps);
    float x_term = bzbs_deadband_abs(input->position_m,
                                     config->x_deadband_m);
    float rate_term = bzbs_absf(input->theta_rate_dps);
    float current_term = bzbs_absf(input->current_abs_a);

    if (config->v_scale_mps > 0.0f) {
        v_term /= config->v_scale_mps;
    }
    if (config->x_scale_m > 0.0f) {
        x_term /= config->x_scale_m;
    }
    if (config->rate_scale_dps > 0.0f) {
        rate_term /= config->rate_scale_dps;
    }
    if (config->current_scale_a > 0.0f) {
        current_term /= config->current_scale_a;
    }

    return config->w_v * bzbs_squaref(v_term) +
           config->w_x * bzbs_squaref(x_term) +
           config->w_rate * bzbs_squaref(rate_term) +
           config->w_current * bzbs_squaref(current_term);
}

void BalanceZeroBiasSearch_Init(BalanceZeroBiasSearch_t *search)
{
    BalanceZeroBiasSearch_Reset(search, 1U);
}

void BalanceZeroBiasSearch_Reset(BalanceZeroBiasSearch_t *search, uint8_t clear_bias)
{
    uint8_t keep_frozen;

    if (search == NULL) {
        return;
    }

    keep_frozen = ((clear_bias == 0U) && (search->frozen != 0U)) ? 1U : 0U;

    if (clear_bias != 0U) {
        search->bias_deg = 0.0f;
    }

    search->cost_base = 0.0f;
    search->cost_plus = 0.0f;
    search->cost_minus = 0.0f;
    search->cost_sum = 0.0f;
    search->eval_count = 0U;
    search->settle_count = 0U;
    if (keep_frozen == 0U) {
        search->stable_rounds = 0U;
        search->frozen = 0U;
    }
    search->phase = BALANCE_ZERO_BIAS_PHASE_BASE;
}

float BalanceZeroBiasSearch_GetCandidate(const BalanceZeroBiasSearch_t *search,
                                         const BalanceZeroBiasSearch_Config_t *config,
                                         const BalanceZeroBiasSearch_Input_t *input)
{
    float candidate;

    if ((search == NULL) || (config == NULL)) {
        return 0.0f;
    }

    if (config->enable == 0U) {
        return 0.0f;
    }

    candidate = search->bias_deg;
    if (search->frozen != 0U) {
        return bzbs_limit_abs(candidate, config->bias_limit_deg);
    }

    if (bzbs_safe(config, input) == 0U) {
        return bzbs_limit_abs(candidate, config->bias_limit_deg);
    }

    if (search->phase == BALANCE_ZERO_BIAS_PHASE_PLUS) {
        candidate += config->dither_deg;
    } else if (search->phase == BALANCE_ZERO_BIAS_PHASE_MINUS) {
        candidate -= config->dither_deg;
    }

    return bzbs_limit_abs(candidate, config->bias_limit_deg);
}

static void bzbs_finish_phase(BalanceZeroBiasSearch_t *search,
                              const BalanceZeroBiasSearch_Config_t *config,
                              float average_cost)
{
    if (search->phase == BALANCE_ZERO_BIAS_PHASE_BASE) {
        search->cost_base = average_cost;
        search->phase = BALANCE_ZERO_BIAS_PHASE_PLUS;
    } else if (search->phase == BALANCE_ZERO_BIAS_PHASE_PLUS) {
        search->cost_plus = average_cost;
        search->phase = BALANCE_ZERO_BIAS_PHASE_MINUS;
    } else {
        uint8_t updated = 0U;

        search->cost_minus = average_cost;

        if ((search->cost_plus + config->improve_eps < search->cost_base) &&
            (search->cost_plus <= search->cost_minus)) {
            search->bias_deg += config->update_step_deg;
            updated = 1U;
        } else if ((search->cost_minus + config->improve_eps < search->cost_base) &&
                   (search->cost_minus < search->cost_plus)) {
            search->bias_deg -= config->update_step_deg;
            updated = 1U;
        }

        search->bias_deg = bzbs_limit_abs(search->bias_deg,
                                          config->bias_limit_deg);

        if (updated != 0U) {
            search->stable_rounds = 0U;
        } else if (config->freeze_enable != 0U) {
            uint8_t stable_limit = config->freeze_stable_rounds;

            if (stable_limit == 0U) {
                stable_limit = 1U;
            }
            if (search->stable_rounds < stable_limit) {
                search->stable_rounds++;
            }
            if (search->stable_rounds >= stable_limit) {
                search->frozen = 1U;
            }
        }

        search->phase = BALANCE_ZERO_BIAS_PHASE_BASE;
    }

    search->cost_sum = 0.0f;
    search->eval_count = 0U;
    search->settle_count = 0U;
}

void BalanceZeroBiasSearch_Update(BalanceZeroBiasSearch_t *search,
                                  const BalanceZeroBiasSearch_Config_t *config,
                                  const BalanceZeroBiasSearch_Input_t *input)
{
    if ((search == NULL) || (config == NULL) || (input == NULL)) {
        return;
    }

    if (search->frozen != 0U) {
        return;
    }

    if (bzbs_safe(config, input) == 0U) {
        BalanceZeroBiasSearch_Reset(search, 0U);
        return;
    }

    if (search->settle_count < config->settle_steps) {
        search->settle_count++;
        search->cost_sum = 0.0f;
        search->eval_count = 0U;
        return;
    }

    search->cost_sum += bzbs_cost(config, input);
    search->eval_count++;

    if (search->eval_count >= config->eval_steps) {
        bzbs_finish_phase(search,
                          config,
                          search->cost_sum / (float)search->eval_count);
    }
}

float BalanceZeroBiasSearch_GetBias(const BalanceZeroBiasSearch_t *search)
{
    if (search == NULL) {
        return 0.0f;
    }

    return search->bias_deg;
}

uint8_t BalanceZeroBiasSearch_IsFrozen(const BalanceZeroBiasSearch_t *search)
{
    if (search == NULL) {
        return 0U;
    }

    return search->frozen;
}
