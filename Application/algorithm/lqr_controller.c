#include "lqr_controller.h"

#include <stddef.h>

static float lqr_clampf(float value, float min_value, float max_value)
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

static uint8_t lqr_limit_dim(uint8_t state_dim)
{
    return (state_dim > LQR_MAX_STATE_DIM) ? LQR_MAX_STATE_DIM : state_dim;
}

static void lqr_copy_gain(float *dst, const float *src, uint8_t state_dim)
{
    uint8_t i;

    if ((dst == NULL) || (src == NULL))
    {
        return;
    }

    for (i = 0U; i < LQR_MAX_STATE_DIM; i++)
    {
        dst[i] = 0.0f;
    }

    for (i = 0U; i < state_dim; i++)
    {
        dst[i] = src[i];
    }
}

static float lqr_feedback(const float *k, const float *x, uint8_t state_dim)
{
    uint8_t i;
    float u;

    if ((k == NULL) || (x == NULL))
    {
        return 0.0f;
    }

    u = 0.0f;
    for (i = 0U; i < state_dim; i++)
    {
        u -= k[i] * x[i];
    }

    return u;
}

void BalanceLQR_BuildState(float x[BALANCE_LQR_STATE_DIM],
                           float position,
                           float position_ref,
                           float velocity,
                           float velocity_ref,
                           float theta,
                           float theta_ref,
                           float theta_dot)
{
    if (x == NULL)
    {
        return;
    }

    x[0] = position - position_ref;
    x[1] = velocity - velocity_ref;
    x[2] = theta - theta_ref;
    x[3] = theta_dot;
}

void BalanceLQR_MixYaw(float u_balance,
                       float yaw_cmd,
                       float u_min,
                       float u_max,
                       float *u_left,
                       float *u_right)
{
    float left;
    float right;

    left = u_balance - yaw_cmd;
    right = u_balance + yaw_cmd;

    if (u_left != NULL)
    {
        *u_left = lqr_clampf(left, u_min, u_max);
    }
    if (u_right != NULL)
    {
        *u_right = lqr_clampf(right, u_min, u_max);
    }
}

void LQR_FixedGain_Init(LQR_FixedGain_t *lqr,
                        const float *k,
                        uint8_t state_dim,
                        float u_min,
                        float u_max)
{
    uint8_t dim;

    if (lqr == NULL)
    {
        return;
    }

    dim = lqr_limit_dim(state_dim);
    lqr->state_dim = dim;
    lqr->u_min = u_min;
    lqr->u_max = u_max;
    lqr_copy_gain(lqr->k, k, dim);
}

float LQR_FixedGain_Update(const LQR_FixedGain_t *lqr, const float *x)
{
    float u;

    if ((lqr == NULL) || (x == NULL))
    {
        return 0.0f;
    }

    u = lqr_feedback(lqr->k, x, lqr->state_dim);
    return lqr_clampf(u, lqr->u_min, lqr->u_max);
}

void LQR_GainSchedule_Init(LQR_GainSchedule_t *lqr,
                           uint8_t state_dim,
                           float u_min,
                           float u_max)
{
    uint8_t i;

    if (lqr == NULL)
    {
        return;
    }

    lqr->state_dim = lqr_limit_dim(state_dim);
    lqr->point_count = 0U;
    lqr->u_min = u_min;
    lqr->u_max = u_max;

    for (i = 0U; i < LQR_MAX_STATE_DIM; i++)
    {
        lqr->k_eff[i] = 0.0f;
    }
}

uint8_t LQR_GainSchedule_SetPoint(LQR_GainSchedule_t *lqr,
                                  uint8_t index,
                                  float schedule,
                                  const float *k)
{
    if ((lqr == NULL) || (k == NULL) || (index >= LQR_GAIN_SCHEDULE_MAX))
    {
        return 0U;
    }

    lqr->point[index].schedule = schedule;
    lqr_copy_gain(lqr->point[index].k, k, lqr->state_dim);

    if ((uint8_t)(index + 1U) > lqr->point_count)
    {
        lqr->point_count = (uint8_t)(index + 1U);
    }

    return 1U;
}

float LQR_GainSchedule_Update(LQR_GainSchedule_t *lqr,
                              float schedule,
                              const float *x)
{
    uint8_t i;
    uint8_t low;
    uint8_t high;
    float span;
    float ratio;
    float u;

    if ((lqr == NULL) || (x == NULL) || (lqr->point_count == 0U))
    {
        return 0.0f;
    }

    low = 0U;
    high = 0U;

    if (lqr->point_count == 1U)
    {
        lqr_copy_gain(lqr->k_eff, lqr->point[0].k, lqr->state_dim);
    }
    else
    {
        if (schedule <= lqr->point[0].schedule)
        {
            low = 0U;
            high = 0U;
        }
        else if (schedule >= lqr->point[(uint8_t)(lqr->point_count - 1U)].schedule)
        {
            low = (uint8_t)(lqr->point_count - 1U);
            high = low;
        }
        else
        {
            for (i = 0U; i < (uint8_t)(lqr->point_count - 1U); i++)
            {
                if ((schedule >= lqr->point[i].schedule) &&
                    (schedule <= lqr->point[(uint8_t)(i + 1U)].schedule))
                {
                    low = i;
                    high = (uint8_t)(i + 1U);
                    break;
                }
            }
        }

        if (low == high)
        {
            lqr_copy_gain(lqr->k_eff, lqr->point[low].k, lqr->state_dim);
        }
        else
        {
            span = lqr->point[high].schedule - lqr->point[low].schedule;
            if ((span > -1.0e-6f) && (span < 1.0e-6f))
            {
                ratio = 0.0f;
            }
            else
            {
                ratio = (schedule - lqr->point[low].schedule) / span;
            }

            ratio = lqr_clampf(ratio, 0.0f, 1.0f);

            for (i = 0U; i < lqr->state_dim; i++)
            {
                lqr->k_eff[i] = lqr->point[low].k[i] +
                                ratio * (lqr->point[high].k[i] - lqr->point[low].k[i]);
            }
        }
    }

    u = lqr_feedback(lqr->k_eff, x, lqr->state_dim);
    return lqr_clampf(u, lqr->u_min, lqr->u_max);
}

void LQR_Incremental_Init(LQR_Incremental_t *lqr,
                          const float *k,
                          uint8_t state_dim,
                          float u_min,
                          float u_max,
                          float du_min,
                          float du_max)
{
    uint8_t i;
    uint8_t dim;

    if (lqr == NULL)
    {
        return;
    }

    dim = lqr_limit_dim(state_dim);
    lqr->state_dim = dim;
    lqr->ready = 0U;
    lqr->u = 0.0f;
    lqr->u_min = u_min;
    lqr->u_max = u_max;
    lqr->du_min = du_min;
    lqr->du_max = du_max;
    lqr_copy_gain(lqr->k, k, dim);

    for (i = 0U; i < LQR_MAX_STATE_DIM; i++)
    {
        lqr->last_x[i] = 0.0f;
    }
}

void LQR_Incremental_Reset(LQR_Incremental_t *lqr,
                           float initial_u,
                           const float *x)
{
    uint8_t i;

    if (lqr == NULL)
    {
        return;
    }

    lqr->u = lqr_clampf(initial_u, lqr->u_min, lqr->u_max);

    if (x != NULL)
    {
        for (i = 0U; i < lqr->state_dim; i++)
        {
            lqr->last_x[i] = x[i];
        }
        lqr->ready = 1U;
    }
    else
    {
        lqr->ready = 0U;
    }
}

float LQR_Incremental_Update(LQR_Incremental_t *lqr, const float *x)
{
    uint8_t i;
    float dx[LQR_MAX_STATE_DIM];
    float du;

    if ((lqr == NULL) || (x == NULL))
    {
        return 0.0f;
    }

    if (lqr->ready == 0U)
    {
        LQR_Incremental_Reset(lqr, lqr->u, x);
        return lqr->u;
    }

    for (i = 0U; i < lqr->state_dim; i++)
    {
        dx[i] = x[i] - lqr->last_x[i];
        lqr->last_x[i] = x[i];
    }

    du = lqr_feedback(lqr->k, dx, lqr->state_dim);
    du = lqr_clampf(du, lqr->du_min, lqr->du_max);
    lqr->u = lqr_clampf(lqr->u + du, lqr->u_min, lqr->u_max);

    return lqr->u;
}
