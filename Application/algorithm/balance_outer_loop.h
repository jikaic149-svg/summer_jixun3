#ifndef BALANCE_OUTER_LOOP_H
#define BALANCE_OUTER_LOOP_H

#include <stdint.h>

typedef struct
{
    uint8_t enable;
    uint8_t velocity_enable;
    uint8_t position_enable;
    uint8_t integral_enable;
    float theta_ref_limit_deg;
    float k_v_deg_per_mps;
    float k_x_deg_per_m;
    float k_i_deg_per_m_s;
    float integral_limit_m_s;
    float active_theta_deg;
    float active_current_a;
} BalanceOuterLoop_Config_t;

typedef struct
{
    float position_m;
    float velocity_mps;
    float theta_deg;
    float current_abs_a;
    float dt_sec;
    uint8_t fault_ok;
} BalanceOuterLoop_Input_t;

typedef struct
{
    float position_integral_m_s;
    float theta_ref_deg;
} BalanceOuterLoop_t;

void BalanceOuterLoop_Init(BalanceOuterLoop_t *loop);
void BalanceOuterLoop_Reset(BalanceOuterLoop_t *loop);
float BalanceOuterLoop_Update(BalanceOuterLoop_t *loop,
                              const BalanceOuterLoop_Config_t *config,
                              const BalanceOuterLoop_Input_t *input);

#endif
