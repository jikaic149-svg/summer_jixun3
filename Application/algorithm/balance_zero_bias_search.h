#ifndef BALANCE_ZERO_BIAS_SEARCH_H
#define BALANCE_ZERO_BIAS_SEARCH_H

#include <stdint.h>

typedef struct
{
    uint8_t enable;
    float bias_limit_deg;
    float active_theta_deg;
    float active_rate_dps;
    float active_current_a;
    float active_position_m;
    float active_velocity_mps;
    float v_deadband_mps;
    float x_deadband_m;
    float dither_deg;
    float update_step_deg;
    uint32_t settle_steps;
    uint32_t eval_steps;
    uint8_t freeze_enable;
    uint8_t freeze_stable_rounds;
    float improve_eps;
    float v_scale_mps;
    float x_scale_m;
    float rate_scale_dps;
    float current_scale_a;
    float w_v;
    float w_x;
    float w_rate;
    float w_current;
} BalanceZeroBiasSearch_Config_t;

typedef struct
{
    float theta_deg;
    float theta_rate_dps;
    float position_m;
    float velocity_mps;
    float current_abs_a;
    uint8_t fault_ok;
} BalanceZeroBiasSearch_Input_t;

typedef struct
{
    float bias_deg;
    float cost_base;
    float cost_plus;
    float cost_minus;
    float cost_sum;
    uint32_t eval_count;
    uint32_t settle_count;
    uint8_t stable_rounds;
    uint8_t frozen;
    uint8_t phase;
} BalanceZeroBiasSearch_t;

void BalanceZeroBiasSearch_Init(BalanceZeroBiasSearch_t *search);
void BalanceZeroBiasSearch_Reset(BalanceZeroBiasSearch_t *search, uint8_t clear_bias);
float BalanceZeroBiasSearch_GetCandidate(const BalanceZeroBiasSearch_t *search,
                                         const BalanceZeroBiasSearch_Config_t *config,
                                         const BalanceZeroBiasSearch_Input_t *input);
void BalanceZeroBiasSearch_Update(BalanceZeroBiasSearch_t *search,
                                  const BalanceZeroBiasSearch_Config_t *config,
                                  const BalanceZeroBiasSearch_Input_t *input);
float BalanceZeroBiasSearch_GetBias(const BalanceZeroBiasSearch_t *search);
uint8_t BalanceZeroBiasSearch_IsFrozen(const BalanceZeroBiasSearch_t *search);

#endif
