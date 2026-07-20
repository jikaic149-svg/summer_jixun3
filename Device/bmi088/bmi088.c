/**
 * @file    bmi088.c
 * @brief   Device层 — BMI088 六轴 IMU 驱动实现
 * @note    通过函数指针调用 BSP 层，零硬件耦合
 *          含最小二乘线性拟合零飘校准
 */

#include "bmi088.h"
#include "bmi088_reg.h"
#include <string.h>

/* ======================== 灵敏度 (当前配置) ======================== */

static const float accel_sen = BMI088_ACCEL_3G_SEN;
static const float gyro_sen  = BMI088_GYRO_2000_SEN;

/* ======================== SPI 读写宏 ======================== */

/* 加速度计: SPI 读取需要一个 dummy byte */
#define ACC_WRITE_REG(inst, reg, data)   \
    do {                                 \
        (inst)->accel_cs_low();          \
        (inst)->spi_read_write(reg);     \
        (inst)->spi_read_write(data);    \
        (inst)->accel_cs_high();         \
    } while (0)

#define ACC_READ_REG(inst, reg, result)          \
    do {                                         \
        (inst)->accel_cs_low();                  \
        (inst)->spi_read_write((reg) | 0x80);    \
        (inst)->spi_read_write(0x55);            \
        (result) = (inst)->spi_read_write(0x55); \
        (inst)->accel_cs_high();                 \
    } while (0)

#define ACC_READ_MULI(inst, reg, buf, len)       \
    do {                                         \
        (inst)->accel_cs_low();                  \
        (inst)->spi_read_write((reg) | 0x80);    \
        (inst)->spi_read_write(0x55);            \
        for (uint8_t _i = 0; _i < (len); _i++)  \
            (buf)[_i] = (inst)->spi_read_write(0x55); \
        (inst)->accel_cs_high();                 \
    } while (0)

/* 陀螺仪: SPI 读取无 dummy byte */
#define GYRO_WRITE_REG(inst, reg, data)  \
    do {                                 \
        (inst)->gyro_cs_low();           \
        (inst)->spi_read_write(reg);     \
        (inst)->spi_read_write(data);    \
        (inst)->gyro_cs_high();          \
    } while (0)

#define GYRO_READ_REG(inst, reg, result)             \
    do {                                             \
        (inst)->gyro_cs_low();                       \
        (inst)->spi_read_write((reg) | 0x80);        \
        (result) = (inst)->spi_read_write(0x55);     \
        (inst)->gyro_cs_high();                      \
    } while (0)

#define GYRO_READ_MULI(inst, reg, buf, len)          \
    do {                                             \
        (inst)->gyro_cs_low();                       \
        (inst)->spi_read_write((reg) | 0x80);        \
        for (uint8_t _i = 0; _i < (len); _i++)      \
            (buf)[_i] = (inst)->spi_read_write(0x55);\
        (inst)->gyro_cs_high();                      \
    } while (0)

/* ======================== 寄存器配置表 ======================== */

static const uint8_t accel_reg_config[BMI088_WRITE_ACCEL_REG_NUM][3] =
{
    {BMI088_ACC_PWR_CTRL,  BMI088_ACC_ENABLE_ACC_ON,                                                            BMI088_ACC_PWR_CTRL_ERROR},
    {BMI088_ACC_PWR_CONF,  BMI088_ACC_PWR_ACTIVE_MODE,                                                          BMI088_ACC_PWR_CONF_ERROR},
    {BMI088_ACC_CONF,      BMI088_ACC_NORMAL | BMI088_ACC_800_HZ | BMI088_ACC_CONF_MUST_Set,                     BMI088_ACC_CONF_ERROR},
    {BMI088_ACC_RANGE,     BMI088_ACC_RANGE_3G,                                                                  BMI088_ACC_RANGE_ERROR},
    {BMI088_INT1_IO_CTRL,  BMI088_ACC_INT1_IO_ENABLE | BMI088_ACC_INT1_GPIO_PP | BMI088_ACC_INT1_GPIO_LOW,       BMI088_INT1_IO_CTRL_ERROR},
    {BMI088_INT_MAP_DATA,  BMI088_ACC_INT1_DRDY_INTERRUPT,                                                       BMI088_INT_MAP_DATA_ERROR},
};

static const uint8_t gyro_reg_config[BMI088_WRITE_GYRO_REG_NUM][3] =
{
    {BMI088_GYRO_RANGE,               BMI088_GYRO_2000,                                          BMI088_GYRO_RANGE_ERROR},
    {BMI088_GYRO_BANDWIDTH,           BMI088_GYRO_1000_116_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set,  BMI088_GYRO_BANDWIDTH_ERROR},
    {BMI088_GYRO_LPM1,               BMI088_GYRO_NORMAL_MODE,                                    BMI088_GYRO_LPM1_ERROR},
    {BMI088_GYRO_CTRL,                BMI088_DRDY_ON,                                             BMI088_GYRO_CTRL_ERROR},
    {BMI088_GYRO_INT3_INT4_IO_CONF,   BMI088_GYRO_INT3_GPIO_PP | BMI088_GYRO_INT3_GPIO_LOW,      BMI088_GYRO_INT3_INT4_IO_CONF_ERROR},
    {BMI088_GYRO_INT3_INT4_IO_MAP,    BMI088_GYRO_DRDY_IO_INT3,                                  BMI088_GYRO_INT3_INT4_IO_MAP_ERROR},
};

/* ======================== 内部初始化 ======================== */

static uint8_t bmi088_accel_init(BMI088_Instance_t *inst)
{
    uint8_t res = 0;

    /* 通信检测 (加速度计需要读两次) */
    ACC_READ_REG(inst, BMI088_ACC_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    ACC_READ_REG(inst, BMI088_ACC_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    /* 软复位 */
    ACC_WRITE_REG(inst, BMI088_ACC_SOFTRESET, BMI088_ACC_SOFTRESET_VALUE);
    inst->delay_ms(BMI088_LONG_DELAY_TIME);

    /* 复位后再次检测 */
    ACC_READ_REG(inst, BMI088_ACC_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    ACC_READ_REG(inst, BMI088_ACC_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    if (res != BMI088_ACC_CHIP_ID_VALUE)
        return BMI088_NO_SENSOR;

    /* 配置寄存器并校验 */
    for (uint8_t i = 0; i < BMI088_WRITE_ACCEL_REG_NUM; i++)
    {
        ACC_WRITE_REG(inst, accel_reg_config[i][0], accel_reg_config[i][1]);
        inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        ACC_READ_REG(inst, accel_reg_config[i][0], res);
        inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        if (res != accel_reg_config[i][1])
            return accel_reg_config[i][2];
    }

    return BMI088_NO_ERROR;
}

static uint8_t bmi088_gyro_init(BMI088_Instance_t *inst)
{
    uint8_t res = 0;

    /* 通信检测 */
    GYRO_READ_REG(inst, BMI088_GYRO_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    GYRO_READ_REG(inst, BMI088_GYRO_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    /* 软复位 */
    GYRO_WRITE_REG(inst, BMI088_GYRO_SOFTRESET, BMI088_GYRO_SOFTRESET_VALUE);
    inst->delay_ms(BMI088_LONG_DELAY_TIME);

    /* 复位后检测 */
    GYRO_READ_REG(inst, BMI088_GYRO_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    GYRO_READ_REG(inst, BMI088_GYRO_CHIP_ID, res);
    inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    if (res != BMI088_GYRO_CHIP_ID_VALUE)
        return BMI088_NO_SENSOR;

    /* 配置寄存器并校验 */
    for (uint8_t i = 0; i < BMI088_WRITE_GYRO_REG_NUM; i++)
    {
        GYRO_WRITE_REG(inst, gyro_reg_config[i][0], gyro_reg_config[i][1]);
        inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        GYRO_READ_REG(inst, gyro_reg_config[i][0], res);
        inst->delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        if (res != gyro_reg_config[i][1])
            return gyro_reg_config[i][2];
    }

    return BMI088_NO_ERROR;
}

/* ======================== 公共接口 ======================== */

uint8_t BMI088_Init(BMI088_Instance_t *inst, const BMI088_Config_t *config)
{
    /* 注入 BSP 函数指针 */
    inst->spi_read_write = config->spi_read_write;
    inst->accel_cs_low   = config->accel_cs_low;
    inst->accel_cs_high  = config->accel_cs_high;
    inst->gyro_cs_low    = config->gyro_cs_low;
    inst->gyro_cs_high   = config->gyro_cs_high;
    inst->delay_us       = config->delay_us;
    inst->delay_ms       = config->delay_ms;

    /* 清零数据 */
    memset(inst->accel, 0, sizeof(inst->accel));
    memset(inst->gyro, 0, sizeof(inst->gyro));
    memset(inst->gyro_offset, 0, sizeof(inst->gyro_offset));
    memset(inst->gyro_drift, 0, sizeof(inst->gyro_drift));
    inst->temperature = 0.0f;
    inst->calibrated = 0;
    inst->calib_start_tick = 0;

    /* 初始化传感器 */
    uint8_t error = BMI088_NO_ERROR;
    error |= bmi088_accel_init(inst);
    error |= bmi088_gyro_init(inst);

    return error;
}

void BMI088_Read(BMI088_Instance_t *inst)
{
    uint8_t buf[8] = {0};
    int16_t raw;

    /* ----- 读取加速度 (6字节) ----- */
    ACC_READ_MULI(inst, BMI088_ACCEL_XOUT_L, buf, 6);

    raw = (int16_t)((buf[1] << 8) | buf[0]);
    inst->accel[0] = raw * accel_sen;
    raw = (int16_t)((buf[3] << 8) | buf[2]);
    inst->accel[1] = raw * accel_sen;
    raw = (int16_t)((buf[5] << 8) | buf[4]);
    inst->accel[2] = raw * accel_sen;

    /* ----- 读取陀螺仪 (先读 CHIP_ID + 6字节陀螺仪数据) ----- */
    GYRO_READ_MULI(inst, BMI088_GYRO_CHIP_ID, buf, 8);

    if (buf[0] == BMI088_GYRO_CHIP_ID_VALUE)
    {
        raw = (int16_t)((buf[3] << 8) | buf[2]);
        inst->gyro[0] = raw * gyro_sen;
        raw = (int16_t)((buf[5] << 8) | buf[4]);
        inst->gyro[1] = raw * gyro_sen;
        raw = (int16_t)((buf[7] << 8) | buf[6]);
        inst->gyro[2] = raw * gyro_sen;

        /* 如已校准，减去零偏 (简化: 仅减截距，忽略微小漂移斜率) */
        if (inst->calibrated)
        {
            inst->gyro[0] -= inst->gyro_offset[0];
            inst->gyro[1] -= inst->gyro_offset[1];
            inst->gyro[2] -= inst->gyro_offset[2];
        }
    }

    /* ----- 读取温度 (2字节) ----- */
    ACC_READ_MULI(inst, BMI088_TEMP_M, buf, 2);

    raw = (int16_t)((buf[0] << 3) | (buf[1] >> 5));
    if (raw > 1023)
        raw -= 2048;
    inst->temperature = raw * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;
}

/* ======================== 最小二乘零飘校准 ======================== */

/**
 * @brief  5秒采样 + 最小二乘线性拟合
 * @note   拟合模型: gyro(t) = drift * t + offset
 *
 *         最小二乘公式:
 *         drift = (N * Σ(ti*yi) - Σti * Σyi) / (N * Σ(ti²) - (Σti)²)
 *         offset = (Σyi - drift * Σti) / N
 *
 *         其中 ti = 采样时间(秒), yi = 陀螺仪原始值(rad/s)
 *
 *         为节省 RAM，使用在线累加（不存储所有样本）
 */
void BMI088_CalibrateZeroDrift(BMI088_Instance_t *inst)
{
    const uint32_t N = BMI088_CALIB_TOTAL_SAMPLES;
    const float dt = 1.0f / (float)BMI088_CALIB_SAMPLE_FREQ; /* 5ms */

    /* 累加变量 (double 防止精度溢出) */
    double sum_t       = 0.0;
    double sum_t2      = 0.0;
    double sum_y[3]    = {0.0, 0.0, 0.0};
    double sum_ty[3]   = {0.0, 0.0, 0.0};

    float filtered_gy[3] = {0.0f, 0.0f, 0.0f};
    uint8_t first_sample = 1;
    const float alpha = 0.02f; /* 强低通滤波，有效滤除高频振动影响最小二乘斜率 */
    
    uint32_t valid_N = 0; /* 极其重要：必须记录真实有效采样数，防止分母被错误放大 */

    inst->calibrated = 0;

    for (uint32_t i = 0; i < N; i++)
    {
        float t = (float)i * dt;

        /* 读取原始陀螺仪 (不做补偿) */
        uint8_t buf[8] = {0};
        int16_t raw;

        GYRO_READ_MULI(inst, BMI088_GYRO_CHIP_ID, buf, 8);

        if (buf[0] == BMI088_GYRO_CHIP_ID_VALUE)
        {
            float gy[3];
            raw = (int16_t)((buf[3] << 8) | buf[2]);
            gy[0] = raw * gyro_sen;
            raw = (int16_t)((buf[5] << 8) | buf[4]);
            gy[1] = raw * gyro_sen;
            raw = (int16_t)((buf[7] << 8) | buf[6]);
            gy[2] = raw * gyro_sen;

            if (first_sample) {
                for (int ax = 0; ax < 3; ax++) filtered_gy[ax] = gy[ax];
                first_sample = 0;
            } else {
                for (int ax = 0; ax < 3; ax++) {
                    filtered_gy[ax] = filtered_gy[ax] + alpha * (gy[ax] - filtered_gy[ax]);
                }
            }

            sum_t  += t;
            sum_t2 += (double)t * t;

            for (int ax = 0; ax < 3; ax++)
            {
                sum_y[ax]  += filtered_gy[ax];
                sum_ty[ax] += (double)t * filtered_gy[ax];
            }
            
            valid_N++;
        }

        /* 等待采样间隔 */
        inst->delay_ms(1000U / BMI088_CALIB_SAMPLE_FREQ);
    }

    /* 最小二乘求解 (严格使用真实的 valid_N) */
    double denom = (double)valid_N * sum_t2 - sum_t * sum_t;

    if (valid_N > 100 && (denom > 1e-5 || denom < -1e-5)) /* 防除零及样本过少 */
    {
        for (int ax = 0; ax < 3; ax++)
        {
            inst->gyro_drift[ax]  = (float)(((double)valid_N * sum_ty[ax] - sum_t * sum_y[ax]) / denom);
            inst->gyro_offset[ax] = (float)((sum_y[ax] - (double)inst->gyro_drift[ax] * sum_t) / (double)valid_N);
        }
    }
    else if (valid_N > 0)
    {
        /* 退化为均值 */
        for (int ax = 0; ax < 3; ax++)
        {
            inst->gyro_offset[ax] = (float)(sum_y[ax] / (double)valid_N);
            inst->gyro_drift[ax]  = 0.0f;
        }
    }
    else 
    {
        /* 传感器完全无响应 */
        for (int ax = 0; ax < 3; ax++)
        {
            inst->gyro_offset[ax] = 0.0f;
            inst->gyro_drift[ax]  = 0.0f;
        }
    }

    inst->calibrated = 1;
}
