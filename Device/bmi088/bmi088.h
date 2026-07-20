#ifndef BMI088_H
#define BMI088_H

/**
 * @file    bmi088.h
 * @brief   Device层 — BMI088 六轴 IMU 驱动 (OOP + Register 机制)
 * @note    通过函数指针注入 BSP 层操作，实现硬件解耦
 *          支持 5 秒采样最小二乘线性拟合零飘校准
 */

#include <stdint.h>

/* ======================== 校准参数 ======================== */

#define BMI088_CALIB_SAMPLE_FREQ    200U    /* 校准采样频率 (Hz) */
#define BMI088_CALIB_DURATION_S     5U      /* 校准持续时间 (秒) */
#define BMI088_CALIB_TOTAL_SAMPLES  (BMI088_CALIB_SAMPLE_FREQ * BMI088_CALIB_DURATION_S)

/* ======================== 函数指针类型 ======================== */

typedef uint8_t (*BMI088_SpiReadWrite_t)(uint8_t txdata);
typedef void    (*BMI088_CsCtrl_t)(void);
typedef void    (*BMI088_DelayUs_t)(uint16_t us);
typedef void    (*BMI088_DelayMs_t)(uint16_t ms);

/* ======================== 配置结构体 (注入BSP) ======================== */

typedef struct
{
    BMI088_SpiReadWrite_t spi_read_write;   /* SPI 全双工读写 */
    BMI088_CsCtrl_t       accel_cs_low;     /* 加速度计 CS 拉低 */
    BMI088_CsCtrl_t       accel_cs_high;    /* 加速度计 CS 拉高 */
    BMI088_CsCtrl_t       gyro_cs_low;      /* 陀螺仪 CS 拉低 */
    BMI088_CsCtrl_t       gyro_cs_high;     /* 陀螺仪 CS 拉高 */
    BMI088_DelayUs_t      delay_us;         /* 微秒延时 */
    BMI088_DelayMs_t      delay_ms;         /* 毫秒延时 */
} BMI088_Config_t;

/* ======================== 实例结构体 ======================== */

typedef struct
{
    /* BSP 函数指针 (由 Register 注入) */
    BMI088_SpiReadWrite_t spi_read_write;
    BMI088_CsCtrl_t       accel_cs_low;
    BMI088_CsCtrl_t       accel_cs_high;
    BMI088_CsCtrl_t       gyro_cs_low;
    BMI088_CsCtrl_t       gyro_cs_high;
    BMI088_DelayUs_t      delay_us;
    BMI088_DelayMs_t      delay_ms;

    /* 传感器数据 (物理量) */
    float accel[3];         /* 加速度 (g) */
    float gyro[3];          /* 角速度 (rad/s), 已扣除零飘 */
    float temperature;      /* 温度 (°C) */

    /* 零飘校准参数 */
    float gyro_offset[3];   /* 陀螺仪零偏截距 b (rad/s) */
    float gyro_drift[3];    /* 陀螺仪漂移斜率 a (rad/s²) */
    uint8_t calibrated;     /* 校准完成标志 */
    uint32_t calib_start_tick; /* 校准起始时间戳 (ms) */
} BMI088_Instance_t;

/* ======================== 接口函数 ======================== */

/**
 * @brief  初始化 BMI088 实例 (注册 BSP 函数 + 配置传感器寄存器)
 * @param  inst    实例指针 (调用者分配)
 * @param  config  配置结构体 (BSP 函数指针)
 * @retval 0=成功, 非0=错误码 (BMI088_Error_e)
 */
uint8_t BMI088_Init(BMI088_Instance_t *inst, const BMI088_Config_t *config);

/**
 * @brief  读取 BMI088 传感器数据
 * @param  inst 实例指针
 * @note   读取后 inst->accel[], inst->gyro[], inst->temperature 更新
 *         如已校准，gyro 自动减去零偏
 */
void BMI088_Read(BMI088_Instance_t *inst);

/**
 * @brief  执行 5 秒零飘校准 (阻塞)
 * @param  inst 实例指针
 * @note   使用最小二乘线性拟合 y = a*t + b
 *         校准完成后 inst->calibrated = 1
 *         调用时 IMU 必须保持静止
 */
void BMI088_CalibrateZeroDrift(BMI088_Instance_t *inst);

#endif /* BMI088_H */
