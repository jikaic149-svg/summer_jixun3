# 平衡车控制结构与调试结论

> 仅包含两轮自平衡底盘，参数以当前源码为准。

## 1. 控制结构

~~~mermaid
flowchart LR
    A[IMU/轮速反馈] --> B[倾角/车速估计]
    B --> C[速度或位置外环]
    C --> D[目标倾角]
    D --> E[直立角度环]
    E --> F[左右轮目标转速]
    F --> G[轮速 PI]
    G --> H[电机电流]
~~~

一句话说明：

> 外环决定车身应该倾多少，直立环移动轮子追重心，轮速环把目标转速转换为电流。

## 2. 核心公式

~~~text
1. 加速度角
theta_accel = atan2(accel_y, accel_z) * 180 / PI - 0.35 deg

2. Kalman 预测与修正
theta_rate = gyro_rate - gyro_bias
theta_pred = theta_est + theta_rate * dt
innovation = theta_accel_lpf - theta_pred
theta_est = theta_pred + K0 * innovation

仅当：
0.75 <= accel_norm_g <= 1.25
abs(innovation) <= 10 deg
才融合加速度角，否则只积分陀螺仪。

3. 车速与位置
RPM_TO_MPS = 2 * PI * 0.031 / 60 = 0.0032463
velocity = (left_rpm + right_rpm) / 2 * RPM_TO_MPS
position = position + velocity * 0.001

4. 行走速度外环
velocity_error = velocity_ref - velocity
theta_speed =
    clamp(10.5 * deadband(velocity_error, 0.01)
          + 1.2 * speed_integral,
          -1.6 deg, +1.6 deg)

5. 静立位置/速度外环
theta_hold =
    clamp(0.30 * position_error
          + 3.00 * (0 - velocity),
          -1.6 deg, +1.6 deg)

6. 直立角度环
theta_error = theta_actual - theta_ref
balance_rpm =
    clamp(100 * theta_error + 0.4 * theta_rate,
          -rpm_limit, +rpm_limit)

7. 轮速 PI
wheel_error = wheel_ref_rpm - wheel_actual_rpm
current =
    speed_ff
    + 0.0025 * wheel_error
    + 0.0010 * wheel_integral

8. 姿态域停车
brake_theta = clamp(-6 * velocity, -0.9 deg, +0.9 deg)
~~~

## 3. 当前参数

| 项目 | 当前值 |
|---|---:|
| 控制周期 | 1 ms / 1 kHz |
| 轮速 PI | 0.00250 / 0.001000 / 0 |
| 直立 P/I/D | 100 / 0 / 0.400 |
| 轮速限幅 | 全局 320 rpm，静立 220 rpm |
| 电流限幅/斜率 | 0.50 A / 60 A/s |
| 行走速度 | ±0.12 m/s |
| 行走速度 PI | 10.5 / 1.2 |
| 静立位置 Kx / 速度 Kv | 0.30 / 3.00 |
| 行走/总倾角限幅 | ±1.6 / ±3.2 deg |
| 安全倾角/角速度 | ±30 deg / ±600 dps |

运动状态：

~~~text
HOLD -> STARTING -> RUNNING -> BRAKING -> HOLD
~~~

- STARTING：最大 1.10 deg、2 deg/s，克服静摩擦。
- RUNNING：速度 PI 控制行走。
- BRAKING：速度目标归零，并加入 -6v 姿态制动。
- 超速守卫：0.35 m/s 开启，0.15 m/s 解除。

## 4. process.md 调试结论

| 问题 | 根因与最终方案 | 结论 |
|---|---|---|
| 轮速抖动/响应软 | 过度追踪反馈；改轻滤波、PI和前馈 | 已实测 |
| 小角度甩飞 | 轮加速度污染加速度角；增加模长和创新门控 | 已实测 |
| 姿态方向异常 | gyro 与加速度角符号不一致；修正符号并重播种 | 已实测 |
| 三层极性误判 | 非最小相位瞬时响应误导；最终均为 +1 | 86.2 s站立验证 |
| forward 先不动后冲 | 无限位置参考累积；改有限启动辅助和速度PI | 原结构已删除 |
| 小倾角不走 | 被约 ±1.4 deg 静摩擦带吞掉 | 改 STARTING 助力 |
| 约 12 Hz 抖动 | 角度 P=150～180 超稳定域 | 回到 P=100附近 |
| STAND 匀速爬行 | 约 0.9 deg 零点误差 | offset + zero trim |
| 停车越刹越快 | 轮级反刹与姿态环打架 | 改为 -6v 姿态制动 |
| 行走约 8 Hz 振荡 | actual-base 高频反馈削弱轮速PI阻尼 | 加 0.10 s低通，仍待最终实测 |

## 5. 不再采用与待验证

不再采用：

- 固定小倾角或固定 +25/+35 rpm 基底；
- 大速度积分倾角硬顶静摩擦；
- pulse 或 launch 阶跃起步；
- 角度 P 提高到 150～180；
- stand Kv 提高到 5 或更高；
- 限制核心直立轮速输出斜率；
- 低速硬死区；
- 轮级直接反向制动；
- 只看轮子瞬时方向翻转控制极性。

仍需实车确认：

1. 直走稳定 2 s 时速度标准差不超过 0.015 m/s，且无持续 7～9 Hz 翻向；
2. forward 切回 stand 后 1 s 内进入 HOLD，反向速度不低于 -0.02 m/s；
3. actual-base 的 0.10 s 低通和 65% 接管能否兼顾平均速度与动态阻尼。

最终结论：

> 基础直立、轮速、极性和零点问题已有实测闭环；最终行走动态和停车阈值仍需实车确认。

