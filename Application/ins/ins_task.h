#ifndef INS_TASK_H
#define INS_TASK_H

/**
 * @file    ins_task.h
 * @brief   Application层 — 平衡车 IMU 姿态任务
 * @note    整合 BMI088 + Kalman平衡角 + 蓝牙输出
 *          作为 FreeRTOS 任务运行
 *          严禁包含任何 MCU 相关头文件 (Rule 2)
 */

#include <stdint.h>
#include "vision.h"

/* ======================== 任务接口 ======================== */

/**
 * @brief  INS 系统初始化 (调度器启动前调用)
 * @note   内部完成:
 *         1. BSP_SPI / BSP_UART6 初始化
 *         2. BMI088 初始化
 *         3. Kalman平衡角滤波器初始化
 *         4. 蓝牙实例初始化
 */
void INS_Init(void);

/**
 * @brief  INS FreeRTOS 任务函数
 * @param  argument FreeRTOS 传入参数 (未使用)
 * @note   SPI 1000Hz读 IMU → Kalman输出平衡角/角速度 → 定期蓝牙发送
 */
extern void INS_Task(void *argument);
extern void BT_RxTask(void *argument);
extern void Gimbal_Task(void *argument);

// 平衡车底盘姿态输出
extern float global_balance_angle;
extern float global_balance_rate;
extern float global_balance_accel_angle;
extern float global_balance_accel_forward_ms2;
extern float global_chassis_yaw;
extern float global_chassis_yaw_rate;
extern volatile uint8_t imu_yaw_cal_state;

#define INS_YAW_CAL_IDLE      0U
#define INS_YAW_CAL_RUNNING   1U
#define INS_YAW_CAL_DONE      2U

// 旧云台兼容变量，当前平衡车阶段不作为控制依据
extern float global_gimbal_yaw;
extern float global_gimbal_pitch;
extern float global_imu_yaw_speed;
extern float global_imu_pitch_speed;
extern volatile uint8_t emergency_stop;

/* ======================== 数据获取 ======================== */

float INS_GetYaw(void);
float INS_GetPitch(void);
float INS_GetRoll(void);
float INS_GetYawTotal(void);
float INS_GetBalanceAngle(void);
float INS_GetBalanceRate(void);
float INS_GetChassisYaw(void);
float INS_GetChassisYawRate(void);
float INS_GetChassisYawError(float target_yaw_deg);
uint8_t INS_IsCalibrated(void);
void INS_RequestYawCalibration(void);
uint8_t INS_GetYawCalibrationState(void);
float INS_GetYawBiasDps(void);
void INS_RequestBalanceAngleReseed(void);
uint32_t INS_GetVisionTarget(int16_t *dx, int16_t *dy, uint8_t *valid);
uint32_t INS_GetVisionTaskTarget(Vision_TaskSnapshot_t *snapshot);

#endif /* INS_TASK_H */
