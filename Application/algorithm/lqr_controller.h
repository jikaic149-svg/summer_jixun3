#ifndef LQR_CONTROLLER_H
#define LQR_CONTROLLER_H

#include <stdint.h>

#define LQR_MAX_STATE_DIM          6U
#define LQR_GAIN_SCHEDULE_MAX      4U
#define BALANCE_LQR_STATE_DIM      4U

typedef struct
{
    float position;
    float velocity;
    float theta;
    float theta_dot;
} BalanceLQR_State_t;

typedef struct
{
    uint8_t state_dim;
    float k[LQR_MAX_STATE_DIM];
    float u_min;
    float u_max;
} LQR_FixedGain_t;

typedef struct
{
    float schedule;
    float k[LQR_MAX_STATE_DIM];
} LQR_GainPoint_t;

typedef struct
{
    uint8_t state_dim;
    uint8_t point_count;
    LQR_GainPoint_t point[LQR_GAIN_SCHEDULE_MAX];
    float u_min;
    float u_max;
    float k_eff[LQR_MAX_STATE_DIM];
} LQR_GainSchedule_t;

typedef struct
{
    uint8_t state_dim;
    uint8_t ready;
    float k[LQR_MAX_STATE_DIM];
    float last_x[LQR_MAX_STATE_DIM];
    float u;
    float u_min;
    float u_max;
    float du_min;
    float du_max;
} LQR_Incremental_t;

void BalanceLQR_BuildState(float x[BALANCE_LQR_STATE_DIM],
                           float position,
                           float position_ref,
                           float velocity,
                           float velocity_ref,
                           float theta,
                           float theta_ref,
                           float theta_dot);
void BalanceLQR_MixYaw(float u_balance,
                       float yaw_cmd,
                       float u_min,
                       float u_max,
                       float *u_left,
                       float *u_right);

void LQR_FixedGain_Init(LQR_FixedGain_t *lqr,
                        const float *k,
                        uint8_t state_dim,
                        float u_min,
                        float u_max);
float LQR_FixedGain_Update(const LQR_FixedGain_t *lqr, const float *x);

void LQR_GainSchedule_Init(LQR_GainSchedule_t *lqr,
                           uint8_t state_dim,
                           float u_min,
                           float u_max);
uint8_t LQR_GainSchedule_SetPoint(LQR_GainSchedule_t *lqr,
                                  uint8_t index,
                                  float schedule,
                                  const float *k);
float LQR_GainSchedule_Update(LQR_GainSchedule_t *lqr,
                              float schedule,
                              const float *x);

void LQR_Incremental_Init(LQR_Incremental_t *lqr,
                          const float *k,
                          uint8_t state_dim,
                          float u_min,
                          float u_max,
                          float du_min,
                          float du_max);
void LQR_Incremental_Reset(LQR_Incremental_t *lqr,
                           float initial_u,
                           const float *x);
float LQR_Incremental_Update(LQR_Incremental_t *lqr, const float *x);

#endif
