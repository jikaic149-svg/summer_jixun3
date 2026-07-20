# jixun3 代码完整阅读报告

> 本报告基于当前工作区代码生成，包含当前未提交的本地修改。  
> 当前固件主线是 STM32F407IGHx 上的平衡车底盘控制：BMI088 姿态采样、Kalman 平衡角、CAN 左右轮 QDrive/QD4310 电机反馈与控制、VOFA+ 串口遥测。

## 1. 项目作用概览

这个工程是一个 DJI C 板 / STM32F407 平衡车底盘电控工程。它的主要工作是：

1. 通过 SPI1 读取 BMI088 六轴 IMU。
2. 从加速度计和陀螺仪估计平衡角、角速度和底盘 yaw 角速度。
3. 通过 CAN1 接收左右轮无刷电机反馈。
4. 在 FreeRTOS 任务里运行底盘安全状态机。
5. 在按键授权后，用固定增益 LQR 根据姿态误差计算左右轮电流。
6. 通过 CAN 向左右轮电机发送 enable 和 current 命令。
7. 通过 USART6 输出 VOFA+ CSV 遥测，并接收简单文本调参命令。

当前代码里还保留了蓝牙、视觉、Mahony、ADRC/ESO、旧云台等模块，但这些模块多数不是当前主闭环路径。根目录还有一个 `25e.py`，它是 MaixCAM2 视觉识别脚本，不参与 STM32 固件编译。

## 2. 工程结构

```text
jixun3/
  Src/                         CubeMX/HAL 初始化与中断入口
    main.c                     系统启动、外设初始化、任务创建、INS 初始化
    freertos.c                 FreeRTOS 任务创建
    gpio.c                     BMI088 CS/DRDY、LED、KEY 初始化
    spi.c                      SPI1 + DMA2 Stream2/3 初始化
    can.c                      CAN1 初始化
    tim.c                      TIM10 PWM 初始化，当前未看到启动调用
    stm32f4xx_it.c             中断入口，转发到 BSP / HAL

  Inc/
    config.h                   底盘、电机、IMU、LQR、VOFA、旧云台参数
    main.h                     引脚宏
    FreeRTOSConfig.h           FreeRTOS 内核参数

  bsp/                         板级硬件封装
    bsp_spi.c/.h               SPI1 轮询与 DMA，BMI088 CS 控制
    bsp_can.c/.h               CAN 电机协议、反馈解析
    bsp_uart.c/.h              USART1 蓝牙物理层
    bsp_uart6.c/.h             USART6 VOFA/视觉串口物理层
    bsp_delay.c/.h             SysTick 微秒/毫秒延时

  Device/                      设备抽象层
    bmi088/                    当前实际编译使用的 BMI088 驱动
    vofa/                      VOFA+ JustFloat/CSV/Text 输出
    bluetooth/                 二进制 CRC 帧 + 文本行解析，当前未初始化
    vision/                    "dx,dy\n" 文本视觉协议，当前未初始化

  Application/
    ins/                       IMU 采样、Kalman 姿态、VOFA 输出/命令接收
    gimbal/                    名字保留为 gimbal，实际是底盘电机安全与 LQR 控制任务
    algorithm/                 Kalman/Mahony/Biquad/LQR/外环/零偏搜索/ADRC/CRC

  Middlewares/Third_Party/     FreeRTOS
  Drivers/                     STM32 HAL 和 CMSIS
  MDK-ARM/                     Keil 工程
  .vscode/                     VS Code 构建、烧录、串口监视任务
  Makefile                     GCC 构建入口
  25e.py                       MaixCAM2 视觉识别与 UART 输出脚本，非 STM32 固件
```

## 3. 实际编译进固件的主要代码

Makefile 和 Keil 工程都把下面这些自有模块编译进固件：

- `Src/main.c`, `Src/freertos.c`, `Src/gpio.c`, `Src/spi.c`, `Src/can.c`, `Src/tim.c`, `Src/dma.c`, `Src/stm32f4xx_it.c`
- `bsp/bsp_spi.c`, `bsp/bsp_can.c`, `bsp/bsp_uart.c`, `bsp/bsp_uart6.c`, `bsp/bsp_delay.c`
- `Device/bmi088/bmi088.c`, `Device/vofa/vofa.c`, `Device/bluetooth/bluetooth.c`, `Device/vision/vision.c`
- `Application/ins/ins_task.c`, `Application/gimbal/gimbal_task.c`
- `Application/algorithm/attitude_algorithm.c`, `lqr_controller.c`, `balance_outer_loop.c`, `balance_zero_bias_search.c`, `adrc_eso.c`, `crc16.c`

`component/devices/BMI088driver.c` 和 `BMI088Middleware.c` 是旧版/参考 BMI088 驱动，当前没有被 Makefile 和 Keil 工程加入编译。

根目录 `25e.py` 是 MaixCAM2 端 Python 程序，不属于 STM32 固件构建产物，也不会被 Makefile/Keil 编译。

## 4. 启动流程

启动入口在 `Src/main.c`。

```text
Reset_Handler
  -> main()
     -> HAL_Init()
     -> SystemClock_Config()
     -> MX_GPIO_Init()
     -> MX_DMA_Init()
     -> MX_SPI1_Init()
     -> MX_TIM10_Init()
     -> MX_CAN1_Init()
     -> CAN_Filter_Init()
     -> Motor_Enable(left)
     -> Motor_Enable(right)
     -> FreeRTOS_Init()
     -> INS_Init()
     -> vTaskStartScheduler()
```

关键点：

- 上电后会先通过 CAN 给左右轮发送 enable 命令，但不会立刻输出非零电流。
- `emergency_stop` 默认是 1，电机控制任务默认只发送 0A。
- 只有左右轮反馈正常、左右轮 enabled 正常，并且 PA0 按键授权后，才可能进入 LQR 平衡控制输出。
- `INS_Init()` 在调度器启动前执行，会初始化 BMI088、UART、VOFA、Kalman 和 SPI DMA。
- BMI088 初始化失败时 `INS_Init()` 会一直重试，不会进入调度器。

## 5. FreeRTOS 任务

| 任务 | 优先级 | 栈 | 作用 |
|---|---:|---:|---|
| `INS_Task` | 5 | 512 words | BMI088 采样、Kalman 姿态、yaw 积分、VOFA 输出、CAN 错误恢复 |
| `Gimbal_Task` | 4 | 256 words | 实际是底盘电机安全状态机 + LQR/速度测试输出 |
| `BT_RxTask` | 3 | 256 words | 名字保留，实际轮询 VOFA 文本命令队列 |

任务周期：

- `INS_Task` 理想情况下由 BMI088 gyro DRDY + SPI DMA 完成通知驱动，目标约 1000Hz；如果 DMA/DRDY 没数据，会走轮询降级。
- `Gimbal_Task` 用 `vTaskDelayUntil()` 固定 1ms 周期运行。
- `BT_RxTask` 每 2ms 检查一次文本命令。

## 6. 用到的硬件

### 6.1 主控

- MCU：`STM32F407IGHx`
- 内核：Cortex-M4F，硬浮点
- 主频：168MHz
- 外部晶振：12MHz HSE
- Flash：1024KB
- SRAM：128KB + 64KB CCMRAM
- 调试/烧录：SWD，PA13/SWDIO，PA14/SWCLK

### 6.2 BMI088 IMU

BMI088 通过 SPI1 通信：

| 信号 | 引脚 | 说明 |
|---|---|---|
| SPI1_SCK | PB3 | SPI 时钟 |
| SPI1_MISO | PB4 | SPI MISO |
| SPI1_MOSI | PA7 | SPI MOSI |
| ACCEL CS | PA4 | 加速度计片选 |
| GYRO CS | PB0 | 陀螺仪片选 |
| ACCEL DRDY | PC4 / EXTI4 | 加速度数据就绪，中断下降沿 |
| GYRO DRDY | PC5 / EXTI9_5 | 陀螺仪数据就绪，中断下降沿 |

SPI 配置：

- SPI1 主机模式
- CPOL high，CPHA second edge
- 8-bit 数据
- 预分频 8，约 10.5 Mbit/s
- 运行时使用 DMA2_Stream2 RX、DMA2_Stream3 TX

BMI088 配置：

- 加速度计：3G 量程，800Hz ODR
- 陀螺仪：2000 dps 量程，1000Hz ODR / 116Hz 带宽
- 加速度 INT1 和陀螺 INT3 配成 DRDY 输出

### 6.3 左右轮无刷电机

代码按 QD4310 / QDrive 参数配置左右轮：

| 项 | 值 |
|---|---|
| 左轮 CAN ID | `0x00` |
| 右轮 CAN ID | `0x01` |
| CAN 命令帧 | `0x400 + motor_id` |
| CAN 反馈帧 | `0x500 + motor_id` |
| CAN 速率 | 1Mbps |
| 额定电压 | 7V-26V |
| 额定电流 | 1A |
| 峰值电流 | 2A |
| 额定转矩 | 0.2 N*m |
| 额定速度 | 500 rpm |
| 峰值速度 | 800 rpm |
| 编码器 | 15 bit |

CAN1 引脚：

| 信号 | 引脚 |
|---|---|
| CAN1_RX | PD0 |
| CAN1_TX | PD1 |

CAN1 位时序：APB1 42MHz，Prescaler=3，1+BS1(7)+BS2(6)=14 TQ，所以 42MHz / 3 / 14 = 1Mbps。

### 6.4 串口

USART6 当前被 VOFA 使用：

| 信号 | 引脚 | 参数 |
|---|---|---|
| USART6_TX | PG14 | 115200 8N1 |
| USART6_RX | PG9 | 115200 8N1 |

USART1 也实现了 BSP：

| 信号 | 引脚 | 参数 |
|---|---|---|
| USART1_TX | PA9 | 115200 8N1 |
| USART1_RX | PB7 | 115200 8N1 |

当前 `config.h` 里 `VOFA_UART_PORT` 选择的是 USART6。

### 6.5 按键和指示灯

| 功能 | 引脚 | 逻辑 |
|---|---|---|
| KEY | PA0 | 上拉输入，按下为低电平 |
| 蓝灯 | PH10 | 状态指示 |
| 绿灯 | PH11 | 已授权、可输出 |
| 红灯 | PH12 | 反馈/使能异常 |

按键 30ms 消抖。默认急停；按下后如果左右轮反馈和 enable 都正常，则解除急停并授权控制；再次按下会进入急停。

### 6.6 TIM10 / PF6

`TIM10_CH1` 映射到 PF6，周期 4999，PWM pulse=0。当前代码只初始化了 TIM10，没有看到 `HAL_TIM_PWM_Start()`，所以它像是预留 PWM 输出，当前主控制链路没有使用。

### 6.7 MaixCAM2 视觉脚本硬件

`25e.py` 面向 MaixCAM2 / MaixPy 运行环境，使用：

- Maix camera：320x240 灰度图。
- Maix display：本地显示调试画面。
- Maix UART2：尝试通过 `/dev/ttyS2` 以 115200 波特率通信。
- Pinmap：尝试把 `B0` 配为 `UART2_TX`，`B1` 配为 `UART2_RX`。

它和 STM32 固件里的 `Device/vision` 文本协议不是同一个协议：`25e.py` 发的是二进制包，STM32 `Device/vision` 解析的是 `"dx,dy\n"` 文本行，而且当前 STM32 固件没有初始化 `Vision_Init()`。

## 7. 信息采样

### 7.1 IMU 采样

运行时主路径：

```text
BMI088 DRDY 中断
  -> HAL_GPIO_EXTI_Callback()
     -> 设置 gyro/accel/temp 的 DRDY 标志
     -> imu_cmd_spi_dma()
        -> 拉低对应 CS
        -> BSP_SPI_DMA_Enable()
           -> DMA2_Stream2/3 传输
              -> DMA2_Stream2_IRQHandler()
                 -> BSP_SPI_DMA_RxCpltCallback()
                    -> 拉高 CS
                    -> 设置 UPDATE 标志
                    -> 如果 gyro 数据完成，通知 INS_Task
```

DMA 读取长度：

- 陀螺仪：8 字节，命令从 `0x82` 开始。
- 加速度计：9 字节，命令从 `0x92` 开始，加速度计 SPI 读需要 dummy byte。
- 温度：4 字节，命令从 `0xA2` 开始。

降级路径：

- 如果 10ms 内没有收到 DMA 通知，`INS_Task` 不直接复用旧数据。
- 在 `IMU_POLL_FALLBACK_ENABLE=1` 时，会调用阻塞式 `BMI088_Read()` 轮询读取一次 IMU。
- 当前 `IMU_POLL_FALLBACK_DT_SEC=0.010f`，所以轮询降级模式按 10ms 时间步处理，不是 1ms。

### 7.2 电机反馈采样

CAN 中断回调在 `bsp_can.c` 的 `HAL_CAN_RxFifo0MsgPendingCallback()`。

反馈帧范围：`0x500` 到 `0x508`，当前只使用：

- `0x500`：左轮
- `0x501`：右轮

反馈数据解析：

| 字节 | 含义 |
|---|---|
| byte0 bit0 | enabled 标志 |
| byte2-3 | int16 小端，电流，映射到 ±10A |
| byte4-5 | int16 小端，电机反馈速度，映射到 ±1000 rpm |
| byte6-7 | uint16 小端，编码器原始角度，映射到 0-360 deg |

代码还会根据编码器角度差累加 `total_angle`，每 20ms 计算一次编码器窗口速度，并做一阶低通。

方向归一化：

- 左轮反馈符号：`-1.0`
- 右轮反馈符号：`+1.0`
- 左轮电流命令符号：`-1.0`
- 右轮电流命令符号：`+1.0`

这样代码内部“正方向”统一表示车体前进。

### 7.3 按键采样

`Gimbal_Task` 每 1ms 读取 PA0：

```text
PA0 low -> 按下
30ms 稳定后才接受状态变化
```

如果当前不是急停，按键会立刻急停；如果当前急停且电机反馈/使能正常，按键会清故障、授权并解除急停。

### 7.4 VOFA 文本命令采样

USART6 的 RXNE 中断逐字节调用 `vofa_rx_byte_callback()`，按 `\n` 组行后放进一个 16 深度的软件队列。

`BT_RxTask` 每 2ms 取出命令：

- `Bxxx`、`bxxx`、`biasxxx`：设置手动平衡角偏置，单位 deg。
- 普通数字：设置轮速测试目标 rpm。

当前因为 `BALANCE_LQR_ENABLE=1` 且 LQR 输出模式是电流模式，普通轮速目标主要用于保留的速度测试路径，不是当前 LQR 主输出。

### 7.5 MaixCAM2 图像采样

`25e.py` 的采样链路：

```text
Maix camera 320x240
  -> 灰度图
  -> 二值化
  -> find_blobs 找白色外框
  -> 在外框 ROI 内找黑色靶芯
  -> 角点提取
  -> 对角线交点求靶心
  -> EMA 平滑
```

图像阈值：

- 黑色胶布二值化阈值：灰度 0-115。
- 白色 blob 阈值：灰度 200-255。
- 黑色 blob 阈值：灰度 0-50。

它还保留了 last ROI、last corners、中心跳变保护和局部角点放大镜逻辑，用于提高目标锁定稳定性。

## 8. 信息处理

### 8.1 BMI088 原始量换算

加速度计：

```text
raw int16 -> accel[g] = raw * BMI088_ACCEL_3G_SEN
```

陀螺仪：

```text
raw int16 -> gyro[rad/s] = raw * BMI088_GYRO_2000_SEN
```

温度：

```text
raw 11bit -> temp = raw * 0.125 + 23
```

当前 `BMI088_CalibrateZeroDrift()` 函数存在，但在 `INS_Init()` 里被注释掉了；运行中 `gyro_bias[3]` 也全为 0。实际只对 yaw rate 额外减了一个固定偏置 `CHASSIS_YAW_RATE_BIAS_DPS=0.085`。

### 8.2 坐标轴处理

`INS_Task` 将 BMI088 陀螺仪坐标统一到加速度计坐标：

```c
gx_deg =  imu_gyro[0] * Rad2Ang;
gy_deg = -imu_gyro[1] * Rad2Ang;
gz_deg = -imu_gyro[2] * Rad2Ang;
```

当前平衡角配置：

- 使用 Roll 加速度角：`atan2(accel_y, accel_z)`
- 加速度角符号：`-1.0`
- 陀螺仪平衡轴：X 轴
- 陀螺仪符号：`+1.0`
- 机械角度偏置：0 deg
- 开机自动归零：关闭

### 8.3 平衡角 Kalman

处理链：

```text
accel[3]
  -> calc_balance_accel_angle()
  -> 加速度角一阶低通 alpha=0.05
  -> KalmanAngle_Update(accel_angle, gyro_rate, dt)
  -> global_balance_angle
  -> global_balance_rate
```

Kalman 参数：

- `Q_angle = 0.012`
- `Q_bias = 0.003`
- `R_measure = 0.110`

`global_balance_rate` 来自 Kalman 内部的 `gyro_rate - bias`。

### 8.4 yaw rate 和 yaw 估计

处理链：

```text
gyro Z
  -> 符号处理
  -> 减固定 yaw rate bias
  -> 标量 Kalman
  -> 积分得到 global_chassis_yaw
```

参数：

- yaw gyro 轴：Z
- yaw 符号：`+1.0`
- 固定 bias：`0.085 dps`
- 标量 Kalman：`Q=0.10`, `R=0.50`

### 8.5 轮速和位置估计

`Gimbal_Task` 中根据左右轮 rpm 估算车体速度：

```text
velocity_raw_mps = 0.5 * (left_rpm + right_rpm) * wheel_radius * 2*pi / 60
```

参数：

- 轮半径：0.031m
- 速度限幅：±1.5m/s
- 速度低通：alpha=0.15
- 位置由速度积分，限幅 ±0.30m

注意：当前 `BALANCE_LQR_USE_WHEEL_STATE=0`，所以位置和速度虽然被计算并用于 VOFA 显示，但进入 LQR 状态向量时被置为 0。也就是说当前 LQR 主要只用 `theta` 和 `theta_dot`。

### 8.6 外环和零偏搜索

代码实现了两个辅助模块：

- `BalanceOuterLoop`：根据速度/位置/积分生成 `theta_ref`。
- `BalanceZeroBiasSearch`：通过微小 dither 和代价函数搜索直立零偏。

但当前配置中：

- `BALANCE_OUTER_ENABLE=0`
- `BALANCE_ZERO_BIAS_ENABLE=0`

所以它们不会主动改变控制，只保留接口和遥测变量。手动 bias 命令仍然能改变 `balance_manual_bias_deg`，并进入 `theta_ref`。

### 8.7 MaixCAM2 视觉处理

`25e.py` 的目标检测逻辑是漏斗式筛选：

1. 全图或上一帧 ROI 内找白色大区域。
2. 按外框面积和宽高比过滤。
3. 在外框内找黑色靶芯。
4. 按黑芯几何比例、面积、重心位置、相对面积过滤环境干扰。
5. 用外框角点计算对角线交点，作为透视下更稳定的物理中心。
6. 对中心点做 EMA 平滑，`SMOOTH_ALPHA=0.25`。
7. 若中心跳变超过 `LOCK_MAX_JUMP=80`，丢弃该帧锁定结果。

最终误差：

```text
dx = target_center_x - screen_center_x
dy = target_center_y - screen_center_y
```

## 9. 信息传递

### 9.1 中断到任务

```text
EXTI / DMA ISR
  -> 设置 volatile 标志
  -> vTaskNotifyGiveFromISR(INS_Task)
```

### 9.2 INS 到底盘控制任务

`INS_Task` 更新全局变量：

- `global_balance_angle`
- `global_balance_rate`
- `global_chassis_yaw`
- `global_chassis_yaw_rate`

`Gimbal_Task` 每 1ms 直接读取这些变量做控制。变量是 `float` 全局变量，没有互斥锁。

### 9.3 CAN 到控制任务

CAN ISR 更新：

- `left_wheel_motor`
- `right_wheel_motor`
- `can_rx_count`
- `can_last_std_id`
- `can_last_dlc`

`Gimbal_Task` 读取这些反馈，判断是否超时、是否 enabled，并参与速度/位置估计。

### 9.4 控制任务到电机

`Gimbal_Task` 每 1ms 调用：

```c
Motor_SetCurrent(LEFT_WHEEL_MOTOR_ID, left_current);
Motor_SetCurrent(RIGHT_WHEEL_MOTOR_ID, right_current);
```

`Motor_SetCurrent()` 会：

1. 按左右轮电流方向符号修正。
2. 限幅到 ±10A。
3. 把电流映射到 int16。
4. 发送 CAN 标准帧 `0x400 + id`，DLC=3，payload=`cmd + int16_le(value)`。

命令字：

- `0x01`：enable
- `0x02`：disable
- `0x03`：set current
- `0x05`：set angle

### 9.5 遥测输出

VOFA 当前配置：

- 输出开关：开
- 输出模式：`VOFA_OUTPUT_MODE_ENCODER`
- 因为 `BALANCE_LQR_ENABLE=1`，实际输出 12 通道
- 周期：20ms
- 物理串口：USART6
- 格式：CSV 文本，每行 `\r\n`

当前表头：

```text
theta_deg,theta_rate_dps,theta_ref_deg,x_m,v_mps,
f_theta_n,f_rate_n,f_total_n,u_target_a,u_cmd_a,u_fb_a,fault
```

### 9.6 MaixCAM2 到外部控制器

`25e.py` 通过 UART2 发送二进制 payload：

```python
struct.pack("<BBhhB", 0x55, 0xAA, dx, dy, 0x0A)
```

也就是：

| 字节 | 含义 |
|---|---|
| 0 | 0x55 |
| 1 | 0xAA |
| 2-3 | int16 little-endian dx |
| 4-5 | int16 little-endian dy |
| 6 | 0x0A |

脚本还会读取 UART 输入：

- 收到 `T`：进入 tracking on。
- 收到 `U`：tracking off。

当前 STM32 固件没有解析这个 `0x55 0xAA` 二进制视觉包，所以这条链路更像是给另一个控制器或后续协议改造预留。

## 10. 控制执行逻辑

### 10.1 安全状态机

`Gimbal_Task` 的状态机如下：

```text
左右轮反馈超时
  -> CHASSIS_MOTOR_WAIT_FEEDBACK
  -> fault = CHASSIS_FAULT_FEEDBACK
  -> emergency_stop = 1
  -> output = 0A

左右轮未 enabled
  -> CHASSIS_MOTOR_WAIT_ENABLE
  -> fault = CHASSIS_FAULT_ENABLE
  -> emergency_stop = 1
  -> output = 0A

急停或未按键授权
  -> CHASSIS_MOTOR_ESTOP
  -> output = 0A

反馈正常 + enabled 正常 + 按键授权 + BALANCE_LQR_ENABLE=1
  -> CHASSIS_MOTOR_BALANCE_LQR
  -> 计算 LQR 电流输出
```

左右轮反馈超时时间：100ms。

### 10.2 当前 LQR 状态

LQR 状态定义：

```text
X = [x, v, theta, theta_dot]^T
```

但当前配置 `BALANCE_LQR_USE_WHEEL_STATE=0`，所以实际进入 LQR 的状态是：

```text
x = 0
v = 0
theta = global_balance_angle - theta_ref
theta_dot = global_balance_rate
```

`theta_ref` 由三部分相加：

```text
theta_ref =
  manual_bias
  + BalanceOuterLoop_Update(...)
  + BalanceZeroBiasSearch_GetCandidate(...)
```

当前外环和零偏搜索禁用，因此主要是 `manual_bias`，默认 0 deg。

### 10.3 LQR 增益

配置里的原始增益：

```text
K = [-1.1349, -3.9377, -50.6923, -9.0807]
F = -K * X
```

当前实际使用前又乘了缩放：

| 状态 | 原始 K | 缩放 | 实际参与系数 |
|---|---:|---:|---:|
| x | -1.1349 | 0.34 | -0.385866 |
| v | -3.9377 | 0.36 | -1.417572 |
| theta | -50.6923 | 0.10 | -5.06923 |
| theta_dot | -9.0807 | 0.050 | -0.454035 |

代码为了遥测把力拆成四部分：

```text
force_x_n
force_v_n
force_theta_n
force_rate_n
force_n = sum(...)
```

### 10.4 力到电流

当前输出模式：

```c
#define BALANCE_LQR_OUTPUT_MODE BALANCE_LQR_OUTPUT_MODE_CURRENT
```

所以 LQR 的 `force_n` 直接换成每个轮子的目标电流：

```text
Kt = QD4310_RATED_TORQUE_NM / QD4310_RATED_CURRENT_A = 0.2 N*m/A
I_each = F * wheel_radius / (2 * Kt)
       = F * 0.031 / 0.4
       = F * 0.0775 A/N
```

注意：`config.h` 注释里写过 “暂用 Kt=0.05，I_each=0.31F”，但当前宏实际按 QD4310 额定转矩算出来是 `0.0775 A/N`。

限幅与斜率：

- LQR 电流限幅：±0.40A
- 等效力限幅：约 ±5.16N
- 普通电流斜率：25A/s，即 1ms 内最大变化 0.025A
- 反向过零卸载斜率：80A/s，即 1ms 内最大变化 0.080A
- 最小有效电流：0.035A，只有姿态偏差或角速度达到阈值时才抬到该值

当前左右轮输出相同：

```text
left_current = current_each
right_current = current_each
```

`BalanceLQR_MixYaw()` 和 yaw 差动符号宏存在，但当前主路径没有使用 yaw 差动。

### 10.5 保护逻辑

进入 LQR 后仍持续检查：

- `abs(theta) > 45 deg`：fault=3，急停，输出 0A。
- `abs(theta_rate) > 900 dps`：fault=4，急停，输出 0A。
- 左右轮反馈或 enabled 异常：fault=1/2，急停，输出 0A。

LED 状态：

- 绿灯：急停解除且已授权。
- 红灯：反馈或 enable 异常。
- 蓝灯：反馈/enable 正常但未授权或急停。

### 10.6 速度测试路径

代码保留了速度测试路径：

```text
target rpm
  -> 目标斜坡
  -> 左右轮速度低通
  -> PI + feedforward
  -> 电流斜率限制
```

但当前 `BALANCE_LQR_ENABLE=1`，状态机会优先进入 `CHASSIS_MOTOR_BALANCE_LQR`，所以速度测试状态不会成为当前默认主路径。只有关闭 LQR 编译宏后，`WHEEL_SPEED_TEST_ENABLE=1` 才会进入速度测试状态。

### 10.7 ADRC/ESO 当前状态

`Application/algorithm/adrc_eso.c` 已实现：

- 一阶 ESO：`z1` 估计轮速，`z2` 估计总扰动。
- PI + ESO 控制器。
- 风险削弱积分。
- 输出饱和和抗积分饱和。

但当前主路径没有调用 `PI_ESO_Update()`。现在 LQR 电流模式没有接入 ESO 补偿，速度测试路径也使用 `chassis_speed_pid_update()`，不是 `PI_ESO_Controller_t`。

## 11. 通信协议

### 11.1 QDrive CAN 命令

发送帧：

```text
StdId = 0x400 + motor_id
DLC   = 3
Data  = [cmd, value_l, value_h]
```

电流命令映射：

```text
current[A] -> int16 = current / 10.0 * 32767
```

如果 CAN 发送邮箱满，代码直接丢帧，避免任务在 CAN 断线时阻塞。

### 11.2 QDrive CAN 反馈

接收帧：

```text
StdId = 0x500 + motor_id
DLC >= 8
```

代码接受所有 CAN 帧，但只解析 `0x500` 和 `0x501` 到左右轮结构体。

### 11.3 VOFA CSV

USART6 输出文本 CSV，当前 20ms 一行。也能通过 USART6 接收文本命令。

### 11.4 蓝牙协议

`Device/bluetooth` 实现了：

```text
Header(0xA5,0x5A) + CMD + Length + Payload + CRC16
```

也支持按 `\n` 装配文本行。但当前没有看到 `Bluetooth_Init()` 被调用，所以它只是编译进固件，没有进入当前运行链路。

### 11.5 视觉协议

`Device/vision` 实现了 ASCII：

```text
dx,dy\n
```

但当前没有看到 `Vision_Init()` 被调用，所以视觉输入没有进入当前控制链路。

## 12. 当前没有真正使用的模块

| 模块 | 状态 |
|---|---|
| `Device/bluetooth` | 编译了，但未初始化 |
| `Device/vision` | 编译了，但未初始化 |
| `MahonyAHRSupdateIMU()` | 编译了，但 `INS_Task` 当前用 Kalman，不调用 Mahony |
| `Filter_Init()` / Biquad 姿态滤波 | 编译了，但当前 INS 主路径未调用 |
| `adrc_eso.c` | 编译了，但当前 LQR/速度测试主路径未调用 |
| `component/devices/BMI088driver.c` | 保留参考代码，未编译 |
| `TIM10 PWM` | 初始化了，但未启动 |
| `BalanceLQR_MixYaw()` | 实现了，但当前未用于左右轮差动 |
| `25e.py` | MaixCAM2 视觉脚本，独立运行，不进入 STM32 固件 |

## 13. 编译方法

### 13.1 通过 VS Code

`.vscode/tasks.json` 已配置：

- `STM32: build`
- `STM32: clean`
- `STM32: rebuild`
- `STM32: flash`
- `STM32: build and flash`
- `STM32: openocd server`
- `Serial: monitor VOFA`
- `Serial: capture VOFA 10s`

默认构建命令：

```powershell
D:/MAKE/make.exe
```

### 13.2 通过命令行

如果系统 PATH 已包含 ARM GCC：

```powershell
D:\MAKE\make.exe
```

如果 PATH 没有 ARM GCC，可以临时加入：

```powershell
$env:Path = "D:\GCC\ARMGCC\10 2021.10\bin;$env:Path"
D:\MAKE\make.exe
```

输出文件：

```text
build/spi_bmi088.elf
build/spi_bmi088.hex
build/spi_bmi088.bin
build/spi_bmi088.map
```

本次读取时已有构建产物：

```text
text = 41572
data = 56
bss  = 26544
dec  = 68172
bin  = 41636 bytes
```

注意：直接运行 `D:\MAKE\make.exe -B` 会因为 `build` 目录已存在而触发 `mkdir build` 报错。需要强制全量重建时，用 VS Code 的 `STM32: rebuild`，或先执行：

```powershell
D:\MAKE\make.exe clean
D:\MAKE\make.exe
```

### 13.3 Keil 编译

Keil 工程文件：

```text
MDK-ARM/spi_bmi088.uvprojx
```

目标：

- TargetName：`spi_bmi088`
- Device：`STM32F407IGHx`
- Define：`USE_HAL_DRIVER, STM32F407xx, BMI088_USE_SPI`
- FreeRTOS 使用 RVDS/ARM_CM4F port

如果使用 Keil，打开 `MDK-ARM/spi_bmi088.uvprojx` 后选择 `spi_bmi088` target 编译即可。

## 14. 烧录方法

### 14.1 OpenOCD + ST-Link

`.vscode/tasks.json` 中的烧录命令是：

```powershell
D:\OPENOCD\xpack-openocd-0.12.0-7\bin\openocd.exe `
  -f interface/stlink.cfg `
  -f target/stm32f4x.cfg `
  -c "adapter speed 1000" `
  -c "program build/spi_bmi088.elf verify reset exit"
```

烧录前需要：

1. ST-Link 连接 C 板 SWD。
2. 目标板供电正常。
3. 已生成 `build/spi_bmi088.elf`。

### 14.2 VS Code 一键烧录

可直接运行：

```text
STM32: build and flash
```

它会先构建，再用 OpenOCD 下载并校验，最后 reset。

### 14.3 调试

`.vscode/launch.json` 已配置 Cortex-Debug：

```text
STM32F4: Debug ST-Link OpenOCD
```

参数：

- executable：`build/spi_bmi088.elf`
- device：`STM32F407IGHx`
- interface：SWD
- openocd config：`interface/stlink.cfg`, `target/stm32f4x.cfg`
- runToEntryPoint：`main`

## 15. 运行和调试流程建议

1. 先烧录固件，连接 USART6 对应串口。
2. 打开 VOFA 或 VS Code 任务 `Serial: monitor VOFA`，波特率 115200。
3. 上电后确认 VOFA 有 CSV 表头和数据。
4. 确认 CAN 反馈正常：fault 不应长期为 1，左右轮反馈应更新。
5. 确认左右轮 enabled 正常：fault 不应长期为 2。
6. 默认急停，电机命令为 0A。
7. 机械固定车体，确认姿态角 `theta_deg` 和 `theta_rate_dps` 方向正确。
8. 按 PA0 授权前，确认车体安全、离地或有保护。
9. 按 PA0 后才会解除急停，进入 LQR 输出。
10. 若 `theta` 超 45deg 或 `theta_rate` 超 900dps，会自动急停。

## 16. 当前代码注意事项

1. `README.md` 写着“当前阶段不允许电机输出电流”，但当前代码在 PA0 授权且反馈正常后会进入 LQR 并输出非零电流。实际调试时要按当前代码逻辑看待安全风险。
2. `Error_Handler()` 目前是空函数，HAL 初始化失败后不会可靠停机。
3. `INS_Init()` 中 BMI088 初始化失败会无限重试，没有超时、LED 报错或降级状态。
4. `BALANCE_ACCEL_GATE_ENABLE` 已定义，但当前 Kalman 更新里没有看到加速度模长门控逻辑。
5. `VOFA_OUTPUT_DIVIDER` 已定义，但当前输出节奏实际使用 `VOFA_OUTPUT_PERIOD_MS=20`。
6. `CAN1_RX1_IRQn` 在 `can.c` 里被 enable，但当前中断文件只实现了 `CAN1_RX0_IRQHandler()`；滤波器使用 FIFO0，所以正常主路径不受影响。
7. `Motor_SetAngle()` 注释写“弧度”，实现按 0-360 度处理，而且当前未使用。
8. `component/devices` 里的 BMI088 旧驱动和当前 `Device/bmi088` 驱动并存，后续维护要避免混用。
9. `adrc_eso.c` 与 `config.h` 中 ESO 参数已经存在，但当前没有接入主控制路径。
10. `Gimbal_Task` 文件名和状态名是旧云台遗留，当前实际职责是底盘电机安全与控制。

## 17. 一句话数据流总图

```text
BMI088 DRDY/SPI DMA
  -> INS_Task
  -> 坐标修正 + 加速度角低通 + Kalman
  -> global_balance_angle / global_balance_rate
  -> Gimbal_Task 1ms 安全状态机
  -> LQR force
  -> 每轮电流
  -> CAN current command
  -> QDrive/QD4310 左右轮
  -> CAN feedback
  -> 轮速/电流/编码器状态
  -> Gimbal_Task 安全判断 + VOFA 遥测
```
