#include "bsp_laser.h"

#include "tim.h"

static uint8_t laser_ready = 0U;
static uint8_t laser_on = 0U;

uint8_t BSP_LaserInit(void)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK) {
        laser_ready = 0U;
        laser_on = 0U;
        return 0U;
    }

    laser_ready = 1U;
    laser_on = 0U;
    return 1U;
}

void BSP_LaserOn(void)
{
    uint32_t period;

    if (laser_ready == 0U) {
        if (BSP_LaserInit() == 0U) {
            return;
        }
    }

    period = __HAL_TIM_GET_AUTORELOAD(&htim3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, period + 1U);
    laser_on = BSP_LaserIsOn();
}

void BSP_LaserOff(void)
{
    if (laser_ready == 0U) {
        return;
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    laser_on = 0U;
}

uint8_t BSP_LaserIsOn(void)
{
    if ((laser_ready == 0U) ||
        ((htim3.Instance->CR1 & TIM_CR1_CEN) == 0U) ||
        ((htim3.Instance->CCER & TIM_CCER_CC3E) == 0U) ||
        (__HAL_TIM_GET_COMPARE(&htim3, TIM_CHANNEL_3) == 0U)) {
        return 0U;
    }
    return 1U;
}
