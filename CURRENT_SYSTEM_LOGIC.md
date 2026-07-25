# 当前系统正式逻辑汇总（不含临时手动测试 UI）

> 基线日期：2026-07-24
>
> 目标硬件：STM32F407VET6 + 双 QD4310 + ILI9341/XPT2046 + ESP8266 + MaixCAM2/PMOD_TOF100

## 0. 文档范围

本文以当前工作区源码为准，汇总 STM32F407 固件、MaixCAM2 视觉/TOF、双轴控制、弹道、充电发射、TFT 正式界面和通信协议的实际逻辑与公式。

- `Inc/lcd_demo_config.h` 当前默认 `CANNON_MANUAL_TEST_MODE=0U`，使用完整 TFT UI 和正式自动流程。
- 本文不描述该临时精简 UI，也不把临时手动充电/发射规则算入正式系统逻辑。
- 充电、发射和完整 TFT 页面均按 `CANNON_MANUAL_TEST_MODE=0U` 分支说明；原实现仍保留在源码中，并未被删除。
- 当前板卡已烧入本文正式基线对应的 Debug 固件（完整 UI、累计充电 30 s 后自动击发），并已完成写后逐字节校验。
- `GIMBAL_SCAN_ENABLED=0U` 是另一个当前有效产品开关：它只关闭人工往复 Sweep 入口，不会关闭上电后的双轴位置保持，也不会阻止自动搜索流程修改 Yaw 目标。

## 1. 系统边界与当前入口

### 1.1 总体数据流

```text
相机图像 ─┐
          ├─> MaixCAM2 箱面中心识别 + TOF中心测距
TOF矩阵 ──┘                 │
                            │ JX1/VIS，TCP :9000
                            v
STM32 USART3 <─> ESP8266 <─ Wi-Fi ─> MaixCAM2
       │
       ├─> 视觉距离校正、弹道、自动搜索状态机
       ├─> Yaw/Pitch PID ─> CAN1 ─> 两个 QD4310
       ├─> PB1 充电继电器 / PB0 发射继电器
       └─> ILI9341 正式 UI <─ XPT2046 触摸

STM32 ── JX1/ACK、TEL、DBG ──> MaixCAM2
```

### 1.2 STM32 实际入口

构建目标由 `CMakeLists.txt` 确定为 `STM32F407xx`、Cortex-M4F、硬浮点 ABI。入口在 `Src/lcd_demo_main.c`：

```text
bsp_board_init()
bsp_cannon_relay_init()
lcd_demo_app_init()
while (1):
    bsp_board_poll()
    lcd_demo_app_step(millis)
    WFI
```

固件是裸机协作式主循环，不使用 FreeRTOS，不使用动态内存，不使用整屏帧缓冲。链接脚本定义：

| 资源 | 当前定义 |
|---|---:|
| Flash | 512 KiB，`0x08000000..0x0807FFFF` |
| SRAM | 128 KiB，`0x20000000..0x2001FFFF` |
| CCM | 64 KiB，`0x10000000..`，当前未放置业务段 |
| 最小栈预算 | 4 KiB |
| 堆预算 | 0 |

### 1.3 MaixCAM2 实际入口

当前入口为 `maixcam/main.py`：

```text
box_face_app.main(loop_hook=VisionTcpBridge.poll)
```

因此当前运行的是箱面识别、TOF、相机调试画面和 TCP 视觉桥。`maixcam/gimbal_app.py`、`gimbal_ui.py` 是保留的触屏遥测/命令控制台，不是当前入口，不能与视觉桥同时占用 TCP 9000 端口。

## 2. 时钟、调度与初始化顺序

### 2.1 STM32 时钟公式

外部晶振 HSE 为 25 MHz，PLL 参数为 `M=25, N=336, P=2, Q=7`：

$$
f_{VCO,in}=\frac{25\ \mathrm{MHz}}{25}=1\ \mathrm{MHz}
$$

$$
f_{VCO}=1\ \mathrm{MHz}\times336=336\ \mathrm{MHz}
$$

$$
f_{SYSCLK}=\frac{336}{2}=168\ \mathrm{MHz}
$$

$$
f_{USB}=\frac{336}{7}=48\ \mathrm{MHz}
$$

总线时钟：

$$
f_{APB1}=168/4=42\ \mathrm{MHz},\qquad
f_{APB2}=168/2=84\ \mathrm{MHz}
$$

启动是非阻塞状态机：等待 HSE、等待 PLL、等待 SYSCLK 切换三个阶段，各自最多等待 100 ms。任一阶段失败后进入明确错误状态，不继续初始化 LCD、CAN 和应用硬件。

SysTick 在初始 HSI 16 MHz 和最终 168 MHz 下分别重新配置为 1 ms；其优先级为最低。主循环空闲时执行 `WFI`。

### 2.2 应用单步顺序

时钟和硬件就绪后，`lcd_demo_app_step()` 的关键执行顺序为：

1. 更新 PB0 发射脉冲截止时间和 PB1 充电累计时间。
2. 轮询 CAN FIFO，推进两个 QD4310 Enable/Disable 事务。
3. 完成双轴归零条件检查。
4. 更新两轴显示角度及 100 Hz Pitch、Yaw 控制器。
5. 轮询 ESP8266、JX1 命令、视觉帧和遥测发送。
6. 推进 ILI9341 非阻塞初始化；LCD 未 Ready 时不进入 UI。
7. 轮询触摸并将命令发布到静态 FIFO。
8. 每个主循环最多消费一条应用命令。
9. 执行统一的“任务充电完成自动击发”处理。
10. 推进自动搜索状态机。
11. 更新炮控计时显示并按控件脏区重绘。

第 9 步先于第 10 步，这直接决定了正式模式下的最终自动击发行为，详见第 9 节。

## 3. 硬件资源与引脚映射

### 3.1 STM32 主要硬件映射

| 功能 | 引脚/资源 | 配置 |
|---|---|---|
| CAN1 RX/TX | PB8/PB9，AF9 | APB1 42 MHz，1 Mbit/s |
| QD4310 Yaw/Pitch | CAN ID `0x00`/`0x01` | 响应 ID `0x500`/`0x501` |
| 发射继电器 | PB0 | 推挽、下拉、高有效，上电先写低 |
| 充电继电器 | PB1 | 推挽、下拉、高有效，上电先写低 |
| DAP 虚拟串口 | USART1 PA9/PA10，AF7 | 115200 8N1 |
| ESP8266 | USART3 PB10/PB11，AF7 | 115200 8N1 |
| LCD 总线 | FSMC Bank1/NE1，16 位 | CMD `0x60000000`，DATA `0x60020000` |
| LCD 背光 | PA15 | 高有效，仅保留 SWD，不使用完整 JTAG |
| 触摸 CS | PD13 | 低有效 |
| 触摸 SCK/MOSI/MISO | PE0/PE2/PE3 | GPIO 软件 SPI |
| 触摸 PENIRQ | PE4 | 低有效、轮询 |

FSMC LCD 主要信号为 PD7/PD11/PD5/PD4 对应 NE1/A16/NWE/NOE，数据总线分布在 PD0、PD1、PD8..PD10、PD14、PD15 和 PE7..PE15。16 位总线下外部 A16 对应 CPU 字节地址 A17，因此数据地址相对命令地址偏移 `0x20000`。

FSMC 时序：读 `ADDSET=15, DATAST=60`，写 `ADDSET=3, DATAST=8`。

### 3.2 CAN 位时序

当前配置为 Prescaler=6、BS1=5 TQ、BS2=1 TQ，加上同步段 1 TQ：

$$
f_{CAN}=\frac{42\ \mathrm{MHz}}{6\times(1+5+1)}=1\ \mathrm{Mbit/s}
$$

CAN 使用接收 FIFO0 全接收过滤，主循环轮询，不使用 CAN 中断和 DMA。`NART=1`，即单次发送；三个邮箱都忙时立即返回失败，不无限等待或自动重发。

### 3.3 UART 缓冲和中断

USART1/USART3 使用 RXNE/TXE 中断，优先级均为 6，不使用 DMA。存在三个 256 字节静态 SPSC 环形缓冲：

- PC → ESP；
- ESP → PC；
- ESP → 应用。

环形缓冲实际可用容量为 255 字节；满载时丢弃新字节并累计 dropped 计数。ESP 被应用层 `claim` 后，PC → ESP 的透明转发停止，ESP 数据进入应用解码缓冲；ESP → PC 的开发调试镜像仍保留。

### 3.4 MaixCAM2 TOF 引脚

PMOD_TOF100 同时使用 I2C7 控制线和 SPI2 数据线：

| 信号 | MaixCAM2 引脚 |
|---|---|
| I2C7 SCL/SDA | A8/A9 |
| SPI2 CS1 | B21 |
| SPI2 MISO/MOSI/SCK | B19/B18/B20 |

预留 UART2 为 B0/B1、115200，但当前 `UART_ENABLED=False`，正式视觉数据走 TCP，不走该串口。

## 4. LCD、触摸与正式 TFT 页面

### 4.1 ILI9341

ILI9341 使用 RGB565、横屏 320×240、`MADCTL=0xA8`。初始化过程为：

1. 上电等待 10 ms，背光保持关闭。
2. 发送软件复位 `0x01`，等待 10 ms。
3. 读取 `0xD3` 控制器 ID，并记录是否等于 `0x9341`；ID 不匹配不会阻断后续初始化。
4. 写电源、VCOM、像素格式、Gamma、横屏等寄存器。
5. 发送 Sleep Out `0x11`，等待 120 ms。
6. 发送 Display On `0x29`，进入 Ready。
7. 首次正式 UI 绘制完成后才打开 PA15 背光。

没有 TE 同步和整屏帧缓冲，界面通过控件级 dirty mask 局部刷新。

### 4.2 XPT2046 采样和坐标公式

- 每 5 ms 轮询一次。
- 横屏 X/Y 命令分别为 `0x90`、`0xD0`。
- 每次读取 X、Y 各 5 个 12 位样本并取中位数。
- 全 0 或全 1 的 ADC 故障值无效。
- 按下、释放都要求连续 3 次有效判定。

原始坐标映射到像素：

$$
p=\operatorname{clamp}\left(
\frac{(raw-raw_{start})(N-1)}{raw_{end}-raw_{start}},
0,N-1\right)
$$

当前标定为：X 左/右 `3680/207`，Y 上/下 `577/3643`。最终坐标严格限制在 `x=0..319, y=0..239`。

触摸控件只有“按下时命中的控件”和“释放时命中的控件”相同才会激活动作，避免滑出按钮后误触发。

### 4.3 正式完整 UI（测试开关为 0）

正式首页包含：

- 手工设定距离和视觉 `DIST` 显示；
- “距离定位”“固定靶”“自动搜索”；
- Yaw/Pitch 实际角度与目标角度卡片；
- 人工 Sweep 入口；当前因 `GIMBAL_SCAN_ENABLED=0U` 显示不可用；
- Motor、充电、Theme 页面入口。

输入规则：

- 距离输入为 50..100 cm，默认 75 cm；该值目前只保留 UI/协议兼容用途，三个正式任务都读取视觉 TOF 距离。
- Yaw 和 Pitch 人工目标都允许 -50°..+50°。

Motor 页面提供 Enable/Home、Sweep/Angle Hold、Zero Current、Disable，并显示两轴实际角度和事务状态。`ZERO CURRENT` 停止两轴 PID 并各发送一次 0 A，但保持电机 Enable；`DISABLE` 才发送 Disable 命令。

任务受理成功后进入正式发射页，显示累计充电时间、发射脉冲剩余时间和 `IDLE/CHARGING/PAUSED/WAIT_AIM/FIRING/READY` 状态。充电页允许开始或恢复充电、立即断开；返回会发布充电中止。Theme 页面保留两张静态主题预览。

## 5. QD4310 协议、归零和反馈换算

### 5.1 CAN 帧

命令标准 ID：

$$
ID_{cmd}=0x400+motor\_id
$$

响应标准 ID：

$$
ID_{resp}=0x500+motor\_id
$$

命令帧 DLC=3：

| 命令 | Byte 0 | Byte 1..2 |
|---|---:|---|
| Enable | `0x01` | `00 00` |
| Disable | `0x02` | `00 00` |
| Current | `0x03` | `int16` 小端电流命令 |

响应帧至少 8 字节：状态、保留、电流 `int16`、速度 `int16`、角度 `uint16`，多字节字段均为小端。状态 bit0 表示 Enabled。

Enable/Disable 事务超时 250 ms，状态为 `IDLE/WAITING/CONFIRMED/REJECTED/TIMEOUT/CAN_ERROR`。

### 5.2 上电归零

CAN 初始化成功后自动向 Yaw 和 Pitch 发送 Enable。只有两个节点都确认 Enabled，且两个节点相对于本次请求都至少收到一帧新反馈后，才完成归零并启动两个位置控制器：

- Yaw 零点取当时反馈编码器值，因此上电当前位置定义为 0°。
- Pitch 零点固定为 `0xEE77`，不采用上电当前位置。
- 两轴初始目标均为 0°。

### 5.3 反馈换算

角度差先按 `int16_t` 解释，自动处理 16 位编码器回绕：

$$
\Delta raw=\operatorname{int16}(raw_{angle}-raw_{zero})
$$

$$
angle_{deg}=\Delta raw\times\frac{360}{65536}
$$

速度和反馈电流：

$$
speed_{rpm}=raw_{speed}\times\frac{1000}{32767}
$$

$$
current_A=raw_{current}\times\frac{10}{32767}
$$

电流命令编码：

$$
raw_{current}=\operatorname{round}\left(
\frac{current_A}{10}\times32767\right)
$$

## 6. 双轴位置控制

### 6.1 通用控制公式

Yaw、Pitch 都以 10 ms 周期运行。令目标误差：

$$
e=target_{deg}-actual_{deg}
$$

先限幅：

$$
e_l=\operatorname{clamp}(e,-e_{max},+e_{max})
$$

再应用连续死区：

$$
e_{db}=\begin{cases}
e_l-d_b,&e_l>d_b\\
e_l+d_b,&e_l<-d_b\\
0,&|e_l|\le d_b
\end{cases}
$$

积分只在原始误差位于积分区时累计：

$$
I_k=\operatorname{clamp}(I_{k-1}+K_i e\Delta t,-I_{max},I_{max})
$$

若 $|e|$ 超出积分区，积分立即清零。输出采用位置 P、积分 I 和 QD4310 原始转速阻尼：

$$
u=\operatorname{clamp}(K_p e_{db}+I-K_d speed_{rpm},
-I_{cmd,max},+I_{cmd,max})
$$

$$
current_{cmd}=polarity\times u
$$

目标改变时积分清零。实际角度越过硬边界时控制失败并输出 0 A；在恰好位于硬边界且输出还会继续向外时，也将输出和积分清零。

### 6.2 当前参数

| 参数 | Yaw | Pitch |
|---|---:|---:|
| 控制周期 | 10 ms | 10 ms |
| `Kp` | 0.080 A/° | 0.010 A/° |
| `Ki` | 0.002 A/(°·s) | 0.0002 A/(°·s) |
| `Kd` | 0.006 A/rpm | 0.0005 A/rpm |
| 误差限幅 | ±8° | ±8° |
| 死区 | 0.25° | 0.20° |
| 积分区 | ±1.5° | ±1.0° |
| 积分限幅 | ±0.010 A | ±0.010 A |
| 电流限幅 | ±0.70 A | ±0.30 A |
| 硬角度边界 | ±55° | ±90° |
| 反馈超时 | 100 ms | 100 ms |
| 电流极性 | +1 | +1 |

Yaw 人工目标限 ±50°。Pitch 人工目标限 ±50°，弹道等内部目标限 ±85°。

虽然模块名仍叫 `yaw_scan_controller`，当前主要行为是位置保持器。人工 Sweep 入口关闭后，控制器仍会在 Enable/Home 后保持目标，并允许自动搜索状态机以 8°/s 的目标斜坡修改 Yaw 目标。

### 6.3 故障处理

控制故障会取消自动瞄准，停止两个 PID，尝试向两个节点各发送一次 0 A，但不会自动 Disable 电机。故障码：

| 码 | 含义 |
|---:|---|
| 1 | 双轴没有同时处于 Enabled |
| 2 | Yaw 反馈超时 |
| 4 | Yaw 控制更新失败或越界 |
| 5 | Yaw 电流帧发送失败 |
| 6 | Pitch 反馈超时 |
| 7 | Pitch 控制更新失败或越界 |
| 8 | Pitch 电流帧发送失败 |

## 7. 弹道模型

### 7.1 标定参数

| 参数 | 当前值 |
|---|---:|
| 炮口离地高度 $H_m$ | 12.0 cm |
| 0° 水平发射落地点 $R_0$ | 67.5 cm |
| 固定靶目标高度 $H_t$ | 36.5 cm |
| 箱体前沿到靶心的水平距离 $\Delta d$ | 25.0 cm |
| 普通距离低/高弹道切换阈值 | 5.0° |
| 手工地面靶距离 / 视觉箱体前沿距离 | 50..100 cm |

模型用 0° 水平发射落地点反推等效重力项，不直接保存初速度：

$$
G(d)=H_m\left(\frac{d}{R_0}\right)^2
$$

相对高度：

$$
\Delta H=H_t-H_m
$$

判别式：

$$
D=d^2-4G(G+\Delta H)
$$

若 $D<0$、数值非有限、$G<0.001$ 或最终角度超过 ±85°，本次求解失败。

低、高弹道切线：

$$
\tan\theta_{low}=\frac{d-\sqrt D}{2G}
$$

$$
\tan\theta_{high}=\frac{d+\sqrt D}{2G}
$$

$$
\theta=\arctan(\tan\theta)
$$

`cannon_aim_pitch_for_distance()` 以地面 0 cm 为目标并强制选择低弹道。固定靶函数使用下面的实测高弹道距离修正。

提交给 Pitch 控制器前整体取反：

$$
pitch_{target}=-\theta
$$

当前“距离定位”调用地面 0 cm 低弹道；“固定靶”和“自动搜索”把视觉 TOF 给出的箱体前沿距离 $d_f$ 映射为靶心水平距离：

$$
d_t=d_f+25\ \mathrm{cm}
$$

高弹道实测标定点为：TFT 显示目标靶心距离 100 cm 时，弹丸只到达箱体前沿 37 cm 的位置。观测值也必须加同一个 25 cm 靶心偏移，所以：

$$
d_{obs}=37+25=62\ \mathrm{cm}
$$

$$
q=\frac{d_{obs}}{d_{ref}}=\frac{62}{100}=0.62
$$

固定靶/自动搜索不再直接使用原高根，而先把当前靶心距离映射为虚拟求解距离。为使高射轨道
进一步增加射程，在距离域加入1.013倍乘法裕量：

$$
d_v=\frac{d_t}{q}\times1.013
$$

若 $d_v$ 在模型的可达范围内，则：

$$
\theta_{fixed}=\theta_{high}(d_v,H_t)
$$

由当前水平发射标定得到：

$$
S=\frac{R_0^2}{2H_m},\qquad
d_{max}=\sqrt{S\left(S-2\Delta H\right)}=163.52\ \mathrm{cm}
$$

若 $d_v\ge d_{max}$，说明单点实测修正后的请求已超出当前模型可达包络，不返回无效目标，而使用最大射程角：

$$
\theta_{range}=\arctan\sqrt{\frac{S}{S-2\Delta H}}=49.26^\circ
$$

弹丸直径不参与计算，也不叠加固定Pitch角。这是以`37+25=62 cm`实测点得到的水平距离比例映射；
1.013把虚拟距离增加1.3%，让高根角度进一步降低并换取水平射程。下表是当前代表值：

| 箱体前沿距离 $d_f$ | TFT靶心距离 $d_t$ | 虚拟距离 $d_v$ | 最终 Pitch 目标 |
|---:|---:|---:|---:|
| 50 cm | 75 cm | 122.54 cm | -67.6547° |
| 67.5 cm | 92.5 cm | 151.13 cm | -59.0745° |
| 75 cm | 100 cm | 163.39 cm | -50.2424° |
| 100 cm | 125 cm | 204.23 cm，超包络 | -49.2606° |

注意：该比例只有一个实测点。`d_t=100 cm`的新角度仍为高弹道，并从上一版 -51.3932°进一步降到
-50.2424°，用少量高度换取水平射程；`d_t>q d_{max}/1.013\approx100.08 cm`时当前标定认为理论上
无法精确命中，只能给最大射程角，必须继续用新落点补充标定。

## 8. 视觉距离、任务入口与目标来源

### 8.1 视觉距离校正

STM32 把有效 VIS/TOF 距离视为炮口到箱体前沿的水平距离，固定靶弹道求解时映射到靶心距离：

$$
d_{target,mm}=d_{box-front,mm}+250
$$

“固定靶”和“自动搜索”验证原始箱体前沿距离为 500..1000 mm，再用 750..1250 mm 的靶心距离求解 Pitch；TFT 的 `DIST` 显示靶心距离。“距离定位”仍使用人工设定的 50..100 cm 地面靶距离，不消费 VIS 距离。

视觉数据超过 500 ms 没有新帧时置为离线，`DIST` 无效。若加 250 mm 会产生 `uint16` 溢出，则清除距离有效标志。

### 8.2 三个正式任务

| 入口 | 当前实际距离源 | Pitch | Yaw | 正式充电/击发 |
|---|---|---|---|---|
| 距离定位 | 当前设定距离 50..100 cm | 地面 0 cm 低弹道并取反 | 保持现目标 | 成功后新开 30 s 周期 |
| 固定靶 | 最新箱体前沿 VIS 距离 50..100 cm | 离地 36.5 cm 靶心、`d_v=d_t/0.62×1.013`校正高弹道取反 | 保持现目标 | 成功后新开 30 s 周期 |
| 自动搜索 | 对准阶段的最新箱体前 VIS 距离 | 同上 | ±50°搜索、一次锁定 | 开始任务时新开 30 s 周期 |

“距离定位”调用 `cannon_aim_pitch_for_distance()`，只消费 UI/JX1 保存的设定距离并强制选择地面 0 cm 低弹道；“固定靶”和“自动搜索”调用 36.5 cm 固定靶高弹道路径，并消费箱体前沿 VIS 距离，弹道函数内部加 25 cm 后求解。所需距离无效或越界时保持原 Pitch 目标，不开始任务充电。

## 9. 正式充电和发射逻辑

### 9.1 继电器规则

- PB1：充电，高有效。
- PB0：发射，高有效，固定脉宽 500 ms。
- 初始化时先建立两路无效电平，再把 PB0/PB1 切为输出。
- 发射前若 PB1 仍导通，先关闭 PB1，再打开 PB0。
- 软件不允许 PB0/PB1 同时导通。

### 9.2 充电累计

正式充电目标为 30,000 ms。只累计 PB1 实际处于导通状态的主循环时间：

$$
t_{charge}\leftarrow
\min(t_{charge}+\Delta t,30000\ \mathrm{ms})
$$

- 新任务调用 `begin_new_charge_cycle()`：累计清零并立即导通 PB1。
- 人工断开或任务取消：PB1 关闭，累计值保留，状态为 Paused。
- 再次开始：从原累计值恢复。
- 自动搜索暂停充电时，任务截止时间在恢复时按暂停时长顺延，等价于暂停期间冻结 120 s 超时。
- 累计满 30 s 后先关闭 PB1，再把本轮标记为 Complete。
- 手工单独开始充电也会在 30 s 自动关闭，但只有任务设置了 `s_task_auto_fire_pending` 才会自动击发。

### 9.3 发射条件

正式模式下，手动发射也要求：

```text
charge_cycle_started == true
charge_cycle_complete == true
fire_relay_active == false
```

满足后 PB0 高 500 ms，截止时间到达后自动关闭。再次点击中止、STOP、DISABLE、控制故障或自动任务取消都会关闭 PB0。

### 9.4 当前最终自动击发行为

源码中存在两处自动击发路径：

1. `AUTO_AIM_SETTLE` 路径要求视觉稳定、两轴到位、稳定 300 ms、有进入稳定态后的新视觉帧、充电完成且剩余任务时间足够。
2. 统一的 `position_auto_fire_step()` 只检查“任务待击发 + 30 s 充电完成 + 当前未发射”。

由于主循环先执行统一路径，再执行自动搜索状态机，所以三个任务的当前最终行为都是：

```text
任务受理 → PB1累计导通30 s → 关闭PB1 → 立即输出PB0高500 ms
```

此时不再检查 Yaw/Pitch 是否到位、视觉是否在线、目标是否居中、TOF 是否有效或 `AIM_STABLE` 是否成立。换言之，自动搜索中的精确稳定发射门控目前被统一充电完成路径旁路；这是当前源码的实际执行结果，不是文档推测。

## 10. 自动搜索状态机

当前 `AUTO_AIM_SINGLE_PASS_LOCK=1U`，有效状态序列为：

```text
IDLE → SEARCH → SETTLE → FIRING → IDLE
```

原 `CALIBRATE_A / CALIBRATE_B / ALIGN` 双程标定分支仍保留在源码中；将开关改回 `0U` 可恢复，当前运行时不进入该分支。

### 10.1 前置条件和超时

启动要求：

- 双轴均确认 Enabled；
- 本次归零有效；
- Yaw/Pitch 控制器都在运行；
- 当前无发射脉冲、无自动搜索、无控制故障。

自动搜索 active 时，正式模式只允许 STOP、DISABLE、发射中止、充电开始/恢复和充电中止通过应用分发；新的定位、角度、Sweep、START 和手动发射命令都会被拒绝。充电开始/中止之所以保留，是为了支持暂停后恢复并冻结任务超时。

总任务超时 120 s。非暂停状态到期会失败并取消任务；充电暂停时不判超时，恢复充电时将截止时间顺延。

### 10.2 慢速搜索

搜索范围为相对零点 ±50°，外层机械硬边界仍为 ±55°。未见目标时，起始方向取当前位置符号的反方向：

$$
direction=\begin{cases}-1,&yaw\ge0\\+1,&yaw<0\end{cases}
$$

Yaw 目标按 8°/s 斜坡推进：

$$
\Delta yaw_{max}=8\ \frac{deg}{s}\times\frac{\Delta t_{ms}}{1000}
$$

收到普通 `X_VALID` 后立即计算像素误差：

$$
e_x=x-\frac{frame\_width}{2}
$$

相机固定在Yaw轴上，$e_x$是当前视轴相对目标的误差，不是一次性世界坐标。每个新视觉序号只消费
一次，并按以下外环图像伺服更新命令目标：

$$
\Delta yaw_k=\operatorname{clamp}\left(-0.020e_{x,k},
-\Delta yaw_{max,k},\Delta yaw_{max,k}\right)
$$

$$
\Delta yaw_{max,k}=\min\left(2^\circ,
3\ \frac{deg}{s}\frac{\Delta t_{ms}}{1000}\right)
$$

$$
yaw_{cmd,k}=\operatorname{clamp}(yaw_{cmd,k-1}+\Delta yaw_k,-50^\circ,50^\circ)
$$

因此 $e_x>0$ 时命令向负Yaw累计，$e_x<0$时向正Yaw累计，并随相机转动后的新相对误差继续闭环，
直至进入中心。命令基准始终是上一条命令目标，不回写为当前电机反馈角；搜索斜坡和图像伺服使用
连续目标接口，目标变化不反复清空Yaw积分。

若 $|e_x|\le8$ px，保持最后一条 Yaw 命令目标，不再继续到端点、不反向回扫、不进入两点标定。
同时读取有效箱体前沿距离，加 25 cm 得到靶心水平距离，再按 $d_v=d_t/q\times1.013$ 映射虚拟距离
并求36.5 cm靶心高弹道；超出可达包络时使用49.26°最大射程角。两轴到目标 ±1° 后进入 SETTLE。

Yaw锁定使用迟滞死区：未锁定时必须满足 $|e_x|\le8$ px才停止；锁定后只要
$|e_x|\le16$ px就继续保持最后命令。超过16 px时必须连续2个新样本方向相同才解除锁定并恢复相对
误差累计，单帧`EDGE_QUAD`等离群点不解锁。锁定期间偶发无效帧不会立即启动Yaw，沿用现有500 ms
目标丢失超时；连续500 ms没有有效X后才重新搜索。重新搜索从上一命令目标连续斜坡，不回退到反馈角。

若从未锁定且未见目标，Yaw继续扫向当前±50°端点；到端点仍未见目标时才反向。

### 10.3 稳定判定

SETTLE 中持续检查：

- 新视觉帧仍有 `X_VALID`；当前单程模式不要求 `X_FINAL` 或 `AIM_STABLE`；
- 像素误差未被连续2个同方向、超过±16 px的新样本判定为解锁；
- 距离仍有效，Pitch 可重新求解；
- 两轴仍在目标 ±1°。

视觉连续失效500 ms或确认越过退出窗口则回 SEARCH；两轴瞬时未到位时保持当前目标并重新累计稳定
时间，不启动第二次标定。理论稳定条件为持续 300 ms且收到进入 SETTLE 后的新视觉帧，但第 9.4 节
所述统一 30 s 自动击发路径会优先执行。

## 11. 应用命令总线和 JX1 协议

### 11.1 应用命令 FIFO

UI 和 ESP/JX1 命令统一转换为 `app_command_t`，进入 8 槽静态 FIFO：

- FIFO 满时立即返回 FULL，丢弃新命令；
- 每个主循环最多处理一条；
- STOP、DISABLE、发射中止、充电中止在发布阶段就先执行相应安全动作，之后命令仍进入 FIFO 做状态处理。

### 11.2 JX1 通用帧

所有 TCP 帧为 ASCII，以换行结束：

```text
JX1|KIND|SEQ|PAYLOAD_LEN|field1=value1|field2=value2|CRC16\n
```

- 最大帧长 384 字节。
- `SEQ` 为 0..65535。
- `PAYLOAD_LEN` 是 ASCII payload 的字节数，不含其前导 `|`。
- CRC 覆盖从 `JX1` 开始到 CRC 前一个分隔符之前的完整 body。
- CRC16-CCITT 初值 `0xFFFF`，多项式 `0x1021`，最高位优先，输出 4 位十六进制。
- `\r` 被忽略；缓冲溢出、CRC、长度、字段或范围错误都会累计协议错误计数。

位更新形式：

$$
crc_0=0xFFFF
$$

每个输入字节先执行 $crc\mathrel{\hat{=}}byte\ll8$，再迭代 8 位：

$$
crc\leftarrow\begin{cases}
(crc\ll1)\oplus0x1021,&crc\ \&\ 0x8000\\
crc\ll1,&otherwise
\end{cases}
$$

所有步骤保留低 16 位。

### 11.3 CMD

```text
JX1|CMD|seq|len|name=COMMAND|value=N|crc
```

| 命令 | `value` | 当前含义 |
|---|---:|---|
| `SET_DISTANCE` | cm | 50..100，更新距离定位使用的设定值 |
| `FIRE_DISTANCE` | 忽略 | 使用当前设定距离执行地面 0 cm 低弹道定位 |
| `FIXED_TARGET` | 忽略 | 使用最新箱体前沿 VIS 距离，加 25 cm 后按`d_v=d_t/0.62×1.013`映射虚拟距离并选择36.5 cm靶心高弹道；超包络用最大射程角 |
| `AUTO_SEARCH` | 忽略 | 启动自动搜索 |
| `SWEEP` | 忽略 | 当前因产品开关为 0 被拒绝 |
| `START` | 忽略 | 双轴 Enable/Home |
| `STOP` | 忽略 | 取消任务、关闭继电器、双轴 0 A，保持 Enable |
| `DISABLE` | 忽略 | 取消任务、关闭继电器并 Disable 双轴 |
| `SET_YAW_TARGET` | ° | -50..+50 |
| `SET_PITCH_TARGET` | ° | -50..+50 |

JX1 线协议没有直接暴露本地 UI 的 `FIRE_PULSE/FIRE_ABORT/CHARGE_START/CHARGE_ABORT` 四个扩展命令。

CMD 入队后返回 ACK：`ACCEPTED/FULL/INVALID`。ACK 也只代表语法和入队结果，不代表命令已经完成。ACK 队列也是 8 槽；发送优先级高于遥测。

### 11.4 VIS

```text
JX1|VIS|seq|len|x=N|d=MM|f=FLAGS|w=WIDTH|crc
```

| Flag | 值 | 含义 |
|---|---:|---|
| `X_VALID` | `0x01` | `x` 是有效水平中心 |
| `X_FINAL` | `0x02` | 中心来自最终精定位；必须同时有 X_VALID |
| `DISTANCE_VALID` | `0x04` | `d` 是有效毫米距离 |
| `AIM_STABLE` | `0x08` | Maix 连续稳定判定成立；必须同时有 X_FINAL |

无效 X 必须为 `-1`；有效 X 必须满足 `0 <= x < w`。无效距离必须为 `0xFFFF`。VIS 是传感数据，不是发射命令，STM32 不对 VIS 返回 ACK；解码器只保留尚未被应用取走的最新一帧 VIS。

### 11.5 TEL、DBG

STM32 每 100 ms 尝试发送一帧 TEL，下一次空闲发送 DBG；若有 ACK，先发 ACK。ESP 发送器同一时刻只容纳一帧待发数据，因此忙时不会阻塞主循环，保留到下次再试。

TEL 字段：

| 字段 | 单位/含义 |
|---|---|
| `t` | ms 时间戳 |
| `s/f` | UI 运行状态/反馈文本 |
| `d` | 手工兼容距离 cm，不是 VIS 距离 |
| `yr/pr` | Yaw/Pitch 原始编码器 |
| `yv/pv` | 两轴反馈有效位 |
| `y/p` | 两轴实际角 mdeg |
| `g/h` | Yaw/Pitch 目标角 mdeg |
| `c/i` | Yaw 命令/反馈电流 mA |
| `pc/pi` | Pitch 命令/反馈电流 mA |
| `v` | Yaw 速度 mrpm |
| `ys/ps` | 两轴 QD4310 事务状态 |
| `a/e/x` | 控制器 active、fault、fault code |

DBG 字段包含响应次数、控制命令次数、Yaw 最小/最大角、峰值命令电流、ESP 收包丢弃、ESP 发帧数、UART 丢弃和协议错误数。

## 12. ESP8266 非阻塞 TCP 状态机

STM32 初始化时占用 ESP UART，并按以下流程运行：

```text
BOOT_WAIT 3 s
→ AT
→ AT+CIPMODE=0
→ AT+CIPMUX=0
→ 等待 WIFI GOT IP（最多等到连接超时）
→ AT+CIPSTART="TCP","192.168.215.66",9000
→ READY
```

- 普通 AT/发送等待超时 2 s。
- TCP 连接超时 8 s。
- 失败、`CLOSED` 或发送失败后等待 3 s，再从 `AT` 重试。
- 识别 `OK/ERROR/FAIL/CONNECT/ALREADY CONNECTED/CLOSED/SEND OK/WIFI GOT IP/WIFI DISCONNECT`。
- TCP 下行通过 `+IPD,length:` 解析，进入 255 字节环形缓冲；满载丢新字节。
- 上行先发 `AT+CIPSEND=n`，等待 `>`，写 payload，等待 `SEND OK`。
- 上行单帧最大 255 字节，且只允许一帧 pending；忙或未 Ready 时 `send()` 立即失败。

ESP8266 的 Wi-Fi 入网参数不在本固件中配置，默认依赖模块已保存的 STA 配置。

## 13. MaixCAM2 当前视觉逻辑

### 13.1 当前有效配置

| 项目 | 当前值 |
|---|---:|
| 相机 | 640×480，30 FPS，3 缓冲 |
| YOLO 模型路径 | 空，当前不运行 YOLO |
| 中心方法 | Native `find_blobs` 引导 + 灰度边缘四边形精定位 |
| 调试显示 | 开启，每帧刷新 |
| 日志周期 | 0.5 s |
| TOF | 必需，50×50，SPI2 |
| TOF 采样 | 每 3 个 RGB 帧一次 |
| TOF 业务有效范围 | 200..1500 mm |
| TOF 业务 ROI | 中心 1×1，至少 1 个有效点 |
| TCP 服务 | `0.0.0.0:9000`，单客户端、非阻塞 |
| UART2 视觉输出 | 关闭 |

TOF 被配置为 required，打开失败会终止当前视觉程序；RGB 空帧则记录错误、调用一次空 hook、等待 20 ms 后继续。

### 13.2 当前识别流程

当前没有 YOLO，因此每帧在全图运行：

```text
LAB find_blobs 粗引导
→ 颜色面积 EMA/稳定峰值判定
→ 灰度 + Gaussian + Canny + 闭运算
→ 凸四边形筛选
→ 左右真实竖边 Canny + HoughLinesP
→ 水平中轴自适应 EMA
→ 与本次 TOF 中心距离组成同一数据包
```

#### 13.2.1 Blob 粗引导

LAB 阈值为 `(L 20..50, A 1..9, B 6..20)`。ROI 动态门限：

$$
pixels_{min}=\max(40,round(A_{ROI}\times0.01))
$$

$$
boxArea_{min}=\max(100,round(A_{ROI}\times0.03))
$$

候选宽、高还必须分别达到 ROI 的 20%。对每个候选：

$$
fill=\operatorname{clamp}\left(\frac{pixels}{w h},0,1\right)
$$

$$
rank=pixels\times(0.5+fill)
$$

选取 rank 最大的 Blob。粗中心取外接框中心而不是像素质心：

$$
c_x=x+\frac{w}{2},\qquad c_y=y+\frac{h}{2}
$$

Blob 阶段中心标记为 `center_is_final=False`，不能产生 `X_FINAL`。若候选触及全图左右边界容差：

$$
margin=\max(3px,0.025\times frameWidth)
$$

则标记为不完整并清空面积稳定历史。

#### 13.2.2 面积稳定峰值

面积使用所有 Blob 候选阈值像素数之和，不使用最佳外接框面积。EMA：

$$
A_{ema,k}=0.35A_k+0.65A_{ema,k-1}
$$

保留最近 8 帧，峰值为窗口最大 EMA。至少有 5 帧且同时满足：

$$
\frac{\max(A)-\min(A)}{\max(1,A_{peak})}\le0.08
$$

$$
A_{ema}\ge0.92A_{peak}
$$

才确认进入四边形精定位。确认后若当前原始面积低于：

$$
A<0.86A_{peak}
$$

则解除确认。

#### 13.2.3 灰度四边形精定位

精定位对 ROI 以 0.50 比例处理：灰度化、5×5 Gaussian、Canny 40/120、5×5 矩形核闭运算 3 次、轮廓提取、`approxPolyDP`，精度为周长的 0.035。只保留凸四边形。

四点按左上、右上、右下、左下排序。四边形中心采用两条对角线交点。令线段 `p1-p2` 与 `p3-p4`，分母：

$$
den=(x_1-x_2)(y_3-y_4)-(y_1-y_2)(x_3-x_4)
$$

若 $|den|<10^{-9}$ 则无中心。否则：

$$
c_x=\frac{(x_1y_2-y_1x_2)(x_3-x_4)-(x_1-x_2)(x_3y_4-y_3x_4)}{den}
$$

$$
c_y=\frac{(x_1y_2-y_1x_2)(y_3-y_4)-(y_1-y_2)(x_3y_4-y_3x_4)}{den}
$$

候选指标：

$$
areaRatio=\frac{A_{quad}}{A_{ROI}}
$$

$$
aspect=\frac{(top+bottom)/2}{(left+right)/2}
$$

$$
rectangularity=\min\left(1,\frac{A_{quad}}{A_{minAreaRect}}\right)
$$

每个角的正交分数为 $1-|\cos\phi|$，四角取平均。左右边竖直度：

$$
verticality=\max\left(0,1-\frac{1}{2}
\left(\frac{|\Delta x_L|}{len_L}+\frac{|\Delta x_R|}{len_R}\right)\right)
$$

综合分数：

$$
score=0.28S_{area}+0.27S_{rect}+0.25S_{orth}
+0.15S_{vertical}+0.05S_{center}
$$

其中 $S_{area}=\min(1,areaRatio/0.55)$，中心分数按候选中心到 ROI 中心的归一化距离计算。

筛选阈值：原图等效面积至少 1200 px²、ROI 面积比 0.02..0.98、宽高比 0.35..4.0、矩形度至少 0.55、正交度至少 0.20、竖直度至少 0.35、最短边至少 18 px。选择 score 最大者。

精定位有效后进入 corner mode。每帧继续重新检测；第一次漏检仅沿用上一帧已确认四边形一帧，连续 2 次失败才清空 corner mode 并回到 Blob 搜索。

#### 13.2.4 左右真实竖边和中轴

四边形先提供左右搜索带；在 0.50 缩放图上执行 Gaussian、Canny 40/120、概率 Hough：阈值 18，最短线长为箱高的 25%，最大断点为箱高的 8%。只接受：

$$
tilt=\frac{|\Delta x|}{|\Delta y|}\le0.30
$$

搜索半带宽为：

$$
bandHalf=\max(12px,0.23\times boxWidth)
$$

单边候选分数：

$$
S_{edge}=0.48S_{vertical}+0.25S_{length}
+0.20S_{position}+0.07S_{temporal}
$$

左右边间距至少为四边形预期宽度的 45%。边对分数：

$$
S_{pair}=0.425S_L+0.425S_R+0.15S_{width}
$$

在共同参考高度处取左右边横坐标，中轴强制为竖线：

$$
c_x=\frac{x_L+x_R}{2}
$$

若本帧 Hough 竖边提取失败，仍保留本帧四边形左右边生成的中轴，不把该帧直接伪装成 Hough 成功。历史只用于下一帧候选预测，不直接平均原始边线；连续丢失超过 2 帧会清除 4 帧历史。

最后对控制中轴做自适应 EMA。令 $e=c_{raw}-c_{filtered}$：

$$
\alpha=\begin{cases}
0,&|e|\le1.5px\\
0.30,&1.5px<|e|\le6px\\
0.80,&|e|>6px
\end{cases}
$$

$$
c_{filtered}\leftarrow c_{filtered}+\alpha e
$$

无效视觉帧不会伪造有效中心。

程序另保留最多 8 帧的 partial track：可按最近两个最终中心的速度，或 YOLO 粗框中心位移，对暂时出画的目标做短期预测。该预测只写入调试 `track_*` 字段，不会把 `result.valid`、`center_final` 或 TCP 的 `X_VALID` 改为真，因此不能参与 STM32 自动对中或发射判定。

### 13.3 保留但当前未启用的 YOLO 路径

若以后设置 `YOLO_MODEL_PATH`：

- 目标类别为 0，置信度阈值 0.25，IoU 0.45；
- 候选排序量为 $w h(0.5+confidence)$；
- YOLO 粗框四周扩大 16% 后交给传统视觉；
- 正常每 2 帧运行一次 YOLO，中间帧根据最近两框线性外推；
- 真正漏检后立即清空旧框；当前不回退全图传统视觉；
- 框面积占全图至少 55% 或距边界不超过 3 px 时视为近目标；当前 `YOLO_EVERY_FRAME_WHEN_NEAR=False`；
- 近目标传统处理比例 0.30，普通比例 0.40。

此路径是当前源码能力，但因模型路径为空，不参与当前运行结果。

## 14. TOF 统计与视觉稳定标志

### 14.1 TOF ROI

深度矩阵按 `matrix[y][x]` 访问。中心正方形 ROI：

$$
x_0=\left\lfloor\frac{W}{2}\right\rfloor-
\left\lfloor\frac{W_{roi}}{2}\right\rfloor
$$

$$
y_0=\left\lfloor\frac{H}{2}\right\rfloor-
\left\lfloor\frac{H_{roi}}{2}\right\rfloor
$$

有效点必须位于 200..1500 mm。统计量：

$$
validRatio=\frac{N_{valid}}{N_{ROI}}
$$

代表距离取中位数：

$$
d=median(\{d_i\})
$$

并计算均值、最小/最大、线性插值 P10/P90 和中位绝对偏差：

$$
MAD=median(|d_i-median(d)|)
$$

当前 ROI 为 1×1、最少有效点为 1，因此业务距离就是中心单元深度；其余统计量主要用于日志和后续扩展。RGB 与 TOF 尚无外参配准，目标未居中时中心 TOF 单元可能测到背景。

### 14.2 原子视觉/TOF 数据包

每 3 个 RGB 帧采集一次 TOF。该帧的视觉中轴和同一次 TOF 统计被放入同一 Python 字典，再同时用于日志、可选 UART 和 TCP hook，避免重复采集导致时间不同步。

TOF 时间戳取采集开始与结束的中点：

$$
t_{frame}=\frac{t_{start}+t_{end}}{2}
$$

### 14.3 AIM_STABLE

TCP 桥只对同时满足以下条件的原子数据包计数：

```text
center_valid
center_final
horizontal_valid
abs(cx - frame_width/2) <= 8 px
```

连续 3 个包成立后置 `AIM_STABLE`；任一包失败立即清零。当前 30 FPS、每 3 帧一个 TOF/VIS 包，名义上约需 0.3 s。

发送时 X 和距离四舍五入为整数；无效值分别编码为 `-1` 和 `0xFFFF`。

## 15. MaixCAM2 TCP 与预留 UART

### 15.1 当前 TCP 视觉桥

MaixCAM2 在 9000 端口只允许一个 TCP 客户端，socket 全部非阻塞。每个新视觉包用递增 16 位序号编码为 JX1/VIS。

发送队列上限 1024 字节：当前帧尚未发完时，只额外保留一份“最新下一帧”，新包覆盖旧的 next frame，避免视觉积压。未连接或队列无法容纳时丢弃并计数。

当前入口只解析并打印 STM32 的 ACK/TEL，不生成 CMD；因此 MaixCAM2 调试屏只是相机识别画面，不是炮控触摸 UI。

### 15.2 保留的 Maix 触摸遥测控制台

`gimbal_app.py` 可作为替代入口：显示 STM32 TEL/DBG、生成 CMD，并可在 TCP 9001 向 VOFA 输出 FireWater 遥测。它与当前视觉入口都监听 9000，不能同时启动；切换到它也会失去当前 `VisionTcpBridge` 的 VIS 发送，除非重新做入口组合。

### 15.3 预留 UART2 二进制帧

UART2 当前关闭。保留协议为固定 16 字节：

```text
AA 55 | version | type | payload_len | seq_u16 | frame_u16
      | x_i16 | distance_u16 | flags | CRC16_Modbus_u16
```

CRC 覆盖 version 到 flags，初值 `0xFFFF`、多项式 `0xA001`、低位优先。该预留协议没有 `AIM_STABLE` 位，且 STM32 当前没有接入这条链路。

## 16. 停止、取消和失败路径

| 事件 | 双轴控制 | 充电 PB1 | 发射 PB0 | 自动任务 |
|---|---|---|---|---|
| STOP | 停止 PID，各发一次 0 A，保持 Enable | 关闭/暂停 | 关闭 | 取消 |
| DISABLE | 停止 PID并发 Disable | 关闭/暂停 | 关闭 | 取消 |
| FIRE_ABORT | 控制器保持 | 不一定关闭；自动任务取消时会关闭 | 立即关闭 | 取消 |
| CHARGE_ABORT | 控制器保持 | 关闭/暂停 | 不变 | 状态机可继续，但超时冻结直到恢复 |
| 控制故障 | 停止两个 PID，尝试 0 A | 自动任务取消时关闭 | 关闭 | 取消 |
| 视觉丢失 | 位置 PID继续保持 | 当前统一计时可继续 | 30 s 待击发仍可能触发 | 自动搜索回 SEARCH |
| 自动任务超时 | 保持当前控制器状态 | 关闭/暂停 | 关闭 | 回 IDLE并报无效目标 |

注意：`auto_aim_cancel()` 只在任务原先 active 或待击发且 PB1 正在导通时关闭充电；暂停累计值不会自动清零。下一次正式任务会强制开启全新的充电周期并清零。

## 17. 当前已知边界与实际风险

1. 正式自动击发最终只以 30 s 软件累计为门槛，当前会旁路视觉、角度和稳定判定，见第 9.4 节。
2. 没有充满反馈、母线电压、电流、温度、弹丸在位或硬件急停输入；30 s 只是软件时间。
3. PB0/PB1 互锁是软件互锁。复位阶段 GPIO 在固件接管前仍可能是高阻，硬件侧应有可靠下拉和功率级默认关闭设计。
4. Pitch `CURRENT_POLARITY=+1`、±0.30 A 闭环和 ±90°边界仍需实机确认；编译成功不能证明机械方向正确。
5. RGB 与 TOF 未做外参配准，当前中心 1×1 TOF 只有目标居中时才可靠代表目标距离。
6. 当前视觉主要依赖现场 LAB 阈值、边缘和面积平台；照明、颜色、遮挡或背景矩形变化可能影响结果。
7. CAN 单次发送、邮箱满即失败；控制电流帧发送失败会进入控制故障，不自动无限重试。
8. JX1/TCP 没有认证或加密，网络内任何能连接该端口并构造合法 CRC 的节点都可能提交现有 CMD。
9. `SET_DISTANCE` 会更新后续 `FIRE_DISTANCE` 使用的设定距离；弱 `control_port_submit()` 仍是兼容/演示端口，接受不代表驱动新的硬件动作。
10. 本文排除了临时手动测试 UI；要恢复本文行为必须用 `CANNON_MANUAL_TEST_MODE=0U` 重新构建和烧录，并重新执行功率回路断开的台架验证。

## 18. 关键源码索引

| 逻辑 | 文件 |
|---|---|
| STM32 构建和入口 | `CMakeLists.txt`, `Src/lcd_demo_main.c` |
| 应用编排、任务、充电发射、自动搜索 | `Src/lcd_demo_app.c` |
| 全局参数 | `Inc/lcd_demo_config.h` |
| Yaw/Pitch PID | `Application/control/yaw_scan_controller.c`, `pitch_hold_controller.c` |
| 弹道 | `Application/control/cannon_aim_model.c` |
| QD4310 | `Device/qd4310/qd4310.c` |
| JX1 | `Application/protocol/gimbal_link_protocol.c`, `maixcam/gimbal_protocol.py` |
| ESP8266 | `Device/esp8266/esp8266.c` |
| 正式 TFT UI | `Application/ui/target_ui.c` |
| 触摸/LCD | `Device/xpt2046/xpt2046.c`, `Device/ili9341/ili9341.c` |
| Maix 当前入口 | `maixcam/main.py` |
| 视觉主循环 | `maixcam/box_face_app.py` |
| 四边形/竖边几何 | `maixcam/box_face_geometry.py` |
| TOF | `maixcam/tof100_service.py` |
| 视觉 TCP 桥 | `maixcam/vision_link.py` |
| Maix 参数 | `maixcam/vision_config.py` |
