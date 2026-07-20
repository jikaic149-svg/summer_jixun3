/**
 * @file    ins_task.c
 * @brief   Application层 — 平衡车 IMU/Kalman 姿态任务实现
 * @note    整合 BMI088(Device) + Kalman角度滤波 + VOFA(Device)
 *          参考 QGimbal: SPI DMA + EXTI DRDY 中断驱动 + TaskNotify 同步
 *
 *          运行模式:
 *          1. 初始化阶段: SPI 轮询读写 BMI088 寄存器
 *          2. 运行阶段:   EXTI DRDY → SPI DMA 读取 → TaskNotify → Kalman
 */

#include "ins_task.h"
#include "main.h"
#include <math.h>
#include <stddef.h>

/* Device 层 */
#include "bmi088.h"
#include "bmi088_reg.h"
#include "vofa.h"
#include "track.h"
#include "vision.h"

/* Algorithm 层 */
#include "attitude_algorithm.h"
#include "bsp_can.h"
#include "config.h"
#include "chassis_serial_cmd.h"
#include "task_control.h"
#include "gimbal_task.h"
#include "mode_control.h"
#include "mode_gimbal.h"

/* BSP 层 (仅用于 Init 时注入函数指针) */
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "bsp_uart6.h"
#include "bsp_delay.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* ======================== 跨任务暴露的全局变量 ======================== */
float global_gimbal_yaw = 0.0f;
float global_gimbal_pitch = 0.0f;
float global_imu_yaw_speed = 0.0f;
float global_imu_pitch_speed = 0.0f;
float global_balance_angle = 0.0f;
float global_balance_rate = 0.0f;
float global_balance_accel_angle = 0.0f;
float global_balance_accel_forward_ms2 = 0.0f;
float global_chassis_yaw = 0.0f;
float global_chassis_yaw_rate = 0.0f;
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
volatile uint8_t imu_yaw_cal_state = INS_YAW_CAL_IDLE;
#else
volatile uint8_t imu_yaw_cal_state = INS_YAW_CAL_DONE;
#endif

/* ======================== SPI DMA 缓冲区 (参考 QGimbal) ======================== */

#define SPI_DMA_GYRO_LEN      8   /* 1(cmd) + 6(data) + 1(dummy) */
#define SPI_DMA_ACCEL_LEN     9   /* 1(cmd) + 1(dummy) + 6(data) + 1 */
#define SPI_DMA_TEMP_LEN      4   /* 1(cmd) + 1(dummy) + 2(data) */

#define BMI088_GYRO_RX_BUF_DATA_OFFSET  1
#define BMI088_ACCEL_RX_BUF_DATA_OFFSET 2

static uint8_t gyro_dma_rx_buf[SPI_DMA_GYRO_LEN];
static uint8_t gyro_dma_tx_buf[SPI_DMA_GYRO_LEN]  = {0x82, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t accel_dma_rx_buf[SPI_DMA_ACCEL_LEN];
static uint8_t accel_dma_tx_buf[SPI_DMA_ACCEL_LEN] = {0x92, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t temp_dma_rx_buf[SPI_DMA_TEMP_LEN];
static uint8_t temp_dma_tx_buf[SPI_DMA_TEMP_LEN]   = {0xA2, 0xFF, 0xFF, 0xFF};

/* ======================== DMA 状态标志 (参考 QGimbal) ======================== */

#define IMU_DR_SHFITS       0
#define IMU_SPI_SHFITS      1
#define IMU_UPDATE_SHFITS   2

static volatile uint8_t gyro_update_flag = 0;
static volatile uint8_t accel_update_flag = 0;
static volatile uint8_t accel_temp_update_flag = 0;
static volatile uint8_t imu_start_dma_flag = 0;

/* ======================== 模块实例 ======================== */

static BMI088_Instance_t    g_bmi088;
static KalmanAngle_t        g_balance_kalman;
static KalmanScalar_t       g_yaw_rate_kalman;
static Vofa_Instance_t      g_vofa;
static Track_Instance       g_track;
static Vision_Instance_t    g_vision;

/* INS 任务句柄 (用于 TaskNotify) */
static TaskHandle_t ins_task_handle_local = NULL;

/* IMU 数据 (DMA 解析后) */
static float imu_gyro[3]  = {0};
static float imu_accel[3] = {0};
static float imu_temp     = 0.0f;
static volatile uint8_t yaw_cal_request = 0U;
static volatile uint8_t balance_reseed_request = 0U;
static float yaw_bias_dps = CHASSIS_YAW_RATE_BIAS_DPS;

/* ======================== 前向声明 ======================== */

static void imu_cmd_spi_dma(void);
static void gyro_read_over(const uint8_t *rx_buf, float gyro_out[3]);
static void accel_read_over(const uint8_t *rx_buf, float accel_out[3]);
static void temperature_read_over(const uint8_t *rx_buf, float *temp_out);
static float calc_balance_accel_angle(const float accel[3]);
static float select_gyro_axis(const float gyro_dps[3], uint8_t axis);
static float wrap_angle_360_deg(float angle_deg);
static float wrap_angle_error_deg(float error_deg);
static void vofa_rx_byte_callback(uint8_t byte);
static uint8_t vofa_cmd_pop(uint8_t *line, uint8_t *len);

#define VOFA_CMD_BUF_LEN    64U
#define VOFA_CMD_QUEUE_LEN  16U
#define VOFA_TARGET_AIM_CHANNEL_COUNT 31U

static volatile uint8_t vofa_cmd_rx_len = 0U;
static volatile uint8_t vofa_cmd_rx_discard = 0U;
static uint8_t vofa_cmd_rx_buf[VOFA_CMD_BUF_LEN];
static volatile uint8_t vofa_cmd_head = 0U;
static volatile uint8_t vofa_cmd_tail = 0U;
static volatile uint8_t vofa_cmd_count = 0U;
static volatile uint32_t vofa_cmd_overflow_count = 0U;
static uint8_t vofa_cmd_queue[VOFA_CMD_QUEUE_LEN][VOFA_CMD_BUF_LEN];
static uint8_t vofa_cmd_len_queue[VOFA_CMD_QUEUE_LEN];

/* ======================== BSP <-> Device 粘合 ======================== */

static bool vofa_send_adapter(const uint8_t *data, uint16_t len)
{
    return BSP_UART6_SendBytes(data, len);
}

static void track_send_adapter(uint8_t *data, uint16_t len)
{
    (void)BSP_UART1_SendBytes(data, len);
}

/* ======================== 初始化 ======================== */

void INS_Init(void)
{
    /* ----- 1. BSP 初始化 ----- */
    BSP_DWT_Init();
    BSP_SPI_Init();
    BSP_UART6_Init();
    BSP_UART6_RegisterRxCallback(vofa_rx_byte_callback);

    Vofa_Init(&g_vofa, vofa_send_adapter);
    Vision_Init(&g_vision);
#if TRACKING_UART_ENABLE
    BSP_UART1_Init();
    Track_Init(&g_track, track_send_adapter);
    BSP_UART1_RegisterRxCallback(Track_OnRxByte);
#endif

    /* ----- 2. BMI088 初始化 (轮询模式，注入 BSP 函数指针) ----- */
    BMI088_Config_t bmi_config = {
        .spi_read_write = BSP_SPI_ReadWriteByte,
        .accel_cs_low   = BSP_SPI_AccelCS_Low,
        .accel_cs_high  = BSP_SPI_AccelCS_High,
        .gyro_cs_low    = BSP_SPI_GyroCS_Low,
        .gyro_cs_high   = BSP_SPI_GyroCS_High,
        .delay_us       = BSP_Delay_Us,
        .delay_ms       = BSP_Delay_Ms,
    };

    while (BMI088_Init(&g_bmi088, &bmi_config) != 0)
    {
        BSP_Delay_Ms(100);
    }

    /* ----- 3. 零飘校准 (已改为静态补偿，跳过阻塞 5 秒) ----- */
    // BMI088_CalibrateZeroDrift(&g_bmi088);
    // g_bmi088.calib_start_tick = xTaskGetTickCount();

    /* ----- 4. Kalman 滤波器初始化 ----- */
    KalmanAngle_Init(&g_balance_kalman,
                     BALANCE_KALMAN_Q_ANGLE,
                     BALANCE_KALMAN_Q_BIAS,
                     BALANCE_KALMAN_R_MEASURE);
    KalmanScalar_Init(&g_yaw_rate_kalman,
                      CHASSIS_YAW_RATE_KALMAN_Q,
                      CHASSIS_YAW_RATE_KALMAN_R);

    /* ----- 5. 切换到 SPI DMA 模式 (运行时高速读取) ----- */
    BSP_SPI_DMA_Init();
}

/* ======================== INS 主任务 ======================== */

/**
 * @brief  INS FreeRTOS 任务 — 由 DRDY 中断 + SPI DMA 驱动
 * @note   参考 QGimbal InsTask:
 *         DRDY EXTI → SPI DMA 传输 → DMA完成中断 → TaskNotify → Kalman
 *         实际频率由 BMI088 DRDY 决定 (~1kHz for gyro)
 *         DRDY/DMA未触发时使用SPI轮询降级，保证VOFA链路可见
 */
void INS_Task(void *argument)
{
    (void)argument;

    /* 保存任务句柄用于 ISR 中的 TaskNotify */
    ins_task_handle_local = xTaskGetCurrentTaskHandle();

    /* 启动 DMA 模式 */
    imu_start_dma_flag = 1;

    uint32_t vofa_last_send_tick = 0U;
    uint8_t vofa_header_sent = 0U;

    for (;;)
    {
        uint8_t has_new_gyro = 0U;
        float imu_dt_sec = BALANCE_IMU_DT_SEC;
        static float imu_time_sec = 0.0f;
        static uint32_t last_imu_update_us = 0U;
        static uint8_t imu_dt_ready = 0U;
#if VOFA_OUTPUT_ENABLE && (VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_ENCODER) && \
    (VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_IMU_ATTITUDE)
        static uint32_t loop_integrate_count = 0U;   /* 诊断：真实积分步计数 */
        static uint32_t loop_count_at_last_vofa = 0U; /* 诊断：上次VOFA时的积分步数 */
#endif
        uint32_t imu_now_us;
#if IMU_POLL_FALLBACK_ENABLE
        static uint32_t poll_fallback_divider = 0U;
#endif

        /* 等待 SPI DMA 完成通知；超时只用于让任务可恢复，不更新旧数据。 */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        imu_now_us = BSP_GetMicros();

        static float gyro_bias[3] = {0.0f, 0.0f, 0.0f};
#if IMU_GYRO_BIAS_CAL_ENABLE
        static float gyro_bias_sum[3] = {0.0f, 0.0f, 0.0f};
        static uint32_t gyro_bias_count = 0U;
        static uint8_t gyro_bias_ready = 0U;
#endif

        /* 处理 DMA 读取完成的数据 */
        if (gyro_update_flag & (1 << IMU_UPDATE_SHFITS))
        {
            gyro_update_flag &= ~(1 << IMU_UPDATE_SHFITS);
            gyro_read_over(gyro_dma_rx_buf + BMI088_GYRO_RX_BUF_DATA_OFFSET, imu_gyro);

            has_new_gyro = 1U;
        }

        if (accel_update_flag & (1 << IMU_UPDATE_SHFITS))
        {
            accel_update_flag &= ~(1 << IMU_UPDATE_SHFITS);
            accel_read_over(accel_dma_rx_buf + BMI088_ACCEL_RX_BUF_DATA_OFFSET, imu_accel);
        }

        if (accel_temp_update_flag & (1 << IMU_UPDATE_SHFITS))
        {
            accel_temp_update_flag &= ~(1 << IMU_UPDATE_SHFITS);
            temperature_read_over(temp_dma_rx_buf + BMI088_ACCEL_RX_BUF_DATA_OFFSET, &imu_temp);
        }

        if (!has_new_gyro) {
#if IMU_POLL_FALLBACK_ENABLE
            poll_fallback_divider++;
            if (poll_fallback_divider < IMU_POLL_FALLBACK_DIVIDER) {
                continue;
            }
            poll_fallback_divider = 0U;

            BMI088_Read(&g_bmi088);
            imu_gyro[0] = g_bmi088.gyro[0];
            imu_gyro[1] = g_bmi088.gyro[1];
            imu_gyro[2] = g_bmi088.gyro[2];
            imu_accel[0] = g_bmi088.accel[0];
            imu_accel[1] = g_bmi088.accel[1];
            imu_accel[2] = g_bmi088.accel[2];
            imu_temp = g_bmi088.temperature;
            imu_dt_sec = IMU_POLL_FALLBACK_DT_SEC * (float)IMU_POLL_FALLBACK_DIVIDER;
#else
            continue;
#endif
        }

        /* 针对 BMI088 芯片特性进行坐标轴重映射（极其重要）
         * BMI088 内部的加速度计和陀螺仪的坐标系是不同的！
         * 根据数据手册：Gyro 的 Y、Z 轴方向与 Accel 的 Y、Z 轴刚好相反。
         * 必须将它们统一到一个坐标系下，否则互补滤波会发生严重的轴耦合错乱。*/

        /* dt = 相邻“真实样本”之间的 DWT 微秒间隔。
         * 关键：只有走到这里才算一次真实样本（DMA新陀螺 或 fallback轮询），
         * 循环因多源唤醒可能空转，但空转会在上面 continue，不会到这里积分。
         * 不再把亚毫秒 dt 强拉成 1ms —— 那正是过积分的根源。
         * 保证：所有积分步的 dt 之和 == 真实流逝时间。 */
        if (imu_dt_ready) {
            uint32_t dt_us = imu_now_us - last_imu_update_us;  /* 无符号差值，自动处理回绕 */
            imu_dt_sec = (float)dt_us * 1e-6f;
        }
        last_imu_update_us = imu_now_us;
        imu_dt_ready = 1U;
        /* 只保留上限保护（首样本/长阻塞/回绕异常）；不设会伪造时间的下限。 */
        if (imu_dt_sec <= 0.0f || imu_dt_sec > 0.020f) {
            imu_dt_sec = BALANCE_IMU_DT_SEC;
        }
#if VOFA_OUTPUT_ENABLE && (VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_ENCODER) && \
    (VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_IMU_ATTITUDE)
        loop_integrate_count++;
#endif

#if IMU_GYRO_BIAS_CAL_ENABLE
        if (gyro_bias_ready == 0U) {
            gyro_bias_sum[0] += imu_gyro[0];
            gyro_bias_sum[1] += imu_gyro[1];
            gyro_bias_sum[2] += imu_gyro[2];
            gyro_bias_count++;

            if (gyro_bias_count >= IMU_GYRO_BIAS_CAL_SAMPLES) {
                float inv_count = 1.0f / (float)gyro_bias_count;
                gyro_bias[0] = gyro_bias_sum[0] * inv_count;
                gyro_bias[1] = gyro_bias_sum[1] * inv_count;
                gyro_bias[2] = gyro_bias_sum[2] * inv_count;
                gyro_bias_ready = 1U;
            }

            imu_gyro[0] = 0.0f;
            imu_gyro[1] = 0.0f;
            imu_gyro[2] = 0.0f;
        } else
#endif
        {
            imu_gyro[0] -= gyro_bias[0];
            imu_gyro[1] -= gyro_bias[1];
            imu_gyro[2] -= gyro_bias[2];
        }

        float gx_deg = imu_gyro[0] * Rad2Ang;
        float gy_deg = -imu_gyro[1] * Rad2Ang;
        float gz_deg = -imu_gyro[2] * Rad2Ang;
        float gyro_dps[3] = {gx_deg, gy_deg, gz_deg};
        float accel_angle_raw = calc_balance_accel_angle(imu_accel) *
                                BALANCE_ACCEL_ANGLE_SIGN -
                                BALANCE_ANGLE_OFFSET_DEG;
        static uint8_t accel_angle_lpf_ready = 0U;
        static float accel_angle_lpf = 0.0f;
        if (!accel_angle_lpf_ready) {
            accel_angle_lpf = accel_angle_raw;
            accel_angle_lpf_ready = 1U;
        } else {
            accel_angle_lpf += BALANCE_ACCEL_ANGLE_LPF_ALPHA *
                               (accel_angle_raw - accel_angle_lpf);
        }
        float accel_angle = accel_angle_lpf;
        float balance_rate = select_gyro_axis(gyro_dps, BALANCE_GYRO_AXIS) *
                             BALANCE_GYRO_SIGN;
        static float yaw_raw_angle = 0.0f;
        static float yaw_kalman_angle = 0.0f;
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
        static float yaw_cal_sum_dps = 0.0f;
        static uint32_t yaw_cal_count = 0U;
#endif
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
        if (yaw_cal_request != 0U) {
            yaw_cal_request = 0U;
            yaw_cal_sum_dps = 0.0f;
            yaw_cal_count = 0U;
            imu_yaw_cal_state = INS_YAW_CAL_RUNNING;
        }
#else
        yaw_cal_request = 0U;
#endif

        float yaw_rate_raw = select_gyro_axis(gyro_dps, CHASSIS_YAW_GYRO_AXIS) *
                             CHASSIS_YAW_GYRO_SIGN;
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
        if (imu_yaw_cal_state == INS_YAW_CAL_RUNNING) {
            yaw_cal_sum_dps += yaw_rate_raw;
            yaw_cal_count++;
            if (yaw_cal_count >= IMU_YAW_BIAS_KEY_CAL_SAMPLES) {
                yaw_bias_dps = yaw_cal_sum_dps / (float)yaw_cal_count;
                imu_yaw_cal_state = INS_YAW_CAL_DONE;
                yaw_raw_angle = 0.0f;
                yaw_kalman_angle = 0.0f;
                KalmanScalar_SetState(&g_yaw_rate_kalman, 0.0f);
            }
        }
#endif

        float yaw_rate = yaw_rate_raw - yaw_bias_dps;
        /* 先做小死区抑制残余零偏积分，再过标量 Kalman 平滑掉抽样噪声。
         * yaw 无重力参考，只能靠积分，任何未被压掉的小零偏都会线性漂移。 */
        if ((CHASSIS_YAW_RATE_DEADBAND_DPS > 0.0f) &&
            (fabsf(yaw_rate) < CHASSIS_YAW_RATE_DEADBAND_DPS)) {
            yaw_rate = 0.0f;
        }
        float yaw_rate_kalman = KalmanScalar_Update(&g_yaw_rate_kalman, yaw_rate);
        static uint8_t kalman_ready = 0U;
#if BALANCE_BOOT_ZERO_ENABLE
        static uint32_t zero_count = 0U;
        static float accel_zero_sum = 0.0f;
        static float kalman_zero_sum = 0.0f;
        static float accel_zero = 0.0f;
        static float kalman_zero = 0.0f;
#else
        const float kalman_zero = 0.0f;
#endif
        float kalman_angle;
        float kalman_angle_view;

        if (!kalman_ready) {
            KalmanAngle_SetAngle(&g_balance_kalman, accel_angle);
            kalman_ready = 1U;
        }

        /* KEY 解锁瞬间用当前加速度角重播种：故障期间的陀螺积分误差
         * 不带进新一轮控制，避免解锁后立刻再触发角度保护。 */
        if (balance_reseed_request != 0U) {
            balance_reseed_request = 0U;
            KalmanAngle_SetAngle(&g_balance_kalman, accel_angle);
        }

#if BALANCE_ACCEL_GATE_ENABLE
        /* 加速度模长偏离 1g 说明混入了车体平动加速度，此时加速度角
         * 是假角，只做陀螺积分，不让污染写进角度和零漂估计。 */
        float accel_norm_g = sqrtf(imu_accel[0] * imu_accel[0] +
                                   imu_accel[1] * imu_accel[1] +
                                   imu_accel[2] * imu_accel[2]) /
                             BALANCE_ACCEL_GRAVITY_MS2;
        float accel_innovation_deg;
        uint8_t accel_gate_reject = 0U;

        kalman_angle = KalmanAngle_Predict(&g_balance_kalman,
                                           balance_rate,
                                           imu_dt_sec);
        accel_innovation_deg = accel_angle - kalman_angle;

        if ((accel_norm_g < BALANCE_ACCEL_NORM_MIN_G) ||
            (accel_norm_g > BALANCE_ACCEL_NORM_MAX_G)) {
            accel_gate_reject = 1U;
        }
        if ((BALANCE_ACCEL_ANGLE_GATE_DEG > 0.0f) &&
            (fabsf(accel_innovation_deg) > BALANCE_ACCEL_ANGLE_GATE_DEG)) {
            accel_gate_reject = 1U;
        }

        if (accel_gate_reject == 0U) {
            kalman_angle = KalmanAngle_Correct(&g_balance_kalman,
                                               accel_angle);
        }
#else
        kalman_angle = KalmanAngle_Update(&g_balance_kalman,
                                          accel_angle,
                                          balance_rate,
                                          imu_dt_sec);
#endif

#if BALANCE_BOOT_ZERO_ENABLE
        if (zero_count < 1000U) {
            accel_zero_sum += accel_angle;
            kalman_zero_sum += kalman_angle;
            zero_count++;
            accel_zero = accel_zero_sum / (float)zero_count;
            kalman_zero = kalman_zero_sum / (float)zero_count;
        }
#endif

        kalman_angle_view = kalman_angle - kalman_zero;

        imu_time_sec += imu_dt_sec;
        global_balance_angle = kalman_angle_view;
        global_balance_rate = KalmanAngle_GetRate(&g_balance_kalman);
        global_balance_accel_angle = accel_angle;
        global_balance_accel_forward_ms2 = imu_accel[BALANCE_AXIS_X];
        yaw_raw_angle += yaw_rate * imu_dt_sec;
        yaw_kalman_angle += yaw_rate_kalman * imu_dt_sec;

        global_chassis_yaw_rate = yaw_rate_kalman;
        global_chassis_yaw = wrap_angle_360_deg(yaw_kalman_angle);

        global_gimbal_pitch = global_balance_angle;
        global_gimbal_yaw = global_chassis_yaw;
        global_imu_pitch_speed = global_balance_rate;
        global_imu_yaw_speed = global_chassis_yaw_rate;

#if VOFA_OUTPUT_ENABLE
        uint32_t now_tick = xTaskGetTickCount();
        if ((uint32_t)(now_tick - vofa_last_send_tick) >= pdMS_TO_TICKS(VOFA_OUTPUT_PERIOD_MS))
        {
#if VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_ENCODER
#if VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_MOTOR_FEEDBACK
            float vofa_left_motor_enabled = (float)left_wheel_motor.enabled;
            float vofa_right_motor_enabled = (float)right_wheel_motor.enabled;
            float vofa_left_can_speed_rpm = left_wheel_motor.speed_can_rpm;
            float vofa_right_can_speed_rpm = right_wheel_motor.speed_can_rpm;
            float vofa_left_feedback_current_a = left_wheel_motor.current;
            float vofa_right_feedback_current_a = right_wheel_motor.current;
            float vofa_left_rx_count = (float)left_wheel_motor.rx_count;
            float vofa_right_rx_count = (float)right_wheel_motor.rx_count;
            float vofa_last_can_id = (float)can_last_std_id;
            float vofa_last_can_dlc = (float)can_last_dlc;
            float vofa_can_rx_count = (float)can_rx_count;
            float vofa_fault_code = (float)chassis_fault_code;
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                vofa_left_motor_enabled,
                vofa_right_motor_enabled,
                vofa_left_can_speed_rpm,
                vofa_right_can_speed_rpm,
                vofa_left_feedback_current_a,
                vofa_right_feedback_current_a,
                vofa_left_rx_count,
                vofa_right_rx_count,
                vofa_last_can_id,
                vofa_last_can_dlc,
                vofa_can_rx_count,
                vofa_fault_code
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "left_enabled,right_enabled,left_can_rpm,right_can_rpm,left_feedback_a,right_feedback_a,left_rx_count,right_rx_count,last_can_id,last_can_dlc,can_rx_count,fault\r\n");
                vofa_header_sent = 1U;
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_IMU_ATTITUDE
            float vofa_kalman_balance_angle_deg = kalman_angle_view;
            float vofa_gyro_balance_rate_dps = global_balance_rate;
            float vofa_accel_balance_angle_deg = global_balance_accel_angle;
            float vofa_accel_x_ms2 = imu_accel[BALANCE_AXIS_X];
            float vofa_accel_y_ms2 = imu_accel[BALANCE_AXIS_Y];
            float vofa_accel_z_ms2 = imu_accel[BALANCE_AXIS_Z];
            float vofa_imu_dt_ms = imu_dt_sec * 1000.0f;
            float vofa_steps_per_vofa = (float)(loop_integrate_count - loop_count_at_last_vofa);
            loop_count_at_last_vofa = loop_integrate_count;
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                vofa_kalman_balance_angle_deg,
                vofa_gyro_balance_rate_dps,
                vofa_accel_balance_angle_deg,
                vofa_accel_x_ms2,
                vofa_accel_y_ms2,
                vofa_accel_z_ms2,
                vofa_imu_dt_ms,
                vofa_steps_per_vofa
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "kalman_theta_deg,gyro_theta_rate_dps,accel_theta_deg,accel_x_ms2,accel_y_ms2,accel_z_ms2,imu_dt_ms,steps_per_vofa\r\n");
                vofa_header_sent = 1U;
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_SPEED_LOOP
            float vofa_left_speed_reference_rpm = wheel_speed_ref_left_rpm;
            float vofa_left_actual_speed_rpm = wheel_speed_actual_left_rpm;
            float vofa_left_current_command_a = wheel_speed_output_left_a;
            float vofa_right_speed_reference_rpm = wheel_speed_ref_right_rpm;
            float vofa_right_actual_speed_rpm = wheel_speed_actual_right_rpm;
            float vofa_right_current_command_a = wheel_speed_output_right_a;
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                vofa_left_speed_reference_rpm,
                vofa_left_actual_speed_rpm,
                vofa_left_current_command_a,
                vofa_right_speed_reference_rpm,
                vofa_right_actual_speed_rpm,
                vofa_right_current_command_a
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "left_ref_rpm,left_actual_rpm,left_cmd_a,right_ref_rpm,right_actual_rpm,right_cmd_a\r\n");
                vofa_header_sent = 1U;
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_TRACK_ADC
            Track_Snapshot_t vofa_track_snapshot;
            ModeCommand_t vofa_mode_command;
            float vofa_track_error = TRACK_LOST_LINE;
            float vofa_track_black_mask = 0.0f;

            if (Track_GetSnapshot(&g_track, &vofa_track_snapshot) != 0U) {
                vofa_track_error = vofa_track_snapshot.error;
                vofa_track_black_mask =
                    (float)vofa_track_snapshot.black_mask;
            }
            Mode_GetCommand(&vofa_mode_command);
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                vofa_track_error,
                (float)chassis_mode,
                (float)balance_motion_state,
                vofa_mode_command.velocity_mps,
                balance_pid_velocity_ref_mps,
                balance_pid_velocity_mps,
                wheel_speed_ref_left_rpm,
                wheel_speed_actual_left_rpm,
                wheel_speed_ref_right_rpm,
                wheel_speed_actual_right_rpm,
                balance_pid_theta_ref_deg,
                balance_pid_theta_deg,
                vofa_track_black_mask,
                (float)Mode_SquareTrackingGetPhase(),
                (float)Mode_SquareTrackingGetCornerCount(),
                (float)balance_stop_brake_state
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "track_error,chassis_mode,motion_state,cmd_velocity_mps,target_velocity_mps,actual_velocity_mps,left_target_rpm,left_actual_rpm,right_target_rpm,right_actual_rpm,target_theta_deg,actual_theta_deg,black_mask,square_phase,square_corner_count,stop_brake_state\r\n");
                vofa_header_sent = 1U;
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_YAW_CAN
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                (float)gimbal_yaw_enable_queue_ok_count,
                (float)gimbal_yaw_enable_queue_fail_count,
                (float)can_gimbal_rx_count,
                (float)gimbal_yaw_motor.rx_count,
                (float)gimbal_yaw_motor.enabled,
                (float)can_last_std_id,
                (float)can_last_dlc,
                (float)can_rx_count,
                gimbal_yaw_motor.angle,
                gimbal_yaw_motor.speed_can_rpm,
                gimbal_yaw_motor.current,
                (float)Motor_FeedbackReady(
                    &gimbal_yaw_motor,
                    now_tick,
                    pdMS_TO_TICKS(GIMBAL_YAW_FEEDBACK_TIMEOUT_MS))
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "yaw_enable_queue_ok,yaw_enable_queue_fail,yaw_rx_count,yaw_motor_rx_count,yaw_enabled,last_can_id,last_can_dlc,can_rx_count,yaw_angle_deg,yaw_speed_rpm,yaw_feedback_a,yaw_feedback_fresh\r\n");
                vofa_header_sent = 1U;
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_TARGET_AIM
            static uint32_t vofa_vision_last_frame = 0U;
            static uint32_t vofa_vision_last_tick = 0U;
            Track_Snapshot_t vofa_track_snapshot;
            ModeCommand_t vofa_mode_command;
            GimbalCommand_t vofa_gimbal_command;
            CompetitionTaskStatus_t vofa_task_status;
            int16_t vofa_vision_dx = 0;
            int16_t vofa_vision_dy = 0;
            uint8_t vofa_vision_valid = 0U;
            float vofa_track_error = TRACK_LOST_LINE;
            float vofa_track_black_mask = 0.0f;
            float vofa_track_black_count = 0.0f;
            uint32_t vofa_vision_frame =
                INS_GetVisionTarget(&vofa_vision_dx,
                                    &vofa_vision_dy,
                                    &vofa_vision_valid);

            if (vofa_vision_frame != vofa_vision_last_frame) {
                vofa_vision_last_frame = vofa_vision_frame;
                if (vofa_vision_valid != 0U) {
                    vofa_vision_last_tick = now_tick;
                }
            }
            if (Track_GetSnapshot(&g_track, &vofa_track_snapshot) != 0U) {
                vofa_track_error = vofa_track_snapshot.error;
                vofa_track_black_mask =
                    (float)vofa_track_snapshot.black_mask;
                vofa_track_black_count =
                    (float)vofa_track_snapshot.black_count;
            }
            Mode_GetCommand(&vofa_mode_command);
            Mode_GimbalGetCommand(&vofa_gimbal_command);
            Task_GetStatus(&vofa_task_status);
            float vofa_channels[VOFA_TARGET_AIM_CHANNEL_COUNT] = {
                (float)vofa_vision_dx,
                (float)vofa_vision_dy,
                (float)vofa_vision_valid,
                (float)(now_tick - vofa_vision_last_tick),
                (float)chassis_mode,
                (float)Mode_TargetAimGetPhase(),
                (float)Mode_GimbalGet(),
                vofa_gimbal_command.target_angle_deg,
                gimbal_yaw_motor.angle,
                vofa_gimbal_command.error_deg,
                gimbal_yaw_motor.speed_can_rpm,
                vofa_gimbal_command.yaw_current_a,
                gimbal_yaw_motor.current,
                (float)Motor_FeedbackReady(
                    &gimbal_yaw_motor,
                    now_tick,
                    pdMS_TO_TICKS(GIMBAL_YAW_FEEDBACK_TIMEOUT_MS)),
                vofa_mode_command.velocity_mps,
                balance_pid_velocity_mps,
                (float)balance_motion_state,
                balance_pid_theta_ref_deg,
                balance_pid_theta_deg,
                vofa_mode_command.turn_rpm,
                vofa_track_error,
                vofa_track_black_mask,
                vofa_track_black_count,
                (float)gimbal_laser_on,
                (float)Mode_TrackingGetPhase(),
                (float)vofa_task_status.task_id,
                (float)vofa_task_status.phase,
                (float)chassis_fault_code,
                (float)emergency_stop,
                (float)chassis_motor_feedback_ok,
                (float)chassis_motor_enabled_ok
            };
            if (vofa_header_sent == 0U) {
                if (Vofa_SendText(
                        &g_vofa,
                        "dx,dy,valid,age_ms,mode,phase,gmode,yaw_ref,yaw,yaw_err,yaw_rpm,yaw_cmd_a,yaw_fb_a,yaw_fresh,v_ref,v,motion,theta_ref,theta,turn_ref,track_err,mask,count,laser,route_phase,task_id,task_phase,fault,estop,motor_feedback_ok,motor_enabled_ok\r\n")) {
                    vofa_header_sent = 1U;
                }
            }
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_STOP_CONTROL
            ModeCommand_t vofa_mode_command;

            Mode_GetCommand(&vofa_mode_command);
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                (float)chassis_mode,
                (float)Mode_TargetAimGetPhase(),
                (float)balance_motion_state,
                vofa_mode_command.velocity_mps,
                balance_pid_velocity_ref_mps,
                balance_pid_velocity_mps,
                balance_pid_position_ref_m,
                balance_pid_position_m,
                wheel_speed_ref_left_rpm,
                wheel_speed_actual_left_rpm,
                wheel_speed_ref_right_rpm,
                wheel_speed_actual_right_rpm,
                balance_pid_theta_ref_deg,
                balance_pid_theta_deg,
                (float)balance_stop_brake_state,
                balance_pid_turn_speed_ref_rpm
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(
                    &g_vofa,
                    "mode,target_phase,motion,cmd_v,ramp_v,v,stop_pos_ref,pos,left_ref_rpm,left_rpm,right_ref_rpm,right_rpm,theta_ref,theta,stop_state,turn_ref_rpm\r\n");
                vofa_header_sent = 1U;
            }
#else
            float vofa_balance_angle_deg = balance_pid_theta_deg;
            float vofa_balance_angle_reference_deg = balance_pid_theta_ref_deg;
            float vofa_balance_angle_rate_dps = balance_pid_theta_rate_dps;
            float vofa_speed_angle_reference_deg = balance_pid_speed_theta_ref_deg;
            float vofa_velocity_reference_mps = balance_pid_velocity_ref_mps;
            float vofa_velocity_mps = balance_pid_velocity_mps;
            float vofa_velocity_error_mps =
                vofa_velocity_reference_mps - vofa_velocity_mps;
            float vofa_balance_speed_reference_rpm = balance_pid_balance_speed_ref_rpm;
            float vofa_angle_speed_reference_rpm = balance_pid_angle_speed_ref_rpm;
            float vofa_rate_speed_reference_rpm = balance_pid_rate_speed_ref_rpm;
            float vofa_average_speed_reference_rpm =
                0.5f * (wheel_speed_ref_left_rpm + wheel_speed_ref_right_rpm);
            float vofa_average_actual_speed_rpm =
                0.5f * (wheel_speed_actual_left_rpm + wheel_speed_actual_right_rpm);
            float vofa_average_current_command_a =
                0.5f * (wheel_speed_output_left_a + wheel_speed_output_right_a);
            float vofa_zero_trim_deg = balance_pid_zero_trim_deg;
            float vofa_fault_code = (float)chassis_fault_code;
            float vofa_chassis_mode = (float)chassis_mode;
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                vofa_balance_angle_deg,
                vofa_balance_angle_reference_deg,
                vofa_balance_angle_rate_dps,
                vofa_speed_angle_reference_deg,
                vofa_velocity_reference_mps,
                vofa_velocity_mps,
                vofa_velocity_error_mps,
                vofa_balance_speed_reference_rpm,
                vofa_angle_speed_reference_rpm,
                vofa_rate_speed_reference_rpm,
                vofa_average_speed_reference_rpm,
                vofa_average_actual_speed_rpm,
                vofa_average_current_command_a,
                vofa_zero_trim_deg,
                vofa_fault_code,
                vofa_chassis_mode
            };
            if (vofa_header_sent == 0U) {
                (void)Vofa_SendText(&g_vofa,
                                    "theta_deg,theta_ref_deg,theta_rate_dps,speed_theta_ref_deg,velocity_ref_mps,velocity_mps,velocity_err_mps,balance_ref_rpm,angle_p_ref_rpm,rate_d_ref_rpm,avg_ref_rpm,avg_actual_rpm,avg_cmd_a,zero_trim_deg,fault,chassis_mode\r\n");
                vofa_header_sent = 1U;
            }
#endif
#elif VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_CAN_DIAG
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                (float)left_wheel_motor.enabled,
                (float)right_wheel_motor.enabled,
                (float)left_wheel_motor.rx_count,
                (float)right_wheel_motor.rx_count,
                (float)can_last_std_id,
                (float)can_rx_count
            };
#else
            float vofa_channels[VOFA_OUTPUT_CHANNEL_COUNT] = {
                kalman_angle_view,
                yaw_kalman_angle
            };
#endif

            vofa_last_send_tick = now_tick;
            (void)Vofa_SendCsvFloat(&g_vofa,
                                    vofa_channels,
                                    (uint8_t)(sizeof(vofa_channels) /
                                              sizeof(vofa_channels[0])));
        }
#endif

        /* ===== 指示灯: 急停红, 正常绿 ===== */
#if 0
        if (0) {
            HAL_GPIO_WritePin(GPIOH, GPIO_PIN_12, GPIO_PIN_SET);    /* 红 */
            HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_RESET);
        } else if (0) {
            HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);    /* 绿 */
            HAL_GPIO_WritePin(GPIOH, GPIO_PIN_12, GPIO_PIN_RESET);
        }
#endif

        /* 极端保护：如果由于瞬间并发数据塞爆了 RX_FIFO 导致 CAN 硬件报出 Overrun 错误锁死，
           我们必须强行清除错误并重新打开中断，否则后续永远进不了接收回调！ */
        extern CAN_HandleTypeDef hcan1;
        if (hcan1.ErrorCode != HAL_CAN_ERROR_NONE) {
            hcan1.ErrorCode = HAL_CAN_ERROR_NONE;
            __HAL_CAN_CLEAR_FLAG(&hcan1, CAN_FLAG_FOV0);
            __HAL_CAN_CLEAR_FLAG(&hcan1, CAN_FLAG_FOV1);
            HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
        }
    }
}

/* ======================== 蓝牙接收任务 ======================== */

void BT_RxTask(void *argument)
{
#if TRACKING_UART_ENABLE
    uint32_t track_consumed_frame = 0U;
#endif
    uint32_t task_status_last_tick = 0U;
    uint32_t task_status_last_version = 0U;

    (void)argument;

    for (;;)
    {
        /* 当前阶段蓝牙只作为 VOFA+ TX 物理链路，本任务保留为兼容占位。 */
        uint8_t line[VOFA_CMD_BUF_LEN];
        uint8_t len;
        while (vofa_cmd_pop(line, &len) != 0U) {
            (void)ChassisSerialCmd_HandleLine(line, len);
        }
        {
            CompetitionTaskStatus_t status;
            uint32_t now_tick = (uint32_t)xTaskGetTickCount();

            Task_GetStatus(&status);
            if ((status.task_id != TASK_ID_NONE) &&
                ((status.version != task_status_last_version) ||
                 ((uint32_t)(now_tick - task_status_last_tick) >=
                  pdMS_TO_TICKS(TASK_STATUS_PERIOD_MS)))) {
                char status_frame[40];
                size_t status_len =
                    Task_FormatStatusFrame(status_frame,
                                           sizeof(status_frame));

                if ((status_len != 0U) &&
                    BSP_UART6_SendBytes((const uint8_t *)status_frame,
                                       (uint16_t)status_len)) {
                    task_status_last_version = status.version;
                    task_status_last_tick = now_tick;
                }
            }
        }
#if TRACKING_UART_ENABLE
        {
            Track_Snapshot_t track_snapshot;

            if ((Track_GetSnapshot(&g_track, &track_snapshot) != 0U) &&
                (track_snapshot.frame_count != 0U) &&
                (track_snapshot.frame_count != track_consumed_frame)) {
                track_consumed_frame = track_snapshot.frame_count;
                Mode_TrackingSetSample(track_snapshot.error,
                                       track_snapshot.black_count,
                                       track_snapshot.black_mask,
                                       (uint32_t)xTaskGetTickCount());
            }
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* ======================== DMA 数据解析 (参考 QGimbal) ======================== */

static void vofa_rx_byte_callback(uint8_t byte)
{
    uint8_t i;

    if ((byte == '\r') || (byte == '\n')) {
        if (vofa_cmd_rx_discard != 0U) {
            vofa_cmd_rx_discard = 0U;
            vofa_cmd_rx_len = 0U;
            return;
        }
        if (vofa_cmd_rx_len > 0U) {
            Vision_ParseResult_t vision_result =
                Vision_ParseLine(&g_vision,
                                 vofa_cmd_rx_buf,
                                 vofa_cmd_rx_len);
            if (vision_result != VISION_PARSE_NOT_VISION) {
                vofa_cmd_rx_len = 0U;
                return;
            }
            if (vofa_cmd_count < VOFA_CMD_QUEUE_LEN) {
                for (i = 0U; i < vofa_cmd_rx_len; i++) {
                    vofa_cmd_queue[vofa_cmd_head][i] = vofa_cmd_rx_buf[i];
                }
                vofa_cmd_len_queue[vofa_cmd_head] = vofa_cmd_rx_len;
                vofa_cmd_head++;
                if (vofa_cmd_head >= VOFA_CMD_QUEUE_LEN) {
                    vofa_cmd_head = 0U;
                }
                vofa_cmd_count++;
            } else {
                vofa_cmd_overflow_count++;
            }
        }
        vofa_cmd_rx_len = 0U;
        return;
    }

    if (vofa_cmd_rx_discard != 0U) {
        return;
    }

    if (vofa_cmd_rx_len < VOFA_CMD_BUF_LEN) {
        vofa_cmd_rx_buf[vofa_cmd_rx_len++] = byte;
    } else {
        vofa_cmd_rx_len = 0U;
        vofa_cmd_rx_discard = 1U;
        vofa_cmd_overflow_count++;
    }
}

static uint8_t vofa_cmd_pop(uint8_t *line, uint8_t *len)
{
    uint8_t i;
    uint8_t local_len;
    uint8_t local_tail;

    if (line == NULL || len == NULL) {
        return 0U;
    }

    taskENTER_CRITICAL();
    if (vofa_cmd_count == 0U) {
        taskEXIT_CRITICAL();
        return 0U;
    }

    local_tail = vofa_cmd_tail;
    local_len = vofa_cmd_len_queue[local_tail];
    for (i = 0U; i < local_len; i++) {
        line[i] = vofa_cmd_queue[local_tail][i];
    }
    vofa_cmd_tail++;
    if (vofa_cmd_tail >= VOFA_CMD_QUEUE_LEN) {
        vofa_cmd_tail = 0U;
    }
    vofa_cmd_count--;
    taskEXIT_CRITICAL();

    *len = local_len;
    return 1U;
}

static void gyro_read_over(const uint8_t *rx_buf, float gyro_out[3])
{
    int16_t raw;
    raw = (int16_t)((rx_buf[1] << 8) | rx_buf[0]);
    gyro_out[0] = raw * BMI088_GYRO_2000_SEN;
    raw = (int16_t)((rx_buf[3] << 8) | rx_buf[2]);
    gyro_out[1] = raw * BMI088_GYRO_2000_SEN;
    raw = (int16_t)((rx_buf[5] << 8) | rx_buf[4]);
    gyro_out[2] = raw * BMI088_GYRO_2000_SEN;
}

static void accel_read_over(const uint8_t *rx_buf, float accel_out[3])
{
    int16_t raw;
    raw = (int16_t)((rx_buf[1] << 8) | rx_buf[0]);
    accel_out[0] = raw * BMI088_ACCEL_3G_SEN;
    raw = (int16_t)((rx_buf[3] << 8) | rx_buf[2]);
    accel_out[1] = raw * BMI088_ACCEL_3G_SEN;
    raw = (int16_t)((rx_buf[5] << 8) | rx_buf[4]);
    accel_out[2] = raw * BMI088_ACCEL_3G_SEN;
}

static void temperature_read_over(const uint8_t *rx_buf, float *temp_out)
{
    int16_t raw = (int16_t)((rx_buf[0] << 3) | (rx_buf[1] >> 5));
    if (raw > 1023) raw -= 2048;
    *temp_out = raw * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;
}

static float calc_balance_accel_angle(const float accel[3])
{
#if BALANCE_ACCEL_ANGLE_MODE == BALANCE_ACCEL_ANGLE_ROLL
    return atan2f(accel[1], accel[2]) * Rad2Ang;
#else
    return atan2f(-accel[0],
                  sqrtf(accel[1] * accel[1] + accel[2] * accel[2])) * Rad2Ang;
#endif
}

static float select_gyro_axis(const float gyro_dps[3], uint8_t axis)
{
    if (axis == BALANCE_AXIS_Y) {
        return gyro_dps[1];
    }
    if (axis == BALANCE_AXIS_Z) {
        return gyro_dps[2];
    }
    return gyro_dps[0];
}

static float wrap_angle_360_deg(float angle_deg)
{
    angle_deg = fmodf(angle_deg, 360.0f);
    if (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static float wrap_angle_error_deg(float error_deg)
{
    error_deg = fmodf(error_deg + 180.0f, 360.0f);
    if (error_deg < 0.0f) {
        error_deg += 360.0f;
    }
    return error_deg - 180.0f;
}

/* ======================== SPI DMA 调度 (参考 QGimbal) ======================== */

/**
 * @brief  根据标志位启动下一次 SPI DMA 传输
 *         优先级: gyro > accel > temp
 */
static void imu_cmd_spi_dma(void)
{
    /* 陀螺仪 DMA */
    if ((gyro_update_flag & (1 << IMU_DR_SHFITS))
        && !(accel_update_flag & (1 << IMU_SPI_SHFITS))
        && !(accel_temp_update_flag & (1 << IMU_SPI_SHFITS)))
    {
        gyro_update_flag &= ~(1 << IMU_DR_SHFITS);
        gyro_update_flag |= (1 << IMU_SPI_SHFITS);

        BSP_SPI_GyroCS_Low();
        BSP_SPI_DMA_Enable((uint32_t)gyro_dma_tx_buf,
                           (uint32_t)gyro_dma_rx_buf,
                           SPI_DMA_GYRO_LEN);
        return;
    }

    /* 加速度计 DMA */
    if ((accel_update_flag & (1 << IMU_DR_SHFITS))
        && !(gyro_update_flag & (1 << IMU_SPI_SHFITS))
        && !(accel_temp_update_flag & (1 << IMU_SPI_SHFITS)))
    {
        accel_update_flag &= ~(1 << IMU_DR_SHFITS);
        accel_update_flag |= (1 << IMU_SPI_SHFITS);

        BSP_SPI_AccelCS_Low();
        BSP_SPI_DMA_Enable((uint32_t)accel_dma_tx_buf,
                           (uint32_t)accel_dma_rx_buf,
                           SPI_DMA_ACCEL_LEN);
        return;
    }

    /* 温度 DMA */
    if ((accel_temp_update_flag & (1 << IMU_DR_SHFITS))
        && !(gyro_update_flag & (1 << IMU_SPI_SHFITS))
        && !(accel_update_flag & (1 << IMU_SPI_SHFITS)))
    {
        accel_temp_update_flag &= ~(1 << IMU_DR_SHFITS);
        accel_temp_update_flag |= (1 << IMU_SPI_SHFITS);

        BSP_SPI_AccelCS_Low();
        BSP_SPI_DMA_Enable((uint32_t)temp_dma_tx_buf,
                           (uint32_t)temp_dma_rx_buf,
                           SPI_DMA_TEMP_LEN);
        return;
    }
}

/* ======================== EXTI DRDY 中断回调 ======================== */

/**
 * @brief  GPIO EXTI 回调 (HAL 框架调用)
 *         PC4 = INT1_ACCEL (DRDY), PC5 = INT1_GYRO (DRDY)
 *         参考 QGimbal: HAL_GPIO_EXTI_Callback
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == INT1_ACCEL_Pin)
    {
        accel_update_flag |= (1 << IMU_DR_SHFITS);
        accel_temp_update_flag |= (1 << IMU_DR_SHFITS);
        if (imu_start_dma_flag)
            imu_cmd_spi_dma();
    }
    else if (GPIO_Pin == INT1_GRYO_Pin)
    {
        gyro_update_flag |= (1 << IMU_DR_SHFITS);
        if (imu_start_dma_flag)
            imu_cmd_spi_dma();
    }
}

/* ======================== SPI DMA 完成回调 ======================== */

/**
 * @brief  SPI1 RX DMA 完成回调 (覆盖 BSP 弱定义)
 *         参考 QGimbal: DMA2_Stream0_IRQHandler 内部逻辑
 */
void BSP_SPI_DMA_RxCpltCallback(void)
{
    /* 陀螺仪读取完成 */
    if (gyro_update_flag & (1 << IMU_SPI_SHFITS))
    {
        gyro_update_flag &= ~(1 << IMU_SPI_SHFITS);
        gyro_update_flag |= (1 << IMU_UPDATE_SHFITS);
        BSP_SPI_GyroCS_High();
    }

    /* 加速度计读取完成 */
    if (accel_update_flag & (1 << IMU_SPI_SHFITS))
    {
        accel_update_flag &= ~(1 << IMU_SPI_SHFITS);
        accel_update_flag |= (1 << IMU_UPDATE_SHFITS);
        BSP_SPI_AccelCS_High();
    }

    /* 温度读取完成 */
    if (accel_temp_update_flag & (1 << IMU_SPI_SHFITS))
    {
        accel_temp_update_flag &= ~(1 << IMU_SPI_SHFITS);
        accel_temp_update_flag |= (1 << IMU_UPDATE_SHFITS);
        BSP_SPI_AccelCS_High();
    }

    /* 继续下一个 DMA 传输 */
    imu_cmd_spi_dma();

    /* 陀螺仪数据就绪时通知 INS 任务 */
    if (gyro_update_flag & (1 << IMU_UPDATE_SHFITS))
    {
        if (ins_task_handle_local != NULL)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(ins_task_handle_local, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

/* ======================== 数据获取 ======================== */

float INS_GetYaw(void)       { return global_chassis_yaw; }
float INS_GetPitch(void)     { return Pitch_a; }
float INS_GetRoll(void)      { return Roll_a; }
float INS_GetYawTotal(void)  { return global_chassis_yaw; }
float INS_GetBalanceAngle(void) { return global_balance_angle; }
float INS_GetBalanceRate(void) { return global_balance_rate; }
float INS_GetChassisYaw(void) { return global_chassis_yaw; }
float INS_GetChassisYawRate(void) { return global_chassis_yaw_rate; }
float INS_GetChassisYawError(float target_yaw_deg)
{
    return wrap_angle_error_deg(wrap_angle_360_deg(target_yaw_deg) -
                                global_chassis_yaw);
}
uint8_t INS_IsCalibrated(void) { return g_bmi088.calibrated; }
void INS_RequestYawCalibration(void)
{
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
    yaw_cal_request = 1U;
#else
    yaw_cal_request = 0U;
    imu_yaw_cal_state = INS_YAW_CAL_DONE;
#endif
}

uint8_t INS_GetYawCalibrationState(void)
{
    return imu_yaw_cal_state;
}

float INS_GetYawBiasDps(void)
{
    return yaw_bias_dps;
}

void INS_RequestBalanceAngleReseed(void)
{
    balance_reseed_request = 1U;
}

uint32_t INS_GetVisionTarget(int16_t *dx, int16_t *dy, uint8_t *valid)
{
    return Vision_GetSnapshot(&g_vision, dx, dy, valid);
}

uint32_t INS_GetVisionTaskTarget(Vision_TaskSnapshot_t *snapshot)
{
    return Vision_GetTaskSnapshot(&g_vision, snapshot);
}
