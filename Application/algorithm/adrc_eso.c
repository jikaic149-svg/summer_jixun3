#include "adrc_eso.h"

#include <stddef.h>

#define ADRC_EPSILON 1.0e-6f

static float adrc_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float adrc_clampf(float value, float min_value, float max_value)
{
    if (value > max_value)
    {
        return max_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    return value;
}

static float adrc_limit_abs(float value, float abs_limit)
{
    float limit;

    limit = adrc_absf(abs_limit);
    if (limit <= ADRC_EPSILON)
    {
        return value;
    }

    return adrc_clampf(value, -limit, limit);
}

static float adrc_safe_b0(float b0)
{
    if (b0 >= 0.0f)
    {
        return (b0 > ADRC_EPSILON) ? b0 : ADRC_EPSILON;
    }

    return (b0 < -ADRC_EPSILON) ? b0 : -ADRC_EPSILON;
}

void ESO_FirstOrder_Init(ESO_FirstOrder_t *eso,
                         float b0,
                         float wo,
                         float z2_limit)
{
    if (eso == NULL)
    {
        return;
    }

    eso->z1 = 0.0f;
    eso->z2 = 0.0f;
    eso->ready = 0U;
    ESO_FirstOrder_SetParams(eso, b0, wo, z2_limit);
}

void ESO_FirstOrder_Reset(ESO_FirstOrder_t *eso, float omega)
{
    if (eso == NULL)
    {
        return;
    }

    eso->z1 = omega;
    eso->z2 = 0.0f;
    eso->ready = 1U;
}

void ESO_FirstOrder_SetParams(ESO_FirstOrder_t *eso,
                              float b0,
                              float wo,
                              float z2_limit)
{
    float wo_abs;

    if (eso == NULL)
    {
        return;
    }

    wo_abs = adrc_absf(wo);
    eso->b0 = adrc_safe_b0(b0);
    eso->wo = wo_abs;
    eso->beta1 = 2.0f * wo_abs;
    eso->beta2 = wo_abs * wo_abs;
    eso->z2_limit = adrc_absf(z2_limit);
}

void ESO_FirstOrder_Update(ESO_FirstOrder_t *eso,
                           float omega,
                           float u_last,
                           float dt_sec)
{
    float e_eso;
    float z1_dot;
    float z2_dot;

    if ((eso == NULL) || (dt_sec <= 0.0f))
    {
        return;
    }

    if (eso->ready == 0U)
    {
        ESO_FirstOrder_Reset(eso, omega);
        return;
    }

    e_eso = omega - eso->z1;
    z1_dot = eso->z2 + eso->b0 * u_last + eso->beta1 * e_eso;
    z2_dot = eso->beta2 * e_eso;

    eso->z1 += dt_sec * z1_dot;
    eso->z2 += dt_sec * z2_dot;
}

float ESO_FirstOrder_GetOmega(const ESO_FirstOrder_t *eso)
{
    if (eso == NULL)
    {
        return 0.0f;
    }

    return eso->z1;
}

float ESO_FirstOrder_GetDisturbance(const ESO_FirstOrder_t *eso)
{
    if (eso == NULL)
    {
        return 0.0f;
    }

    return eso->z2;
}

float ESO_FirstOrder_GetLimitedDisturbance(const ESO_FirstOrder_t *eso)
{
    if (eso == NULL)
    {
        return 0.0f;
    }

    return adrc_limit_abs(eso->z2, eso->z2_limit);
}

float ESO_FirstOrder_Compensate(const ESO_FirstOrder_t *eso,
                                float u_base,
                                float u_max)
{
    float b0;
    float u_raw;

    if (eso == NULL)
    {
        return adrc_limit_abs(u_base, u_max);
    }

    b0 = adrc_safe_b0(eso->b0);
    u_raw = u_base - ESO_FirstOrder_GetLimitedDisturbance(eso) / b0;
    return adrc_limit_abs(u_raw, u_max);
}

void PI_ESO_Init(PI_ESO_Controller_t *ctrl,
                 float kp,
                 float ki,
                 float kaw,
                 float b0,
                 float wo,
                 float u_max,
                 float z2_limit)
{
    if (ctrl == NULL)
    {
        return;
    }

    ESO_FirstOrder_Init(&ctrl->eso, b0, wo, z2_limit);
    ctrl->kp = kp;
    ctrl->ki = ki;
    ctrl->kaw = kaw;
    ctrl->u_max = adrc_absf(u_max);
    ctrl->integral = 0.0f;
    ctrl->last_u = 0.0f;
    ctrl->risk = 0.0f;
    ctrl->integral_enable = 0U;
    ctrl->compensation_enable = 0U;
}

void PI_ESO_Reset(PI_ESO_Controller_t *ctrl, float omega)
{
    if (ctrl == NULL)
    {
        return;
    }

    ESO_FirstOrder_Reset(&ctrl->eso, omega);
    ctrl->integral = 0.0f;
    ctrl->last_u = 0.0f;
    ctrl->risk = 0.0f;
}

void PI_ESO_SetGains(PI_ESO_Controller_t *ctrl,
                     float kp,
                     float ki,
                     float kaw)
{
    if (ctrl == NULL)
    {
        return;
    }

    ctrl->kp = kp;
    ctrl->ki = ki;
    ctrl->kaw = kaw;
}

void PI_ESO_EnableIntegral(PI_ESO_Controller_t *ctrl, uint8_t enable)
{
    if (ctrl == NULL)
    {
        return;
    }

    ctrl->integral_enable = (enable != 0U) ? 1U : 0U;
}

void PI_ESO_EnableCompensation(PI_ESO_Controller_t *ctrl, uint8_t enable)
{
    if (ctrl == NULL)
    {
        return;
    }

    ctrl->compensation_enable = (enable != 0U) ? 1U : 0U;
}

float PI_ESO_CalcRisk(float theta_deg,
                      float theta_dot_dps,
                      float theta_limit_deg,
                      float theta_dot_limit_dps)
{
    float risk_theta;
    float risk_rate;

    risk_theta = 0.0f;
    risk_rate = 0.0f;

    if (adrc_absf(theta_limit_deg) > ADRC_EPSILON)
    {
        risk_theta = adrc_absf(theta_deg) / adrc_absf(theta_limit_deg);
    }
    if (adrc_absf(theta_dot_limit_dps) > ADRC_EPSILON)
    {
        risk_rate = adrc_absf(theta_dot_dps) / adrc_absf(theta_dot_limit_dps);
    }

    return adrc_clampf(risk_theta + risk_rate, 0.0f, 1.0f);
}

float PI_ESO_Update(PI_ESO_Controller_t *ctrl,
                    float omega_ref,
                    float omega,
                    float theta_deg,
                    float theta_dot_dps,
                    float theta_limit_deg,
                    float theta_dot_limit_dps,
                    float dt_sec)
{
    float risk_gain;
    float e_w;
    float d_used;
    float u0;
    float u_raw;
    float u_sat;
    float b0;
    float integral_delta;

    if ((ctrl == NULL) || (dt_sec <= 0.0f))
    {
        return 0.0f;
    }

    ESO_FirstOrder_Update(&ctrl->eso, omega, ctrl->last_u, dt_sec);

    ctrl->risk = PI_ESO_CalcRisk(theta_deg,
                                 theta_dot_dps,
                                 theta_limit_deg,
                                 theta_dot_limit_dps);
    risk_gain = 1.0f - ctrl->risk;
    if (ctrl->compensation_enable != 0U) {
        e_w = omega_ref - ctrl->eso.z1;
    } else {
        e_w = omega_ref - omega;
    }
    integral_delta = e_w * dt_sec * risk_gain;

    if (ctrl->integral_enable != 0U)
    {
        ctrl->integral += integral_delta;
    }

    d_used = 0.0f;
    if (ctrl->compensation_enable != 0U)
    {
        d_used = ESO_FirstOrder_GetLimitedDisturbance(&ctrl->eso);
    }

    b0 = adrc_safe_b0(ctrl->eso.b0);
    u0 = ctrl->kp * e_w;
    if (ctrl->integral_enable != 0U)
    {
        u0 += ctrl->ki * risk_gain * ctrl->integral;
    }
    u_raw = (u0 - d_used) / b0;
    u_sat = adrc_limit_abs(u_raw, ctrl->u_max);

    if ((ctrl->integral_enable != 0U) && (adrc_absf(u_raw - u_sat) > ADRC_EPSILON))
    {
        if (ctrl->kaw > 0.0f)
        {
            ctrl->integral -= ctrl->kaw * (u_raw - u_sat) * dt_sec;
        }
        else
        {
            ctrl->integral -= integral_delta;
        }

        u0 = ctrl->kp * e_w + ctrl->ki * risk_gain * ctrl->integral;
        u_raw = (u0 - d_used) / b0;
        u_sat = adrc_limit_abs(u_raw, ctrl->u_max);
    }

    ctrl->last_u = u_sat;
    return u_sat;
}
