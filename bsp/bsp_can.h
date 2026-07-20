#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include <stdint.h>

#include "main.h"
#include "config.h"

typedef struct {
    uint8_t enabled;
    float current;
    float speed_can_rpm;
    float angle;
    uint16_t encoder_raw;
    uint8_t init_flag;
    uint32_t rx_count;
    uint32_t last_rx_tick;
} Motor_Measure_t;

extern volatile Motor_Measure_t left_wheel_motor;
extern volatile Motor_Measure_t right_wheel_motor;
extern volatile Motor_Measure_t gimbal_yaw_motor;
extern volatile uint32_t can_rx_count;
extern volatile uint32_t can_wheel_rx_count;
extern volatile uint32_t can_gimbal_rx_count;
extern volatile uint32_t can_last_std_id;
extern volatile uint32_t can_last_dlc;

void CAN_Filter_Init(void);
HAL_StatusTypeDef Motor_Enable(uint8_t id);
void Motor_Disable(uint8_t id);
HAL_StatusTypeDef Motor_TrySetCurrent(uint8_t id, float current);
void Motor_SetCurrent(uint8_t id, float current);
void Motor_SetAngle(uint8_t id, float angle);
uint8_t Motor_FeedbackReady(const volatile Motor_Measure_t *motor,
                            uint32_t now_tick,
                            uint32_t timeout_ms);

#endif /* __BSP_CAN_H */
