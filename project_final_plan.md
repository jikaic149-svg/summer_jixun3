# 平衡式巡逻打靶小车最终整理文档

> 本文件由 `final_architecture.md`、`task_division.md`、`process.md` 合并整理而来。  
> 已删除重复内容、早期调参流水账、与当前代码状态冲突的描述。  
> 当前仓库内已实现的重点是 DJI C 板底盘平衡阶段；ESP32-S3 与 MaixCam2 部分是最终系统方案和任务边界。

## 1. 最终系统结论

本项目按三主控节点划分：

```text
DJI C板：
  底盘平衡、两轮无刷、底盘姿态估计、底盘任务状态。

ESP32-S3：
  yaw云台主控、pitch舵机控制、上层BMI088采样、yaw电机CAN控制、
  激光控制、无线通信网关。

MaixCam2：
  USB摄像头采图、靶子ROI识别、靶心计算、摄像头-激光固定映射补偿、
  score计算、WAV语音播报。
```

核心边界：

```text
DJI C板只控制底盘，不做yaw云台高频内环。
ESP32-S3控制yaw、pitch和激光。
MaixCam2只处理视觉和语音，不控制电机。
MaixCam2到ESP32-S3只传坐标或状态，不传图像。
DJI C板和ESP32-S3之间只做低频状态/位姿同步，不进入实时闭环。
```

当前代码状态补充：

```text
当前STM32固件已经具备：
  BMI088 SPI采样、Kalman平衡角、CAN左右轮反馈、LQR电流输出、VOFA串口遥测。

当前STM32固件尚未具备：
  灰度巡线、ESP32-S3通信、MaixCam2视觉数据接入、yaw云台控制、激光控制。
```

## 2. 硬件约束

已确定约束：

```text
1. 两个无刷电机作为平衡车驱动轮。
2. 驱动轮通过CAN与DJI C板通信。
3. DJI C板根据内置BMI088做姿态闭环，实现底盘直立。
4. yaw云台使用无刷电机。
5. ESP32-S3通过外接CAN收发器控制yaw电机。
6. ESP32-S3通过SPI以约1000Hz读取上层BMI088。
7. pitch轴使用轻量舵机，由ESP32-S3控制。
8. USB摄像头和激光笔安装在pitch端。
9. USB摄像头连接下层MaixCam2。
10. 没有USB滑环，USB线不能穿过滑环。
11. yaw不做360度连续旋转，采用软件约±120度、机械约±130度限位。
12. MaixCam2需要语音播报。
13. MaixCam2与ESP32-S3之间传低维坐标/状态。
14. DJI C板与ESP32-S3之间低频同步空间位姿和任务状态。
15. 底层允许根据调试结果添加配重块，保证重心居中且偏低。
```

重要修正：

```text
ESP32-S3是yaw云台控制主节点。
DJI C板不负责yaw云台内环。
MaixCam2无线或串口传输的是dx/dy/score/valid等低维结果，不是图像。
```

## 3. 系统拓扑

```text
                         上层pitch端
                 ┌──────────────────────┐
                 │ USB摄像头 + 激光笔    │
                 │ pitch舵机             │
                 └──────────┬───────────┘
                            │ PWM/电源/激光控制
                 ┌──────────▼───────────┐
                 │ ESP32-S3              │
                 │ 上层BMI088            │
                 │ CAN收发器             │
                 │ yaw/pitch/laser控制   │
                 └──────────┬───────────┘
                            │
            USB线 + 电源线 + CAN线，随yaw轴柔性走线
            yaw软件限位±120度，机械限位约±130度
                            │
                 ┌──────────▼───────────┐
                 │ 下层车体              │
                 │ MaixCam2              │
                 │ DJI C板               │
                 │ 两轮无刷              │
                 │ 灰度阵列 + 分电板      │
                 └──────────────────────┘
```

走线原则：

```text
USB线不走滑环。
普通导电滑环只传电源和低速/中速通信。
跨yaw活动区域的线束必须柔软、固定、留余量。
图像原始数据不跨无线和滑环。
底部配重必须左右对称、靠近车体中心、固定可靠。
```

## 4. 三端任务分工

### 4.1 视觉端

负责对象：

```text
MaixCam2
USB摄像头
摄像头-激光固定标定参数
音频输出
```

任务：

```text
1. USB摄像头采图。
2. 靶子ROI识别。
3. 靶心/靶面中心计算。
4. 根据固定标定关系补偿摄像头-激光安装偏差。
5. 计算dx、dy、score、valid、quality。
6. 向ESP32-S3发送视觉结果。
7. 接收ESP32-S3播报命令并播放WAV。
```

禁止事项：

```text
不要无线传图。
不要控制电机。
不要无条件自主播报分数。
不要让音频播放阻塞视觉主循环。
```

### 4.2 硬件端

负责对象：

```text
平衡车底盘
yaw无刷云台
pitch舵机支架
USB摄像头安装座
激光笔安装座
ESP32-S3固定板
MaixCam2固定板
电池、分电板、稳压模块
CAN收发器
激光MOSFET/三极管驱动
线束应力释放和机械限位
```

关键要求：

```text
1. yaw在±120度内线束不拉扯。
2. pitch舵机全行程不碰撞。
3. USB摄像头不掉线。
4. ESP32-S3能稳定读上层BMI088。
5. ESP32-S3能通过CAN控制yaw电机。
6. 激光由MOSFET/三极管驱动，不能直接接ESP32-S3 GPIO。
7. 舵机电源不要直接从ESP32-S3板载5V硬拉。
8. 电机电源噪声不能污染视觉和IMU供电。
```

### 4.3 电控端

DJI C板负责：

```text
1. 底盘BMI088采样与姿态估计。
2. 左右驱动轮CAN反馈读取。
3. LQR平衡控制。
4. 左右驱动轮CAN电流/力矩控制。
5. 后续灰度巡线和标识点停车。
6. 底盘任务状态同步。
```

ESP32-S3负责：

```text
1. 接收MaixCam2视觉结果。
2. 对dx/dy做时间戳检查、滤波、预测。
3. SPI读取上层BMI088。
4. CAN读取yaw电机/编码器反馈。
5. CAN控制yaw无刷电机。
6. PWM控制pitch舵机。
7. 控制激光开关。
8. 与DJI C板低频同步空间位姿和任务状态。
9. 与MaixCam2交互语音播报命令。
```

## 5. 主链路

### 5.1 底盘链路

最终目标：

```text
底盘BMI088 / 左右轮反馈 / 灰度传感器
  -> DJI C板
  -> 姿态估计
  -> LQR平衡控制
  -> PI + ESO / LADRC执行层
  -> 两轮无刷
```

当前仓库已实现：

```text
BMI088 / 左右轮CAN反馈
  -> DJI C板
  -> Kalman平衡角
  -> LQR电流输出
  -> CAN电流命令
  -> 两轮无刷
```

当前仓库未接入：

```text
灰度巡线。
ESP32-S3联动。
MaixCam2视觉联动。
PI + ESO / LADRC主控制路径。
```

### 5.2 x轴打靶链路

```text
USB摄像头
  -> MaixCam2本地视觉处理
  -> 发送dx/valid/score/timestamp
  -> ESP32-S3滤波/预测
  -> ESP32-S3读取上层BMI088
  -> yaw串级闭环
  -> CAN控制yaw无刷
```

### 5.3 y轴打靶链路

```text
USB摄像头
  -> MaixCam2本地视觉处理
  -> 发送dy/valid/timestamp
  -> ESP32-S3滤波
  -> ESP32-S3控制pitch舵机
```

### 5.4 语音播报链路

```text
MaixCam2计算score
  -> ESP32-S3判断lock状态
  -> ESP32-S3触发MaixCam2播放WAV
  -> MaixCam2回传audio_state
```

## 6. 数据归属

| 数据 | 拥有节点 | 用途 |
|---|---|---|
| 底盘BMI088原始数据 | DJI C板 | 平衡控制、底盘姿态估计 |
| 左右轮反馈 | DJI C板 | 轮速、电流、故障、速度估计 |
| 灰度阵列 | DJI C板 | 巡线、标识点停车 |
| USB图像帧 | MaixCam2 | 视觉识别，不外传图像 |
| 视觉dx/dy/score | MaixCam2输出，ESP32-S3使用 | 打靶外环 |
| 上层BMI088 | ESP32-S3 | yaw增稳、扰动预测 |
| yaw电机反馈 | ESP32-S3 | yaw角度/速度闭环 |
| pitch舵机状态 | ESP32-S3 | y轴控制和状态同步 |
| 激光状态 | ESP32-S3 | 打靶安全、锁定 |
| 底盘任务状态 | DJI C板 | 任务流程推进 |

最终禁止跨界：

```text
MaixCam2不控制电机。
DJI C板不处理原始图像。
DJI C板和ESP32-S3低频无线同步不进入yaw高频内环。
MaixCam2到ESP32-S3只传坐标/状态，不传图像。
```

## 7. 控制结构

### 7.1 DJI C板底盘控制

推荐频率：

```text
BMI088采样：1000Hz
姿态估计：500Hz-1000Hz
LQR平衡环：500Hz-1000Hz
PI+ESO/LADRC执行层：200Hz-500Hz
灰度巡线PID：100Hz-200Hz
底盘状态同步：10Hz-30Hz
```

底盘核心原则：

```text
LQR拥有最高救车优先级。
不按高速/低速切换PI和ADRC。
ESO补偿必须限幅。
积分项必须带平衡风险削弱和饱和保护。
调试时先让LQR站稳，再逐步打开外环、ESO和积分。
```

当前代码注意：

```text
当前STM32代码已经允许在按键授权、反馈正常、enabled正常后输出LQR电流。
因此旧文档里“当前阶段禁止驱动轮输出电流”的描述已经不作为当前事实。
```

LQR状态定义：

```text
X = [x, v, theta, theta_dot]^T

x         车体前后位移，m
v         车体前后速度，m/s
theta     车体俯仰角，直立为0，rad
theta_dot 俯仰角速度，rad/s
```

推荐公式链：

```text
theta, theta_dot = Kalman(acc, gyro)
v = r / 2 * (omega_L + omega_R)
theta_ref = 外环(v_ref, x_ref, line_error)
X = [x - x_ref, v - v_ref, theta - theta_ref, theta_dot]^T
u_balance = -K * X
u_L = u_balance - yaw_cmd
u_R = u_balance + yaw_cmd
```

### 7.2 ESP32-S3 yaw控制

```text
视觉外环：
  dx -> yaw_angle_ref
  频率：50Hz-100Hz

角度环：
  yaw_angle_ref - yaw_encoder
  频率：200Hz-500Hz

角速度环：
  gyro_z / yaw_rate
  频率：500Hz-1000Hz

执行：
  CAN输出yaw电机命令
```

视觉数据使用规则：

```text
age < 50ms：正常使用。
50ms <= age < 100ms：降低视觉外环增益。
100ms <= age < 150ms：保持或短时预测。
age >= 150ms：丢弃视觉目标，yaw保持或回中。
```

### 7.3 ESP32-S3 pitch控制

```text
dy_ref -> pitch_target -> PWM舵机
```

要求：

```text
pitch_target必须限制在机械安全角内。
舵机动作限速，避免过冲。
valid=0时保持或回中。
```

### 7.4 激光控制

推荐连接：

```text
ESP32-S3 GPIO -> MOSFET/三极管驱动 -> 激光笔电源
```

控制条件：

```text
laser_on = local_lock_ok && chassis_permission
```

安全规则：

```text
任一关键通信超时：关闭激光。
vision_valid=0：关闭激光。
yaw超限：关闭激光。
pitch故障：关闭激光。
底盘不稳定：关闭激光。
```

## 8. 通信协议

### 8.1 MaixCam2 -> ESP32-S3

最终建议协议：Wi-Fi UDP 或可靠串口二进制包，内容保持低维。

建议字段：

```text
msg_id
seq
timestamp_ms
valid
dx
dy
score
target_x, target_y
laser_est_x, laser_est_y
quality
vision_fps
audio_state
crc16
```

要求：

```text
只传坐标，不传图像。
保留vision_timestamp，不能把重复帧伪装成新帧。
ESP32-S3只使用最新seq。
旧包、乱序包直接丢弃。
```

当前仓库里的 `25e.py` 使用的是 MaixCam2 UART二进制输出：

```text
0x55 0xAA dx(int16 little-endian) dy(int16 little-endian) 0x0A
```

如果后续接入ESP32-S3，需要让ESP32-S3解析这个二进制协议，或把MaixCam2脚本改成最终UDP协议。

### 8.2 DJI C板 -> ESP32-S3

低频状态包，建议10Hz-30Hz：

```text
msg_id
seq
timestamp_ms
chassis_x
chassis_y
chassis_yaw
chassis_v
line_state
mark_detected
balance_ok
battery_voltage
laser_enable_permission
fault_code
crc16
```

### 8.3 ESP32-S3 -> DJI C板

低频云台状态包，建议10Hz-30Hz：

```text
msg_id
seq
timestamp_ms
gimbal_yaw
gimbal_pitch
target_valid
dx_ref
dy_ref
score
lock_state
laser_state
fault_code
crc16
```

### 8.4 ESP32-S3 <-> yaw电机

有线CAN：

```text
ESP32-S3 -> yaw电机：
  yaw_motor_cmd
  yaw_target
  yaw_rate_target
  current_or_torque_cmd

yaw电机 -> ESP32-S3：
  yaw_encoder
  yaw_speed
  yaw_current
  yaw_motor_fault
```

## 9. 任务状态机

推荐总流程：

```text
INIT
BALANCE_READY
LINE_FOLLOW
LINE_LOST_STRAIGHT
MARK_DETECTED
STOP_AT_MARK
GIMBAL_CENTER
AIM_SEARCH
AIM_TRACK
LOCK_3S
REPORT_SCORE
WAIT_TARGET_MOVE
RELOCK
CONTINUE_TO_B
STOP_AT_B
```

分层：

```text
DJI C板：
  负责底盘任务状态、巡线、停车、底盘安全许可。

ESP32-S3：
  负责云台打靶状态、视觉锁定、激光许可。

MaixCam2：
  负责视觉结果和语音执行，不承担总状态机。
```

状态同步：

```text
DJI C板停车并发送MARK_DETECTED/STOP_AT_MARK。
ESP32-S3进入AIM_SEARCH/AIM_TRACK。
ESP32-S3完成LOCK_3S后触发MaixCam2播报。
ESP32-S3回传lock_state和score。
DJI C板收到完成状态后继续巡线。
```

## 10. 机械建模最终保留信息

本节来自 `process.md` 后半段，保留为当前机械设计约束。

### 10.1 目标

云台上半段采用：

```text
YAW无刷电机
  -> YAW输出上的单片机/舵机平台
  -> SG90作为pitch轴
  -> SG90输出连接摄像头+激光整体负载
```

不用 SG90 原装摄像头支架。需要重点保证 SG90 pitch 输出件与摄像头/激光整体可靠连接。

当前仓库已有：

```text
cad_parts/sg90_to_payload_connector.SLDPRT
```

### 10.2 实测尺寸记录

```text
YAW_Gimbal.STL：
  bbox 26.67 x 49 x 49 mm。
  圆盘形云台电机，输出/枢轴沿X。
  直径49圆面为法兰面，厚26.67。
  X约12.67处有12.6方颈。

SG90本体：
  43 x 27 x 14 mm，X居中。

SG90 pitch摆臂：
  自身导出Z 73.8-107.8，输出接口在此。

摄像头支架：
  43 x 26 x 45 mm，X 9.67-52.67，Z 64.67-109.67。

激光笔支架：
  7 x 45 x 45 mm，X 2.67-9.67，Z 64.67-109.67。
  与摄像头支架同坐标系、X相邻，可螺丝拼成整体。

DJI C板：
  41.2 x 60.4 x 16.6 mm。
```

### 10.3 已锁定决策

```text
1. 单片机平台采用50 x 50 mm通用方板。
2. 平台四周布置阵列孔和扎带槽。
3. 平台在下，SG90立在平台上。
4. 平台下接YAW法兰。
5. SG90输出接口按标准SG90舵盘处理。
6. 摄像头和激光先拼成一个整体，再通过一个连接件接到SG90舵盘。
7. 连接件随舵机转动。
```

### 10.4 仍需实物复核

```text
1. YAW法兰螺孔PCD、孔径、孔数、相位。
2. 摄像头+激光整体负载底面的真实安装孔位。
3. SG90输出轴、负载重心、YAW轴线是否干涉。
4. yaw±120度范围内USB线、电源线、CAN线是否拉扯。
```

## 11. 集成顺序

```text
1. 确认DJI C板底盘BMI088、CAN反馈、VOFA遥测稳定。
2. 确认左右轮方向、电流方向、急停和按键授权正确。
3. 先只调LQR，让底盘能短时站稳。
4. 再加入速度/位置/巡线外环。
5. ESO先只观察z1/z2，确认可信后再限幅参与补偿。
6. 硬件端完成供电、安装、线束、CAN、激光驱动。
7. ESP32-S3完成yaw电机CAN通信和反馈读取。
8. ESP32-S3完成上层BMI088读取。
9. ESP32-S3完成yaw角速度环和角度环。
10. ESP32-S3完成pitch舵机控制。
11. MaixCam2完成靶心识别和视觉结果输出。
12. ESP32-S3接入视觉结果，完成yaw/pitch外环。
13. 接入激光锁定和安全许可。
14. MaixCam2接入语音播报。
15. DJI C板和ESP32-S3完成低频状态同步。
16. 联调完整任务流程。
```

## 12. 风险与处理

| 风险 | 后果 | 处理 |
|---|---|---|
| MaixCam2视觉包抖动 | yaw视觉外环滞后 | timestamp、seq、age判断，超过150ms丢弃 |
| 视觉发送频率高于真实帧率 | 重复旧数据 | 包内保留vision_timestamp |
| ESP32-S3 CAN反馈不足 | yaw闭环不完整 | 必须读取yaw编码器/速度反馈 |
| 激光无硬件驱动 | GPIO损坏 | 使用MOSFET/三极管 |
| 底盘和云台状态不同步 | 空间坐标误差 | 低频同步只做辅助，超时冻结 |
| USB线被yaw卷绕 | 摄像头掉线 | yaw软件±120度、机械±130度 |
| Wi-Fi占用S3资源 | 实时任务抖动 | 控制任务独立核心，通信低优先级 |
| 激光误开 | 安全风险 | 双条件授权，任一故障立即关闭 |
| LQR未站稳就打开积分/ESO | 车体抢控制或猛冲 | 先Ki=0，ESO只观察 |
| 输出饱和但积分继续累加 | 脱离饱和后突然冲车 | 冻结积分或使用Kaw抗饱和 |
| ESO估计过激 | 高频抖动、电流震荡 | z2限幅，wo从小到大调 |

## 13. 历史调试有效结论

从旧 `process.md` 中只保留以下有效结论，早期具体参数不再作为当前配置依据：

```text
1. 速度/位置外环必须给pitch内环让路；大角度或大角速度时外环应退出或reset。
2. 救车档不能过硬，电流峰值和电流变化率都需要限制。
3. 如果故障前出现数百dps级角速度尖峰，优先检查角速度通道噪声、滤波和机械冲击。
4. 不要用继续加强外环来掩盖pitch内环方向、阻尼、轮胎打滑或IMU噪声问题。
5. 调参顺序应该是先确认极性和安全，再调内环，再接外环，最后接ESO/积分。
```

