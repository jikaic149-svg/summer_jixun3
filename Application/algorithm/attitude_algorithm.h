/**
 * @file    attitude_algorithm.h
 * @brief   姿态解算算法: Mahony互补滤波 + 二阶Biquad数字滤波器
 * @origin  H 项目 Application/algorithm/attitude_algorithm.h
 * @note    100%可移植, 纯数学算法, 无硬件依赖
 *          使用方法: 
 *          1. 调用 Filter_Init() 初始化滤波器
 *          2. 每个周期调用 MahonyAHRSupdateIMU() 传入IMU原始数据
 *          3. 读取 Roll_a, Pitch_a, Yaw_a 获取欧拉角(度)
 */
#ifndef ATTITUDE_ALGORITHM_H
#define ATTITUDE_ALGORITHM_H

#include <stdint.h>
#include <math.h>

#ifndef PI
#define PI 3.141592654f
#endif

#define Ang2Rad 0.01745329252f  // 角度制转弧度制
#define Rad2Ang 57.295779513f   // 弧度制转角度制

/* ======================== 二阶Biquad滤波器 ======================== */
typedef enum
{
    BIQUAD_LOWPASS,          // 低通滤波器
    BIQUAD_HIGHPASS,         // 高通滤波器
    BIQUAD_BANDPASS_PEAK,    // 带通滤波器(峰值)
    BIQUAD_BANDSTOP_NOTCH,   // 带阻滤波器(陷波)
} biquad_type;

typedef struct
{
    float a0, a1, a2, a3, a4;
    float x1, x2, y1, y2;
} biquad_state;

typedef struct
{
    float q_angle;
    float q_bias;
    float r_measure;
    float angle;
    float bias;
    float rate;
    float p00;
    float p01;
    float p10;
    float p11;
} KalmanAngle_t;

typedef struct
{
    float q;
    float r;
    float x;
    float p;
    uint8_t ready;
} KalmanScalar_t;

/* ======================== 姿态角输出(全局变量) ======================== */
extern float Pitch_a, Roll_a, Yaw_a;           // 实时姿态角(度)
extern float Pitch_g, Roll_g, Yaw_g;           // 实时角速度(度/秒)
extern float Pitch_g_F, Roll_g_F, Yaw_g_F;     // 滤波后角速度
extern float Yaw_TotalAngle;                    // Yaw总角度(含多圈)
extern float Yaw_AngleLast;                     // 上一次Yaw角度
extern int   Yaw_RoundCount;                    // Yaw圈数计数
extern biquad_state adc_error;                  // 预留: ADC误差滤波器

/* ======================== 函数声明 ======================== */

/**
 * @brief 初始化所有二阶滤波器参数
 */
void Filter_Init(void);

/**
 * @brief Mahony互补滤波姿态更新(仅IMU, 6轴)
 * @param gx,gy,gz  陀螺仪原始数据(度/秒)
 * @param ax,ay,az  加速度计原始数据(任意单位, 内部会归一化)
 * @note  调用后可读取 Roll_a, Pitch_a, Yaw_a, Yaw_TotalAngle 等变量
 */
void MahonyAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az);

/**
 * @brief 初始化二阶Biquad滤波器
 * @param state   滤波器状态
 * @param type    滤波器类型(低通/高通/带通/带阻)
 * @param fs      采样率(Hz)
 * @param fc      截止频率(Hz)
 * @param q_value 品质因数(Butterworth=0.7071, Thomson-Bessel=0.5773)
 */
void biquad_filter_init(biquad_state *state, biquad_type type, int fs, float fc, float q_value);

/**
 * @brief 二阶Biquad滤波
 * @param state  滤波器状态
 * @param data   输入数据
 * @return float 滤波后数据
 */
float biquad(biquad_state *state, float data);

void KalmanAngle_Init(KalmanAngle_t *kf,
                      float q_angle,
                      float q_bias,
                      float r_measure);
void KalmanAngle_SetAngle(KalmanAngle_t *kf, float angle_deg);
float KalmanAngle_Predict(KalmanAngle_t *kf,
                          float gyro_rate_dps,
                          float dt_sec);
float KalmanAngle_Update(KalmanAngle_t *kf,
                         float accel_angle_deg,
                         float gyro_rate_dps,
                         float dt_sec);
float KalmanAngle_Correct(KalmanAngle_t *kf, float accel_angle_deg);
float KalmanAngle_GetRate(const KalmanAngle_t *kf);
void KalmanScalar_Init(KalmanScalar_t *kf, float q, float r);
void KalmanScalar_SetState(KalmanScalar_t *kf, float value);
float KalmanScalar_Update(KalmanScalar_t *kf, float measurement);

/**
 * @brief 快速平方根倒数(Quake III算法)
 */
float invSqrt(float x);

#endif /* ATTITUDE_ALGORITHM_H */
