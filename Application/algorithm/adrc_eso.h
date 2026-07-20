#ifndef ADRC_ESO_H
#define ADRC_ESO_H

#include <stdint.h>

typedef struct
{
    float b0;
    float wo;
    float beta1;
    float beta2;
    float z1;
    float z2;
    float z2_limit;
    uint8_t ready;
} ESO_FirstOrder_t;

typedef struct
{
    ESO_FirstOrder_t eso;
    float kp;
    float ki;
    float kaw;
    float u_max;
    float integral;
    float last_u;
    float risk;
    uint8_t integral_enable;
    uint8_t compensation_enable;
} PI_ESO_Controller_t;

void ESO_FirstOrder_Init(ESO_FirstOrder_t *eso,
                         float b0,
                         float wo,
                         float z2_limit);
void ESO_FirstOrder_Reset(ESO_FirstOrder_t *eso, float omega);
void ESO_FirstOrder_SetParams(ESO_FirstOrder_t *eso,
                              float b0,
                              float wo,
                              float z2_limit);
void ESO_FirstOrder_Update(ESO_FirstOrder_t *eso,
                           float omega,
                           float u_last,
                           float dt_sec);
float ESO_FirstOrder_GetOmega(const ESO_FirstOrder_t *eso);
float ESO_FirstOrder_GetDisturbance(const ESO_FirstOrder_t *eso);
float ESO_FirstOrder_GetLimitedDisturbance(const ESO_FirstOrder_t *eso);
float ESO_FirstOrder_Compensate(const ESO_FirstOrder_t *eso,
                                float u_base,
                                float u_max);

void PI_ESO_Init(PI_ESO_Controller_t *ctrl,
                 float kp,
                 float ki,
                 float kaw,
                 float b0,
                 float wo,
                 float u_max,
                 float z2_limit);
void PI_ESO_Reset(PI_ESO_Controller_t *ctrl, float omega);
void PI_ESO_SetGains(PI_ESO_Controller_t *ctrl,
                     float kp,
                     float ki,
                     float kaw);
void PI_ESO_EnableIntegral(PI_ESO_Controller_t *ctrl, uint8_t enable);
void PI_ESO_EnableCompensation(PI_ESO_Controller_t *ctrl, uint8_t enable);
float PI_ESO_CalcRisk(float theta_deg,
                      float theta_dot_dps,
                      float theta_limit_deg,
                      float theta_dot_limit_dps);
float PI_ESO_Update(PI_ESO_Controller_t *ctrl,
                    float omega_ref,
                    float omega,
                    float theta_deg,
                    float theta_dot_dps,
                    float theta_limit_deg,
                    float theta_dot_limit_dps,
                    float dt_sec);

#endif
