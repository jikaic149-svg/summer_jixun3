# 调试过程记录

> 只记录关键测试、关键参数修改和关键结论，避免后续反复改错。普通解释、临时讨论、无效尝试不写入。

## 记录规则

- 发生参数修改时，记录：修改项、旧值、新值、原因。
- 发生测试时，记录：测试指令、VOFA通道、主要数据结论。
- 发生控制链路变化时，记录：当前使用的数据源、关闭/启用的模块。
- 不记录完整终端日志，不记录重复数据，只保留能指导下一步调参的信息。

## 2026-07-20 STM32F407VET6 LCD/触摸独立目标

- 范围：按 v5.5 原理图新增 2.8 寸 ILI9341 + XPT2046 横屏交互固件，并重建 Git/构建链。
- 用户最终决策：只从工作区移除 `task/` 与 `mode/`；`bsp/`、`Device/` 及其余旧文件保留。早期“清空全部旧业务”的方向已被否决。旧 `.git` 未恢复，当前仓库为新的 `main`。
- 架构：新增裸机 `stm32f407ve_lcd` 目标。BSP 持有 25 MHz HSE、FSMC、GPIO 软件 SPI、SysTick；Device 持有 ILI9341/XPT2046；`Application/ui` 不依赖 MCU、HAL、RTOS。旧 BSP/Device 不进入新目标链接。
- 执行：主循环末尾 `WFI`；SysTick 1 kHz。HSE/PLL 各 100 ms 有限超时；LCD 上电、软件复位、Sleep Out 分别用 10/10/120 ms 轮询状态机。触摸每 5 ms 采 5 组中值，按下/释放各连续 3 次才确认。
- 命令 Topic：`app_command_t` 8 槽静态 FIFO，单位为 cm/ms；满载立即返回，无自动重试。弱 `control_port_submit()`明确返回“控制器未接入”，不伪造执行机构反馈。
- UI：320×240 横屏，主界面含 50–100 cm 输入、距离发射、固定靶、自动搜索、往复扫描、停止、状态、方位角、仰角；数字键盘非法范围不提交。刷新采用控件 dirty mask，无整屏帧缓冲。
- 硬件：FSMC NE1 CMD=`0x60000000`、DATA=`0x60020000`；读 ADDSET/DATAST=15/60，写=3/8。PA15 背光高有效。XPT2046 为 PE0/PE2/PE3/PE4 + PD13 软件 SPI，PEN 低有效。P5 pin28 和 pin32 均为 NC。
- 修复：未沿用板厂示例的 FSMC `DATAST=0`、横屏 X 轴漏负号、触摸不足样本仍成功、无限校准等待、坐标夹到 width/height 和闭区间刷新等缺陷。
- 内存：无动态分配、无 CCM 数据。字形缓冲 512 B；链接器计入 4 KiB 栈预算。Release 使用 Flash 13,964 B、RAM 4,816 B（含栈预算）；Debug 使用 Flash 14,864 B、RAM 4,816 B。
- 构建验证：GNU Arm GCC 10.3.1 + CMake 3.30.2 + Ninja；Debug/Release 均生成 ELF/HEX/BIN，无编译警告。ELF 向量表位于 `0x08000000`，硬浮点 ABI 为 VFP registers；map 未链接旧 task/mode/gimbal/vision/FreeRTOS 符号。
- 测试验证：`python -m unittest tests\\lcd_ui_static_test.py -v` 共 8 项通过；主界面与距离键盘预览均为 320×240。
- 已知限制：尚未连接实物。ILI9341 ID、FSMC WR/RD 波形、PA15 背光极性、XPT2046 四角原始值、横屏轴向和无 TE 条件下的撕裂仍待上板确认；编译通过不等同于实物通过。
- 主要文件：`CMakeLists.txt`、`linker/STM32F407VETX_FLASH.ld`、`bsp/bsp_*f407ve*`、`bsp/bsp_lcd_fsmc.*`、`bsp/bsp_xpt2046_io.*`、`Device/ili9341/`、`Device/xpt2046/`、`Application/ui/`、`docs/hardware_mapping.md`。

### 实机烧录验证（2026-07-20）

- 用户决策：禁止下载/使用 pyOCD；该尝试已停止，最终只使用本机 `D:\\OPENOCD\\openocd-custom-20260615`。
- 探针：板载 CMSIS-DAP FW 1.6.0，SWD 1 MHz，序列号 `2024-09-01`。
- 芯片识别：CPUID=`0x410FC241`（Cortex-M4F），DBGMCU_IDCODE=`0x101F6413`（DEV_ID 0x413），Flash size=`0x0200`（512 KiB），与 STM32F407VET6 镜像边界一致。
- 烧录：Release ELF SHA-256=`957D4D97ED0D01C32532925714353E6A790772F2EE748CBF68A7F3038995F6AF`；OpenOCD 完成擦写、`Verified OK` 和复位运行。
- 运行验证：PC=`0x08000606`，解析到 `bsp_board_idle()` 的 `WFI`；CFSR/HFSR 均为 0。RCC 已切到 PLL；FSMC BCR1/BTR1/BWTR1 分别为 `0x5011/0x3C0F/0x0803`；PA15 为输出高电平，证明 UI 首帧路径已执行并打开背光。
- 验证边界：寄存器证据不能证明 LCD 玻璃实际接收并显示了每次写入。用户报告的“没有刷新”仍需区分白/黑屏与首帧静止；后者当前因控制端口未接入、方位/仰角数据恒定而不会自行变化。

### 触摸无响应修复（2026-07-20）

- 原因：用户确认界面可见但触摸不变化。旧实现把命令后的 16 位返回值右移 3 位后保留为 13 位，未显式处理 BUSY/null 拍；同时上电后在 PENIRQ 首次变低前不发送 `PD0=0` 控制字，存在 PENIRQ 尚未明确使能的启动闭环。
- 决策：按同板板厂时序实现至少约 1 us 软件时钟半周期，以及 `command -> BUSY/null 拍 -> 高/低各至少 5 us -> 12 位 ADC`，结果限定为 `0x000..0xFFF`；初始化先完成一次 `PD1:PD0=00` 转换；5 点采样批次只在入口确认 PEN，拒绝中值为全 0 或全 1 的总线故障值。未修改 5 ms 周期、3 次按下/释放去抖、UI API 或控制 Topic。
- 硬件证据：开发板和 2.8 寸屏两份原理图共同确认 U3=XPT2046，PE0/PE2/PE3/PE4/PD13 映射正确，PENIRQ/CS 均低有效；屏模块 PENIRQ 使用芯片内部约 50 kΩ 上拉，CS 有 10 kΩ 外部上拉。
- 涉及文件：`bsp/bsp_xpt2046_io.*`、`Device/xpt2046/xpt2046.c`、`tests/lcd_ui_static_test.py`、`docs/hardware_mapping.md`。
- 验证：Debug/Release 均无警告构建；Release Flash=14,072 B、RAM=4,816 B；反汇编确认命令末尾保留完整低半周期、软件 SPI 半周期循环 60 次、转换等待循环各 300 次且返回指令只提取 12 位；静态时序下限分别超过 1 us 和 5 us，精确值待逻辑分析仪确认。静态测试 9 项通过。最终 Release ELF SHA-256=`4991584D91168DC9023FC0DDA376ADA2A4DF7964F25378CAB3A26F3C69AC03E3`，OpenOCD 烧录、校验和复位均成功。
- 板上边界：运行 PC 位于 `bsp_board_idle()` 的 WFI，CFSR/HFSR 为 0。两轮定时采样均未捕获 PE4 低电平，`s_touch` 也未留下原始坐标；采样窗口内的人工按压动作尚未由用户回报确认，因此不能把实际触摸交互写成通过。若用触控笔明确按压后 PE4 仍为高，应检查屏模块、P5 接触和 PENIRQ 实物通路，不再改坐标标定掩盖硬件故障。

## 2026-07-05 速度闭环测试基线

### 控制链路

- 当前测试目标：只调左右轮速度闭环，不上直立环。
- 当前速度反馈源：只使用 QD 电机 CAN 反馈速度 `speed_can_rpm`。
- 已删除 `encoder_rpm` 差分速度链路，避免把 byte6-7 位置字段误当速度。
- `BALANCE_PID_ENABLE = 0U`，按 KEY 后进入 `CHASSIS_MOTOR_SPEED_TEST`。
- `WHEEL_SPEED_TEST_ENABLE = 1U`。

### VOFA通道

当前 `VOFA_DEBUG_STAGE = VOFA_DEBUG_STAGE_SPEED_LOOP`，只输出 6 路：

| 通道 | 名称 |
|---|---|
| I0 | `left_ref_rpm` |
| I1 | `left_can_rpm` |
| I2 | `left_cmd_a` |
| I3 | `right_ref_rpm` |
| I4 | `right_can_rpm` |
| I5 | `right_cmd_a` |

### 初始基线速度环参数

| 参数 | 当前值 | 说明 |
|---|---:|---|
| `WHEEL_SPEED_TEST_REF_RPM` | `0.0f` | KEY 解锁后默认不转 |
| `WHEEL_SPEED_TEST_REF_MAX_ABS_RPM` | `200.0f` | 当前速度测试最大指令 |
| `WHEEL_SPEED_TEST_CURRENT_LIMIT_A` | `0.35f` | 当前测试限流 |
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00150f` | 速度环 P |
| `WHEEL_SPEED_PID_KI_A_PER_RPM_S` | `0.001000f` | 速度环 I |
| `WHEEL_SPEED_FF_STATIC_A` | `0.015f` | 静摩擦/低速前馈 |
| `WHEEL_SPEED_FF_KV_A_PER_RPM` | `0.00015f` | 转速前馈 |
| `LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | 左轮反馈滤波 |
| `RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | 右轮反馈滤波 |

### 最近一次数据结论

- `S50/S100/S150/S200` 下速度闭环能跟随目标，说明 CAN 速度反馈和电流控制链路可用。
- 旧参数下高转速实际速度偏高，说明高速前馈偏大。
- 右轮阶跃超调明显，原因包括右轮原先反馈滤波更慢，以及手持悬空时负载不均。
- 电流指令远小于 `0.35A` 限幅，当前不是电流不够的问题。

### 下一步测试

1. 烧录当前参数。
2. 悬空测试 `S50 -> S100 -> S150 -> S200 -> S0`。
3. 重点观察：
   - `left_can_rpm/right_can_rpm` 是否贴近目标。
   - `left_cmd_a/right_cmd_a` 是否平滑、是否接近限幅。
   - 阶跃瞬间是否仍有明显右轮超调。
4. 若 `S200` 响应仍偏慢，再小幅增加 `WHEEL_SPEED_FF_KV_A_PER_RPM`。
5. 若 `S200` 稳态仍偏高或波动变大，则降低 `WHEEL_SPEED_FF_KV_A_PER_RPM` 或降低速度环 P。

## 2026-07-05 速度环 P 微调

### 测试结论

- 新前馈参数下高速响应保留，但 `S150/S200` 稳态仍有小幅来回修正。
- `S200` 稳态均值接近目标，但左轮峰峰值约 `36 rpm`，右轮峰峰值约 `23 rpm`。
- 电流指令平均仍只有约 `0.04A`，没有接近 `0.35A` 限幅。
- 判断：主要不是电流不足，而是速度环 P 略硬，导致 CAN 速度波动被放大成电流修正。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00150f` | `0.00120f` | 小幅降低 P，减少高转速来回修正，同时保留当前前馈响应 |

### 下一步测试

- 继续测试 `S50/S100/S150/S200/S-50/S-100/S0`。
- 若响应明显变慢但波动改善，可小幅提高前馈 `WHEEL_SPEED_FF_KV_A_PER_RPM`，不要先加 P。
- 若仍然来回抖，再考虑继续小幅降低 P 或降低反馈滤波 alpha。

## 2026-07-05 速度环频率判断

### 测试结论

- 当前 STM32 外部速度 PID 按 `WHEEL_SAFE_TASK_PERIOD_MS = 1U` 运行，即约 `1000Hz`。
- 当前 VOFA 按 `VOFA_OUTPUT_PERIOD_MS = 20U` 输出，即约 `50Hz`，VOFA 只用于观察，不能代表控制频率。
- 本次 `S100/S150/S200` 数据均值能跟随目标，电流指令约 `0.03~0.05A`，远低于 `0.35A` 限幅。
- `S100` 稳态：左轮均值约 `98.93rpm`，右轮约 `97.27rpm`；`S150` 稳态：左轮约 `150.55rpm`，右轮约 `149.16rpm`；`S200` 稳态：左轮约 `202.44rpm`，右轮约 `201.47rpm`。
- 主要问题不是外部 PID 频率低，而是 CAN 速度反馈点有抖动/估计噪声，速度环 P 偏硬时会追着反馈噪声修正电流。

### 下一步判断

- 若怀疑 QD 反馈刷新率不足，临时切到 `VOFA_DEBUG_STAGE_MOTOR_FEEDBACK`，观察左右轮 `rx_count` 每 `20ms` 增量。
- 若每行 `rx_count` 增量明显低于预期，再判断为反馈刷新率问题；否则继续按参数问题处理。
- 参数调整优先级：先小幅降 `WHEEL_SPEED_PID_KP_A_PER_RPM` 或降低反馈 LPF alpha；若响应变慢，再小幅补 `WHEEL_SPEED_FF_KV_A_PER_RPM`，不要直接加 P。

## 2026-07-05 速度环保持响应的抖动处理

### 调整原则

- 平衡车最终需要快速轮速响应，不继续加重反馈滤波。
- 当前抖动不优先靠降低响应速度解决，而是保留偏硬速度环，同时压掉很小的 CAN 速度量化/估计噪声。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | `1.0f` | 关闭左轮速度反馈时间滤波，减少相位滞后 |
| `RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | `1.0f` | 关闭右轮速度反馈时间滤波，减少相位滞后 |
| `WHEEL_SPEED_PID_ERROR_DEADBAND_RPM` | 无 | `2.0f` | 只对 P 项加入小误差软死区，抑制小幅速度噪声造成的电流抖动，不降低大误差响应 |

### 下一步测试

- 重新烧录后测试 `S50/S100/S150/S200/S0`。
- 重点比较：阶跃响应是否更快、稳态电流是否更碎、速度峰峰值是否下降。
- 若速度响应变快但电流明显抖动，先把 `WHEEL_SPEED_PID_ERROR_DEADBAND_RPM` 从 `2.0f` 调到 `3.0f`，不要重新加重 LPF。

## 2026-07-05 速度环软感排查

### 测试结论

- 关闭 LPF 后响应链路没有时间滤波，但 `2rpm` 的 P 项软死区会降低目标附近的小误差刚度。
- 例如 `5rpm` 误差在 `Kp = 0.00120A/rpm` 下，原本 P 输出约 `0.006A`；加入 `2rpm` 软死区后只剩约 `0.0036A`，目标附近等效 P 降低约 `40%`。
- 本次数据中 `S150` 左轮均值约 `146.79rpm`，偏低约 `3.21rpm`；`S200` 均值能到，但峰峰值仍约 `30rpm`，说明死区没有有效解决大幅抖动，反而可能带来软感。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_ERROR_DEADBAND_RPM` | `2.0f` | `0.0f` | 先关闭 P 项软死区，验证软感是否由死区造成；继续保持速度反馈无 LPF |

### 下一步测试

- 重新烧录后复测 `S100/S150/S200/S0`。
- 若软感消失但抖动仍大，说明死区不是合适方案；下一步在无 LPF、无死区基础上考虑小幅调 `Kp` 或排查 QD CAN 反馈速度刷新/估计抖动。

## 2026-07-05 速度反馈显示与轻滤波

### 测试结论

- 无 LPF、无死区后，`S150/S200` 均值基本能到目标，但 raw CAN 速度峰峰值仍约 `25~31rpm`，VOFA 曲线明显抖动。
- 这不是 VOFA 串口慢导致，而是 QD CAN 回传速度本身有较大的瞬时估计抖动。
- 速度环不能长期直接追 raw CAN，否则 P 环会追噪声；但滤波也不能过重，避免直立控制响应变慢。

### 参数/显示修改

| 修改项 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `1.0f` | `0.50f` | 加回很轻的一阶反馈滤波，约毫秒级延迟，降低 raw CAN 抖动 |
| `RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `1.0f` | `0.50f` | 同上 |
| VOFA速度通道 | raw `speed_can_rpm` | `wheel_speed_actual_*_rpm` | VOFA显示 PID 实际使用的速度，raw CAN 仍可切到 `MOTOR_FEEDBACK` 诊断 |

### 下一步测试

- 复测 `S100/S150/S200/S0`。
- 观察新 VOFA 表头：`left_ref_rpm,left_actual_rpm,left_cmd_a,right_ref_rpm,right_actual_rpm,right_cmd_a`。
- 若曲线仍抖但响应可以接受，再把 alpha 从 `0.50f` 降到 `0.40f`；若响应明显变软，再升回 `0.60f`。

## 2026-07-05 回到硬速度环轻滤波版本

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.50f` | `0.25f` | 回到之前更稳的轻滤波版本，兼顾硬度与可观察性 |
| `RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.50f` | `0.25f` | 同上 |

### 当前速度环基准

- `WHEEL_SPEED_PID_KP_A_PER_RPM = 0.00120f`，保持当前硬度。
- `WHEEL_SPEED_PID_ERROR_DEADBAND_RPM = 0.0f`，不使用死区，避免目标附近发软。
- `WHEEL_SPEED_FF_STATIC_A = 0.015f`，`WHEEL_SPEED_FF_KV_A_PER_RPM = 0.00015f`，保持当前前馈响应。
- VOFA 速度通道继续输出 `wheel_speed_actual_*_rpm`，即速度环实际使用的滤波后速度。

## 2026-07-05 进入 IMU 调试

### 阶段判断

- 当前轮速环平均误差与响应速度可以接受，先进入 IMU/陀螺仪调试。
- 直立环仍保持关闭：`BALANCE_PID_ENABLE = 0U`，本阶段不让车尝试直立。
- 调 IMU 时不要按 KEY，避免进入轮速测试导致电机输出。

### 配置修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_SPEED_LOOP` | `VOFA_DEBUG_STAGE_IMU_ATTITUDE` | VOFA 改为输出 Kalman 角度、陀螺仪角速度、加速度角和三轴 IMU 数据 |

### VOFA通道

| 通道 | 名称 |
|---|---|
| I0 | `kalman_theta_deg` |
| I1 | `gyro_theta_rate_dps` |
| I2 | `accel_theta_deg` |
| I3 | `accel_x_ms2` |
| I4 | `accel_y_ms2` |
| I5 | `accel_z_ms2` |
| I6 | `gyro_x_dps` |
| I7 | `gyro_y_dps` |
| I8 | `gyro_z_dps` |
| I9 | `yaw_deg` |
| I10 | `yaw_rate_dps` |
| I11 | `fault` |

### 下一步测试

- 静止放平：看 `gyro_theta_rate_dps` 是否接近 `0dps`，`kalman_theta_deg` 与 `accel_theta_deg` 是否接近。
- 手动前倾/后仰：确认 `kalman_theta_deg` 和 `gyro_theta_rate_dps` 方向是否符合直立环极性。
- 原地绕竖直方向转动：确认 `yaw_rate_dps` 方向和零漂。

## 2026-07-05 Yaw 零漂处理

### 测试结论

- 静止数据中 `yaw_deg` 近似线性漂移，说明 `yaw_rate_dps` 存在近似常值零偏后被积分。
- 当前代码中 `gyro_bias[3]` 原本只是全 0 数组，没有真正完成零偏估计。
- 平衡车开机时不一定静止，也不一定处在可控姿态，因此不采用开机自动采样零偏。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.085f` | `0.122f` | 根据本次静止数据中 `gyro_z_dps` 均值补偿 yaw 零偏 |
| `CHASSIS_YAW_RATE_DEADBAND_DPS` | 无 | `0.15f` | 抑制残余极小 yaw 角速度被持续积分成线性漂移 |
| `IMU_GYRO_BIAS_CAL_ENABLE` | 无/临时 `1U` | `0U` | 不做开机自动采样，避免车体开机晃动时把真实运动当零偏 |

### 下一步测试

- 烧录后静止观察 `yaw_rate_dps` 是否围绕 `0`，`yaw_deg` 是否停止明显单向漂移。
- 若静止时仍线性漂移，根据新数据微调 `CHASSIS_YAW_RATE_BIAS_DPS`；不要启用开机自动采样。

## 2026-07-05 KEY 两段式 Yaw 校准

### 控制流程

- 不做开机自动零偏采样，因为平衡车开机时可能倒地、晃动或被手拿着。
- 第一次按 KEY：只启动 yaw 轴零偏校准，电机保持急停，不进入轮速/直立控制。
- 校准期间要求车体倒地或放置静止；校准只使用 yaw 轴角速度均值，不修改 pitch 角度、不修改 pitch 陀螺仪零点。
- 第二次按 KEY：只有在 `yaw_cal_state = 2` 后才允许解锁当前控制模式。

### 灯光提示

| 状态 | 灯光 |
|---|---|
| 未校准，安全等待 | 蓝灯常亮 |
| yaw 校准中 | 蓝灯闪烁 |
| yaw 校准完成，等待第二次 KEY | 蓝灯+绿灯常亮 |
| 控制已解锁 | 绿灯常亮 |
| 电机反馈/使能故障 | 红灯常亮 |

### 参数/接口修改

| 修改项 | 当前值 | 说明 |
|---|---:|---|
| `IMU_YAW_BIAS_KEY_CAL_SAMPLES` | `500U` | 第一次 KEY 后采样 yaw 角速度均值 |
| `IMU_GYRO_BIAS_CAL_ENABLE` | `0U` | 继续禁用开机自动三轴陀螺仪标定 |
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.122f` | 手动校准前的备用 yaw 零偏 |
| `CHASSIS_YAW_RATE_DEADBAND_DPS` | `0.15f` | 抑制残余小零漂积分 |
| VOFA新增 | `yaw_cal_state` | `0=未校准`，`1=校准中`，`2=校准完成` |

## 2026-07-05 取消 KEY Yaw 校准，回到固定补偿

### 测试结论

- KEY 后 yaw 均值校准对静止要求太高，校准窗口内轻微晃动会把真实转动拟合进零偏，效果可能比固定补偿更差。
- 两组静止数据中 `gyro_z_dps` 均值约为 `0.1224dps` 和 `0.1253dps`，加权后约 `0.124dps`。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `IMU_YAW_BIAS_KEY_CAL_ENABLE` | `1U` | `0U` | 取消 KEY yaw 拟合校准，避免操作晃动污染零偏 |
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.125f` | `0.124f` | 使用两组静止数据加权均值作为固定 yaw 零偏 |
| `CHASSIS_YAW_RATE_DEADBAND_DPS` | `0.15f` | `0.0f` | 先关闭死区，保留线性漂移特征，便于继续精调固定值 |

### 当前流程

- KEY 恢复为直接解锁/急停切换，不再执行 yaw 校准。
- `yaw_cal_state` 固定为 `2`，仅表示当前不需要 KEY 校准。
- 若后续仍有线性漂移，只微调 `CHASSIS_YAW_RATE_BIAS_DPS`。

## 2026-07-05 Yaw 直线拟合

### 测试结论

- 本次静止数据 `yaw_deg` 线性拟合结果：`yaw = 0.265276 * t + 1.716281`，假设 VOFA 行间隔为 `20ms`，`R^2 = 0.99830`。
- 等价为每行约增加 `0.00531deg`，线性漂移非常明显。
- 同一段数据中 `gyro_z_dps` 均值约 `0.13456dps`，`yaw_rate_dps` 均值约 `0.01056dps`。
- 当前 `CHASSIS_YAW_RATE_BIAS_DPS = 0.124f`，从 `gyro_z_dps` 均值看，下一版固定 yaw 零偏应优先试 `0.135f` 附近。
- 注意：`yaw_deg` 拟合斜率与 `yaw_rate_dps` 均值不一致，不能直接用 `yaw` 斜率反推到 `0.389dps` 这种明显异常的 bias；后续需要确认 VOFA 数据是否等间隔，以及 yaw 积分使用的 `imu_dt_sec` 是否正确。

## 2026-07-05 Yaw 漂移二次拟合与 dt 修正

### 测试结论

- 最新静止数据共 `221` 行；若按 VOFA 目标输出周期 `20ms` 计算，总时长约 `4.4s`。
- `yaw_deg` 直线拟合：`yaw = 0.329520 * t + 0.453970`，`R^2 = 0.999595`，说明漂移仍然高度线性。
- 同一段数据中 `gyro_z_dps` 均值为 `0.163679dps`，当前固定补偿为 `0.124f`，所以固定 yaw 零偏先改为 `0.164f`。
- `yaw_rate_dps` 均值只有 `0.039679dps`，但 `yaw_deg` 按 20ms 反推斜率是 `0.329520dps`，两者相差约 `8.3` 倍；这说明问题不只是 bias，IMU 积分使用的 `imu_dt_sec` 也需要核对。

### 修改内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.124f` | `0.164f` | 按最新静止数据 `gyro_z_dps` 均值补偿 yaw 零偏 |
| `imu_dt_sec` | DMA 用固定 `BALANCE_IMU_DT_SEC`，fallback 用固定 `IMU_POLL_FALLBACK_DT_SEC` | 每帧 IMU 数据使用 `xTaskGetTickCount()` 计算实际间隔，并限幅到 `0.5ms~20ms` | 避免 yaw 角积分周期和实际任务周期不一致 |
| VOFA IMU 通道 | 13 列 | 14 列，新增 `imu_dt_ms` | 下一次直接确认真实 IMU 积分周期 |

### 下一步测试

- 烧录后静止采集 yaw 数据，重点看最后一列 `imu_dt_ms` 是否稳定。
- 若 `gyro_z_dps` 均值接近 `0.164` 且 `yaw_rate_dps` 围绕 `0`，但 `yaw_deg` 仍漂，则继续检查 `imu_dt_ms` 和 yaw 积分路径。

## 2026-07-05 为什么 yaw 零漂比 pitch 难

### 问题总结

- Pitch 可以用加速度计里的重力方向修正，所以即使陀螺仪有一点零漂，Kalman/互补滤波也能慢慢拉回来。
- Yaw 没有重力参考；当前没有磁力计、视觉定位或外部航向参考，所以 yaw 只能靠 `gyro_z_dps` 积分。只要 `gyro_z_dps` 有很小零偏，`yaw_deg` 就会一直线性漂。
- yaw 零偏很小，通常只有 `0.1~0.2 dps` 量级；但积分 10 秒就是 `1~2 deg`，所以看起来漂得很明显。
- VOFA 输出是降采样观察值，不是 IMU 每 1ms 的完整积分过程。`yaw_deg` 是内部连续积分结果，`yaw_rate_dps` 是被 VOFA 抽样出来的瞬时值；直接拿两者按行号强行拟合，可能得出互相矛盾的结论。
- 因此 yaw 零漂调试不能只看一列 `yaw_deg` 的斜率，还要同时看 `gyro_z_dps` 均值、`yaw_rate_dps` 均值、IMU 积分周期和 VOFA 实际输出周期。

### 当前判断

- 公式本身是清楚的：`yaw_rate_dps = gyro_z_dps - CHASSIS_YAW_RATE_BIAS_DPS`，`yaw_deg += yaw_rate_dps * dt`。
- 最新数据里 `gyro_z_dps` 和 `yaw_rate_dps` 的关系符合当前 bias，但 `yaw_deg` 的行间变化仍与抽样到的 `yaw_rate_dps` 不完全一致。
- 下一步不能继续盲改 bias，需要先让 VOFA 输出真实时间信息，例如 `imu_time_sec` 或 `vofa_period_ms`，再判断到底是 bias 问题、输出降采样问题，还是积分路径问题。

## 2026-07-05 定位到 dt 积分根因并改用 DWT 微秒时钟

### 根因结论

- `yaw_deg` 斜率远大于抽样 `yaw_rate_dps` 的本质是**积分 dt 被系统性放大**，不是单纯 bias 问题。
- 陀螺仪 ODR 配置为 `BMI088_GYRO_1000_116_HZ`，即 DRDY ~1000Hz；而原 `imu_dt_sec` 用 `xTaskGetTickCount()` 计算，tick 只有 1ms 分辨率。
- 陀螺仪 1000Hz 与 SysTick 1000Hz 是两个独立时钟，会拍频：同一个 1ms tick 内常落入多帧陀螺仪数据。此时 `dt_ticks==0`，旧代码跳过 dt 更新却**仍然积分**，并把 `imu_dt_sec` 兜底成 `BALANCE_IMU_DT_SEC = 1ms`。
- 结果：每帧都按 ~1ms 积分，但真实间隔并没有 1ms，`yaw_deg` 被过积分放大，而 VOFA 每 20ms 抽样出的 `yaw_rate_dps` 仍是真实瞬时值 —— 两者对不上正是这个 bug 的指纹。
- Pitch 不受影响，因为有加速度计重力修正把误差拉回；yaw 无绝对参考，dt 误差会一直累积。

### 修改内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---|---|---|
| dt 时钟源 | `xTaskGetTickCount()` 整毫秒差值 | 新增 `BSP_DWT_Init()`/`BSP_GetMicros()`，用 DWT->CYCCNT(168MHz) 微秒时间戳算每帧真实间隔 | 保证「积分 dt 之和 == 真实流逝时间」，消除 tick 量化过积分 |
| 首帧 dt | `last_imu_update_tick==0` 判首帧 | `imu_dt_ready` 标志，首帧只播种时间戳不积分 | 避免首帧用巨大/无效 dt |
| dt 下限 | `< 0.5ms` 兜底 1ms | `< 0.2ms` 兜底 `BALANCE_IMU_DT_SEC` | 微秒时钟下真实 dt 约 1ms，仅极端异常才兜底 |
| yaw 速率滤波 | 工作副本被改成 `yaw_rate_kalman = yaw_rate`（无滤波） | 恢复 `KalmanScalar_Update`，并先过死区再滤波 | 压掉抽样噪声与残余零偏积分 |
| `CHASSIS_YAW_RATE_DEADBAND_DPS` | `0.0f` | `0.05f` | 抑制 bias 补偿后残余极小 yaw 角速度被持续积分成线性漂移 |
| `BSP_DWT_Init()` 调用 | 无 | `INS_Init()` 最前面调用一次 | 调度器启动前使能周期计数器 |

### 下一步测试

- 烧录后静止采集 IMU 数据，重点看最后一列 `imu_dt_ms`：现在应稳定在 ~1.0ms 附近（真实微秒 dt），不再是恒定兜底值。
- 观察 `yaw_deg` 是否停止明显单向线性漂移；此时按 20ms 行间隔反推的斜率应与 `yaw_rate_dps` 均值基本一致（不再有 ~8 倍偏差）。
- 若 `imu_dt_ms` 稳定、斜率与 rate 一致但仍有缓慢漂移，则纯属残余固定零偏，只微调 `CHASSIS_YAW_RATE_BIAS_DPS`（当前 `0.164f`）。
- 若静止时 `yaw_rate_dps` 已围绕 0 但偶发跳变被积分，再把 `CHASSIS_YAW_RATE_DEADBAND_DPS` 从 `0.05f` 适当上调。

## 2026-07-06 steps_per_vofa 定位真凶：SysTick 慢 8 倍，不是积分错

### 测试结论（1.txt，新增第15列 steps_per_vofa）

- `steps_per_vofa` 稳定为 **159~160**：每个"20ms" VOFA 周期内实际发生了 160 次积分步。
- `imu_dt_ms`（DWT 真实测量）稳定 ~1.003ms：陀螺仪确实按 1kHz 出数据，每步积分 dt 也是真的 1ms。
- 160 步 × 1ms = **每行真实时长 160ms**，不是 20ms。此前所有"按 20ms/行"的拟合把时间轴压缩了 8 倍，才出现"斜率是 rate 8 倍"的假象。
- 用真实行距 160ms 重新拟合：`yaw_deg` 斜率 = **-0.0366 dps**，与 `yaw_rate_dps` 均值 **-0.0290** 之比 = **1.26 ≈ 1**。
- **结论：yaw 积分路径从头到尾是对的。真正的 bug 是 FreeRTOS 的 tick 走慢了 8 倍**：`pdMS_TO_TICKS(20)` 的"20ms"实际是 160ms。

### SysTick 8 倍慢的根因

- `FreeRTOSConfig.h` 里显式定义了 `configSYSTICK_CLOCK_HZ = 168000000UL`。
- port.c 的规则：**只要该宏被显式定义**（哪怕值等于 CPU 频率），就认为 SysTick 用外部参考时钟，把 `portNVIC_SYSTICK_CLK_BIT` 置 0 → SysTick CLKSOURCE 位清零 → SysTick 实际走 **HCLK/8 = 21MHz**。
- 但重装载值仍按 168MHz/1000 计算 → 每个"1ms" tick 实际 **8ms**。
- 影响全系统：轮速 PID "1kHz" 实际 125Hz、VOFA "20ms" 实际 160ms、所有 vTaskDelay 都慢 8 倍。之前速度环数据的时间轴同样被压缩过。

### 修改内容

| 修改项 | 旧值 | 新值 | 原因 |
|---|---|---|---|
| `configSYSTICK_CLOCK_HZ` | 显式定义 168000000UL | **删除该定义**（加注释警告） | 让 port.c 默认置位 CLKSOURCE，SysTick 真跑 168MHz，tick 恢复真 1ms |
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.164f` | `0.135f` | 本次数据残余 yaw_rate 均值 -0.029，0.164-0.029=0.135；与 gyro_z 均值 0.1287 一致 |

### 下一步测试

- 烧录后静止采集：`steps_per_vofa` 应从 160 变为 **~20**（tick 修好后 20ms 真的是 20ms）。
- `yaw_deg` 按 20ms/行拟合的斜率应与 `yaw_rate_dps` 均值一致（比值 ~1）。
- 残余漂移应降到 |0.01 dps| 量级；若仍有固定方向小漂移，微调 bias（步长 0.005f）。
- 注意：tick 修复后轮速 PID 真实频率从 125Hz 提高到 1kHz，等效积分增益变化 8 倍，速度环若行为变化属预期，需按新时间基准复测。

## 2026-07-06 VOFA 只保留 Yaw 调试通道

### 测试结论

- 最新静止数据共 `136` 行、`15` 列。
- `steps_per_vofa` 为 `19~20`，均值约 `19.96`，说明 SysTick 修复后 VOFA 的 `20ms` 周期已经恢复正常。
- `imu_dt_ms` 均值约 `1.017ms`，IMU 积分周期正常。
- `gyro_z_dps` 均值约 `0.138dps`，当前 `CHASSIS_YAW_RATE_BIAS_DPS = 0.135f` 接近静止零偏。
- `yaw_rate_dps` 均值约 `-0.0157dps`，`yaw_deg` 在约 `2.715s` 内变化 `-0.061deg`，等效漂移约 `-0.0225dps`。

### 修改内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `VOFA_OUTPUT_CHANNEL_COUNT` (`IMU_ATTITUDE`) | `15U` | `5U` | 当前阶段只看 yaw 相关量，减少 VOFA 曲线干扰 |
| VOFA IMU 通道 | `kalman_theta_deg,gyro_theta_rate_dps,accel_theta_deg,accel_x_ms2,accel_y_ms2,accel_z_ms2,gyro_x_dps,gyro_y_dps,gyro_z_dps,yaw_deg,yaw_rate_dps,fault,yaw_cal_state,imu_dt_ms,steps_per_vofa` | `gyro_z_dps,yaw_deg,yaw_rate_dps,imu_dt_ms,steps_per_vofa` | 保留 raw yaw gyro、yaw 积分角、滤波后 yaw rate 和时间基准诊断 |

### 下一步测试

- 烧录后 VOFA 表头应变为 `gyro_z_dps,yaw_deg,yaw_rate_dps,imu_dt_ms,steps_per_vofa`。
- 先采集一段更长的 yaw-only 静止数据，再比较 `yaw_rate_dps` 均值和 `yaw_deg` 斜率；本次先不继续改 `CHASSIS_YAW_RATE_BIAS_DPS`。

## 2026-07-06 Yaw 角度归一化与最短路径

### 问题判断

- 旧接口 `INS_GetYaw()` 返回的是 `attitude_algorithm` 里的 `Yaw_a`，该值来自没有磁力计约束的旧欧拉角链路，不是当前正在调试的 yaw 积分链路，因此启动后可能直接显示约 `90deg`。
- 当前可信 yaw 链路是 `gyro_z_dps -> CHASSIS_YAW_RATE_BIAS_DPS -> deadband -> Kalman -> 积分`。

### 修改内容

| 修改项 | 旧逻辑 | 新逻辑 | 目的 |
|---|---|---|---|
| `global_chassis_yaw` | 直接使用连续积分角 `yaw_kalman_angle` | 使用 `wrap_angle_360_deg(yaw_kalman_angle)`，范围 `[0,360)` | 对外 yaw 不再超过 360，也不会启动继承旧 90deg |
| VOFA `yaw_deg` | 输出连续积分角 | 输出归一化后的 `global_chassis_yaw` | 曲线/读数统一在 `0~360` |
| `INS_GetYaw()` | 返回旧 `Yaw_a` | 返回 `global_chassis_yaw` | 避免继续使用旧姿态链路的假 yaw |
| `INS_GetChassisYawError(target)` | 无 | 返回 `target-current` 的 `[-180,180)` 最短路径误差 | 后续 yaw 角度闭环避免从 359deg 绕远路转到 1deg |

### 下一步测试

- 烧录后静止开机：VOFA `yaw_deg` 应从接近 `0deg` 或 `360deg` 附近开始，而不是直接 `90deg`。
- 手动转过 `0/360` 边界时，后续控制要使用 `INS_GetChassisYawError(target)` 做角度误差，不要直接 `target - current`。

## 2026-07-06 长时间 Yaw 零漂修正

### 测试结论

- `vofa+.csv` 共 `10955` 行有效 yaw-only 数据，时长约 `218.8s`。
- `steps_per_vofa` 均值约 `19.97`，`imu_dt_ms` 均值约 `1.022ms`，时间基准正常。
- 全段展开 yaw 从 `0.007deg` 到 `2.279deg`，线性拟合漂移约 `+0.0101dps`。
- 后半段漂移略大：最近 `99.9s` 拟合约 `+0.0144dps`，最近 `20.0s` 拟合约 `+0.0173dps`。
- `gyro_z_dps` 全段均值约 `0.1474dps`，后半段均值约 `0.1502dps`，当前 `CHASSIS_YAW_RATE_BIAS_DPS = 0.135f` 偏小。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.135f` | `0.150f` | 按长时间静止数据的 `gyro_z_dps` 后半段均值修正固定 yaw 零偏，抵消约 `+0.01~0.015dps` 的正向漂移 |

### 下一步测试

- 烧录后再采集 `120s~300s` 静止 yaw-only 数据。
- 目标：展开后的 `yaw_deg` 拟合斜率进入 `|0.005dps|` 以内。
- 若改后变成稳定负漂，再按 `0.002~0.005f` 步长回调 `CHASSIS_YAW_RATE_BIAS_DPS`。

## 2026-07-06 Yaw bias 过补回调

### 测试结论

- 新 `vofa+.csv` 共 `4695` 行有效 yaw-only 数据，时长约 `93.9s`。
- `steps_per_vofa` 均值约 `19.99`，`imu_dt_ms` 均值约 `1.035ms`，时间基准正常。
- 当前 `CHASSIS_YAW_RATE_BIAS_DPS = 0.150f` 时，全段 yaw 从 `359.778deg` 漂到 `358.121deg`，展开后约 `-1.657deg`。
- 全段拟合漂移约 `-0.0202dps`；最近 `60.0s` 拟合约 `-0.0136dps`；最近 `30.0s` 拟合约 `-0.0085dps`。
- 说明 `0.150f` 相比上一版 `0.135f` 已从正漂改成负漂，固定 bias 略微过补。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.150f` | `0.143f` | 取 `0.135f` 正漂与 `0.150f` 负漂之间的折中值，优先压低末段稳定负漂 |

### 下一步测试

- 烧录后再采集 `120s~300s` 静止 yaw-only 数据。
- 若仍负漂，继续小幅降到 `0.140f`；若回到正漂，再小幅升到 `0.145f`。

## 2026-07-06 Yaw bias 二次回调

### 测试结论

- 新 `vofa+.csv` 共 `4411` 行有效 yaw-only 数据，时长约 `88.2s`。
- 前 `20s` 出现约 `17.6deg` 变化，判断不是稳定静止段，不用于零漂精调。
- 最近 `60.0s` 展开 yaw 从约 `-0.859deg` 到 `-1.703deg`，拟合漂移约 `-0.0145dps`。
- 最近 `30.0s` 展开 yaw 从约 `-1.412deg` 到 `-1.703deg`，拟合漂移约 `-0.0097dps`。
- 时间基准正常：`steps_per_vofa` 均值约 `19.99`，`imu_dt_ms` 均值约 `1.03ms`。
- 结论：`CHASSIS_YAW_RATE_BIAS_DPS = 0.143f` 仍略高，导致稳定段轻微负漂。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.143f` | `0.138f` | 继续小幅降低固定 yaw 零偏，目标把末段 `-0.01dps` 左右负漂拉回零附近 |

### 下一步测试

- 烧录后采集时尽量保证全程静止，避免开头手动转动混入零漂数据。
- 继续以最近 `60s~120s` 稳定段的展开 yaw 拟合斜率为准；目标仍是 `|0.005dps|` 以内。

## 2026-07-06 Yaw bias 接近零漂微调

### 测试结论

- 新 `vofa+.csv` 共 `6920` 行有效 yaw-only 数据，时长约 `138.3s`。
- `CHASSIS_YAW_RATE_BIAS_DPS = 0.138f` 时，全段展开 yaw 约 `0.084deg -> -1.619deg`，拟合漂移约 `-0.0119dps`。
- 最近 `100s` 拟合漂移约 `-0.0083dps`。
- 最近 `60s` 拟合漂移约 `-0.0061dps`，最近 `30s` 拟合也约 `-0.0061dps`。
- 时间基准正常：`steps_per_vofa` 均值约 `19.98`，`imu_dt_ms` 均值约 `1.03ms`。
- 结论：已接近目标，但稳定段仍有轻微负漂，说明固定 bias 仍略高。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `CHASSIS_YAW_RATE_BIAS_DPS` | `0.138f` | `0.136f` | 小幅降低固定 yaw 零偏，目标把最近稳定段约 `-0.006dps` 的负漂压进 `|0.005dps|` 以内 |

### 下一步测试

- 烧录后再采 `120s~300s` 静止数据。
- 若最近 `60s~120s` 稳定段已经在 `|0.005dps|` 以内，停止继续微调，避免追温漂和噪声。

## 2026-07-06 暂停 Yaw 零漂追参，切到 Pitch 调试

### 判断

- Yaw 固定零偏在 `0.135~0.150dps` 间随采样条件和温度变化，继续手动追固定值会追到温漂和静止条件差异。
- 当前先保留最近参数，不继续调整 yaw bias；后续若要进一步优化，应做静止门控在线 bias 修正，而不是继续手拧常数。

### VOFA 修改

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `VOFA_OUTPUT_CHANNEL_COUNT` (`IMU_ATTITUDE`) | `5U` | `8U` | 切到 pitch 姿态调试通道 |
| VOFA IMU 通道 | `gyro_z_dps,yaw_deg,yaw_rate_dps,imu_dt_ms,steps_per_vofa` | `kalman_theta_deg,gyro_theta_rate_dps,accel_theta_deg,accel_x_ms2,accel_y_ms2,accel_z_ms2,imu_dt_ms,steps_per_vofa` | 观察 pitch Kalman 角、pitch gyro rate、加速度角、三轴加速度和时间基准 |

### 下一步测试

- 静止放平采集 pitch 数据，看 `kalman_theta_deg` 与 `accel_theta_deg` 是否接近、是否有明显零点偏差。
- 手动前倾/后仰，确认 `kalman_theta_deg` 与 `gyro_theta_rate_dps` 的方向是否符合直立控制极性。

## 2026-07-06 切回速度环复测

### 判断

- 手拿平放 pitch 数据只能验证 IMU 链路和方向，不能定机械直立零点。
- SysTick 已修复后，速度环真实频率从旧数据时期的约 `125Hz` 恢复到约 `1000Hz`，因此必须重新复测左右轮速度闭环。
- 当前继续保持 `BALANCE_PID_ENABLE = 0U`，不上直立环；`WHEEL_SPEED_TEST_ENABLE = 1U`，只测轮速闭环。

### VOFA 修改

| 修改项 | 旧值 | 新值 | 原因 |
|---|---|---|---|
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_IMU_ATTITUDE` | `VOFA_DEBUG_STAGE_SPEED_LOOP` | 切回轮速闭环 6 路输出，复测 tick 修复后的速度环行为 |

### 当前 VOFA 通道

| 通道 | 名称 |
|---|---|
| I0 | `left_ref_rpm` |
| I1 | `left_actual_rpm` |
| I2 | `left_cmd_a` |
| I3 | `right_ref_rpm` |
| I4 | `right_actual_rpm` |
| I5 | `right_cmd_a` |

### 下一步测试

- 烧录后按 KEY 进入轮速测试。
- 测试 `S50 -> S100 -> S150 -> S200 -> S0 -> S-50 -> S-100 -> S-150 -> S0`。
- 重点观察实际转速是否跟随目标、电流是否抖动或接近限幅、正负速度方向是否正确。

## 2026-07-06 SysTick 修复后速度环复测

### 测试结论

- `vofa+.csv` 前段混有旧 pitch 数据；从约第 `2197` 行开始是速度环数据。
- 正负速度方向正确，`S0` 能停住。
- 各目标转速稳态均值跟随良好：
  - `S50`：左约 `49.19rpm`，右约 `48.92rpm`。
  - `S100`：左右约 `99.65rpm`。
  - `S150`：左约 `149.77rpm`，右约 `149.86rpm`。
  - `S200`：左约 `200.10rpm`，右约 `200.25rpm`。
  - `S-50/S-100/S-150` 均值也基本贴近目标。
- 稳态速度峰峰值偏大：`S200` 左约 `27rpm`、右约 `25rpm`；`S-150` 左约 `29rpm`。
- 电流指令很小且没有接近 `0.35A` 限幅，稳态电流指令峰峰值多在 `0.02~0.03A`，说明不是电流不够。
- 判断：速度均值没问题，但速度环仍在追 CAN 速度反馈抖动；当前主要问题是速度反馈/控制过敏造成的可感震荡。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00120f` | `0.00100f` | 小幅降低速度 P，减少追反馈噪声带来的电流抖动 |
| `LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | `0.20f` | 略增强左轮速度反馈滤波，降低 CAN 速度抖动进入速度环 |
| `RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA` | `0.25f` | `0.20f` | 同上 |

### 下一步测试

- 烧录后复测 `S50 -> S100 -> S150 -> S200 -> S0 -> S-50 -> S-100 -> S-150 -> S0`。
- 重点比较稳态速度峰峰值是否下降、手感震荡是否减轻、阶跃响应是否还能接受。
- 若响应明显变软，再优先把 `WHEEL_SPEED_FF_KV_A_PER_RPM` 小幅提高，不要直接把 P 加回去。

## 2026-07-06 速度环低速前馈补偿

### 测试结论

- 新数据在 `Kp=0.00100`、反馈 LPF alpha `0.20` 下，`S100/S150/S200` 约 `200ms` 已基本接近目标，响应没有明显失控变慢。
- 低速段和负向段手感偏软更明显，尤其 `S50/S-50` 和负向右轮。
- 稳态均值仍贴目标，说明整体速度前馈不是严重不足；`S200` 稳态电流也已经接近当前前馈量，不宜优先大幅增加 `WHEEL_SPEED_FF_KV_A_PER_RPM`。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_FF_STATIC_A` | `0.015f` | `0.018f` | 小幅增加静摩擦/低速前馈，改善低速和换向初段响应，不放大速度反馈噪声 |

### 下一步测试

- 复测同样速度序列，重点观察 `S50/S-50` 起步是否更干脆。
- 同时确认 `S150/S200` 没有明显超调变大；若高速偏冲，再退回 `0.016~0.017f`。

## 2026-07-06 首次尝试 PID 直立环

### 修改目的

- 轮速环均值跟随已经够进入直立环方向测试，先不追求最终站稳。
- 第一轮只做手扶/架起安全测试，确认 pitch 角度、角速度、轮速输出方向和保护逻辑。
- 清理 `config.h` 中不再参与 PID 路径的旧 LQR、外环、零点搜索配置，避免后续调参混淆。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_TEST_ENABLE` | `1U` | `0U` | 退出离地轮速测试模式 |
| `BALANCE_PID_ENABLE` | `0U` | `1U` | 启用 PID 直立环 |
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_SPEED_LOOP` | `VOFA_DEBUG_STAGE_BALANCE_CASCADE` | 输出直立环 12 通道数据 |
| `BALANCE_PID_CURRENT_LIMIT_A` | `0.35f` | `0.25f` | 首次测试先降低电流上限 |
| `BALANCE_PID_CURRENT_SLEW_A_PER_SEC` | `20.0f` | `10.0f` | 降低电流变化速度，避免方向错误时过猛 |
| `BALANCE_PID_SAFE_ANGLE_DEG` | `45.0f` | `30.0f` | 首次测试更早触发角度保护 |
| `BALANCE_PID_SAFE_RATE_DPS` | `900.0f` | `600.0f` | 首次测试更早触发角速度保护 |
| `BALANCE_PID_BALANCE_SPEED_LIMIT_RPM` | `400.0f` | `200.0f` | 限制直立环给轮速内环的目标转速 |
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | LQR 推导值 | `7.96f` | 改成 PID 显式调参量 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | LQR 推导值 | `0.535f` | 改成 PID 显式调参量 |
| `BALANCE_PID_POLARITY` | `BALANCE_LQR_FORCE_SIGN` | `1.0f` | 方向符号显式放在 PID 配置中 |
| `BALANCE_PID_LEFT_YAW_SIGN` | `BALANCE_LQR_LEFT_YAW_SIGN` | `(-1.0f)` | 删除 LQR 依赖 |
| `BALANCE_PID_RIGHT_YAW_SIGN` | `BALANCE_LQR_RIGHT_YAW_SIGN` | `1.0f` | 删除 LQR 依赖 |

### 当前 VOFA 通道

| 通道 | 名称 |
|---|---|
| I0 | `theta_deg` |
| I1 | `theta_rate_dps` |
| I2 | `accel_deg` |
| I3 | `theta_ref_deg` |
| I4 | `balance_ref_rpm` |
| I5 | `left_ref_rpm` |
| I6 | `left_actual_rpm` |
| I7 | `left_cmd_a` |
| I8 | `right_ref_rpm` |
| I9 | `right_actual_rpm` |
| I10 | `right_cmd_a` |
| I11 | `fault` |

### 首次测试流程

- 烧录后手扶车体或架起，先不要放手。
- 按 KEY 授权进入直立环。
- 小角度前倾/后仰，观察 `theta_deg` 与 `balance_ref_rpm/left_ref_rpm/right_ref_rpm` 的符号关系，并确认轮子是否朝“救车方向”转动。
- 若一倾斜轮子就把车推得更倒，立即按 KEY/断电，下一步只改 `BALANCE_PID_POLARITY` 的符号。
- 采集 `vofa+.csv` 后优先看 `theta_deg`、`theta_rate_dps`、`balance_ref_rpm`、左右轮 `ref/actual/cmd`、`fault`。

## 2026-07-06 直立环极性确认与第二版手扶参数

### 测试结论

- 新 12 路 VOFA 数据显示极性基本正确：
  - `theta_deg < 0` 时，`balance_ref_rpm/left_ref_rpm/right_ref_rpm` 基本为负。
  - `theta_deg > 0` 时，`balance_ref_rpm/left_ref_rpm/right_ref_rpm` 基本为正。
  - 左右轮实际转速与目标转速大多数同号。
- 过零附近少数几行 `theta_deg` 与 `balance_ref_rpm` 短暂反号，是角速度 D 项在刹车，不判断为极性错误。
- `fault=3` 首次出现在 `theta_deg` 约 `-31deg`，与当前 `BALANCE_PID_SAFE_ANGLE_DEG = 30deg` 一致，角度保护正常。
- 手扶反馈：会抖，拉回效果一般。判断第一版角速度 D 偏硬、角度 P 偏弱，且首次限流略保守。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_CURRENT_LIMIT_A` | `0.25f` | `0.30f` | 小幅增加轮速内环可用电流，增强拉回能力 |
| `BALANCE_PID_CURRENT_SLEW_A_PER_SEC` | `10.0f` | `12.0f` | 配合限流略放开，但仍保持温和输出 |
| `BALANCE_PID_BALANCE_SPEED_LIMIT_RPM` | `200.0f` | `250.0f` | 增加直立环最大救车轮速，避免中等角度过早饱和 |
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `7.96f` | `10.50f` | 提高角度 P，小角度时更主动拉回 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.535f` | `0.350f` | 降低角速度 D 对陀螺仪/手扶扰动的敏感度，减轻抖动 |

### 下一步测试

- 只做手扶小角度测试，尽量控制在 `±3~8deg`。
- 重点观察 `theta_deg` 接近零点时是否还高频抖、`balance_ref_rpm` 是否频繁正负跳变。
- 若仍抖但拉力够：继续降 `BALANCE_PID_RATE_KD_RPM_PER_DPS` 到 `0.25~0.30f`。
- 若不抖但仍拉不回来：优先继续小幅加 `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` 或限流，不改极性。

## 2026-07-06 回到轮速内环重调

### 判断

- 新直立环数据中，`balance_ref_rpm` 已经多次打到 `±250rpm`，但实际轮速平均只达到目标的约 `35%~44%`。
- 说明当前主要瓶颈不是直立环极性，而是轮速内环对快速变化目标的跟随偏慢。
- 因此先关闭直立环，回到独立轮速测试；否则外环目标频繁变化，会干扰内环判断。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ENABLE` | `1U` | `0U` | 关闭直立环，只测轮速内环 |
| `WHEEL_SPEED_TEST_ENABLE` | `0U` | `1U` | 打开轮速测试模式 |
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_BALANCE_CASCADE` | `VOFA_DEBUG_STAGE_SPEED_LOOP` | 输出 6 路轮速内环数据 |
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00100f` | `0.00130f` | 提高轮速误差响应 |
| `WHEEL_SPEED_CURRENT_SLEW_A_PER_SEC` | `8.0f` | `20.0f` | 允许轮速电流更快跟随目标变化 |
| `WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE` | 新增 | `1U` | 按 KEY 后自动执行轮速阶跃序列 |
| `WHEEL_SPEED_TEST_AUTO_STEP_MS` | 新增 | `1500U` | 每档保持 1.5s |

### 当前 VOFA 通道

| 通道 | 名称 |
|---|---|
| I0 | `left_ref_rpm` |
| I1 | `left_actual_rpm` |
| I2 | `left_cmd_a` |
| I3 | `right_ref_rpm` |
| I4 | `right_actual_rpm` |
| I5 | `right_cmd_a` |

### 自动测试序列

- 烧录后按 KEY 授权，只做离地轮速测试，不上直立环。
- 按 KEY 后自动执行：`S50 -> S0 -> S100 -> S0 -> S150 -> S0 -> S200 -> S0 -> S-50 -> S0 -> S-100 -> S0 -> S-150 -> S0`。
- 每段保持 `1.5s`，重点看实际轮速是否能更快接近目标、电流是否长期顶到 `0.35A`、是否出现持续振荡。
- 若实际轮速仍明显跟不上且电流未打满，再继续加 `WHEEL_SPEED_PID_KP_A_PER_RPM`；若电流打满，则考虑提高测试限流或继续优化前馈。

## 2026-07-06 轮速内环自动序列复测结论

### 测试结论

- 自动序列正常执行，VOFA 为 6 路速度环数据。
- 当前 `WHEEL_SPEED_PID_KP_A_PER_RPM = 0.00130f`、`WHEEL_SPEED_CURRENT_SLEW_A_PER_SEC = 20.0f` 下，独立轮速内环表现足够好：
  - `S50` 稳态左约 `47.5rpm`，右约接近 `50rpm`，约 `0.04s` 达到 80%。
  - `S100` 稳态左约 `99.1rpm`，约 `0.06s` 达到 80%。
  - `S150` 稳态左约 `150.9rpm`，约 `0.06s` 达到 80%。
  - `S200` 稳态左约 `202.8rpm`，约 `0.06s` 达到 80%。
  - 负向 `S-50/S-100/S-150` 也能快速贴近目标。
- 全序列电流没有接近 `0.35A` 限幅，说明独立轮速测试下不是电流不够。
- 速度反馈峰峰值仍有约 `13~25rpm`，主要像 CAN 速度反馈抖动/量化，不再优先通过继续加速度 P 解决。
- 结论：独立轮速内环已经可以回到直立环；之前直立时跟不上，主要因为直立路径仍使用更保守的 `BALANCE_PID_CURRENT_LIMIT_A = 0.30f` 和 `BALANCE_PID_CURRENT_SLEW_A_PER_SEC = 12.0f`。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_TEST_ENABLE` | `1U` | `0U` | 退出轮速自动测试 |
| `WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE` | `1U` | `0U` | 关闭自动序列，避免后续混淆 |
| `BALANCE_PID_ENABLE` | `0U` | `1U` | 回到直立环手扶测试 |
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_SPEED_LOOP` | `VOFA_DEBUG_STAGE_BALANCE_CASCADE` | 恢复直立环 12 路观察 |
| `BALANCE_PID_CURRENT_LIMIT_A` | `0.30f` | `0.35f` | 让直立路径有与轮速测试一致的可用电流余量 |
| `BALANCE_PID_CURRENT_SLEW_A_PER_SEC` | `12.0f` | `20.0f` | 让直立路径的轮速内环响应跟上快速变化目标 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.350f` | `0.300f` | 进一步减小角速度项导致的来回切换 |

### 下一步测试

- 继续手扶小角度直立测试，尽量控制在 `±3~8deg`。
- 重点看 `balance_ref_rpm` 和左右轮实际速度是否比上一轮更同步，以及 `theta_deg` 是否还频繁过零摆动。
- 若仍抖且轮速已跟得上，下一步再降 `BALANCE_PID_RATE_KD_RPM_PER_DPS`；若不抖但拉回仍弱，再考虑小幅加 `BALANCE_PID_ANGLE_KP_RPM_PER_DEG`。

## 2026-07-06 直立环发散数据复盘

### 数据判断

- 新 VOFA 数据为 12 路直立级联输出，闭环运行约 `2.02s~31.52s` 后触发角度保护。
- 控制段内 `theta_deg` 平均绝对值约 `4.43deg`，但 `theta_rate_dps` 平均绝对值约 `81.9dps`，并出现 `±300dps` 以上的大幅摆动。
- `balance_ref_rpm` 已经能跟随角度和角速度变化，目标轮速与实际轮速方向大部分一致；独立轮速内环不是当前第一瓶颈。
- 异常点在姿态量：`theta_deg` 与 `accel_deg` 同时刻相关性几乎为零，`d(theta)/dt` 与 `theta_rate_dps` 相关性也很低，说明动态时控制角存在明显错相/被运动加速度拉偏。
- 因此当前“不收敛、越摆越大”优先按姿态角来源问题处理，而不是继续单纯加大直立 P/D 或电流。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_KALMAN_ANGLE_WEIGHT` | `0.85f` | `1.0f` | 控制端只使用 Kalman 角，去掉额外加速度计混合 |
| `BALANCE_PID_ACCEL_ANGLE_WEIGHT` | `0.15f` | `0.0f` | 避免运动时加速度角直接拖动控制角 |
| `BALANCE_KALMAN_R_MEASURE` | `0.110f` | `0.500f` | 降低 Kalman 对加速度角的信任，让陀螺仪短时动态占主导 |

### 下一步测试

- 继续手扶小角度测试，先不要让车自由跑，控制在 `±3~8deg`。
- 重点看 `theta_deg` 是否明显更连续，`d(theta)/dt` 与 `theta_rate_dps` 是否同向，`balance_ref_rpm` 是否不再被加速度角拖着反复抽动。
- 如果仍出现同样的发散，再继续处理 Kalman 加速度门控或进一步提高 `BALANCE_KALMAN_R_MEASURE`，而不是优先加大直立环增益。

## 2026-07-06 直立环小角度拉力不足假设

### 数据判断

- 新数据按 KEY 后约 `0.74s` 触发 `fault=3`，说明当前还没有形成稳定收敛。
- 小角度初段 `|theta| <= 5deg` 时，平均 `|theta|` 约 `2.9deg`，平均 `|balance_ref_rpm|` 约 `43rpm`。
- 初段实际轮速明显滞后，车体角度先继续放大；后面 `balance_ref_rpm` 打到 `±250rpm` 时已经属于补救阶段。
- 本轮先接受“车一直在动，不用把按 KEY 前数据当静止零偏”的判断，不再切 IMU 零偏诊断；优先验证角度 P 是否偏小。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `10.50f` | `14.00f` | 增强小角度初段给轮速内环的目标速度，避免等角速度起来后才补救 |

### 下一步测试

- 保持手扶小角度，仍然不要直接放手。
- 重点看按 KEY 后前 `0.2s`：`theta_deg` 是否还从 `2~3deg` 快速放大到 `7deg+`。
- 若初段能更早刹住但过零后反向冲得更厉害，再降 `BALANCE_PID_RATE_KD_RPM_PER_DPS` 或限制 `BALANCE_PID_BALANCE_SPEED_LIMIT_RPM`。
- 若初段仍拉不住且 `balance_ref_rpm` 很快打满，再不是继续加 P，而要看轮速实际响应、电流限幅和机械重心。

## 2026-07-06 串口在线调 PID

### 修改目的

- 避免每改一次直立环参数都重新烧录。
- 通过当前 VOFA 串口 RX 命令在线修改 RAM 中的直立环参数；重启后仍回到 `config.h` 默认值。
- 命令解析放在速度命令之前，避免 `P14` 被旧逻辑误识别成速度目标 `14rpm`。

### 支持命令

| 命令示例 | 参数 |
|---|---|
| `P14` / `KP=14` | `balance_pid_runtime_kp`，角度 P |
| `I0` / `KI=0` | `balance_pid_runtime_ki`，角度 I |
| `D0.45` / `KD=0.45` | `balance_pid_runtime_kd`，角速度 D |
| `L250` / `LIMIT=250` / `O250` | `balance_pid_runtime_speed_limit_rpm`，直立环输出目标轮速限幅 |
| `C0.35` / `CUR=0.35` | `balance_pid_runtime_current_limit_a`，直立路径速度内环电流限幅 |
| `R20` / `SLEW=20` | `balance_pid_runtime_current_slew_a_per_sec`，直立路径电流变化率 |

### 使用说明

- VOFA 串口发送 ASCII 命令，末尾带换行。
- 在线修改后会立即作用到直立环，并清掉角度 PID 积分项。
- 当前先不回发长 ACK，避免和周期性 CSV 输出争用发送缓冲；是否生效通过下一段 VOFA 数据判断。
- 轮速测试命令 `S100` 仍保留，用于速度环测试。

### 代码解耦

- 串口命令解析已从 `Application/ins/ins_task.c` 拆出到 `Application/gimbal/chassis_serial_cmd.c/.h`。
- `ins_task.c` 只保留 UART 收行队列，收到一行后调用 `ChassisSerialCmd_HandleLine(line, len)`。
- 新文件已加入 `Makefile` 和 `MDK-ARM/spi_bmi088.uvprojx`，避免不同构建入口漏编译。

## 2026-07-06 定位"小角度甩飞"根因：姿态 Kalman 被轮子加速度污染

### 数据结论（12 路 BALANCE_CASCADE 数据，14349 行；注意：所有"站立"段均为手扶，车不能自立）

- 极性复核（全部关闭怀疑）：
  - 救车方向正确：轮子加速后 60~100ms 车身角加速度转负（滞后相关 -0.18~-0.32），是负反馈指纹，`BALANCE_PID_POLARITY` 不用再改。
  - 速度/位置外环 corr(轮速, theta_ref) = -0.89，级联形式正确；且 theta_ref 限幅 ±2deg，与"立不起来"无关。
  - 差速 YAW 极性未测，但站立阶段 turn_ref=0，不影响当前问题。
- 带载电流→轮加速度实测：回归得 b0 约 `800~1500 rpm/s/A`（r 仅 0.17~0.33，只能给区间），对应 `0.35A` 可救角度约 `5~10deg`。
- 电流限幅不是当前第一瓶颈：`balance_ref` 打满 ±250rpm 时，电流命令只有 10% 时间碰到 `0.35A`，平均只用 `0.27A`；速度环误差均值 167rpm × Kp 0.0013 ≈ 0.217A，对得上——是速度环偏软没把电流用出去。
- **真正根因：姿态估计器在动态下输出假角。**
  - 按 Q=0.012/Qb=0.003/R=0.5 仿真，加速度修正 63% 时间常数仅 ~0.18s，直立摆动 ~1Hz 时污染大部分穿透。
  - 实测 d(theta)/dt 对 gyro rate 回归斜率只有 0.44（陀螺主导应 ~1.0），theta 一多半变化来自加速度修正而非陀螺积分。
  - 轮子加速度折算假角 std = 14.2deg，与 theta 本身波动同量级；高动态时刻 corr(theta, accel_deg) = **-0.36（反相）**。
  - 甩飞回路：前倾 → 轮子加速去救 → 加速度计被污染 → 估计角被拽向反方向 → 控制器反打 → 甩飞。小角度时假信号占比最大，所以恰恰小角度最炸。
- 静止扶稳段 theta 均值 -2.6deg（accel_deg -2.8deg，两源一致），存在 ~2deg 机械零点偏置，`BALANCE_ANGLE_OFFSET_DEG` 尚未补。

### 修改内容

| 修改项 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_KALMAN_Q_ANGLE` | `0.012f` | `0.001f` | 把加速度修正时间常数从 ~0.18s 放慢到 ~1.2s，陀螺积分为主体 |
| `BALANCE_KALMAN_Q_BIAS` | `0.003f` | `0.0001f` | 旧值让污染同时写坏陀螺零漂估计；零漂本身变化很慢 |
| `BALANCE_KALMAN_R_MEASURE` | `0.500f` | `5.000f` | 同上，降低对加速度角的信任 |
| `BALANCE_ACCEL_GATE_ENABLE` | `0`（宏存在但无实现） | `1`（已在 `ins_task.c` 实现） | 加速度模长偏离 `0.75~1.25g` 时只做 `KalmanAngle_Predict` 陀螺积分，跳过加速度修正，拦截轮子加减速瞬态污染 |
| `VOFA_DEBUG_STAGE` | `BALANCE_CASCADE` | `IMU_ATTITUDE` | 先验证估计器，再回直立环 |

- 代码改动：`ins_task.c` Kalman 更新处新增门控分支（`sqrtf` 模长判断 → 越界走 `KalmanAngle_Predict`，在界内走 `KalmanAngle_Update`）。已编译通过。

### 验收测试（关闭闭环：不按 KEY，电机保持急停，手拿车做）

1. 手扶到真正直立平衡附近 ~30s：`kalman_theta` 与 `accel_theta` 应重合并接近 `0deg`；平放/倒下不是平衡零点，应接近 `+90deg`。
2. **水平快速前后推拉（不倾斜）——核心测试**：`accel_theta` 大幅乱摆属正常，但 `kalman_theta` 应基本不动（<2deg）。旧参数下此测试必挂。
3. 倾斜到某角度停住：`kalman_theta` 跟随，停住后 ~1s 内收敛到 `accel_theta`，不振荡。
4. 保持固定倾角 60s：`kalman_theta` 不应慢慢漂走；若漂，说明 R 过大，往回收。

### 估计器通过后的调参路线（先别做，逐条来）

1. 用真正直立平衡附近的数据微调 `BALANCE_ANGLE_OFFSET_DEG`；不能用平放姿态当零点。
2. 回 `BALANCE_CASCADE`，手扶直立。刚度目标：重力需求约 `0.035~0.065 A/deg`，当前 `14×0.0013=0.018 A/deg` 差 2~3.5 倍。优先提直立路径速度内环等效 Kp 至 ~0.003 A/rpm（把 0.35A 真正用满），角度 P 用串口 `P25` 附近，`D0.5` 跟上。
3. `0.35A` 限幅先不动；若之后电流长期顶满仍救不回，再提。

## 2026-07-06 发现陀螺仪符号反向：昨天的 Kalman 修改暴露了它，并放大了危害

### 数据结论（新 vofa+.csv，12 路 BALANCE_CASCADE，3092 行，含大量 fault=3 手动晃车段）

- **决定性证据（电机断开的 fault=3 段，加速度角无污染）**：`d(accel_deg)/dt` 对 `theta_rate_dps` 回归斜率 = **-1.079**（corr -0.76）。幅度 ≈1 说明轴选对了，符号为负说明**陀螺仪与加速度角对同一倾倒方向的符号相反**。
- 全段 corr(theta, accel_deg) = **-0.95**，theta 与 accel_deg 的差从 2.4deg 单调发散到 **144.8deg**：陀螺主导后 theta 朝错误方向积分，加速度修正（又慢又常被门控跳过）拉不回来。
- `d(theta)/dt` 对 rate 斜率 = 1.014：滤波器内部自洽——它忠实地积分了一个符号错误的角速度。
- **必须诚实记录**：这个符号错误一直存在，但旧参数（R=0.5，加速度主导）下 theta ≈ 加速度角，错误的陀螺项只表现为噪声/抖动；昨天把滤波器改成陀螺主导后，theta 变成错误方向的纯积分，问题从"隐性抖动"变成"显性发散"。昨天归因"轮子加速度污染"只对了一半，真正的第一根因是陀螺符号。
- 症状全部对上：
  - "有时极性正确有时反向"：theta 带着巨大漂移偏置，符号与真实倾角随机对不上，轮子方向自然看起来随机。
  - "按 KEY 没反应"：theta 被错误积分卡在 30deg 外，KEY 解锁后 0.1s 内立刻再触发角度保护（数据里 13.5~17.8s 有 8 次解锁→0.1s 内再故障的循环），表现就是"按了没反应"。
  - D 项 = Kd × 反号角速度 = **正反馈注能**，解释了此前"会抖、小角度甩飞"。
- 符号错误的来源推断:`BALANCE_ACCEL_ANGLE_SIGN = -1` 为控制约定翻转了加速度角，但 `BALANCE_GYRO_SIGN` 留在 +1，两源约定不一致。救车方向极性是按加速度角主导的 theta 验证的，所以保持加速度角符号不动、翻转陀螺仪。

### 修改内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---|---|---|
| `BALANCE_GYRO_SIGN` | `1.0f` | `(-1.0f)` | 与加速度角符号约定对齐；theta 积分方向、D 项阻尼方向、角速度保护全部恢复正确 |
| KEY 解锁路径 | 直接 `emergency_stop = 0` | 解锁瞬间调用新增 `INS_RequestBalanceAngleReseed()`，用当前加速度角重播种 Kalman | 故障期间累计的陀螺积分误差不带进新一轮控制，消除"解锁即再故障"的死循环 |
| `ins_task.c` | 无 | 新增 `balance_reseed_request` 标志 + `INS_RequestBalanceAngleReseed()`（声明加入 `ins_task.h`） | 跨任务安全触发重播种 |

- 昨天的 Q/R/门控修改**保留**：陀螺符号修正后，陀螺主导 + 加速度门控才是正确组合。已编译通过。

### 验收测试（VOFA 保持 BALANCE_CASCADE，theta/rate/accel_deg 在 I0/I1/I2，故障时也可观察）

1. **不按 KEY**，手拿车缓慢前后倾斜：`theta_deg`（I0）与 `accel_deg`（I2）应同向、幅度接近；`theta_rate_dps`（I1）与 theta 变化方向一致。这一条过了，符号链路才算闭合。
2. 快速水平推拉（不倾斜）：theta 应基本不动（验证昨天的门控+慢修正在符号正确后按预期工作）。
3. 按 KEY 解锁：不应再出现"解锁 0.1s 内立刻 fault=3"；倾斜超 30deg 触发保护后扶正、再按 KEY，应能正常进入控制。
4. 手扶直立：D 项方向已从注能变为阻尼，抖动应明显减轻；极性应稳定不再"时对时反"。

### 测试通过后再做

- 读静止 theta 均值填 `BALANCE_ANGLE_OFFSET_DEG`（旧数据显示约 2deg 量级，但那是符号错误时期的数据，以新数据为准）。
- 再按既定路线调刚度（内环等效 Kp → `P25`/`D0.5`）。

## 2026-07-06 水平推拉测试复盘与 innovation gate

### 数据判断

- 本组 `vofa+.csv` 是 8 路 `IMU_ATTITUDE` 数据，约 `109.7s`。
- 用户确认该组包含“手扶保持角度不变，快速水平前后推拉”测试；不能再要求重复同一测试。
- 最像纯水平推拉的一段约 `40s~45s`：
  - `accel_theta` 峰峰值约 `88.6deg`，说明加速度角被水平加速度严重污染。
  - `kalman_theta` 峰峰值约 `8.1deg`，说明新 Kalman/模长门控已经明显压住污染，但仍残留约 `8deg` 假角。
  - `gyro_rate` 平均绝对值约 `2.9dps`，支持“车体角度没有真实大幅转动”的判断。
- `45s~75s` 段 `gyro_rate` 到数百 `dps`，判断包含真实转动/翻动，不能作为纯水平推拉评价。
- 结论：只靠加速度模长门控还不够。水平推拉时可能 `|a|` 接近 `1g`，但 `accel_theta` 已经是假角，因此需要加一层角度创新量门控。

### 修改内容

| 修改项 | 新值/逻辑 | 原因 |
|---|---:|---|
| `BALANCE_ACCEL_ANGLE_GATE_DEG` | `10.0f` | `accel_angle` 与 gyro 预测角相差超过阈值时，判定为异常测量 |
| `KalmanAngle_Correct()` | 新增 | 将 Kalman 更新拆成 `Predict` 和 `Correct`，避免做 innovation gate 时重复预测 |
| `ins_task.c` Kalman 更新 | 先 `KalmanAngle_Predict()`，再检查 `|a|` 和 `|accel_angle - predicted_angle|`，通过才 `KalmanAngle_Correct()` | 拦截“模长正常但角度是假”的水平加速度污染 |

### 下一步验收

- 继续保持 `VOFA_DEBUG_STAGE_IMU_ATTITUDE`，不按 KEY，电机急停。
- 再做一次同样的手扶水平前后推拉，目标：
  - `accel_theta` 可以大幅乱跳。
  - `kalman_theta` 在纯水平推拉段尽量压到 `±2deg` 内；若仍有 `5deg+` 假角，把 `BALANCE_ACCEL_ANGLE_GATE_DEG` 从 `10deg` 降到 `8deg`。
  - 固定真实倾角停住后，`kalman_theta` 仍应能慢慢回到 `accel_theta`，如果回得太慢或漂移，再讨论放宽阈值或回调 `R`。

## 2026-07-06 innovation gate 后的新 VOFA 数据结论

### 数据判断

- 本组 `vofa+.csv` 为 8 路 `IMU_ATTITUDE` 数据，3427 行，按 `imu_dt_ms * steps_per_vofa` 还原时长约 `70.8s`。
- `0s~10s` 手扶静止/小动作段：
  - `kalman_theta` 峰峰值约 `1.39deg`，均值约 `-1.83deg`。
  - `accel_theta` 峰峰值约 `1.32deg`，均值约 `-2.43deg`。
  - `gyro_rate` 平均绝对值约 `0.84dps`。
  - 说明小角度静态下 Kalman 与加速度角一致，传感器小角度参考可信。
- `18s~35s` 固定角度水平快速前后推拉段：
  - `accel_theta` 峰峰值约 `88.5deg`，说明加速度角被水平加速度严重污染。
  - `kalman_theta` 峰峰值约 `3.1deg`；其中 `21s~35s` 较干净段约 `2.3deg`。
  - `gyro_rate` 平均绝对值约 `2.6dps`。
  - 对比上一组约 `8deg` 假角，本次 innovation gate 已经把水平推拉污染压到可接受范围。
- `13s~18s`、`35s` 之后为用户故意做的大角度转动/接近翻转段；按当前约定，直立平衡点必须定义为 `0deg`，平放/倒下约为 `+90deg`。
- 在这段数据里，旧定义下陀螺积分到约 `+84deg`，而 `accel_theta` 到约 `-90deg`。这里不能直接据此改控制角符号，因为此前 12 路直立环数据已经验证过救车极性正确；该段包含大角度翻转/分支，不适合作为直立小角度控制极性的依据。

### 误改复盘

| 修改项 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_ACCEL_ANGLE_SIGN` | `-1.0f` | 错误改成 `1.0f`，已回滚到 `-1.0f` | 该改动翻转了控制用 `theta` 极性，但 `BALANCE_PID_POLARITY` 仍保持 `1.0f`，导致直立环反打 |
| `BALANCE_ANGLE_OFFSET_DEG` | 错误尝试 `90.0f` | `0.0f` | 直立是 `0deg`、平放是 `+90deg`；offset 只用于之后几度级机械偏置微调 |

### 当前结论

- 零点定义固定为：车体直立、重心过轮轴附近是 `0deg`；平放/倒下是 `+90deg`。
- 水平推拉时加速度污染已经基本被门控拦住，`BALANCE_ACCEL_ANGLE_GATE_DEG = 10deg` 先不需要降到 `8deg`。
- 控制极性以真正闭环数据为准；后续新 12 路数据证明 `BALANCE_PID_POLARITY = 1.0f` 会反打，已改为 `BALANCE_PID_POLARITY = -1.0f`。`BALANCE_ACCEL_ANGLE_SIGN = -1.0f` 保持不动。

### 当前结论与下一步

- 这组采样已经完成 IMU/角度定义判断，不需要再重复做 IMU 验证。
- 最终角度约定固定为：直立平衡附近 `theta = 0deg`，平放/倒下约 `theta = +90deg`。
- 旧问题不是 `BALANCE_ACCEL_ANGLE_SIGN = -1.0f`；把它改成 `+1.0f` 是误改，会导致直立环极性反掉。
- 当前代码设置：
  - `BALANCE_ACCEL_ANGLE_SIGN = -1.0f`
  - `BALANCE_PID_POLARITY = -1.0f`
  - `BALANCE_ANGLE_OFFSET_DEG = 0.0f`
  - `VOFA_DEBUG_STAGE = VOFA_DEBUG_STAGE_BALANCE_CASCADE`
- 下一步直接回直立级联测试，不再停留在 `IMU_ATTITUDE`。
- 直立环测试只看真正直立附近的 `|theta| < 10~15deg` 手扶段；平放/翻倒段不能用于判断 PID。
- 按 KEY 后重点看前 `0.2s`：
  - `theta_deg` 是否继续扩大。
  - `balance_ref_rpm` 是否立刻给出救车方向。
  - `left/right_actual_rpm` 是否跟得上 `left/right_ref_rpm`。
  - `left/right_cmd_a` 是否接近限幅，判断是 PID 外环慢、内环慢，还是电流没用出来。

## 2026-07-06 当前 12 路直立数据：PID 极性仍反

### 数据判断

- 本组 `vofa+.csv` 为 12 路 `BALANCE_CASCADE` 数据，3092 行。
- 第一次 `fault=3` 出现在约 `5.48s`，当时：
  - `theta_deg = +30.075deg`
  - `theta_rate_dps = -29.958dps`
  - `balance_ref_rpm = 0`（因为已经进入故障保护）
  - 上一行仍在闭环时 `theta_deg = +29.896deg`，`balance_ref_rpm = +250rpm`，左右电流均已到 `+0.35A`。
- 闭环有效段中，`theta_deg` 与 `balance_ref_rpm` 几乎始终同号：
  - `theta > 0` 时，`balance_ref_rpm > 0`。
  - `theta < 0` 时，`balance_ref_rpm < 0`。
  - 统计上同号 `1156` 行，反号仅 `1` 行。
- 实测回归显示正轮速会让 `theta` 往正方向变化，因此当前同号输出是正反馈：角度越偏，轮子越把它推向更偏。
- 结论：不是轮速内环没跟上，而是直立 PID 输出极性反了。

### 30 度退出原因

- `fault=3` 对应 `CHASSIS_FAULT_PID_ANGLE`。
- 代码使用 `abs(theta_deg) > BALANCE_PID_SAFE_ANGLE_DEG` 触发保护。
- 当前 `BALANCE_PID_SAFE_ANGLE_DEG = 30.0f`，所以无论正负方向到 `30deg` 都会退出。
- 本组第一次退出是显示角 `+30.075deg`；若现场观察为“负方向”，说明现场物理方向叫法与 VOFA 显示正负相反，但保护本身按绝对值触发。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_POLARITY` | `1.0f` | `-1.0f` | 让 `theta > 0` 时输出负轮速、`theta < 0` 时输出正轮速，恢复负反馈 |

### 下一步测试

- 保持 `BALANCE_ACCEL_ANGLE_SIGN = -1.0f` 不再动。
- 重新烧录后只做手扶 KEY 小角度测试。
- 若极性正确，按 KEY 后 `theta_deg > 0` 时 `balance_ref_rpm` 应为负，`theta_deg < 0` 时 `balance_ref_rpm` 应为正。
- 暂时不要放宽 `BALANCE_PID_SAFE_ANGLE_DEG`；等负反馈确认后再考虑是否从 `30deg` 提到 `40~45deg`。

## 2026-07-06 直立环极性按物理方向修正

### 现场判断

- 已确认当前 IMU pitch 正方向与轮子转速正方向在物理上是一致的。
- 对平衡车而言，车体向某方向倾倒时，轮子需要向同一方向运动去追重心；因此在该方向约定下，直立环 `theta` 与目标轮速应同号。
- 上一节仅根据失控数据里 `theta` 与 `balance_ref_rpm` 同号就判定为正反馈，忽略了“轮子追重心”这一物理关系，属于极性判断误判。
- 现在 `BALANCE_PID_POLARITY = -1.0f` 会让 `theta > 0` 输出负轮速、`theta < 0` 输出正轮速，实际反向。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_POLARITY` | `-1.0f` | `1.0f` | IMU pitch 与轮速方向一致时，直立环应让倾角误差与目标轮速同号 |

### 下一步测试

- 重新烧录后手扶小角度 KEY 测试。
- 验证方向：`theta_deg > 0` 时 `balance_ref_rpm > 0`，`theta_deg < 0` 时 `balance_ref_rpm < 0`。
- 先只确认极性和是否有阻尼感，不急着放手。

## 2026-07-06 进入直立调参阶段

### 阶段结论

- 速度内环链路已验证可用。
- IMU pitch 与轮速方向极性已按现场物理方向统一。
- 当前直立环极性使用 `BALANCE_PID_POLARITY = 1.0f`：倾角误差与目标轮速同号。
- `VOFA_DEBUG_STAGE` 保持 `VOFA_DEBUG_STAGE_BALANCE_CASCADE`，后续直接用 12 路级联数据调参。

### 调参原则

- 先手扶小角度确认阻尼和极性，再逐步增加刚度。
- 只看真正直立附近 `|theta| < 10~15deg` 的闭环段；平放、翻倒和故障后的数据不用于 PID 判断。
- 若方向正确但拉不住，优先看 `balance_ref_rpm` 是否打满、轮速实际是否跟上、电流是否碰到限幅，再决定调外环 P/D、速度内环等效刚度或电流限幅。

## 2026-07-06 串口调参链路增强

### 问题判断

- 原 VOFA 串口 RX 只把 `\n` 当作命令结束，收到 `\r` 会直接丢弃。
- 若串口助手或 VOFA 控件只发送回车 `\r`，命令不会入队执行，表现为 `P25/KD=0.5` 等在线调参没有生效。
- 原 12 路 `BALANCE_CASCADE` 没有直接输出运行时 Kp/Kd/限流，是否调参成功只能间接猜。

### 修改内容

| 修改项 | 新逻辑 | 原因 |
|---|---|---|
| 命令结束符 | `\r` 或 `\n` 都触发入队 | 兼容 VOFA/串口助手不同换行设置 |
| 串口命令计数 | 新增 `chassis_serial_cmd_rx_count/ok_count/last_type/last_value` | 便于确认命令是否收到、是否解析成功 |
| BALANCE_CASCADE VOFA | 从 12 路增至 16 路，末尾新增 `runtime_kp,runtime_kd,runtime_current_limit_a,serial_cmd_ok_count` | 在线调参后可直接在 VOFA 看到参数变化和成功计数 |

### 新增 VOFA 通道

- I12 `runtime_kp`
- I13 `runtime_kd`
- I14 `runtime_current_limit_a`
- I15 `serial_cmd_ok_count`

## 2026-07-06 P35/D0.3 直立调参数据

### 数据判断

- 本组 `vofa+.csv` 为 16 路 `BALANCE_CASCADE` 数据，共 `5713` 行，约 `114.26s`。
- 全程 `fault=0`，没有触发角度保护。
- 串口调参链路正常：`serial_cmd_ok_count` 从 `2` 增加到 `6`；最终运行参数为 `runtime_kp=35`、`runtime_kd=0.300`、`runtime_current_limit_a=0.350`。
- `Kp=35` 主要运行段从约 `2.86s` 持续到结束，约 `111s`。
- 全闭环段统计：
  - 平均 `|theta| ≈ 1.13deg`，最大 `|theta| = 24.03deg`。
  - 平均 `|theta_rate| ≈ 6.9dps`。
  - 平均 `|balance_ref_rpm| ≈ 42rpm`。
  - 左右轮平均速度误差约 `17~18rpm`。
  - 电流打满比例很低：左约 `0.2%`，右约 `0.3%`。
- 最大角度出现在约 `20.14s`，`theta=-24.03deg`，同时 `accel=-41.5deg`、`balance_ref=-250rpm`，判断为一次明显外部扰动/快速动作；系统没有 fault，并在随后恢复。
- `33s` 之后多数 10 秒窗口平均 `|theta|` 约 `0.7~1.3deg`，说明当前已进入可调参状态，不再是极性/传感器链路错误。

### 当前结论

- `P35/D0.3` 比 `P50/D0.3` 明显更稳，适合作为下一步调参基线。
- 当前 D 仍偏保守；若想提高抗扰恢复和减少大扰动后的摆动，下一步优先试 `D0.4~D0.45`，不要先继续加 P。
- 当前数据平均角度和 `theta_ref` 都接近 0，不能从这组数据强判机械重心明显偏前；重心偏置仍需结合手感和静止直立均值判断。

## 2026-07-06 P35/D0.4 与 -2deg 偏置测试

### 数据判断

- 本组 `vofa+.csv` 为 16 路 `BALANCE_CASCADE` 数据，共 `8272` 行，约 `165.44s`。
- 最终运行参数为 `runtime_kp=35`、`runtime_kd=0.400`、`runtime_current_limit_a=0.350`。
- 全数据中 `fault=3` 共 `319` 行，分成两段：
  - `6.02s~9.76s`：`Kp=14/D0.3`，触发前 `theta=-25.58deg`、`theta_rate=-212.6dps`，随后角度继续到约 `-81deg`。
  - `26.36s~28.96s`：`Kp=35/D0.3`，触发前 `theta=29.98deg`、`theta_rate=255dps`。
- `D0.4` 生效后没有再出现 fault。
- `P35/D0.4` 在 `62.52s~107.00s` 段：平均 `|theta|≈0.82deg`，最大约 `6.28deg`。
- 最终 `theta_ref=-2deg` 段（约 `148.60s~165.42s`）：
  - 平均 `theta≈-2.02deg`，说明车能跟住 `-2deg` 目标偏置。
  - 平均 `|theta-theta_ref|≈0.62deg`。
  - 平均 `|theta_rate|≈4.1dps`。
  - 平均 `|balance_ref_rpm|≈21.9rpm`。
  - 左右轮平均速度误差约 `9~10rpm`。

### 当前结论

- `P35/D0.4` 明显优于 `P35/D0.3`，阻尼更好，后半段数据更干净。
- `theta_ref=-2deg` 偏置能稳定跟住，说明如果手感上确实需要前/后倾补偿，`B` 偏置链路是可用的。
- 但是否把 `-2deg` 固化为机械零点，还需要现场确认：如果加 `B-2` 后车明显更愿意站住、轮子平均速度/位置漂移更小，再考虑固化；否则只作为手动偏置测试结果。

## 2026-07-06 固化当前直立调参基线

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `14.00f` | `35.00f` | 固化当前稳定的角度 P 基线 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.300f` | `0.400f` | 固化当前阻尼更好的 D 参数 |
| `BALANCE_ANGLE_OFFSET_DEG` | `0.0f` | `-2.0f` | 将 `B-2` 对应的重心/机械偏置写入固定角度零点；烧录后默认 `theta_ref=0` 即等效于此前手动 `B-2` |

### 烧录后验证

- 不再发送 `B-2`，保持默认 `B0`。
- VOFA 中 `runtime_kp` 应为 `35`，`runtime_kd` 应为 `0.4`。
- 直立静态附近 `theta_deg` 应接近 `0deg`，`balance_ref_rpm` 长期均值应接近 `0rpm`。

## 2026-07-06 固化参数烧录后验证

### 数据判断

- 本组 `vofa+.csv` 为 16 路 `BALANCE_CASCADE` 数据，共 `1709` 行，约 `34.18s`。
- 未发送串口调参命令：`serial_cmd_ok_count=0`。
- 固化参数已生效：`runtime_kp=35`，`runtime_kd=0.400`，`runtime_current_limit_a=0.350`。
- 全程 `fault=0`。
- 全段均值：
  - `theta_avg≈0.18deg`，`theta_ref_avg≈0.09deg`。
  - 平均 `|theta-theta_ref|≈1.01deg`。
  - `balance_ref_avg≈1.69rpm`，长期均值接近 0。
  - 左右轮平均速度误差约 `11.6rpm/12.8rpm`。
- 最后约 `5s` 安静段：
  - `theta_avg≈0.30deg`。
  - 平均 `|theta-theta_ref|≈0.37deg`。
  - `balance_ref_avg≈3.6rpm`。
- 约 `14.3s` 有一次明显外部扰动/快速动作：`accel` 瞬间到约 `55deg`，`theta_rate` 到约 `174dps`，`balance_ref` 打到 `250rpm`，但未触发 fault，并在约 `0.3s` 内回到小角度。

### 当前结论

- `BALANCE_ANGLE_OFFSET_DEG=-2.0f` 固化方向正确，默认 `B0` 已基本等效于此前手动 `B-2`。
- 当前 `P35/D0.4/offset=-2` 可作为直立短站和轻推扰动测试基线。
- 下一步不优先改偏置；若仍有回摆，再小幅试 `D0.45`，若小角度偏软再试 `P38~40`。

## 2026-07-06 转向模式与位置/速度外环阶段

### 转向模式验证

- 最新完整转向测试中，`serial_cmd_ok_count` 按 `0 -> 1 -> 2 -> 3 -> 4` 变化，对应 `left -> stand -> right -> stand` 四条命令均被串口正确解析。
- `left` 模式下左右轮目标出现 `-45/+45rpm` 差速；`right` 模式下出现 `+45/-45rpm` 差速；两次 `stand` 后差速均回到 0。
- 新一轮转向测试全程 `fault=0`，转向段电流未饱和，姿态扰动小；若现场观察物理方向正确，则 `BALANCE_PID_LEFT_YAW_SIGN/BALANCE_PID_RIGHT_YAW_SIGN` 不再调整。

### 用力推车数据判断

- 用力外部推动测试中，出现多次 `fault=3`，均为角度超过安全阈值触发保护，不是反馈丢失。
- 故障前典型状态：
  - `theta_ref_deg` 已经接近原上限 `±2deg`。
  - `balance_ref_rpm` 已经接近原上限 `±250rpm`。
  - 左右轮实际平均速度可到约 `200~250rpm`。
  - 电流没有长期打满 `0.35A`，说明当前首要问题不是轮速方向错误，而是外环/直立输出权限偏小。
- 现有位置/速度外环已经工作：它由轮速估算速度，速度积分得到位置，再由 `position_error` 和 `speed_error` 计算 `theta_ref_deg`。
- `theta_ref_deg` 不是由 pitch 角速度直接计算；pitch 角速度 `theta_rate_dps` 用于内层直立环 D 项阻尼。

### 本次参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_THETA_REF_LIMIT_DEG` | `2.0f` | `3.0f` | 放大位置/速度外环可给直立环的目标重心角权限 |
| `BALANCE_PID_BALANCE_SPEED_LIMIT_RPM` | `250.0f` | `320.0f` | 原数据中 `balance_ref_rpm` 频繁顶到 `±250rpm`，需要给直立环更多速度目标余量 |

### 下一步测试

- 先不改轮速 PID/电流环响应，烧录后继续做 `stand` 模式外力推车测试。
- 重点观察：
  - `theta_ref_deg` 是否能用到新的 `±3deg` 范围。
  - `balance_ref_rpm` 是否还长时间顶到 `±320rpm`。
  - 实际轮速是否能跟随目标，电流是否接近 `0.35A`。
- 若 `balance_ref_rpm` 提高后实际轮速仍明显跟不上、且电流仍偏小，再进入轮速响应调参；若电流已打满，则优先考虑电流/动力余量限制。

## 2026-07-06 最新外环符号判断

### 数据判断

- 最新 `vofa+.csv` 共 `5054` 行，约 `50.5s`，全程 `fault=0`。
- `theta_ref_deg` 未打到新上限：范围约 `-0.77deg ~ +1.11deg`。
- `balance_ref_rpm` 也未打到 `±320rpm`：范围约 `-172rpm ~ +235rpm`。
- 电流几乎未饱和，仅约 2 行触及 `0.35A`，说明这组数据不能支持“轮速输出能力已经不够”的结论。
- 关键现象：
  - 当实际平均轮速为正约 `+107rpm` 时，`theta_ref≈-0.77deg`，`balance_ref≈+136rpm`，控制仍在给正向速度目标。
  - 当实际平均轮速为负约 `-111rpm` 时，`theta_ref≈+1.11deg`，`balance_ref≈-122rpm`，控制仍在给负向速度目标。
- 这说明当前速度/位置外环符号会使 `balance_ref` 与已有速度同向，表现为“越推越跑”，不是单纯的轮速响应慢。

### 当前结论

- 下一步应优先翻转外环测量符号：
  - `BALANCE_PID_POSITION_SIGN` 从 `1.0f` 改为 `(-1.0f)`。
  - `BALANCE_PID_VELOCITY_SIGN` 从 `1.0f` 改为 `(-1.0f)`。
- 暂时不改轮速 PID；修正外环符号后再看实际轮速是否还跟不上目标。

## 2026-07-06 VOFA 改为外环极性诊断视图

### 修改目的

- 暂时不改 `BALANCE_PID_POSITION_SIGN/BALANCE_PID_VELOCITY_SIGN`，先只改 VOFA 输出。
- 下一次 `stand` 模式推车时，直接观察位置/速度误差、`speed_theta_ref_deg`、`balance_ref_rpm` 与实际平均轮速的符号关系。
- 目标是先确认“外环极性”而不是继续混合判断轮速响应。

### 新 16 路通道

| 通道 | 名称 | 含义 |
|---:|---|---|
| I0 | `theta_deg` | 当前车身 pitch 角 |
| I1 | `theta_ref_deg` | 最终给直立环的目标角 |
| I2 | `speed_theta_ref_deg` | 位置/速度外环贡献的目标角 |
| I3 | `position_m` | 符号处理后的估计位置 |
| I4 | `position_ref_m` | 目标位置 |
| I5 | `position_error_m` | `position_ref_m - position_m` |
| I6 | `velocity_mps` | 符号处理后的估计速度 |
| I7 | `velocity_ref_mps` | 目标速度 |
| I8 | `velocity_error_mps` | `velocity_ref_mps - velocity_mps` |
| I9 | `balance_ref_rpm` | 直立角度环输出的平均轮速目标 |
| I10 | `avg_ref_rpm` | 左右轮平均目标转速 |
| I11 | `avg_actual_rpm` | 左右轮平均实际转速 |
| I12 | `avg_cmd_a` | 左右轮平均电流命令 |
| I13 | `fault` | 底盘故障码 |
| I14 | `position_sign` | 编译期 `BALANCE_PID_POSITION_SIGN` |
| I15 | `velocity_sign` | 编译期 `BALANCE_PID_VELOCITY_SIGN` |

### 判断方法

- 只测试 `stand`，不要发 `left/right/forward`。
- 推车后若 `avg_actual_rpm` 为正，理想情况下外环应让 `balance_ref_rpm` 往反向抑制速度；若同向，说明速度/位置外环极性仍错误。
- 看 `velocity_error_mps -> speed_theta_ref_deg -> balance_ref_rpm -> avg_actual_rpm` 这条链，比只看 `theta/theta_ref` 更直接。

## 2026-07-06 翻转位置/速度外环符号

### 数据依据

- 使用外环极性诊断视图后，`position_sign=+1`、`velocity_sign=+1` 时，推车数据出现明确同向加速：
  - `avg_actual_rpm > +15rpm` 的样本中，`balance_ref_rpm` 几乎总是同为正，平均约 `+86rpm`。
  - `avg_actual_rpm < -15rpm` 的样本中，`balance_ref_rpm` 几乎总是同为负，平均约 `-66rpm`。
- 故障前 `avg_actual_rpm` 可到 `+334rpm`，`balance_ref_rpm` 打到 `+320rpm`，说明外环/内环链路在继续推动车速，而不是刹车。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | `(-1.0f)` | 翻转位置测量方向，使位置误差回中方向正确 |
| `BALANCE_PID_VELOCITY_SIGN` | `1.0f` | `(-1.0f)` | 翻转速度测量方向，使速度外环对已有速度产生反向制动 |

### 下一步验证

- 烧录后仍使用外环诊断 VOFA 通道，只测 `stand` 推车。
- 期望现象：
  - `I14 position_sign=-1`，`I15 velocity_sign=-1`。
  - `avg_actual_rpm` 为正时，`balance_ref_rpm` 应倾向为负。
  - `avg_actual_rpm` 为负时，`balance_ref_rpm` 应倾向为正。
- 如果符号正确但速度仍刹不住，再判断是否需要调轮速响应或外环增益。

## 2026-07-06 拆分测试：先只测速度环极性

### 原因

- 双 sign 翻转后，整车数据仍表现为 `balance_ref_rpm` 与 `avg_actual_rpm` 同向，且触发多次 `fault=3`。
- 由于位置误差、车身大角度和速度误差混在一起，不能继续用整车大扰动直接判断两个 sign。
- 先退回原始符号，并关闭位置 P，只保留速度项，逐个确认极性。

### 临时参数

| 参数 | 当前临时值 | 目的 |
|---|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | 暂时关闭位置回中项，只测试速度外环 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `1.50f` | 保留速度项 |
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | 退回原始符号，后续单独验证 |
| `BALANCE_PID_VELOCITY_SIGN` | `1.0f` | 退回原始符号，先验证速度项 |

### 测试判据

- 仍使用外环诊断 VOFA 16 路通道。
- 只在 `stand` 模式下做小幅推车，不做大角度摔车测试。
- 当前只看速度链：
  - `velocity_mps`
  - `velocity_error_mps`
  - `speed_theta_ref_deg`
  - `balance_ref_rpm`
  - `avg_actual_rpm`
- 若 `avg_actual_rpm` 与 `balance_ref_rpm` 仍长期同向，再只翻 `BALANCE_PID_VELOCITY_SIGN` 测一次。
- 速度 sign 确认后，再恢复 `BALANCE_POSITION_KP_DEG_PER_M` 并单独测位置 sign。

## 2026-07-06 速度极性单独测试结果与位置极性测试配置

### 速度极性测试结果

- `vofa+.csv` 为追加文件，前半段是旧的 `position_sign=-1/velocity_sign=-1` 数据，后半段才是本次 `+1/+1`、`KP_position=0` 的速度单独测试数据。
- 最新 `+1/+1` 段从约 `idx=2485` 开始，持续约 `26.8s`，全程 `fault=0`。
- 速度项表现：
  - `avg_actual_rpm > 0` 时，`speed_theta_ref_deg < 0`。
  - `avg_actual_rpm < 0` 时，`speed_theta_ref_deg > 0`。
- 这说明速度外环的目标角方向符合“对已有速度产生反向重心修正”的预期，暂定 `BALANCE_PID_VELOCITY_SIGN=1.0f` 正确。
- 但由于 `BALANCE_POSITION_KP_DEG_PER_M=0`，没有位置回中项，手感会比之前虚、不愿意回中心；这不代表直立环或速度 sign 错。

### 位置极性测试配置

| 参数 | 当前测试值 | 原因 |
|---|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `1.00f` | 温和恢复位置回中，用于判断位置极性，避免直接回 `2.50f` 太激进 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `1.50f` | 保留已初步确认方向正确的速度阻尼 |
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | 先测试原始位置方向 |
| `BALANCE_PID_VELOCITY_SIGN` | `1.0f` | 暂定速度方向正确 |

### 下一步判据

- 仍使用外环诊断 VOFA 通道。
- 只测 `stand`，小幅推动后让车自己回中。
- 若 `position_m` 偏正时，位置项应推动 `speed_theta_ref_deg` 朝让 `position_m` 回 0 的方向变化；实际判断仍以低速段、非大角度摔车段为准。
- 如果恢复 `KP=1.0` 后出现明显越离中心越加速，再单独翻 `BALANCE_PID_POSITION_SIGN`，不要动 `VELOCITY_SIGN`。

## 2026-07-06 关闭位置环并修复速度环

### 修正判断

- 前面对速度 sign 的判断只看了 `avg_actual_rpm` 与 `speed_theta_ref_deg` 的关系，不够完整。
- 更可靠的判断应看完整链路最终输出：`avg_actual_rpm` 与 `balance_ref_rpm` 是否反向。
- `KP_position=1.0/KV=1.5/sign=+1/+1` 的数据中，`avg_actual_rpm` 与 `balance_ref_rpm` 仍大多数同向，说明速度项进入内层 `theta_error = theta - theta_ref` 后，最终仍表现为助推。

### 当前临时参数

| 参数 | 当前值 | 目的 |
|---|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | 关闭位置项，排除位置回中干扰 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `1.50f` | 只保留速度阻尼 |
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | 位置项关闭，暂不测试 |
| `BALANCE_PID_VELOCITY_SIGN` | `(-1.0f)` | 修复速度项符号，验证是否能反向压速度 |

### 下一步判据

- 烧录后清空 VOFA 数据，只测 `stand` 小幅推车。
- 期望：
  - `I14 position_sign=1`。
  - `I15 velocity_sign=-1`。
  - `avg_actual_rpm > 0` 时，`balance_ref_rpm` 应倾向变负或至少明显小于旧版同向助推。
  - `avg_actual_rpm < 0` 时，`balance_ref_rpm` 应倾向变正或至少明显小于旧版同向助推。
- 若此配置手感明显能刹速度，再恢复位置项并单独测试 `POSITION_SIGN`。

## 2026-07-06 打开位置环并加入加速度阻尼

### 修改目的

- 最新速度单独测试中，`VELOCITY_SIGN=-1` 后速度项方向更合理，但 `theta_ref_deg` 幅度仍偏小，推车后回中/刹停能力不足。
- 本次恢复一个温和的位置回中项，同时增大速度阻尼，并加入基于轮速变化率的加速度阻尼，先让外环具备“速度刹住 + 位置回中”的完整能力。

### 当前参数

| 参数 | 当前值 | 说明 |
|---|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.80f` | 温和打开位置环，不直接回到激进值 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `2.50f` | 增强速度阻尼，让推车后的目标倾角更明显 |
| `BALANCE_POSITION_KA_DEG_PER_MPS2` | `0.10f` | 新增加速度阻尼，抑制速度继续发散 |
| `BALANCE_POSITION_KI_DEG_PER_M_S` | `0.0f` | 暂不打开位置积分，避免低速慢漂时积累过量 |
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | 先使用原始位置方向 |
| `BALANCE_PID_VELOCITY_SIGN` | `(-1.0f)` | 保持速度单独测试后的方向 |

### 加速度阻尼实现

- 使用已带符号的 `measured_velocity_mps` 做差分：
  - `accel_raw_mps2 = (measured_velocity_mps - last_measured_velocity_mps) / dt`
  - 再使用与速度相同的低通系数滤波。
- 外环新增：
  - `accel_error_mps2 = 0 - accel_lpf_mps2`
  - `accel_theta_ref_deg = BALANCE_POSITION_KA_DEG_PER_MPS2 * accel_error_mps2`
- 最终：
  - `speed_theta_ref = Kp*position_error + Kv*speed_error + Ka*accel_error + Ki*position_integral`

### VOFA 通道变更

仍为 16 路外环诊断，但为了观察加速度阻尼，把两个在 `stand` 下基本恒为 0 的参考通道替换掉：

| 通道 | 当前含义 |
|---|---|
| I0 | `theta_deg` |
| I1 | `theta_ref_deg` |
| I2 | `speed_theta_ref_deg` |
| I3 | `position_m` |
| I4 | `accel_mps2` |
| I5 | `position_error_m` |
| I6 | `velocity_mps` |
| I7 | `accel_theta_ref_deg` |
| I8 | `velocity_error_mps` |
| I9 | `balance_ref_rpm` |
| I10 | `avg_ref_rpm` |
| I11 | `avg_actual_rpm` |
| I12 | `avg_cmd_a` |
| I13 | `fault` |
| I14 | `position_sign` |
| I15 | `velocity_sign` |

### 下一步测试

- 烧录后清空 VOFA 数据，只测 `stand`，先小幅推，再中等力度推。
- 重点看：
  - `fault` 是否仍为 0。
  - `theta_ref_deg/speed_theta_ref_deg` 是否能到约 `1~2deg`，但不要长期打满 `3deg`。
  - 推开后 `position_m` 是否能逐步回到 0 附近。
  - `accel_theta_ref_deg` 是否只在速度变化瞬间起作用，不应长期占主要量。
- 如果车开始越推越跑，优先单独翻 `BALANCE_PID_POSITION_SIGN`；如果只是刹不住，优先继续加 `KV` 或 `KA`。

## 2026-07-06 回退为速度环优先调参

### 原因

- 调参顺序应先确认速度外环能把推车后的速度刹住，再打开位置环做回中。
- 如果速度环和位置环同时打开，推车后的数据会混合位置误差、速度误差和车身姿态误差，不利于判断是速度阻尼不足、位置极性错误，还是位置 P 过大。

### 当前速度环测试参数

| 参数 | 当前值 | 说明 |
|---|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | 再次关闭位置回中项 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `2.50f` | 保留较强速度阻尼 |
| `BALANCE_POSITION_KA_DEG_PER_MPS2` | `0.10f` | 保留加速度阻尼 |
| `BALANCE_POSITION_KI_DEG_PER_M_S` | `0.0f` | 关闭积分 |
| `BALANCE_PID_POSITION_SIGN` | `1.0f` | 位置项关闭，暂不判断 |
| `BALANCE_PID_VELOCITY_SIGN` | `(-1.0f)` | 保持当前速度方向 |

### 测试目标

- 只测 `stand`，不要求回到原点。
- 允许 `position_m` 漂走，因为位置环已关闭。
- 重点看推车后：
  - `avg_actual_rpm` 是否能逐步衰减。
  - `theta_ref_deg/speed_theta_ref_deg` 是否能到约 `1~2deg`，但不要长期打满 `3deg`。
  - `fault` 是否保持 0。
  - `accel_theta_ref_deg` 是否只在速度变化瞬间明显。
- 速度环能稳定刹住后，再把 `BALANCE_POSITION_KP_DEG_PER_M` 从 `0.3~0.5` 小幅打开测试位置回中。

## 2026-07-06 速度环增强一次

### 数据依据

- `KP_position=0/KV=2.5/KA=0.10/VELOCITY_SIGN=-1` 的数据中，`fault=0`，`theta_ref_deg` 未长期打满 `±3deg`，说明外环还有增益余量。
- 近直立区间 `|theta| < 1deg` 时，`theta_ref_deg` 平均只有约 `0.2deg`，不足以明显改变内层直立环输出，推车后速度仍压不住。
- 加速度阻尼瞬时最大可到约 `2.2deg`，说明 `KA=0.10` 对瞬态偏猛，但稳定速度段主要仍缺 `KV`。

### 参数修改

| 参数 | 旧值 | 新值 | 目的 |
|---|---:|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | `0.0f` | 继续关闭位置回中，只调速度环 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `2.50f` | `3.50f` | 增强持续速度阻尼 |
| `BALANCE_POSITION_KA_DEG_PER_MPS2` | `0.10f` | `0.05f` | 降低加速度瞬时尖峰，避免手感发冲 |

### 下一步判据

- 仍只测 `stand`，不要求回原点。
- 期望小推后 `theta_ref_deg` 能更接近 `0.5~1.5deg`，中等推可到 `1.5~2.5deg`，但不要长期顶到 `3deg`。
- 如果速度能衰减但开始前后摆，说明 `KV` 接近偏大或直立环 D 需要微调；如果仍刹不住，再考虑继续小幅加 `KV` 或检查轮速环输出余量。

## 2026-07-06 速度环增强后回退

### 数据/手感

- `KV=3.50/KA=0.05` 后，主观手感“不对”。
- 最新追加数据中出现 `fault=3` 段，车身角度最大到约 `40deg`，说明本次增强没有改善速度刹停，反而更容易把直立环带出安全区。
- 近直立低速段 `theta_ref_deg` 仍只有约 `0.2deg`，但大扰动时车身实际角度远大于目标角，继续单纯加 `KV` 不一定能解决问题。

### 回退参数

| 参数 | 旧值 | 新值 | 目的 |
|---|---:|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | `0.0f` | 位置环继续关闭 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `3.50f` | `2.50f` | 回到较温和速度阻尼 |
| `BALANCE_POSITION_KA_DEG_PER_MPS2` | `0.05f` | `0.0f` | 暂时关闭加速度阻尼，避免瞬态项干扰判断 |

### 下一步

- 用纯速度环再测一组：`KP=0/KV=2.5/KA=0/VELOCITY_SIGN=-1`。
- 若仍表现为越推越跑，应优先重新判断 `VELOCITY_SIGN`，不要继续盲目加 `KV`。
- 判断时优先看小角度区间：`|theta| < 1deg` 时，`theta_ref_deg` 是否正在把 `theta_error = theta - theta_ref` 往减小轮速的方向推。

## 2026-07-06 机械零点偏置微调

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_ANGLE_OFFSET_DEG` | `-2.0f` | `-2.5f` | 现场手感/静态直立均值判断重心偏置补偿不足，继续加大机械零点偏置 |

### 下一步测试

- 烧录后不发 `B` 命令，保持默认 `B0`，观察静止直立时 `theta_deg` 均值是否更接近 0、`balance_ref_rpm` 长期均值是否更接近 0。
- 若出现反方向慢漂（车持续往另一侧走），说明过补，回调到 `-2.2f` 附近。

## 2026-07-06 速度外环极性修正回 +1

### 判断依据

- `balance_ref_rpm` 与 `avg_actual_rpm` 同向不能作为正反馈证据：倒立摆是非最小相位系统，刹车时轮子必须先朝速度同方向加速，把车身甩成反向倾角，再靠倾角减速。此前据此把 `VELOCITY_SIGN` 翻成 `-1` 属于误判。
- 符号推导：`VELOCITY_SIGN=-1` 时，正向速度 `v>0` → `measured_velocity=-v` → `speed_error=+v` → `speed_theta_ref=+KV*v`（前倾目标角）→ 车继续加速，是真正的正反馈。
- 数据指纹吻合：`VELOCITY_SIGN=-1` 下把 `KV` 从 `2.5` 加到 `3.5`，结果更快触发 `fault=3`、角度冲到约 `40deg`——增益越大越发散是符号错误的典型特征。
- `VELOCITY_SIGN=+1` 的单独测试数据本来就正确：`avg_actual_rpm>0` 时 `speed_theta_ref<0`（后仰刹车方向）。当时“刹不住”的真实原因是权限不足：近直立段 `speed_theta_ref` 仅约 `0.2deg`，位置环关闭无回中项，另有 `0.01m/s` 速度死区削弱小速度响应。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_VELOCITY_SIGN` | `(-1.0f)` | `1.0f` | 恢复负反馈：正速度产生负（后仰）目标角 |

### 下一步测试

- 保持 `KP_position=0 / KV=2.5 / KA=0`，只测 `stand` 小幅推车。
- 新判据（不再用 `balance_ref` 与轮速同向与否）：
  - `velocity_mps` 与 `speed_theta_ref_deg` 应反向（正速度 → 负目标角）。
  - 推开后 `avg_actual_rpm` 应在 `1~3s` 内单调衰减。
- 极性确认后若刹车仍弱，加 `KV`；此时加大 `KV` 应表现为刹得更狠而不是发散，本身就是符号正确的交叉验证。之后再以 `0.3~0.5` 小幅打开位置环单独验 `POSITION_SIGN`。

## 2026-07-06 VELOCITY_SIGN=+1 验证通过：连续站立 86s

### 数据判断（16 路外环诊断，5722 行，约 114.4s，`position_sign=+1 / velocity_sign=+1`，`KP_position=0/KV=2.5/KA=0`，offset=-2.5）

- 两段闭环：`0~14.3s` 和 `19.1~105.3s`；第二段**连续闭环 86.2s 无故障**，是目前最长稳定段。
- **速度外环极性判据全部通过**：
  - `|vel|>0.05m/s` 的 2246 行中，`velocity_mps` 与 `speed_theta_ref_deg` 反向比例 **100%**，corr=-1.000，回归斜率 -2.500 deg/(m/s)，正好等于 `-KV`，链路完全按设计工作。
  - 推车事件（峰值速度 ~0.3-0.46m/s）后速度大多在 **1~3s 内衰减到峰值 30% 以下**，能刹住。
- 长段整体质量：平均 `|theta|≈1.09deg`，`theta` 均值 -0.02deg（offset=-2.5 补偿合适）；`speed_theta_ref` 最大 2.28deg 未顶满 3deg；电流饱和仅 1 行。
- 位置环关闭下 `position_m` 顶在 ±0.30m 限幅上来回，属预期（无回中项）。
- 两次 `fault=3`（14.3s、105.3s）都是**强推超出动力权限**，不是极性问题：故障前 `balance_ref` 已顶满 ±320rpm、实际轮速 ~287rpm、电流打到 0.35A，theta 在 0.1s 内从 ~10deg 冲过 30deg 保护线。这是权限极限，行为正常。

### 当前结论

- `BALANCE_PID_VELOCITY_SIGN=1.0f` 定案，不再动；"balance_ref 与轮速同向=正反馈"的旧判据正式作废。
- `BALANCE_ANGLE_OFFSET_DEG=-2.5f` 表现合适（theta 长期均值≈0），暂不再调。
- 下一步按既定路线：`BALANCE_POSITION_KP_DEG_PER_M` 从 `0.3~0.5` 温和打开位置环，单独验证 `POSITION_SIGN`。判据：推开后 `position_m` 应缓慢回向 0，而不是顶在 ±0.3m 限幅上不回。
- 抗强推能力受 0.35A 电流限幅约束；若后续需要抗更大扰动，再讨论提限流，位置环验证阶段不动。

## 2026-07-06 打开位置环验证 POSITION_SIGN

### 打开原因（86s 长段数据的位移分析）

- 直立和速度阻尼已成立，但速度不受控回零：把 `velocity_mps` 积分还原成真实位移，86s 内车在 `-0.36m ~ +2.46m` 间游走，净漂移 `+0.51m`。
- 最长一次同方向移动持续 `7.1s`（另有 `5.3s`），对应现场"位移速度还在变大"的手感。
- `position_m` 有 `22%` 时间顶在 `±0.30m` 限幅上——位置反馈看得见但没人用（`KP_position=0`）。
- 速度阻尼只能衰减速度，无法消除稳态漂移；"直走后停车"必须位置闭环。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.0f` | `0.40f` | 温和打开位置回中项，单独验证 `POSITION_SIGN` 极性 |

其余保持：`KV=2.50 / KA=0 / KI=0`，`POSITION_SIGN=+1 / VELOCITY_SIGN=+1`，offset=-2.5。

### 验证判据

- 只测 `stand`，小幅推开约 `0.2~0.3m` 后放手观察。
- **极性正确**：`position_m` 在数秒~十几秒内缓慢回向 0，不再长期顶在 `±0.30m` 限幅上；`position_error_m` 为正时 `speed_theta_ref_deg` 应含正向（朝误差方向倾）分量。
- **极性错误**：车越离中心越加速跑远、`position_m` 快速打到限幅并伴随速度增大 → 只翻 `BALANCE_PID_POSITION_SIGN`，不动 `VELOCITY_SIGN`。
- 若回中方向正确但来回振荡不收敛，说明 `KP=0.40` 相对 `KV=2.5` 偏大，先降 KP 到 `0.25~0.30`，不要动 KV。
- 位置环极性确认后，再考虑放大 `BALANCE_PID_POSITION_LIMIT_M`（当前 0.30m 对"直走"太小）并测 `forward` 直走→`stand` 停车。

## 2026-07-06 位置环 KP=0.40 验证：POSITION_SIGN 正确，限幅成为新瓶颈

### 数据判断（16 路外环诊断，5098 行，约 102s；连续闭环 93.2s）

- **`POSITION_SIGN=+1` 正确，定案**：
  - 11 次 `|pos|>0.15m` 偏移事件中，10 次都回到 `<0.075m`，典型回中时间 `2~6s`（一次慢至 22.4s，一次被结尾故障打断）。
  - `position_m` 过零仅 6 次/93s，回中是单调爬回，没有位置环振荡，`KP=0.40` 相对 `KV=2.5` 不偏大。
  - 通道自洽验证：`speed_theta_ref = 0.40*position_error + 2.50*velocity_error` 拟合符合率 99.8%，链路按设计工作。
- **新瓶颈是 `POSITION_LIMIT_M=0.30`**：
  - `position_m` 顶限幅时间从上组 22% 升到 **42.4%**（推一下就到 0.30m 饱和）。
  - 位置项最大权限只有 `0.40*0.30=0.12deg`，几乎可以忽略；目前回中主要还是靠速度阻尼+这一点点位置项慢慢蹭回来。
  - 每次积分器顶限幅，"原点"就丢一截：不带限幅还原的真实位移净漂 `-1.13m`（范围 -1.99~0.17m）。车回的是"限幅后的假原点"，物理位置仍在漂。
- 直立质量保持：`theta` 均值 -0.13deg，平均 `|theta|=1.18deg`，与上组相当，位置环打开没有付出直立代价。
- 结尾 `fault=3`（93.2s）仍是强推超权限的老模式：`bref` 顶满 -320、电流 ~0.28A、theta 0.1s 内冲过 30deg，与位置环无关。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_POSITION_LIMIT_M` | `0.30f` | `1.00f` | 位置积分器 42% 时间饱和、原点持续丢失；放大后位置项最大权限从 0.12deg 提到 0.40deg |
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.40f` | `0.80f` | 极性和无振荡已确认，提高回中速度；1m 偏移对应 0.8deg 目标角，仍温和 |

### 下一步测试

- 只测 `stand`：小推后车应更快回中（目标 2~3s 内），且 `position_m` 很少再顶 1.0m 限幅。
- 若出现来回过冲/低频摆动（位置过零明显变多），把 KP 退回 `0.5~0.6`。
- 位置回中满意后，测 `forward` 直走 → `stand` 停车：停车判据是 `position_m` 收敛到停车点附近且 `velocity_mps` 归零。

## 2026-07-06 KP=0.40 第二组数据：极性全部定案，进入纯调参阶段

### 数据判断（16 路外环诊断，4114 行，约 82.3s，全程 fault=0；参数仍为 KP=0.40/LIMIT=0.30）

- 本组为轻推测试（上一组是重推），表现明显更干净：
  - `position_m` 顶 0.30m 限幅时间 **0%**（上一组 42.4%），说明轻扰动下 0.30m 限幅未被触及。
  - 6 次 `|pos|>0.15m` 偏移全部回中，回中时间 **1.0~4.1s**。
  - 真实位移（不带限幅积分）净漂只有 `+0.11m`，范围 `-0.13~0.22m`——位置已经锁得住。
  - 平均 `|theta|=0.50deg`，最大仅 2.5deg；位置过零 5 次，无振荡。
  - `speed_theta_ref = 0.40*perr + 2.50*verr` 符合率 100%。
- 结论：**直立环、速度环、位置环三层极性全部正确并定案**（`POLARITY=+1 / VELOCITY_SIGN=+1 / POSITION_SIGN=+1`），遗留问题只剩参数量级：
  1. 重推时 0.30m 限幅饱和导致原点丢失（上一组已暴露）→ 已放大 `POSITION_LIMIT_M` 到 1.00。
  2. 回中速度偏慢（几秒级）→ 已提 KP 到 0.80，本组数据显示 0.40 无任何振荡迹象，0.80 有裕量。
  3. 强推超 0.35A 动力权限触发 fault=3 属物理极限，暂不处理。
- 当前待验证参数：`KP=0.80 / KV=2.50 / KA=0 / KI=0 / LIMIT=1.00`。

### 下一步测试

- 烧录 KP=0.80/LIMIT=1.00 后先重复 `stand` 轻推+重推：
  - 轻推回中时间应从 ~2-4s 缩短；
  - 重推后 `position_m` 不应长时间贴 1.0m，真实原点不再丢失。
  - 若低频前后摆（位置过零明显增多），KP 退 0.5~0.6。
- 然后进入 `forward` 直走 → `stand` 停车测试。

## 2026-07-07 forward 降为爬行速度并修复裸数字模式命令

### 修改原因

- 现场反馈 `forward` 速度仍偏快，希望降到很慢、随时可以停车的程度。
- 之前发送裸 `1`/`0` 没有切换模式：代码只识别 `m1/m0` 或 `forward/stand`，裸数字会落到最后的浮点解析，变成轮速测试目标值，对当前 `BALANCE_PID` 模式基本无效。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD_VELOCITY_MPS` | `0.18f` | `0.06f` | 将 forward 目标速度降为约 6cm/s 的爬行速度 |
| `BALANCE_MODE_FORWARD_VELOCITY_LIMIT_MPS` | `0.35f` | `0.12f` | 限制 forward 速度目标上限，避免后续误设过快 |
| `BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC` | `0.30f` | `0.10f` | 放慢起步斜坡，减少突然前窜 |

### 命令解析修改

- `chassis_serial_cmd.c` 中 `cmd_parse_mode()` 增加裸单字符数字模式识别：
  - `0` = `stand`
  - `1` = `forward`
  - `2` = `stop`
  - `3` = `left`
  - `4` = `right`
- 仍保留原命令：`m0/m1/m2`、`mode 0/1/2`、`stand/forward/stop`。
- 多位数字或小数仍走轮速目标解析，不影响 `50`、`100` 等速度测试指令。

### 验证

- `arm-none-eabi-gcc -fsyntax-only` 已通过：
  - `Application/gimbal/gimbal_task.c`
  - `Application/gimbal/chassis_serial_cmd.c`

### 下一步测试

- 烧录后清空 VOFA，先发 `B0`，再测试：
  - `1`：进入极慢 forward。
  - `0` 或 `2`：随时停车/站立。
- 若 `0.06m/s` 太慢到克服不了地面摩擦，再小幅加到 `0.08f`；若仍感觉快，再降到 `0.04f`。

## 2026-07-07 forward 无明显响应根因：直立执行限幅被压到 20rpm

### 数据结论

- 最新 `vofa+.csv` 中 `1` 命令实际已经生效：
  - `position_error_m` 从约 0 逐步增长到约 `0.93m`。
  - `speed_theta_ref_deg` 从 0 逐步增长到约 `1.07deg`。
  - `position_error_m` 斜率约为 `0.06m/s`，符合当前 `BALANCE_MODE_FORWARD_VELOCITY_MPS=0.06f`。
- 因此问题不是 `forward` 目标没有进入位置/速度外环，而是后级执行权限不足。
- 数据中 `balance_ref_rpm` 长时间被卡在 `±20rpm`，约 `11.7s` 顶限幅；这不是前进速度，而是直立环给轮速内环的目标轮速权限。
- `±20rpm` 对直立/前进都过低，车无法跟上 moving `position_ref`，导致 `position_error_m` 越积越大，最终触发 `fault=3` 大角度保护。
- 触发 `fault=3` 后 `emergency_stop=1`，串口 `0/1/2` 只会改模式，不会重新解锁电机；必须按 KEY 重新解锁。

### 修复内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RUNTIME_SPEED_LIMIT_MIN_RPM` | 无 | `120.0f` | 防止在线命令 `O20/L20` 把直立执行权限压到无法平衡的 20rpm |
| `Balance_SetPidRuntimeParam(... SPEED_LIMIT ...)` | 运行时限幅可夹到 `20rpm` | 最低夹到 `120rpm` | 保留在线调参能力，但禁止危险低限幅 |
| VOFA I14 | `position_sign` | `chassis_mode` | 直接确认 `0/1/2` 是否切换模式 |
| VOFA I15 | `velocity_sign` | `runtime_speed_limit_rpm` | 直接确认直立环执行限幅是否被压低 |

### 新 VOFA 通道末两列

| 通道 | 含义 |
|---|---|
| I14 | `chassis_mode`：`0=stand`，`1=forward`，`2=stop`，`3=left`，`4=right` |
| I15 | `runtime_speed_limit_rpm`：当前直立环输出轮速限幅，正常应接近 `320`，不应再出现 `20` |

### 验证

- `arm-none-eabi-gcc -fsyntax-only` 已通过：
  - `Application/ins/ins_task.c`
  - `Application/gimbal/gimbal_task.c`
  - `Application/gimbal/chassis_serial_cmd.c`

### 下一步测试

- 烧录后重新上电/复位，清空 VOFA。
- 按 KEY 解锁后发：
  - `B0`
  - `1` 进入慢速 forward
  - `0` 或 `2` 停车/站立
- 判断：
  - I14 应从 `0` 变 `1`，再变回 `0/2`。
  - I15 应为 `320` 附近，至少不应为 `20`。
  - 若触发 `fault=3`，先扶正并按 KEY 重新解锁，再继续测。

## 2026-07-07 forward 指令已生效但结构会攒误差后突然冲

### 最新数据结论

- 新版 VOFA 已确认烧录生效：
  - I14 为 `chassis_mode`。
  - I15 为 `runtime_speed_limit_rpm`。
- `vofa+.csv` 中 `1` 命令确实生效：
  - `t=4.70s` 后 I14 从 `0` 变为 `1`。
  - I15 全程为 `320`，直立执行限幅已恢复，不再是 `20rpm`。
- 但 forward 行为仍不对：
  - `1` 后车最初基本不动，`position_error_m` 持续积累。
  - `position_error_m` 从约 `0` 积到 `1.6m` 量级，`speed_theta_ref_deg` 后期达到 `2.7deg` 左右。
  - 车随后突然大幅动作，最终 `theta` 超过 `30deg`，触发 `fault=3`。
- 根因不是命令没进，而是原 forward 结构把 `position_ref` 按速度目标持续积分；低速/静摩擦下车不动时，位置误差会越攒越大，最终变成一次大动作。

### 控制结构修改

| 修改项 | 旧逻辑 | 新逻辑 | 目的 |
|---|---|---|---|
| `BALANCE_MODE_FORWARD_POSITION_LEAD_M` | 无 | `0.35f` | forward 时只保持一个有限的前方跟随点 |
| forward `position_ref` | `position_ref += velocity_ref * dt`，可长期积累误差 | `position_ref` 以斜坡靠近 `measured_position + 0.35m` | 防止车不动时误差无限积累，避免“先没反应、后突然冲” |
| stand/stop `position_ref` | 保持原逻辑 | 不变 | 停车/站立仍锁当前位置 |

### 预期表现

- 发 `1` 后，I14 应变为 `1`。
- `speed_theta_ref_deg` 应在几秒内上升到约 `0.3~0.5deg`，但不应继续积到 `2~3deg`。
- 车如果克服不了静摩擦，应该只是轻微尝试前走，不应继续攒误差到摔车。
- 若仍完全不走，可小幅增加 `BALANCE_MODE_FORWARD_POSITION_LEAD_M` 到 `0.45f`；若仍太冲，则降到 `0.25f`。

## 2026-07-07 改为按测试 bias 实现前进/后退/停止

### 最新 bias 数据结论

- 最新数据中 `1` 命令仍然已经进模式：
  - I14 从 `0` 变为 `1`，即 `chassis_mode=forward`。
  - I15 大部分为 `120`，说明运行时速度限幅被压到最低钳位，但不再是旧的 `20rpm`。
- 用户测试过的 bias 档位：
  - `B0.5`：稳定，但基本不走，`velocity_mps` 接近 0。
  - `B1.0`：能产生明显动作，是当前可用的前进量级。
  - `B2.0`：过猛，短时间内角度明显增大并接近/触发保护，不适合作为默认行走。
- 原 forward 结构仍不符合当前需求：用户希望按测试 bias 直接实现前进/后退/停止，而不是靠移动位置参考。

### 控制结构修改

| 修改项 | 旧逻辑 | 新逻辑 |
|---|---|---|
| `CHASSIS_MODE_FORWARD` | moving `position_ref`/速度目标 | 固定模式 bias `+1.0deg` |
| `CHASSIS_MODE_BACKWARD` | 无 | 固定模式 bias `-1.0deg` |
| `0`/`stop` | `stand/stop` 混用 | 统一为 `stand`，bias 为 0 |
| `2` | 原 `stop` | 改为 `backward` |
| mode 切换 | 可能叠加之前手动 `B` | 切模式时自动 `Balance_SetManualBiasDeg(0)`，防止手动 bias 残留 |
| 行走模式位置环 | 可能积累位置误差 | `position_ref = measured_position`，`position_error=0`，不再攒误差 |
| 行走模式速度环 | 原速度目标前进 | 目标速度为 0，只保留速度阻尼；实际目标角由 mode bias 提供 |

### 当前命令

| 命令 | 含义 |
|---|---|
| `0` / `stand` / `stop` | 停止并站立，模式 bias 清零 |
| `1` / `forward` / `go` | 前进，模式 bias `+1.0deg` |
| `2` / `backward` / `back` / `reverse` | 后退，模式 bias `-1.0deg` |
| `3` / `left` | 左转 |
| `4` / `right` | 右转 |

### 新参数

| 参数 | 当前值 | 说明 |
|---|---:|---|
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `1.0f` | 根据 `B1.0` 测试作为默认前进 bias |
| `BALANCE_MODE_BACKWARD_BIAS_DEG` | `(-1.0f)` | 后退使用对称 bias |

### 验证

- `arm-none-eabi-gcc -fsyntax-only` 已通过：
  - `Application/gimbal/gimbal_task.c`
  - `Application/gimbal/chassis_serial_cmd.c`
  - `Application/ins/ins_task.c`

### 下一步测试

- 烧录后重新上电/复位，清空 VOFA，按 KEY 解锁。
- 直接测试：
  - `1`：应看到 I14=1，`theta_ref_deg` 约 `+1deg` 附近。
  - `0`：应看到 I14=0，`theta_ref_deg` 回到 0 附近并停车。
  - `2`：应看到 I14=2，`theta_ref_deg` 约 `-1deg` 附近。
- 若 `1deg` 仍嫌猛，把 `BALANCE_MODE_FORWARD_BIAS_DEG` 降到 `0.8f`；若仍不走，再回升到 `1.2f`，不要用 `2.0f` 做默认。

## 2026-07-07 最新 bias 数据结论：B0 保持，B1 前进

### 数据范围

- 最新 `vofa+.csv`：约 `16440` 行，按 20ms 估算约 `328.8s`。
- I14 全程为 `0`，说明本组数据没有进入 `chassis_mode=forward`，只是在 `stand` 模式下测试手动 `B` bias。
- I15 全程为 `320`，说明直立执行限幅正常，没有再被压到 `20/120rpm`。

### bias 结论

| bias | 数据表现 | 结论 |
|---:|---|---|
| `B0` | 前 35.4s 稳定，`theta` 均值约 `-0.60deg`，平均速度绝对值约 `0.016m/s` | 当前保持平衡用 `0deg`，不需要额外手动 bias |
| `B0.5` | 有较多运动/扰动段，且后段接近保护边界；不适合作为明确前进指令 | 太小且不干净，不推荐作为前进默认 |
| `B1.0` | 最长稳定段约 `98.9s`，`theta` 平均绝对值约 `0.59deg`，可产生明显动作但整体不失控 | 当前前进默认使用 `+1.0deg` 合理 |
| `B2.0` | 短段可站住，但过猛，历史数据中也接近/触发保护 | 不作为默认前进 bias |
| `B-1.0` | 可作为后退方向参考，但运动扰动比 `B+1.0` 更明显 | 后退先用 `-1.0deg`，若太猛降到 `-0.8deg` |

### 当前建议

- 保持平衡/停止：`bias = 0.0deg`。
- 前进：`bias = +1.0deg`。
- 后退：`bias = -1.0deg` 先试；若后退更冲，改为 `-0.8deg`。
- 如果用户确实发送了 `1`，但 I14 仍为 `0`，说明 `1` 命令没有进入模式解析；下一步应优先查串口发送是否带换行、是否发到当前 VOFA UART、以及是否烧录了裸数字模式命令版本。

## 2026-07-07 config 清理为 bias-only 行走模式

### 修改内容

- 删除/停用容易误导的旧 forward 速度/位置参数：
  - `BALANCE_MODE_FORWARD_VELOCITY_MPS`
  - `BALANCE_MODE_FORWARD_VELOCITY_LIMIT_MPS`
  - `BALANCE_MODE_FORWARD_POSITION_LEAD_M`
- 当前行走模式只由 bias 参数决定：

| 参数 | 当前值 | 含义 |
|---|---:|---|
| `BALANCE_MODE_STAND_BIAS_DEG` | `0.0f` | 停止/保持平衡 |
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `1.0f` | 前进 |
| `BALANCE_MODE_BACKWARD_BIAS_DEG` | `(-1.0f)` | 后退 |

### 验证

- `Application/gimbal/gimbal_task.c` 已通过 `arm-none-eabi-gcc -fsyntax-only`。

## 2026-07-07 最新短数据：B0 仍优先用于站立

### 数据范围

- 最新 `vofa+.csv`：`3660` 行。
- I14 全程为 `0`，说明本组数据没有真正进入 `chassis_mode=1` 前进模式。
- I15 基本为 `320rpm`，直立执行限幅正常。
- fault 分布：`fault=0` 为 `2806` 行，末尾 `fault=3` 为 `854` 行；末尾摔倒段不能用于评价正常站立 bias。

### 关键段

| 段 | bias | 数据表现 | 结论 |
|---|---:|---|---|
| 开头稳定段 | `0deg` | `984` 行，`theta` 均值约 `-0.046deg`，范围约 `-0.922~0.747deg`，平均速度约 `0.010m/s` | `B0` 本身站立最好 |
| 中间测试段 | `-2deg` | `theta` 均值约 `-2.281deg`，最小到 `-5.025deg` | 偏大，不适合作为停止点 |
| 中间测试段 | `-1deg` | `theta` 均值约 `-0.928deg`，平均速度约 `0.053m/s`，平均轮速约 `16rpm` | 会产生运动趋势，不适合作为停止点 |
| 回零前后 | `-1deg -> 0deg` | 回到 `0deg` 前车体已经有约 `0.9m/s` 速度、角度升到 `5deg+`，随后进入 `fault=3` | 这不是 `B0` 静态立不住，而是高速/大角度状态下回零已来不及救回 |

### 当前判断

- 停止/站立仍选 `B0`，不要用 `B1` 当站立点。
- `B1` 是前进偏置；本组最新数据没有出现 `+1deg` 或 `mode=1` 段，不能用这组数据证明 `1` 比 `0` 更适合站立。
- 如果体感觉得 `0` 有轻微偏倒，应优先微调机械零点或 `BALANCE_MODE_STAND_BIAS_DEG` 的小量 trim，例如 `0.1deg` 级别，不应直接改成 `1deg`。

## 2026-07-07 速度/位置外环减速效果判断

### 数据现象

- 在最新短数据的 `B-1` 后段，速度从约 `0.30m/s` 增加到 `0.94m/s`。
- 同时 `speed_theta_ref_deg` 从约 `-1.28deg` 继续变到 `-3.00deg`，`theta_ref_deg` 已经被 `BALANCE_PID_THETA_REF_LIMIT_DEG = 3.0f` 限死。
- `balance_ref_rpm` 从约 `90rpm` 增加到 `320rpm`，末端已经打到 `BALANCE_PID_BALANCE_SPEED_LIMIT_RPM`。

### 判断

- 速度/位置外环不是完全没起作用；VOFA 上能看到它在不断改变 `speed_theta_ref_deg`。
- 但在当前符号和限幅下，它没有形成有效减速，表现为速度越大，`speed_theta_ref_deg` 越负，最终叠加到更负的 `theta_ref_deg`，内环继续给正向轮速去追车体角度。
- 一旦车体角度已经大、轮速参考接近 `320rpm`，直立优先级会压过速度控制，外环已经没有足够权限把车慢慢刹住。

### 下一步优先级

- 不优先继续加大 `BALANCE_POSITION_KV_DEG_PER_MPS`，因为当前已经会打到 `theta_ref` 限幅。
- 优先验证速度/位置外环符号：当前 `BALANCE_PID_POSITION_SIGN = +1`、`BALANCE_PID_VELOCITY_SIGN = +1` 对这段运动表现像同向加速；下一步可试反外环符号或反外环输出符号。
- 行走 bias 也要加速度保护：速度超过小阈值后应自动削弱/取消 bias，避免一直把车推向限幅。

## 2026-07-07 外环符号严格推导与最新 mode=1 数据

### 不能用直立表现反推外环符号

- `B0` 能站住只能证明直立内环、轮速环、角度反馈在小速度附近没有整体反掉。
- 直立环优先级最高：当车身角度偏了，直立环会优先追 `theta_ref`，因此它可以把外环符号问题暂时掩盖掉。
- 外环符号必须从公式求导判断，不能只看“能不能短时间站住”。

### 当前代码公式

```c
speed_error_mps = balance_velocity_ref_ramp_mps - measured_velocity_mps;
position_error_m = balance_position_ref_m - measured_position_m;

speed_theta_ref_deg =
    BALANCE_POSITION_KP_DEG_PER_M * position_error_m +
    BALANCE_POSITION_KV_DEG_PER_MPS * speed_error_mps +
    accel_theta_ref_deg +
    BALANCE_POSITION_KI_DEG_PER_M_S * balance_speed_integral_m;

theta_ref_deg = manual_bias + mode_bias + speed_theta_ref_deg;
theta_error_deg = theta_deg - theta_ref_deg;

balance_speed_ref_rpm =
    (Kp * theta_error_deg + Ki * integral + Kd * theta_rate_dps) *
    BALANCE_PID_POLARITY;
```

当前参数：

| 参数 | 当前值 |
|---|---:|
| `BALANCE_PID_POLARITY` | `+1` |
| `BALANCE_PID_POSITION_SIGN` | `+1` |
| `BALANCE_PID_VELOCITY_SIGN` | `+1` |
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.80` |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `2.50` |
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `35.00` |

### 由公式得到的符号结论

只看 P 项，直立内环输出为：

```text
u = balance_speed_ref_rpm = Kp_angle * (theta - theta_ref)
```

所以：

```text
du / d(theta_ref) = -Kp_angle < 0
```

结论：

- 增大 `theta_ref` 会让轮速参考 `u` 往负方向变化。
- 减小 `theta_ref` 会让轮速参考 `u` 往正方向变化。

如果当前测得车速 `v > 0`，想减速，轮速参考应该往负方向给，也就是需要：

```text
v > 0  =>  theta_ref 修正量应该为正
```

但当前速度外环是：

```text
speed_error = 0 - v = -v
speed_theta_ref = Kv * speed_error = -Kv * v
```

在 `Kv > 0` 时：

```text
v > 0  =>  speed_theta_ref < 0
speed_theta_ref < 0  =>  theta_ref 变小
theta_ref 变小  =>  balance_speed_ref 往正方向变大
```

这和减速需求相反。也就是说，在当前正向速度定义下，速度外环符号是反的。

位置环同理。如果测得位置 `x > 0`，要拉回原点，需要负向轮速，即 `theta_ref` 应为正；但当前：

```text
position_error = 0 - x = -x
position_theta_ref = Kp_pos * position_error = -Kp_pos * x
```

同样会让 `theta_ref` 变小，导致轮速参考往正方向变大，和拉回方向相反。

### 等价修正方式

以下两类改法等价，建议只选一种：

| 改法 | 含义 |
|---|---|
| 把 `BALANCE_PID_POSITION_SIGN` 和 `BALANCE_PID_VELOCITY_SIGN` 从 `+1` 改成 `-1` | 保持外环公式不变，反转进入外环的位置/速度定义 |
| 保持测量 sign 不变，但把外环误差改成 `measured - target` | 反转外环输出符号 |

更小改动是先改：

```c
#define BALANCE_PID_POSITION_SIGN           (-1.0f)
#define BALANCE_PID_VELOCITY_SIGN           (-1.0f)
```

注意：

- 行走 bias 模式里当前 `position_error=0`，所以前进/后退时主要先验证 `VELOCITY_SIGN`。
- 停止/站立模式会用到位置环，因此最终 `POSITION_SIGN` 也应一起改，否则停车拉回方向仍可能反。
- 改完后必须先测 `0` 站立，再测轻推车体，看 `v>0` 时 `speed_theta_ref` 是否变为正、`balance_ref_rpm` 是否往负方向刹车。

### 最新 `vofa+.csv` 数据

- 文件时间：`2026-07-07 11:16:07`。
- 总行数：`8891`。
- I14 分布：`mode=0` 有 `248` 行，`mode=1` 有 `8643` 行。
- fault 分布：`fault=0` 有 `7510` 行，`fault=3` 有 `1381` 行。
- I15 为 `320rpm`，直立输出限幅正常。
- 这次 `1` 命令已经进入控制模式，问题不是“1 没发进去”。

### 最新数据关键段

| 行范围 | mode | 数据表现 | 结论 |
|---:|---:|---|---|
| `0-247` | `0` | `theta` 均值约 `0.017deg`，速度约 `0m/s` | `0` 静态站立正常 |
| `248-1030` | `1` | `theta_ref` 约 `1.000deg`，`theta` 均值约 `0.994deg`，速度约 `0m/s`，轮速参考约 `0rpm` | `1` 已生效，但只是把车体目标角保持在 `1deg`，没有形成稳定前进 |
| `1031-1682` | `1` | 出现速度到约 `0.88m/s` 的运动，随后进入 `fault=3` | 有运动但不可控，不能算完成直走 |
| `3064-4662` | `1` | `theta_ref` 约 `1deg`，速度均值接近 `0`，但有 `-0.8~0.9m/s` 的扰动峰值 | 仍不是稳定直线匀速 |
| `6199-7241` | `1` | `theta_ref` 接近 `3deg`，速度仍接近 `0` | 更大 bias 也可能只是改变静态目标角，不一定产生可控前进 |
| `7243-7304` | `1` | 速度升到约 `0.99m/s`，`balance_ref` 平均约 `199rpm` | 一旦动起来，直立环输出变大，速度没有被温和限制住 |
| `8645-8890` | `1` | 末尾回到约 `1deg`，速度约 `0m/s` | 仍没有稳定直走 |

### 最新判断

- 当前 `mode=1` 命令链路是通的。
- 当前 `+1deg` bias 能改变目标角，但不能可靠地产生稳定直走：要么几乎不走，要么速度起来后进入大扰动/故障。
- 根据公式，速度/位置外环符号在当前测量定义下是反的；这比“看直立能不能站住”更可靠。
- 下一步应先修正外环符号，再继续谈前进 bias 大小；否则加大 bias 只会更容易把直立环推到限幅。

## 2026-07-07 上节符号推导作废：被控对象回归证明三层极性全部正确

### 为什么上节推导是错的（必须记住，第三次踩同一个坑）

- 上节用 `du/d(theta_ref) = -Kp < 0` 推"theta_ref 变小 → 轮速变正 → 与减速需求相反"。这只描述**瞬时**轮速方向，是非最小相位零点：刹车必须先让轮子与车速同向加速，把车身甩成反向倾角，再靠倾角减速。**这与 07-06"速度外环极性修正回 +1"一节作废的旧判据是同一个谬误**。
- 正确推导要分快慢回路：角度环（~0.1s）收敛后 `theta -> theta_ref`；车体保持倾角时物理上必然 `v_dot = g*sin(theta - theta0) - 摩擦`（直立环无法伪造这个关系）。代入外环得慢回路方程 `v_dot = g*c*(bias - theta0) - g*c*Kv*sigma*v`，sigma 为速度测量方向与 theta 方向是否一致——**这是唯一未知量，只能用数据定**。
- sigma=+1 时速度指数收敛，理论时间常数 `tau = 1/(g*c*Kv) = 2.34s`，与 07-06 实测推车衰减 1~3s 定量吻合；sigma=-1 时 86s 站立在数学上不可能。

### 数据计算（8891 行，analyze_polarity.py 可复用）

- 被控对象回归 `a = k1*theta + k2*sgn(v) + b`（n=1302，闭环无故障、|theta|<8、0.03<|v|<0.7）：
  - `k1 = +0.0867 (m/s^2)/deg`，**符号为正 ⇒ sigma=+1，三层符号 `POLARITY/POSITION_SIGN/VELOCITY_SIGN = +1/+1/+1` 全部正确，最终定案**。
  - 真实平衡零点 `theta0 = +2.0deg`（显示坐标）；无摩擦项交叉验证 `theta0 = +1.6deg`。
  - 摩擦等效倾角 `theta_fric = +1.44deg`。
- 独立验证：全数据"轮子不动、电流≈0、v≈0"的停驻段共 108s，theta 中位数 **+2.05deg**，分布 [+0.5, +3.0]，正是以 theta0 为中心的静摩擦死区。
- 直走失败真凶 1（零点）：theta0=+2 时 `B+1` 的有效 bias = -1deg，还在摩擦死区内。数据印证：theta≈1deg 停驻 44.6s（电流 0.025A、实测加速度 +0.004m/s²，若零点真在 0 必须持续 0.17m/s² 加速，物理不可能）；116.5s 段挂着前进命令净位移 **-7.19m**（后溜）。
- 直走失败真凶 2（高速权限天花板）：|v|>0.5 的 619 行中，电流 ≥0.34A 比例 **0%**（摔倒瞬间仅 0.21A）、轮速跟随误差仅 22rpm——内环和电流都不是瓶颈；`theta_ref` 顶 ±3deg 限幅 72%（最大减速度被钉在 g*sin3° ≈ 0.51m/s²）、`balance_ref` 顶 320rpm 30%（轮速上限 1.04m/s 吃不起非最小相位刹车瞬态）。**根本对策不是加强刹车，而是闭环限速不让速度上去。**
- 固定 bias 无法实现爬行的解析原因：巡航速度 `v* = (bias - theta_fric - theta0误差)/Kv`，摩擦（±1.44deg）和零点漂移（±2deg）都比 bias 本身大——这就是"B0.5 不走、B1 时走时停、B2 冲"的公式解。

### 修改内容

| 修改项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---|---|---|
| `BALANCE_ANGLE_OFFSET_DEG` | `-2.5f` | `-0.5f` | 三重估计零点在显示 +1.6~2.0deg，欠补约 2deg |
| `BALANCE_MODE_FORWARD/BACKWARD_BIAS_DEG` | `±1.0f` | `0.0f` | 弃用开环 bias 行走 |
| `BALANCE_MODE_DRIVE_VELOCITY_MPS` | 无 | `0.10f` | 前进/后退改为速度闭环目标，走现有 ramp（0.10m/s²） |
| `Chassis_ModeTargetVelocityMps()` | 恒 0 | forward/backward 返回 ±0.10 | 同上 |
| `BALANCE_POSITION_KI_DEG_PER_M_S` | `0.0f` | `5.0f` | 行走时积分速度误差，自动收敛到克服摩擦+零点残差所需倾角 |
| `BALANCE_POSITION_INTEGRAL_LIMIT_M_S` | `0.20f` | `0.30f` | 积分权限 = 5.0×0.30 = 1.5deg，略高于摩擦角 1.44deg |
| 积分对象 | drive 清零 / stand 积位置误差（Ki=0 时惰性） | **drive 积速度误差 / stand 清零**；模式切换时清积分 | stand 行为完全不变（P+V 照旧），行走获得摩擦自适应 |
| 限速守卫 | 无 | `GUARD_ON=0.40 / GUARD_OFF=0.15 m/s` 迟滞；触发时 `target_velocity=0` 并冻结积分 | 把车速摁在可回收包络内；只改外环速度目标，不触碰直立角度环任何量 |
| VOFA I7 | `accel_theta_ref_deg`（KA=0 恒 0） | `velocity_ref_mps` | 直接观察守卫动作（超速时 v_ref 被拉 0） |
| VOFA I15 | `runtime_speed_limit_rpm`（恒 320） | `integral_theta_ref_deg` | 观察积分收敛到的倾角 |

- 语法验证通过：`gimbal_task.c / ins_task.c / chassis_serial_cmd.c`。

### 下一步测试

1. 烧录后先测 `stand`：静止直立 theta 均值应接近 0（验证新 offset）；轻推应照旧回中（stand 行为未改）。
2. 发 `1`：车应以约 0.1m/s 匀速爬行。看 I7 `velocity_ref` 爬到 0.10、I6 `velocity_mps` 跟上、I15 积分角收敛到某个稳定小值（预计 0.5~1.5deg）、theta 稳定。
3. 发 `0`：应停车站立，积分清零（I15 回 0）。
4. 行走中人为推快：|v|>0.4 时 I7 应被守卫拉回 0，车刹到 0.15 以下后自动恢复爬行，全程 `fault=0`。
5. 若爬行时积分顶满 1.5deg 仍不走，摩擦角比估计大，把 `INTEGRAL_LIMIT` 提到 0.40；若爬行有低频振荡，把 KI 从 5.0 降到 3.0。
6. 零点验证：stand 长站后若 theta 均值仍有固定偏差 x，把 `BALANCE_ANGLE_OFFSET_DEG` 再加上 x（当前 -0.5 是按 theta0≈+2.0 估计的，允许 ±0.5deg 残差）。

## 2026-07-07 守卫积分冻结漏洞修复：改为向 0 衰减

### 问题（审查发现，未烧录先修）

- 上一版守卫触发时把行走积分**冻结**。但起步顶静摩擦阶段（v=0、误差 +0.1m/s、积分速率 0.5deg/s）积分约 3s 就攒满 +1.5deg，车破摩擦窜出去触发守卫时积分正好停在最大助推值。
- 定量：触发瞬间 v=0.4，速度项 2.5×(-0.4)=-1.0deg，冻结积分 +1.5deg，净 theta_ref=**+0.5deg 仍在向前推**；要到 v=0.6m/s 速度项才压得过积分——守卫 ON 阈值被变相从 0.40 抬到 0.6，蹭到不可回收区边缘。
- 另一个小问题：守卫触发后 v_ref 还要走 1s 斜坡才降到 0，白送 +0.25deg 前倾。

### 修改内容

| 修改项 | 旧逻辑 | 新逻辑 | 原因 |
|---|---|---|---|
| 守卫期间积分 | 冻结（保持原值） | 以 `BALANCE_SPEED_GUARD_INTEGRAL_DECAY_M_PER_S = 0.60f` 向 0 衰减，0.5s 内清完 | 消除同向助推；衰减到 0 即停，不会积成反向，windup 保护仍成立 |
| 守卫触发时 v_ref | 走 0.10m/s² 斜坡降到 0（需 1s） | `balance_velocity_ref_ramp_mps` 立即置 0 | 让 Kv 项立刻全额刹车 |

- 语法验证通过：`gimbal_task.c / ins_task.c`。
- 验收补充：行走中推快触发守卫时，I15 `integral_theta_ref_deg` 应在约 0.5s 内滑到 0，I7 `velocity_ref` 应立刻跳 0（不是斜坡下降）。

## 2026-07-07 新固件首测：零点修好、全程无故障，但爬行是 stick-slip 极限环 → 加轮速前馈

### 数据结论（4102 行，82.0s，全程 fault=0）

- **零点修复验证通过**：stand 静立 60.3s，`theta` 均值 **-0.074deg**；停驻段 theta 中位数 -0.02deg。`BALANCE_ANGLE_OFFSET_DEG=-0.5f` 定案。
- **鲁棒性显著改善**：stand 段被推到 0.97m/s 都救回来了，82s 内 0 次 fault（此前同类推速必 fault=3）。
- **forward 段（56.0~68.9s，12.9s）暴露最后一个问题：静摩擦 stick-slip 极限环**：
  - `v_ref` 正常爬到 0.10，积分攒到 1.40deg（50% 时间顶满 1.5deg 上限），`theta` 实际倾 +1.36deg。
  - 但 `v` 均值仅 **0.007m/s**，净位移 0.09m，车原地抖。
  - `v` 在 ±0.2 间振荡，周期约 1.5s，12.9s 内 17 次过零；corr(积分, v) = -0.47，积分和速度互相追逐。
  - 机理：积分攒倾角→顶破静摩擦→车窜一下(v 到 0.25)→超过 0.1 目标,Kv 刹车+积分回退→轮子重新粘住→v 回 0→再攒。积分权限 1.5deg 与静摩擦角 1.44deg 几乎相等，永远在临界点蹭。
- 守卫未触发（v 最大 0.25 未到 0.4），本组数据不能验证守卫。

### 修改内容：行走轮速前馈

| 修改项 | 逻辑 | 原因 |
|---|---|---|
| `drive_wheel_ff_rpm` | 行走模式把 `v_ref` 换算轮速（`v_ref * VELOCITY_SIGN / RPM_TO_MPS`，0.10m/s≈31rpm）直接加到左右轮目标 | 让轮速内环（自带积分+静摩擦前馈，S50 已验证）破静摩擦持续滚动；外环倾角只做平衡/加减速修正，不再靠攒倾角顶静摩擦 |
| 与守卫的配合 | 守卫触发时 ramp 已被置 0，前馈随之立即消失 | 超速时前馈不会继续推车 |

- 语法验证通过：`gimbal_task.c`。

### 下一步测试

- 烧录后发 `1`：预期 `v`（I6）平滑跟上 0.10 并保持，不再 ±0.2 振荡；`theta` 应接近 0（不再需要 1.4deg 倾角），I15 积分收敛到很小的值（只补动摩擦残差）。
- I10 `avg_ref_rpm` 应能看到 ~31rpm 的前馈基线（平衡修正叠加在上面）。
- 发 `0`：前馈消失，位置锁生效，应平稳停车。
- 若爬行平稳，可把 `BALANCE_MODE_DRIVE_VELOCITY_MPS` 逐步提到 0.15/0.20 测试；若起步仍有一次小窜动，把 `BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC` 从 0.10 降到 0.05。
- 顺带补测守卫：行走中推快过 0.4m/s，看 I7 跳 0、I15 在 0.5s 内滑 0、刹回 0.15 后自动恢复爬行。

## 2026-07-07 14:34 数据：offset 不再调，B 恢复 0，轮速前馈仍未测

### 数据结论（6401 行，128s，全程 stand 模式）

- 122s 连续直立无故障，净位移仅 +0.06m。
- 用户中途发过 `B-2`（77s）和 `B-1.5`（93s）；无偏置段（70s）theta 均值 **-0.21deg**。
- 被控对象回归 theta0 = -0.7~-0.8deg（噪声大），与上组（14:21，残差≈0）不一致 → 剩余为会话间漂移（电池/温度/地面），不是固定误差。
- **决定：`BALANCE_ANGLE_OFFSET_DEG = -0.5f` 不再调**。残差 |0.2~0.8deg| 小于静摩擦死区 ±1.3deg，行走 KI 积分和 stand 位置环本来就自动吸收这个量级；追固定值会重蹈 yaw bias 追参的覆辙。
- `B-2` 方向过头（把 theta 拉到 -1.8，比不加偏置离零点更远），手动 B 恢复 0。
- **本组无 forward 段，轮速前馈（治 stick-slip 的关键改动）仍未验证**；下一步直接发 `1` 测爬行。

## 2026-07-07 前馈版首测反馈"太快、倾角收不住"：降速降斜坡

### 现场反馈

- 轮速前馈版烧录后发 `1`，现场感受"跑太快、倾角收不住"。
- 该次数据未导出（vofa+.csv 仍是 14:34 的 stand 数据），具体是超速还是 pitch 振荡待 CSV 确认。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_DRIVE_VELOCITY_MPS` | `0.10f` | `0.06f` | 爬行目标降到 6cm/s，前馈基线从 ~31rpm 降到 ~18rpm |
| `BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC` | `0.10f` | `0.04f` | 起步斜坡放缓到 1.5s，减小前馈建立时车身的非最小相位后仰扰动 |

### 下一步测试

- 烧录后发 `1` 复测；**务必导出这次的 CSV**，若仍"倾角收不住"，用数据区分：
  - v 超过 0.06 多少（速度环问题）；
  - theta 振荡频率/幅度（角度环与前馈交互问题）；
  - I15 积分是否在前馈存在时仍大幅摆动（KI 与前馈打架，则降 KI 5→2）。

## 2026-07-07 现场确认 B-1.5 才立得住：offset 改 -2.0，发现零点会话间漂移

### 现场证据与数据吻合

- 现场：`offset=-0.5` 下 `B0` 立不稳，`B-1.5` 才立得住。
- 与 14:34 数据一致：停驻分布中位数 `-0.76`、p10 `-2.22`，被控对象回归零点 `-0.7~-0.8deg`，用户当时试的 `B-2` 略过头、`B-1.5` 正好。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_ANGLE_OFFSET_DEG` | `-0.5f` | `-2.0f` | 现场 `B-1.5` 等效固化进零点（-0.5 + -1.5 = -2.0） |

### 重要发现：零点存在 ±1~1.5deg 会话间漂移

- 换算到 raw 角度：上午会话真实零点 raw ≈ `-0.5deg`（当时显示 +2.0），下午会话 raw ≈ `-2.0deg`（显示 -1.5）——几小时内漂了约 `1.5deg`。
- 疑似加速度计温漂（1deg 倾角 ≈ 17mg，BMI088 加速度计温漂可达此量级）或电池/线缆等机械变动。
- **约定：若下次开机又漂，不要再重烧固件追常数**（与 yaw bias 追参同一教训）。两个替代方案：
  1. 短期：开机直立后用 `B` 命令做会话级 trim；
  2. 长期：实现 stand 模式慢速零点自学习（静止直立时 theta 均值就是真零点，用很慢的积分伺服内部 bias 跟上去）。

### 下一步测试

- 烧录后 `B0` 应直接立得住（等效旧 `B-1.5`）。
- 然后继续测 `1`（爬行 0.06m/s + 轮速前馈）并导出 CSV。

## 2026-07-07 14:49 数据定位"发1不动"：倾角积分与轮速前馈死锁，KI 归零

### 数据结论（783 行，15.7s，全程 fault=0）

- stand 段 theta 均值 `-0.011deg`：offset=-2.0 本会话完全正确。
- forward 13.8s：**v = 0.000，轮子从未转动**。链路各环节：
  - `v_ref` 正常爬到 0.057（斜坡工作正常）。
  - 积分顶满 1.5deg（59% 时间），`theta_ref` 被抬到 +1.29deg。
  - 车身实际 theta = +0.81deg，theta - theta_ref = -0.48deg。
  - 角度环输出 35 × (-0.48) ≈ **-17rpm**，与前馈 **+17.5rpm** 精确抵消，轮子净指令 ≈ 0，静摩擦锁死。
- 死锁机理：车不动 → 速度误差不消 → 积分抬 theta_ref → 角度环输出更负 → 恰好吃掉前馈 → 车更不动。系统收敛到"谁也不动"的静态平衡。
- 同一矛盾在动态下就是振荡——上次"跑太快倾角收不住"（数据未导出）与此同源。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KI_DEG_PER_M_S` | `5.0f` | `0.0f` | KI 是为"纯倾角顶摩擦"方案设计的；前馈架构下摩擦由轮速内环破，倾角积分与前馈死锁，数据已证明有害。平地爬行不要再打开 |

### 预期行为（KI=0 后）

- 发 `1`：theta_ref 只剩 Kv 项（起步约 2.5×0.06=0.15deg，随速度接近目标趋于 0）。
- 角度环起步输出约 -5rpm，前馈 +17.5rpm，净 +12.5rpm → 轮速内环（静摩擦前馈+积分）破摩擦起转。
- 巡航平衡点：轮子 ~18rpm、v=0.06、速度误差 0、theta_ref≈0、theta≈0，直立姿态爬行。
- 起步瞬间车身会先轻微后仰零点几度再跟上（非最小相位，属正常）。

## 2026-07-07 结构性收尾：零点自学习 + 巡航速度移入平滑滚动窗口

### 判断（回应"不是顶不动摩擦就是倾角大了救不回"）

- 两个正交根源，各给一个结构性解法，不再手拧参数：
  1. **"自己立不住" = 零点会话间温漂**（raw 零点上午 -0.5deg / 下午 -2.0deg，几小时漂 1.5deg）→ 零点自学习。
  2. **"不动或窜" = 目标速度在低速楼板以下**。楼板成分（无刷电机）：齿槽转矩（磁性静摩擦）、减速箱/轴承摩擦、驱动器电流死区、**CAN 速度反馈噪声（峰峰 13~25rpm，18rpm 目标淹没在噪声里，闭环无从谈起）**。0.06m/s(18rpm) 同时踩着摩擦楼板和反馈噪声楼板；0.20m/s(62rpm) 起步电流 0.108A 直接击穿破断力矩，反馈信噪比 3~5 倍。
- 轮胎与地面的摩擦不是楼板（它是"滚动不打滑"的约束，是好东西）；数据里轮子从未打滑，只有"锁死不转"。

### 零点自学习实现

- 原理：静止直立时车实际站住的角度**就是**物理零点（力学定义）。学习不需要"站得好"，只需要"偶尔停驻"——零点错 1deg 的 14:34 数据里停驻时刻累计 79s，样本充足；正向棘轮（每次停驻拉近零点→站得更省力→停驻更多）。
- 实现（`gimbal_task.c`，约 15 行）：`theta_deg -= balance_zero_trim_deg`；门控满足时 `trim += 0.02 * theta * dt`（时间常数 50s，与直立环 ~1s 解耦），限幅 ±2deg，整个会话保留、不随 fault 清零，掉电回到 `BALANCE_ANGLE_OFFSET_DEG` 基准。
- 门控（全部满足才学习）：stand 模式、|theta|<3deg、|theta_rate|<10dps、|v|<0.03m/s、左右轮 |rpm|<5。被推/行走/摔倒全冻结；若零点错到永远无法停驻（±2deg 外），学习保持冻结，安全失效。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_DRIVE_VELOCITY_MPS` | `0.06f` | `0.20f` | 移入平滑滚动窗口 [0.15, 0.35] 中间；守卫 0.40 封顶 |
| `BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC` | `0.04f` | `0.08f` | 2.5s 到巡航；过慢的斜坡会让起步长时间停留在楼板区 |
| `BALANCE_ZERO_TRIM_*` | 无 | ENABLE=1, RATE=0.02, LIMIT=2.0, 门控 V=0.03/RATE=10/THETA=3/WHEEL=5 | 零点自学习 |
| VOFA I15 | `integral_theta_ref_deg`（KI=0 后恒 0） | `zero_trim_deg` | 观察自学习收敛 |

- 语法验证通过：`gimbal_task.c / ins_task.c / chassis_serial_cmd.c`。

### 下一步测试

1. 烧录后 KEY 解锁站立 1~2 分钟：看 I15 `zero_trim_deg` 从 0 缓慢滑向某个稳定值（量级 ±0.5deg 内），同时 I0 `theta` 均值趋向 0，车踱步减少。
2. 发 `1`：v_ref 2.5s 斜坡到 0.20，预期 v 平滑跟随、theta 接近 0 直立姿态巡航、不再"不动或窜"。
3. 发 `0`：平稳停车。
4. 行走中推快过 0.4：守卫拉 v_ref 到 0，刹回 0.15 后自动恢复。
5. 全程导出 CSV。

## 2026-07-07 15:23 数据：站立/守卫成熟，前馈抵消死锁第二形态（车身后仰替代积分）

### 数据结论（4177 行，83.5s，全程 fault=0）

- 站立成熟：stand 29.6s theta 均值 +0.088deg；零点自学习只学 0.01deg（本会话零点本就准确）。
- **守卫实战验证通过**：63.9s 车被冲到 1.02m/s、theta +5deg、轮子 309rpm 全力追，守卫按住 v_ref=0 后救回，无 fault。历史最大速度偏移且未摔倒。
- **forward 不走，死锁第二形态**（26.9s 准静止段数字自洽）：
  - 车身后仰停在 theta=-1.20deg，theta_ref=+0.46deg（纯 Kv 项）。
  - 角度环输出 35×(-1.66)=-58rpm，前馈 +56rpm，净 -2rpm（实测 avg_ref=-2rpm、轮子 -0.8rpm），落在静摩擦死区。
  - KI 时代由积分抬 theta_ref 提供抵消，KI=0 后由**车身自己后仰**提供抵消——同一个不动点，换了吸收变量。

### 机理定性（本质）

- 角度环单一输出承担两件事（纠正 theta + 消化前馈）不可兼得：扶正后仰车身需要轮子向后滚，前馈持续 +56rpm，两者互相抵消；净指令 ±2rpm 在静摩擦死区内，**死锁点被静摩擦稳定**。
- "直立巡航 0.2m/s（theta≈0、轮 62rpm）"是合法平衡点，但从静止出发到不了——中间隔着死锁坑。
- 教程车前馈加在力矩/PWM 级不会被角度环抵消；我们的前馈加在轮速指令级，与角度环输出同级必然打架。**轮速级速度前馈 + 角度环输出轮速指令的组合存在结构性冲突。**

### 候选修法（未实施，待选）

| 方案 | 思路 | 评价 |
|---|---|---|
| A. 速度解耦 | 轮速指令 = 实测车速换算轮速 + 角度环输出；角度环只管"轮子相对车身"修正，速度调节全交 Kv→theta_ref | 轮速伺服拓扑标准写法，根治；实现前需推一遍 v 正反馈路径稳定性 |
| B. 起步踢脚 | 进 forward 时 0.3~0.5s 反向轮速脉冲把车身甩前倾，跳过死锁坑 | 最小改动，补丁不根治 |
| C. 回纯串级（去前馈） | theta_ref 驱动 | 轮速伺服下巡航需 62/35=1.77deg 恒定角差，速度由摩擦角决定，控不准，不推荐 |

## 2026-07-07 急停/故障后强制回 stand

### 漏洞修复

- 原逻辑在 KEY 急停、角度/角速度保护、反馈/使能故障时只切急停输出，没有把 `chassis_mode` 退回 `CHASSIS_MODE_STAND`。
- 风险：若车在 `forward/backward` 中触发急停或故障，扶正后再次按 KEY 解锁会沿用旧行走模式，可能直接继续前进/后退，而不是先站住。

### 修改内容

| 修改项 | 旧逻辑 | 新逻辑 |
|---|---|---|
| KEY 运行中按下 | `emergency_stop = 1` | 先 `Chassis_SetControlMode(CHASSIS_MODE_STAND)`，再急停 |
| 角度/角速度保护 | 只急停并 reset PID | 先强制回 stand，再急停并 reset PID |
| 电机反馈/使能故障 | 只急停 | 先强制回 stand，再急停 |

### 验证

- `Application/gimbal/gimbal_task.c` 已通过 `arm-none-eabi-gcc -fsyntax-only`。
- 下一次测试：在 `1` 前进中按 KEY 急停，再按 KEY 解锁，I14 应先保持 `0`，不会自动恢复 `1`。

## 2026-07-07 LAUNCH 起步阶段：先给目标倾角，后接速度前馈

### 背景

- 当前 baseline 已本地提交：`3e3a56e Record balance drive tuning baseline`。
- 远程 GitHub push 被执行环境策略拦截，当前本地分支为 `main...origin/main [ahead 1]`。
- 15:23 数据说明 forward 卡住不是单纯参数问题，而是轮速前馈和直立环轮速修正在同一级相互抵消：`drive_ff≈+56rpm`，`balance_ref≈-58rpm`，净指令约 `-2rpm`，落进静摩擦死区。

### 本次结构修改

进入 `forward/backward` 后新增一个短暂 `LAUNCH` 阶段：

| 阶段 | 持续 | 行为 |
|---|---:|---|
| LAUNCH | `0.40s` | `v_ref=0`，速度 ramp 清零，速度积分清零，轮速前馈关闭，只给 `theta_ref=±1deg` |
| DRIVE | LAUNCH 后 | 恢复原来的速度 ramp、Kv 外环和轮速前馈 |

目的不是关闭直立环，而是把直立环的目标点短暂移动：前进先让车主动追 `+1deg`，后退追 `-1deg`，先跳出静摩擦死锁坑，再进入速度控制。

### 测试方法

1. 烧录后 KEY 解锁，只测 `stand` 10~20s，确认 `fault=0`，I0 `theta` 在小范围内，I15 `zero_trim_deg` 缓慢稳定。
2. 发 `1` 前进，重点看前 0.4s：
   - I1 `theta_ref_deg` 应约为 `+1.0deg`。
   - I7 `velocity_ref_mps` 应保持 `0`。
   - I10 `avg_ref_rpm` 不应出现约 `+56rpm` 的速度前馈基线。
3. 0.4s 后继续观察：
   - I7 应按斜坡升向 `+0.20m/s`。
   - I10 才开始出现速度前馈对应的正向轮速指令。
   - 若车能越过静摩擦坑，应看到实际轮速/速度连续起来，而不是长期贴近 0。
4. 发 `0` 停车，确认能回 stand。
5. 若 launch 一开始明显往反方向倒、轮子反抽，或 `theta` 快速越过 `2~3deg` 还继续扩大，立刻 KEY 急停，导出 CSV；下一步优先翻转 `BALANCE_MODE_LAUNCH_BIAS_DEG` 的符号或缩短 launch 时间。

### 编译验证

- `Application/gimbal/gimbal_task.c` 已通过 `arm-none-eabi-gcc -fsyntax-only -Wall`。
- `git diff --check -- Application/gimbal/gimbal_task.c Inc/config.h process.md` 通过。

## 2026-07-07 16:14 LAUNCH 数据：起步有效，但 DRIVE 仍被固定前馈吸回死锁

### 数据结论

- 新数据 `vofa+.csv` 共 6818 行，`fault=0` 全程无保护。
- 安静近直立 stand 段：`theta` 均值约 `-0.125deg`，`zero_trim` 仅约 `-0.11deg`，所以这次看到的 forward 后仰不是 IMU 零点突然偏了。
- 第一次 forward 的 LAUNCH 前 0.4s 有效果：`theta_ref=+1deg`，实际 `theta` 到约 `+2.37deg`，速度到约 `0.13~0.19m/s`。
- 但进入 DRIVE 后重新掉入同一个抵消点：
  - 后期 `theta≈-1.25deg`，`theta_ref≈+0.50deg`。
  - `theta-theta_ref≈-1.75deg`，角度环 `balance_ref≈-61rpm`。
  - 固定速度前馈 `drive_ff≈+61.6rpm`。
  - `avg_ref≈0rpm`，`avg_actual≈0rpm`，车又被静摩擦锁住。

### 结构修改

- `BALANCE_MODE_DRIVE_USE_ACTUAL_BASE = 1`。
- DRIVE 段不再把 `v_ref / RPM_TO_MPS` 作为固定轮速前馈加到轮速目标。
- 新合成：

```c
drive_base_rpm = 0.5f * (left_rpm + right_rpm);
wheel_ref = drive_base_rpm + balance_ref + yaw_ref;
```

- 含义：速度目标只通过外环改变 `theta_ref`；轮速内环的基底跟随当前实测轮速，直立环输出只做相对修正。
- 目的：在刚才的死锁点，`balance_ref=-61rpm` 不会再被固定 `+61rpm` 抵消，轮速目标会真正离开 0。

### 下一次测试

1. 烧录后先 stand 10~20s，确认 `fault=0`、`theta` 接近 0、`zero_trim` 没有快速跑偏。
2. 发 `1`：
   - LAUNCH 前 0.4s 仍应看到 I1 `theta_ref≈+1deg`，I7 `v_ref=0`。
   - 0.4s 后 I7 继续爬向 `+0.20m/s`。
   - 关键检查：I10-I9 不应再固定等于 `+61rpm`，而应大致等于 I11 `avg_actual_rpm`。
3. 若车仍不走，看后期是否还有 `avg_ref≈0`：
   - 若 I9 已给出明显轮速目标但实际不动，问题转向轮速内环/电流/静摩擦。
   - 若 I9 方向导致车反向走，下一步查 `theta_ref` 行走符号，而不是再调角度 offset。

## 2026-07-07 16:27 数据：停车慢，原因是停车初段仍给同向轮速

### 数据结论

- 新数据共 2723 行，`fault=0`。
- `1 -> 0` 发生在 `t=40.64s`，切回 stand 时：
  - `velocity_mps≈+0.093`
  - `avg_actual≈+26.7rpm`
  - `theta_ref≈-0.232deg`
  - `balance_ref/avg_ref≈+17rpm`
- 问题：停车初段虽然目标轮速比实际小，但仍是正向目标；轮速 PID 还会加正向静摩擦前馈，所以前 0.8s 不是强刹，体感像“按 0 后还往前带一下”。数据里速度一度升到约 `0.157m/s`，约 2s 才基本停稳。

### 修改

- `BALANCE_MODE_DRIVE_VELOCITY_MPS: 0.20 -> 0.15`，降低行走动能；可以接受 `1` 时慢一点，换更容易停车。
- `WHEEL_SPEED_FF_DISABLE_WHEN_BRAKING = 1`：当目标和实际同向、且目标绝对值小于实际值时，关闭同向静摩擦前馈，避免“减速时还帮它推”。
- 模式切换时调用 `Chassis_ResetSpeedCtrl()`，清掉轮速 PI 积分和上次输出，避免 `1 -> 0` 延续旧输出。
- 新增 `STOP_BRAKE`：
  - 只在 `forward/backward -> stand` 后启动。
  - 最多持续 `0.80s`，速度低于 `0.02m/s` 提前退出。
  - 轮速目标叠加 `stop_brake_rpm = -1.5 * velocity / RPM_TO_MPS`，限幅 `±90rpm`。

### 预期

- 用 16:27 停车瞬间回放：
  - 旧：`avg_ref≈+17rpm`，实际 `+26.7rpm`，仍是正向小目标。
  - 新：制动项约 `-43rpm`，合成后 `avg_ref≈-26rpm`，会立刻给反向制动。
- 下一次测试：
  1. 发 `1` 后速度目标应爬向 `0.15m/s`。
  2. 发 `0` 后 I10 `avg_ref_rpm` 应在前 0.8s 明显反向，不应继续跟实际速度同向小幅正转。
3. 期望停车时间明显短于 2s；若反向过冲太大，再降低 `BALANCE_MODE_STOP_BRAKE_GAIN` 或 `LIMIT_RPM`。

## 2026-07-07 16:36 数据：STOP_BRAKE 被完整直立环压过，导致越刹越快

### 数据结论

- 新数据共 3334 行，`fault=0`。
- 只有两段很短的 forward：第一次约 `1.44s`，第二次约 `0.82s`，`v_ref` 还没爬到 0.15，但停车后速度仍明显上升。
- 第一次 `1 -> 0` 后：
  - `0.00s`: `vel=+0.044`，旧合成 `avg_ref=+2.5rpm`，仍不是反向刹车。
  - `0.72s`: `vel=+0.243`，`theta=+4.607deg`，完整直立环 `balance_ref=+186.5rpm`。
  - 当时 `STOP_BRAKE` 已经给到约 `-90rpm`，但被 `+186.5rpm` 压过，合成后 `avg_ref=+96.5rpm`，轮子继续正转，速度继续涨。
  - `0.80s` 后制动计时结束，直立环仍正向追车，最大速度冲到 `0.856m/s`，随后车身大角度摆动再被拉回。
- 第二次同理：`0.72s` 左右 `vel=+0.155`，`balance_ref=+135rpm`，旧合成仍 `avg_ref=+63.7rpm`。

### 结论

- 停车慢不是制动项没生效，而是**停车阶段完整直立环优先级过高**。
- 车身前倾时直立环为了扶正，会命令轮子继续向前追车身；这在停车阶段等价于继续加速。
- 因此停车必须短暂“刹车优先”：允许牺牲一部分直立环跟随，不让它完整抵消反向制动。

### 修改

- `BALANCE_MODE_STOP_BRAKE_GAIN: 1.50 -> 2.00`。
- `BALANCE_MODE_STOP_BRAKE_LIMIT_RPM: 90 -> 160`。
- 新增 `BALANCE_MODE_STOP_BRAKE_BALANCE_SCALE = 0.25`。
- `STOP_BRAKE` 激活时：

```c
balance_ref *= 0.25;
stop_brake_rpm = -2.0 * velocity / RPM_TO_MPS;
stop_brake_rpm = limit(stop_brake_rpm, ±160rpm);
avg_ref = balance_ref + stop_brake_rpm;
```

### 回放预期

| 数据点 | 旧 `avg_ref` | 新估算 `avg_ref` |
|---|---:|---:|
| 第一次 `0` 后 0.00s | `+2.5rpm` | `-21.4rpm` |
| 第一次 `0` 后 0.08s | `+59.4rpm` | `-18.2rpm` |
| 第一次 `0` 后 0.72s | `+96.5rpm` | `-103.1rpm` |
| 第二次 `0` 后 0.72s | `+63.7rpm` | `-61.7rpm` |

- 下一次测试如果反向过冲明显，先降 `BALANCE_MODE_STOP_BRAKE_GAIN` 到 `1.50`，不要恢复完整直立环优先级。

## 2026-07-07 改为脉冲行走：走一小段，刹一小段

### 判断

- 可以考虑直走减速，但当前连续速度环仍会被直立环优先级影响：速度一起来后，车身前倾，直立环会命令轮子继续追车身，导致减速目标被吃掉。
- 现阶段先不追求连续匀速，把 forward 降级成“脉冲爬行”更稳：允许走一小段、刹一小段、再走一小段。

### 修改

- `BALANCE_MODE_PULSE_DRIVE_ENABLE = 1`。
- forward 进入后仍保留 LAUNCH。
- LAUNCH 后进入 DRIVE 窗口：
  - 最多 drive `0.35s`。
  - 或者 `|velocity_mps| > 0.10m/s` 时提前进入 brake。
- PULSE_BRAKE：
  - 最多 brake `0.70s`。
  - `|velocity_mps| < 0.03m/s` 时提前恢复 drive。
  - brake 期间复用 STOP_BRAKE：`v_ref=0`、轮速基底关闭、直立环输出降到 25%、反向轮速阻尼生效。

### 对 16:36 数据的预期

- 第一次 forward：旧数据约 1.10s 才超过 `0.10m/s`，新逻辑会先被 `0.35s` drive 窗口切入 brake。
- 第二次 forward：旧数据约 0.42s 就超过 `0.10m/s`，新逻辑会提前切入 brake。
- VOFA 观察：
  - I14 仍为 `1`，因为还在 forward 命令中。
  - I7 `velocity_ref_mps` 会呈现 `0 -> ramp -> 0 -> ramp -> 0` 的节拍。
  - I10 `avg_ref_rpm` 在 brake 窗口应明显反向，不能继续跟着实际速度同向加速。

## 2026-07-07 16:53 数据：脉冲存在，但后续 drive 太弱，肉眼像只蠕动一次

### 数据结论

- 当前目录只有 `vofa+.csv`，未看到单独的 `vofa+1.csv`；本节分析的是 `2026/7/7 16:53:16` 的 `vofa+.csv`。
- 第三段 forward 最能说明问题：
  - 第一次蠕动 `t=69.04~70.12s`：
    - `theta_ref` 最高 `1.000deg`
    - `avg_ref` 最高 `66.2rpm`
    - `avg_actual` 最高 `22.0rpm`
    - `cmdA` 最高 `0.104A`
    - 所以能明显动一下。
  - 后面 `t=70.52~83.64s`：
    - `v_ref` 仍在脉冲，最高 `0.028m/s`
    - 但 `theta_ref` 只有 `0~0.083deg`
    - `avg_ref` 只有 `-4.5~5.6rpm`
    - `avg_actual` 只有 `-3.1~3.8rpm`
    - `cmdA` 只有约 `±0.027A`
- 结论：状态机不是只执行一次；后续确实在 pulse，但每个 drive 窗口太弱，破不了静摩擦，所以看起来像“蠕动一次就不动了”。

### 原因

- 当前 drive 窗口 `0.35s`，速度 ramp 只有 `0.08m/s/s`：

```text
最大 v_ref = 0.35 * 0.08 = 0.028m/s
theta_ref 约 = 2.5 * 0.028 = 0.07deg
balance_ref 约 = 35 * 0.07 = 2.5rpm
```

- 稳住之后车身角度很小，直立环只给几 rpm 的修正；再加上 `DRIVE_USE_ACTUAL_BASE=1` 时基底跟随实测轮速，车停住后基底也接近 0，于是后续 pulse 没有真正的起步推力。

### 修改：真正做成“走一段、停一段、再走一段”

- 初始 LAUNCH 降低，避免再次把角度顶炸：
  - `BALANCE_MODE_LAUNCH_TIME_SEC: 0.40 -> 0.20`
  - `BALANCE_MODE_LAUNCH_BIAS_DEG: 1.0 -> 0.6`
- 每个 drive 段加入最小行走基底：
  - `BALANCE_MODE_PULSE_DRIVE_BASE_RPM = 25.0`
  - drive 段目标变成 `actual_avg_rpm + 25rpm`，不再只靠几 rpm 的角度修正破静摩擦。
- brake 段不允许速度一低就立刻退出：
  - `BALANCE_MODE_PULSE_BRAKE_MIN_TIME_SEC = 0.20`
  - 先至少刹 `0.20s`，之后才允许按 `|v| < 0.03m/s` 提前结束。
- brake 后新增 hold 段：
  - `BALANCE_MODE_PULSE_HOLD_TIME_SEC = 0.15`
  - hold 段 `v_ref=0`、无 drive base、完整直立，形成肉眼可见的“停一下”。

### 下一次测试看什么

1. 发 `1` 后，I7 仍应呈现 `0 -> ramp -> 0 -> hold -> ramp`。
2. I10 在 drive 段不应再只有几 rpm，而应至少比 I11 多约 `+25rpm`。
3. brake 段 I10 应明显反向；hold 段 I10 应回到直立维持的小量输出。
4. 如果仍只动一下，看后续 drive 段 I10-I11 是否真的有约 `25rpm` 差值；若有差值但轮子不动，再把 `BALANCE_MODE_PULSE_DRIVE_BASE_RPM` 加到 `35rpm`。

## 2026-07-07 17:07 数据：节拍有了，但 drive 时间太短，实际没走起来

### 数据结论

- 新数据 `vofa+.csv` 时间为 `2026/7/7 17:07:17`。
- forward 段 `t=67.98~101.78s`，持续 `33.82s`。
- 检测到 `48` 个 drive 窗口：
  - drive 窗口 `0.32~0.34s`，平均 `0.33s`。
  - 周期约 `0.70s`。
  - drive 间隔/刹停约 `0.36~0.38s`。
- 稳定段 `t=68.72~100.00s`：
  - `theta=-1.53~1.02deg`，姿态基本稳。
  - `velocity=-0.037~0.051m/s`，速度没有越跑越快。
  - 但 drive 段实际轮速平均只有 `1.3rpm`，整体仍接近原地抖动。
- 结论：状态机节拍是对的，但 `0.33s` 太短，轮子刚破静摩擦就被切去刹车，体感上还是没走起来。

### 另外一个风险

- 最后 `t=101.60s` 开始角度突然发散：

```text
t=101.40s theta=0.523deg, v=0.035, avgref=57.7rpm
t=101.60s theta=7.034deg, v=0.032, avgref=49.1rpm
t=101.70s theta=14.862deg
t=101.80s theta=30.441deg, fault=3
```

- 当时 `v_ref=0`，说明在 brake/hold 相关阶段；旧逻辑会把直立环压到 25% 并叠加反向刹车，角度偏大时扶正能力不够。

### 修改

- `BALANCE_MODE_PULSE_DRIVE_TIME_SEC: 0.35 -> 0.70`。
  - 理论最大 `v_ref` 从 `0.028m/s` 提高到 `0.056m/s`。
  - 目标是让轮子有时间从破静摩擦进入实际滚动，而不是刚动就刹。
- 新增 `BALANCE_MODE_BRAKE_RECOVER_ANGLE_DEG = 2.0`。
  - brake/stop_brake 阶段若 `|theta| > 2deg`，取消反向刹车和 `balance_scale=0.25`。
  - 仍保持 `v_ref=0`、无 drive base，但让完整直立环优先扶正。
- drive base 条件增加 `drive_pulse_brake_active == 0`：
  - 即使因为角度保护取消了 stop_brake，也不会在 brake 阶段重新加 `+25rpm` 行走基底。

### 下一次测试看什么

1. I7 的 drive 窗口应变成约 `0.67~0.70s`，不是 `0.32~0.34s`。
2. I11 `avg_actual_rpm` 在每个 drive 段应明显高于上一版，最好能稳定超过 `10~20rpm`。
3. I0 若在 brake/hold 中超过 `2deg`，I10 不应继续强反向刹车，而应转为完整直立环扶正输出。
4. 若仍不走，再考虑把 `BALANCE_MODE_PULSE_DRIVE_BASE_RPM` 从 `25rpm` 提到 `35rpm`；先不同时加时间和推力，避免过冲。

## 2026-07-07 继续加大走路窗口

### 判断

- `0.70s` drive 窗口仍然没明显走起来，说明轮子可能刚破静摩擦、刚开始滚动就进入 brake/hold。
- 这一步先只加走路时间，不同时加 `drive_base`，避免无法判断到底是时间不足还是推力不足。

### 修改

- `BALANCE_MODE_PULSE_DRIVE_TIME_SEC: 0.70 -> 1.20`。
- 理论 `v_ref` 峰值：

```text
1.20s * 0.08m/s/s = 0.096m/s
```

- 仍低于 `BALANCE_MODE_PULSE_BRAKE_ON_MPS = 0.10m/s`，所以正常情况下由时间窗口结束进入 brake；如果实际速度提前超过 `0.10m/s`，仍会提前刹车。

### 下一次测试看什么

1. I7 drive 窗口应接近 `1.20s`，除非 I6 提前超过 `0.10m/s`。
2. I11 在 drive 后半段应明显高于 `0.70s` 版本；如果仍低于 `10rpm`，下一步才考虑把 `BALANCE_MODE_PULSE_DRIVE_BASE_RPM` 提到 `35rpm`。
3. I0 角度如果超过 `2deg`，应优先关注站立/倾角稳定性，不要继续加大走路推力。

## 2026-07-07 18:32 数据：走路时间足够，但倾角走法和轮速基底互相抵消

### 数据结论

- 新数据 `vofa+.csv` 时间为 `2026/7/7 18:32:09`。
- forward 段 `t=4.18~37.46s`，持续 `33.30s`，`fault=0`。
- drive 窗口已经接近 `1.18s`，说明 `BALANCE_MODE_PULSE_DRIVE_TIME_SEC=1.20s` 生效。
- I7 `v_ref` 最大 `0.096m/s`，但实际：
  - `velocity=-0.030~0.059m/s`，平均约 0。
  - `avg_actual=-8.8~18.8rpm`，平均约 0。
- 关键矛盾：
  - I9 `balance_ref` 平均约 `-17.9rpm`。
  - I10 `avg_ref` 平均约 `-0.8rpm`。
  - 说明 `+25rpm drive_base` 大量被直立环反向输出抵消。

### 典型点

```text
t=5.18s  theta=-0.738, theta_ref=0.165, vref=0.064
balance_ref=-31.2rpm, avg_ref=-7.0rpm, actual=-0.8rpm

t=10.18s vref=0.092
balance_ref=-25.8rpm, avg_ref=-1.0rpm, actual=-0.2rpm
```

- 结论：不是时间不够，而是走法不对。当前同时使用：
  1. 速度外环改变 `theta_ref`。
  2. 轮速基底 `actual + drive_base`。
- 这两者在当前零点/姿态下互相抵消，最终轮速目标接近 0。

### 修改：先尝试方案 A，纯轮速爬行

- `BALANCE_MODE_LAUNCH_ENABLE: 1 -> 0`。
  - 不再用短暂倾角 launch。
- 新增并设置 `BALANCE_MODE_DRIVE_THETA_REF_ENABLE = 0`。
  - forward/backward 中速度误差不再生成 `theta_ref`。
  - I7 仍保留作 pulse 节拍观察，但不再参与倾角走路。
  - drive 模式清掉速度积分，避免无效积累。
- `BALANCE_MODE_PULSE_DRIVE_BASE_RPM: 25 -> 35`。
  - 上一版 `25rpm` 被约 `-20~-30rpm` 的直立修正吃掉；纯轮速爬行先用 `35rpm` 做更明确的测试。
- 预期：
  - forward 时 I1/I2 `theta_ref` 应接近 `0`，不再随 I7 抬到 `0.1~0.2deg`。
  - drive 段 I10-I11 应更接近 `+35rpm` 的目标差。
  - 如果 I10 明显正而 I11 仍不动，再查轮速内环/摩擦/电流限制；如果 I0 角度变差，再回头处理站立零点和姿态鲁棒性。

## 2026-07-07 18:43 数据：方案 A 生效，但被约 -1deg 倾角零点偏差抵消

### 数据结论

- 新数据 `vofa+.csv` 时间为 `2026/7/7 18:43:21`。
- forward 段 `t=33.00~105.54s`，持续 `72.56s`。
- 方案 A 已生效：
  - I1/I2 `theta_ref` 全程接近 `0`。
  - drive 窗口约 `1.18s`。
  - `fault` 基本没有。
- 但仍没走起来：

```text
forward all: thetaAvg=-0.859deg
drive only : thetaAvg=-0.989deg
drive only : balance_refAvg=-35.1rpm
drive_base : +35rpm
avg_refAvg : -0.1rpm
actualAvg  : 0rpm
```

- 结论：纯轮速爬行本身没错，但当前真实平衡点在 `theta≈-1deg` 左右。直立环为了把车拉回 `0deg`，持续输出约 `-35rpm`，刚好抵消 `+35rpm` 走路基底。

### 修改：采用方法二，临时补偿目标角

- 暂时把直立目标补到 `-0.8deg`：

```c
BALANCE_MODE_STAND_BIAS_DEG   = -0.8deg
BALANCE_MODE_FORWARD_BIAS_DEG = -0.8deg
BALANCE_MODE_BACKWARD_BIAS_DEG= -0.8deg
```

- 这不是最终标定方案，只是验证“零点偏差抵消走路基底”这个判断。
- 保留方案 A：
  - `BALANCE_MODE_DRIVE_THETA_REF_ENABLE = 0`
  - `BALANCE_MODE_PULSE_DRIVE_BASE_RPM = 35`

### 下一次测试看什么

1. I1/I2 应在 stand/forward 中接近 `-0.8deg`。
2. drive 段 I9 `balance_ref` 平均值应从约 `-35rpm` 靠近 `0rpm`。
3. drive 段 I10-I11 应能留下明显正差值，理想接近 `+20~35rpm`。
4. 如果车能慢慢走，说明根因就是倾角零点偏差；之后再把这个 `-0.8deg` 做成正式的零点标定/慢速自学习，而不是永久硬编码。

## 2026-07-07 18:52 数据：-0.8deg 偏置正确，但 +35rpm 基底被直立环 1:1 精确抵消——加性轮速基底结构性走不通

### 数据结论

- 新数据 `vofa+.csv` 时间为 `2026/7/7 18:52`，共 `51.1s`，全程 `fault=0`。
- stand 段（4.1s）：`theta=-0.790`，`tref=-0.808`，`bref=+0.7rpm`，`zero_trim=-0.169`。
  - **-0.8deg 偏置本身是对的**：站立时角误差≈0，直立环输出≈0。真实物理平衡点确实在 -0.8deg 附近。
- forward 段 `t=4.10~51.08s`（47.0s），drive 窗口 31 个、累计 34.4s、每个约 1.13s。
- 逐窗口拉出 balance_ref / drive_base / avg_ref / 实际轮速，31 个窗口全部同一模式：

```text
             theta   tref | bref   base   aref   arpm
窗口#0-30均值 -1.84  -0.80 | -36.3  +35.0  -2.2   -0.9
典型窗口#12   -1.83  -0.80 | -35.9  +35.0  -1.7   -0.8
```

- 关键数字关系（不是巧合，是反馈平衡点）：
  - drive 中 theta 精确停在 `-1.84deg = tref - base/角环DC增益`（实测增益 `-36.3rpm / -1.04deg ≈ 35rpm/deg`）。
  - 即直立环**主动把车身多压倒 1.04deg**，用这个角误差制造出 -36rpm，恰好吃掉 +35 基底。
  - 时间维度上没有任何演化：forward 前 10s 与后 17s 的 theta / bref 均值完全一致，31 个窗口方差极小——系统稳稳锁死在这个抵消点上。
- 与历史对照，这是加性基底第三次被完整吃掉：
  - 18:32：base=25rpm，bref≈-25。
  - 18:43：base=35rpm（偏置错 1deg），bref≈-35。
  - 18:52：base=35rpm（偏置已校对），bref≈-36。
- **结论：给轮速目标加常数基底的"纯轮速爬行"结构性走不通。** 直立环对任何加性 DC 项有无穷稳态权威：车身只要多倾 `base/35deg` 就能完全抵消它，而多倾一点对直立环毫无成本。继续加大 base 只会让车身被压得更斜（base=70 → 车身 -2.8deg），永远走不动。

### 修改：撤掉轮速基底，改为倾角差驱动（重力走路）

- 平衡车前进的物理通道只有一个：**车身前倾离开平衡点 → 重力产生前进加速度 → 轮子追着车身滚**。方向核对（用本次数据闭环验证符号）：
  - `bref ≈ +35 × (theta - tref)`，且 +rpm 基底把 theta 压向更负 → 轮子前滚使 theta 减小 → **theta 正方向 = 前倾**。
  - 因此前进应把 `theta_ref` 从平衡点向正方向抬。
- 参数修改（`Inc/config.h`）：

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_PULSE_DRIVE_BASE_RPM` | `35.0f` | `0.0f` | 被 1:1 抵消且附带 -1deg 寄生倾角，撤掉 |
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `-0.8f` | `-0.4f` | 相对平衡点前倾 +0.4deg，重力加速度 ≈0.068m/s² |
| `BALANCE_MODE_BACKWARD_BIAS_DEG` | `-0.8f` | `-1.2f` | 对称后倾 |

- 保持不变：`STAND_BIAS=-0.8`、`DRIVE_THETA_REF_ENABLE=0`（隔离变量，先测纯开环倾角差）、`LAUNCH_ENABLE=0`、pulse 节拍全套。
- 量级估算：0.4deg 前倾 → 0.068m/s²，1.2s drive 窗口末端 ≈0.08m/s，接近 `PULSE_BRAKE_ON_MPS=0.10`——节拍和倾角量级正好咬合：每窗口爬 ~5cm，超速则由 brake 兜底。
- 语法验证通过：`gimbal_task.c`（arm-none-eabi-gcc -fsyntax-only）。

### 下一次测试看什么

1. 发 `1` 后 I1 `theta_ref` 应变为 `-0.4`；drive 段 I0 `theta` 应从 -0.8 向 -0.4 抬起并越过。
2. I9 `balance_ref` 在 drive 段应变**正**（轮子前滚追车身），而不是这轮的 -36。
3. I6 `velocity` 每个 drive 窗口末端应爬到 0.05~0.10m/s，I11 `avg_actual` 应有明显正段——车应该真的向前蠕动。
4. brake 段车身带着 -0.4 目标刹车，观察是否刹得住（I6 能否回到 0.03 以下）；若 brake 中前倾导致越刹越走，下一步把 bias 门控到 drive 子相位。
5. 若 0.4deg 走不动（静摩擦楼板），先加到 0.6deg 再评估，不要回头加轮速基底。

## 2026-07-07 19:24 数据：0.4deg 前倾被静摩擦整个吃掉——平衡点不是一个点，是一条摩擦带

### 数据结论

- 新数据 `vofa+.csv` 共 `34.5s`，全程 `fault=0`。stand 段 `theta=-0.816`，`tref=-0.810`，`bref=-0.2rpm`。
- forward 段 25.3s，`tref=-0.40` 已生效，抵消现象消失，但车仍不动：

```text
17 个 drive 窗口均值: theta=-0.38  tref=-0.40  bref=+0.7rpm  arpm=+0.0
时间演化: 前10s bref=+1.2 -> 中10s +0.4 -> 后段 +0.1（单调衰减到贴 0）
```

- 车身确实从 -0.8 抬到了 -0.38，**精确追上目标角**——但轮子全程没转。0.4deg 的重力回复力矩远小于静摩擦，车身直接"靠在摩擦上"静止平衡。
- 与 stand 段对照可得关键物理事实：**车身在 -0.82 和 -0.38 两个角度都能零轮速静稳。平衡点不是一个点，而是一条静摩擦带**（历史实测宽度约 ±1.4deg）。任何小于分离角的固定前倾 bias 都只会挪到带内另一个位置站着，这条路和加轮速基底一样结构性走不通。
- `zero_trim` 从 -0.169 漂到 -0.261，也印证零点本身就有摩擦带宽度的模糊性。

### 复盘：已验证可用与不可用的拼图

| 拼图 | 证据 | 结论 |
|---|---|---|
| LAUNCH 甩出摩擦坑 | 16:14 数据：theta 到 +2.37deg、v 到 0.19m/s | **有效**，车真的动了 |
| 平滑滚动窗口 0.15~0.35m/s | 15:23 摩擦分析 | 巡航必须在窗口内 |
| 加性轮速基底 | 18:32/18:43/18:52 三次被 1:1 抵消 | 结构性无效 |
| 固定小角度前倾 | 本次 19:24 | 摩擦带内无效 |
| pulse 蠕动（brake_on=0.10） | 把车永远压在楼板以下 | **与平滑窗口结论自相矛盾**，弃用 |
| launch→drive 交接 | 代码检查发现：launch 期间斜坡清零，结束后从 0 重爬 2s，Kv 项把已滚动的车刹回坑里 | 16:14 失败的另一半原因，本次修复 |

### 修改：回到 LAUNCH + 速度闭环巡航，修复交接斜坡

- 结构修改（`gimbal_task.c`）：launch 结束一拍，把 `balance_velocity_ref_ramp_mps` 用当前实测速度接力（限幅在 0~巡航目标之间），不再从 0 重爬。
- 参数修改（`Inc/config.h`）：

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD/BACKWARD_BIAS_DEG` | `-0.4/-1.2` | `-0.8/-0.8` | 固定前倾已证伪，回真实平衡点，走路倾角交给速度外环 |
| `BALANCE_MODE_LAUNCH_ENABLE` | `0` | `1` | 16:14 验证过的破摩擦手段 |
| `BALANCE_MODE_LAUNCH_TIME_SEC` | `0.20` | `0.40` | 恢复 16:14 验证值 |
| `BALANCE_MODE_LAUNCH_BIAS_DEG` | `0.6` | `1.0` | 恢复 16:14 验证值（相对 bias，实际 tref=+0.2） |
| `BALANCE_MODE_DRIVE_THETA_REF_ENABLE` | `0` | `1` | 巡航维持只能靠 Kv→theta_ref（自适应倾角），KI 保持 0 |
| `BALANCE_MODE_PULSE_DRIVE_ENABLE` | `1` | `0` | 蠕动节拍把车压在楼板下，弃用 |
| `BALANCE_MODE_DRIVE_VELOCITY_MPS` | `0.15` | `0.20` | 回平滑窗口中间（15:23 结论的原值） |

- 保持：`DRIVE_USE_ACTUAL_BASE=1`（巡航自洽：aref=arpm+bref，bref 只做相对修正，没有可被抵消的固定项）、`PULSE_DRIVE_BASE_RPM=0`、守卫 0.40 全套。
- 语法验证通过：`gimbal_task.c`（-Wall -fsyntax-only 无警告）。

### 下一次测试看什么

1. 发 `1` 前 0.4s：I1 `theta_ref≈+0.2`（-0.8+1.0），I7 `v_ref=0`，车身应前甩、轮子跟滚，I6 冲到 0.1~0.2m/s。
2. **交接拍（关键）**：0.4s 后 I7 应直接出现在 ≈当前实测速度（0.1~0.2），而不是从 0 爬。若 I7 从 0 爬 → 接力代码没生效。
3. 巡航段：I6 应稳在 0.20 附近，I0 `theta` 接近 -0.8 直立，I9 `bref` 小幅正负修正、不再有固定 -35 之类的 DC 项。
4. 若 launch 后车又停回去：看停下瞬间 I7 与 I6 的差——若 I7<I6（斜坡拖后腿）加大 `VELOCITY_RAMP`；若 I6 掉进 0.15 以下后粘住，说明巡航目标还得往窗口中上调。
5. 发 `0` 停车观察 STOP_BRAKE 是否还能刹住（pulse 关闭不影响 stand 刹车路径）。
6. 若 launch 甩太猛（theta 冲过 3deg 或守卫频繁触发），先降 `LAUNCH_BIAS` 到 0.8，不要动巡航结构。

## 2026-07-07 19:44 数据：launch 已触发但 1.0deg 甩角到不了分离角；手推证实楼板以下动量必死

### 数据结论（3168 行，63.4s，全程 fault=0）

- 两次 forward 进入（t=1.34s、t=27.00s）launch 都正确触发：`tref` 跳到 `+0.20`（-0.8+1.0），`v_ref=0`。
- **但轮子全程没破摩擦**。逐拍看 launch 段：

```text
t=27.00 theta=-0.84 tref=+0.20 bref=-36.9 arpm=0     <- 角度环全力甩车身
t=27.08 theta=+0.18 tref=+0.20 bref=+0.1  arpm=+1.8  <- 0.08s 车身就追上了
t=27.24 theta=+0.88(过冲峰值)              arpm=+3.1  <- 轮子始终 <4rpm
t=27.40 交接: tref 跳回 -0.80, v=0, 斜坡种子=0
```

- 车身干净地追到 +0.2~+0.9，相对平衡点前倾峰值仅 ~1.1~1.7deg 瞬时、稳态 ~1.0deg，**小于 ~1.4deg 分离角**——车身又是在摩擦带内换个姿势站着。16:14 launch 能把 v 甩到 0.19 是因为当时角度环欠调、theta 过冲到 +2.37deg；现在角度环调好了不过冲，1.0deg bias 反而永远到不了分离角。**甩角幅度必须自己给够，不能靠过冲。**
- 交接后稳态：`vref` 满斜坡 0.20，`tref=-0.33`（= -0.8 + Kv·0.2 = -0.8+0.5），车身追上 tref 后静止——**Kv 项 DC 权限封顶 0.5deg，同样在摩擦带内**。这是死锁第四形态：不是被抵消，而是权限不足。
- **手推段（44s/48s/52s/57s 四次）非常关键**：推到 v=0.04~0.09 时控制器方向正确且在使劲（aref-arpm≈+10rpm 净向前），但 0.09 < 平滑窗口下沿 0.15，粘滑摩擦把动量吃光，每次都停回摩擦带。楼板以下的动量必死，与 15:23 窗口结论定量一致。

### 权限预算表（本次数据钉死的核心矛盾）

| 控制路径 | 最大倾角权限 | 对比分离角 1.4deg |
|---|---:|---|
| launch bias（旧） | 1.0deg | 不够 |
| Kv 巡航项 | 2.5×0.2=0.5deg | 不够 |
| KI（已删） | 曾 1.5deg 但导致粘滑振荡 | 弃用 |

### 修改

| 位置 | 修改 | 原因 |
|---|---|---|
| `BALANCE_MODE_LAUNCH_BIAS_DEG` | `1.0` → `2.2` | 必须越过分离角 1.4deg，留 0.8deg 余量 |
| `BALANCE_MODE_LAUNCH_EXIT_MPS`（新增） | `0.15` | 破摩擦成功（v 进平滑窗口）立即提前退出 launch |
| `gimbal_task.c` launch 块 | 加速度早退判断（方向感知），早退同拍走斜坡接力种子 | 防止 tref=+1.4 继续把车加速到守卫线 0.40 |

- 语法验证通过（-Wall -fsyntax-only 无警告）。

### 下一次测试看什么

1. 发 `1` 后 launch 段：I1 `tref` 应为 `+1.4`（-0.8+2.2），I0 `theta` 应冲过 +0.6（相对前倾 >1.4deg），**这次 I11 轮速必须转起来**（>20rpm）、I6 冲过 0.15。
2. I6 过 0.15 的瞬间 launch 应立即结束：I1 跳回 -0.8 附近，I7 直接从 ≈0.15 起步（接力种子）。
3. 巡航段 I6 稳 0.20 附近；若 I6 缓慢阴跌破 0.15 后粘死，说明 Kv 权限不足以补滚动损耗——下一步把 `BALANCE_POSITION_KV` 从 2.5 提到 5.0（0.2m/s 误差给 1.0deg），或接受楼板巡航速度抬到 0.25。
4. 若 launch 0.4s 内 I6 仍没过 0.15（bias 2.2 还不够或时间不够）：先延长 `LAUNCH_TIME` 到 0.6s，再考虑 bias 2.8。
5. 若 launch 甩出后车身回摆过猛（theta 冲过 -3deg 或触发守卫）：降 bias 到 1.8 并把 `LAUNCH_EXIT_MPS` 降到 0.12。
6. 手推验证捷径：巡航段用手把车推过 0.15m/s 松手——若能持续滚动，证明巡航自洽成立，问题只剩起步；若仍停，问题在巡航维持（Kv 权限），优先改 Kv。

## 2026-07-07 20:02 数据：起步/巡航明显改善；STOP_BRAKE 从未生效——激活门被速度噪声一拍抖穿

### 数据结论（1973 行，39.5s，全程 fault=0）

- 两次 forward（3.56s、25.02s）launch 均成功甩出：`tref=+1.4` 生效，v 冲到 0.08~0.20，斜坡接力种子工作正常（vref 从实测速度起步）。肉眼"好很多"与数据一致。
- 巡航段仍是粘滑极限环：v 在 ±0.1 之间以 ~2.5s 周期打摆，`v>0.10` 占比仅 4%/1%，净位移 +0.08m/-0.05m。与 19:44 预判一致——**Kv=2.5 的 DC 权限 0.5deg 顶不进 1.4deg 摩擦带**。
- **刹车（用户不满意点）量化**：

```text
stop#1: 发0时v=+0.067 -> 先加速冲到+0.146(1.0s) 前冲0.143m -> 回滚-0.076 回撤0.112m -> 4.1s 才稳
stop#2: 发0时v=+0.016(几乎已停) -> 仍被推着冲到+0.109(1.9s) 前冲0.160m -> 回滚-0.084 回撤0.121m -> 6.4s 才稳
```

- **STOP_BRAKE 两次都从未生效**：stand 段每一拍 `aref-bref = 0`（刹车生效应看到 bref×0.25 加 -30rpm 级反向项）。根因逐拍钉死：
  - 激活条件要求 `|v|>0.02`，且旧逻辑"任一拍不满足就把 0.8s timer 清零"。
  - stop#2 切换第 1 拍 `|v|=0.016<=0.02` → timer 当拍作废；stop#1 第 2 拍 `|v|=0.017` 同样中招。
  - 速度信号噪声带 ±0.03，0.02 的门槛泡在噪声里，刹车窗口必死。
- 无刹车时的停车动力学：发 0 后 tref 跳到 -0.9（Kv 项），车身还停在 drive 姿态 -0.16（前倾 0.7deg），角度环只能以 +30rpm 级慢慢向前加速轮子把车身转回去——**前冲 0.14~0.16m、1~2s 就是这个"懒转身"的代价**，随后车身又摆过头到 -1.9 造成回滚。

### 修改

| 位置 | 修改 | 原因 |
|---|---|---|
| `gimbal_task.c` 刹车窗口逻辑 | timer 在 stand 下无条件递减走完；连续低速 0.15s（新 `LOWV_EXIT_SEC`）才提前结束；超角度只暂停本拍输出不清窗口 | 修复"噪声一拍抖穿即永久失效" |
| `gimbal_task.c` 新增姿态域刹车 | 刹车窗口内 `speed_theta_ref += -8.0×v`（限幅 ±1deg，新 `STOP_BRAKE_KV/TREF_LIMIT`） | 倒立摆减速唯一物理路径是先把车身转到平衡点后方；给足后仰权限让转身 0.3~0.4s 完成，替代 Kv 慢蹭 2s |
| `STOP_BRAKE_GAIN` | `2.0` → `0.0` | 轮级反转会把前倾车身推得更前倾（16:36 已证），与姿态域刹车打架 |
| `STOP_BRAKE_BALANCE_SCALE` | `0.25` → `1.0` | 直立环权限恢复满额，刹车不再靠削直立环 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `2.5` → `5.0` | 19:44 预案：巡航 DC 权限 0.5→1.0deg，破粘滑极限环 |

- 语法验证通过（-Wall -fsyntax-only 无警告）。

### 下一次测试看什么

1. 巡航（Kv=5.0）：I6 是否能稳住 0.15~0.20 而不再 ±0.1 打摆。若还打摆但幅度变小 → Kv 再往 7.0；若出现 1s 级快速振荡 → Kv 回 4.0。
2. 发 `0` 刹车：切换后 I1 `tref` 应立刻出现 -0.8-8×v 的深后仰（v=0.1 时约 -1.6），车身 0.3~0.4s 转过平衡点，前冲距离应明显小于 0.14m，整体 2s 内稳住。
3. 若刹车后仰过猛（theta 冲过 -2.5deg 或回滚变大）：`STOP_BRAKE_KV` 8.0 → 5.0，或 `TREF_LIMIT` 1.0 → 0.7。
4. 若停稳前有小幅点头抖动：`LOWV_EXIT_SEC` 0.15 → 0.25，让窗口早退更保守。
5. Kv=5.0 会同步增强 stand 的抗扰（回滚捕捉更快），注意 stand 是否因速度噪声出现可见的 tref 抖动（±0.03×5=±0.15deg）；有则考虑给速度进 Kv 前加一级更强 LPF。

## 2026-07-07 21:12 数据：刹车修好了；Kv=5.0 越过稳定边界，一次向后摔车

### 数据结论（3797 行，75.9s，fault=0，v 范围 ±0.39）

- **姿态域刹车生效且干脆**（本轮成功项）：
  - stop#1（v0≈0.21 行进中发 0）：tref 瞬间打到 `-2.63`（=-0.8 + Kv·(-0.166)·5 + 刹车项 -1.0，公式吻合），v 0.21→0.03 仅 ~0.15s。上一轮 1~2s 前冲 14cm 的"懒转身"消失。
  - stop#2（近停发 0）：0.04s 收尾、零前冲，连续低速早退正常。
- **Kv=5.0 过热，两处独立证据**：
  1. stop#1 刹完后 stand 进入慢速极限环：±0.1m/s 来回爬、漂移 ±0.2m，~9s 才静：位置项 KP=0.8deg/m（0.2m 误差仅 0.16deg）锚不住，Kv 项在速度噪声上追。
  2. fwd#3 摔车：40~64s 粘在摩擦带内安静站着（theta≈+0.2 = -0.8 + Kv 满权限 1.0deg，**5.0 的 DC 权限仍顶不进 1.4deg 摩擦带**）；66s 一次大激励（疑似手推示范）后速度振荡发散：+0.39↔-0.38 来回甩、两次都擦着 0.40 守卫线未触发，t=70 车身 -21deg 向后倒。Kv=2.5 时代同级激励只有 ±0.1 有界摇摆。
- **launch 可靠性问题坐实**：fwd#2 的 launch 中 tref=+1.4 正常、车身甩到 +2.75（远超分离角），但轮子只到 9.7rpm 就缩回——车身绕轮轴转过去又倒回来，轮内环没破摩擦。launch 目前看运气：fwd#1 走了 0.25m，fwd#2 空转。
- **结构性结论**：巡航靠 Kv 的 DC 倾角顶静摩擦封死——稳定上限（≤2.5~3）只给得出 ~0.75deg，摩擦带要 1.4deg，要么不稳要么不动，没有中间解。

### 决策：走路步态化

launch（破摩擦）与姿态刹车（收速）都已被数据验证，走路改为步态循环：**launch 顶出楼板 → 滚动段 Kv 维持 → 速度衰减到阈值以下自动补一发 launch**。与旧 pulse 蠕动的本质区别：旧版主动刹到 0.10 以下把车压进楼板；新版用验证过的 launch 把车顶出楼板。launch 失败（如 fwd#2）时步态自动重试。

### 修改

| 参数/位置 | 旧 | 新 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `5.0` | `3.0` | 回到稳定域；破摩擦职责移交步态,Kv 只管滚动段调速和阻尼 |
| `BALANCE_MODE_LAUNCH_RETRIGGER_ENABLE`（新） | - | `1` | 步态开关 |
| `BALANCE_MODE_LAUNCH_RETRIGGER_BELOW_MPS`（新） | - | `0.08` | 方向速度低于此值开始计时 |
| `BALANCE_MODE_LAUNCH_RETRIGGER_DWELL_SEC`（新） | - | `0.50` | 连续低速 0.5s 才补 launch,防抖/定步态节奏 |
| `gimbal_task.c` | - | drive 模式且守卫未激活时,方向速度 <0.08 持续 0.5s 自动重装 launch timer | 步态核心逻辑 |

### 下一次测试看什么

1. 发 `1`：应看到周期性步态——tref 周期性跳 +1.4（launch）→ 滚一段（v 冲 0.15~0.2 衰减）→ 0.5s 低速 → 再 launch，周期约 2~3s；净位移应持续累积而不是原地摇。
2. launch 失败的步（轮子没破摩擦）应在 ~0.9s 后自动重试，不再永久趴窝。
3. 发 `0` 刹车保持上轮表现（0.2s 级收速）；刹完 stand 的漂移应因 Kv=3.0 明显小于本轮的 ±0.2m/9s。
4. 若步态节奏太急（车身来回点头不舒服）：`RETRIGGER_DWELL` 0.5 → 0.8；若滚动段还没享受完就补 launch：`RETRIGGER_BELOW` 0.08 → 0.05。
5. 若手推/大扰动后仍有发散趋势：Kv 3.0 → 2.5（回到已证稳定值）。
6. 守卫两次擦线未触发（|v|=0.383 vs 0.40）：暂不动,若再出现接近摔车的往复,考虑 `SPEED_GUARD_ON` 0.40 → 0.35。

### git

- 已提交并推送 `fc460d8 Fix stop brake window and add attitude-domain braking`（含 19:24/19:44/20:02 三轮修改与 21:12 前的全部状态）。

## 2026-07-07 诊断：stand 救车"支点不移动"根因——速度内环软 + 电流严重欠用（未改参数，先记录）

### 用户观察

- 主观："直立不鲁棒、容易前后走，受扰动时感受不到轮子快速位移把重心甩回的过程。"
- 追问：是不是轮速响应太慢？是不是只是速度环 Kp 的问题？

### 用 21:12 `vofa+.csv` 的 stand 段逐拍验证（16 路 BALANCE_CASCADE）

取一次干净的 stand(mode=0) 前倾扰动（行 618~622，theta 向负方向加深到 -2.5deg 后自行回正）：

```text
行   theta   bref(直立环要的轮速)  avg_actual(实际轮速)  cmd_A(电流命令)
618  -1.96   -49                    -9                    -0.064
619  -2.37   -62                    -12                   -0.079
620  -2.52   -64                    -19                   -0.075
621  -2.37   -57                    -23                   -0.059
622  -2.08   -47                    -23                   -0.046
```

- **方向正确**：车往负方向倒，直立环 bref 负、轮子 avg_actual 也负——救车方向对，直立环判断没错。
- **但轮子只执行了指令的 ~1/3**：bref=-64 时 avg_actual 只有 -19rpm。-19rpm ≈ -0.075m/s，支点移动慢到肉眼看不出"甩"，这正是用户"感受不到快速位移"的直接来源。
- **电流严重欠用**：cmd 全程 |0.03~0.08|A，stand 段平均 |cmd|=0.024A、最大 0.145A，**打满(≥0.34A)比例 0.0%**。电流限幅 0.35A 只用了约 23%。→ 排除"电流/动力不够"。

### 关键：不止 Kp 软，电流比 Kp 理论值还低（推翻"只是 Kp"）

逐行用 `cmd ?= Kp*err + FF_static*sgn + FF_kv*ref`（Kp=0.0013, FF_static=0.018, FF_kv=0.00015）核对：

```text
行619: err=-49.4  Kp项=-0.064  +前馈应=-0.091   实测cmd=-0.079  (实测 < 理论)
行620: err=-45.2  Kp项=-0.059  +前馈应=-0.086   实测cmd=-0.075  (实测 < 理论)
```

- 每一行实测电流都**低于** "Kp项+静摩擦前馈" 的应有值 → 除了 Kp 偏软，还有第二重压制，电流没被 0.35A 限幅、也没打满，却吐不出应有的量。
- 候选二重压制（按嫌疑排序，尚未逐一证死）：
  1. **CAN 反馈刷新率未知**（最大嫌疑）：`Chassis_GetWheelSpeedRpm` 用原始 `speed_can_rpm`（gimbal_task.c），速度环 dt=1ms 跑 1000Hz，但 CAN 反馈帧真实到达率**从未实测过**（process 100-101 行只建议过测 rx_count 增量，一直没做）。若反馈 ~20ms 才更新一次，速度环有约 20 拍在吃同一个旧 feedback，误差被系统性低估→电流吐不满，整个速度环等效带宽被反馈率而非 Kp 卡死。这是整个调参大厦一个未验证的地基。
  2. **电流爬升率 slew=20A/s**（gimbal_task.c Chassis_SpeedPidUpdate）：1ms 每拍最多变 0.02A；VOFA 20ms 抽样看不到,但扰动瞬间 bref 从 -34 跳到 -64（行617→620 约 4 拍）需要电流快速建立时,slew 会削峰。
  3. **刹车前馈禁用**（WHEEL_SPEED_FF_DISABLE_WHEN_BRAKING=1, gimbal_task.c）：同向且 |ref|<|actual| 时把静摩擦前馈砍 0；本段不满足条件，但走路/减速段会触发,可能吃掉起步力。
- 电机侧：`0x03` 纯电流模式，`LEFT_WHEEL_PID_SPEED_KP` 等内部 PID 宏是死配置（代码从不下发），速度控制全在 STM32 侧,排除"电机内部限速"。

### 结论与待办（未改任何参数，等现场确认后再动）

- 用户判断"救车软/支点不动"**成立**；但根因不是单一 Kp，是"速度内环整体太软 + 电流吐不满"，其中 **CAN 反馈真实刷新率必须先实测**，否则加 Kp 可能只是在一个滞后反馈上追噪声。
- 待用户确认的前置问题（阻塞下一步改参）：
  1. 21:12 那些 ±2deg 晃动，是**放手自站**还是**手扶/手推**？（决定"能站"基线）
  2. 先做 CAN 反馈率诊断：切 `VOFA_DEBUG_STAGE_MOTOR_FEEDBACK`,看 `rx_count` 每 20ms 增量——正常应 ~20（1000Hz），若只有 1~2 则反馈是 50~100Hz,speed 环带宽被它锁死。
- 候选改动（**尚未实施**，按依赖排序）：
  1. 先测反馈率；若反馈率够(≥每20ms 15+),再把 `WHEEL_SPEED_PID_KP_A_PER_RPM` 0.0013→0.0020 小步验证(process 894 行早给过路线"内环等效 Kp→0.003 把 0.35A 用满",当时被搁置)。
  2. 若反馈率不足,先解决反馈率(提高电机上报率或改用编码器差分高频估速),否则加 Kp 无效甚至发抖。

## 2026-07-08 直接调速度内环 Kp 与电流斜率

### 决策

- 本轮先不追 CAN 反馈率，把问题按“速度内环 Kp 偏软 + balance 电流 slew 偏保守”直接处理。
- 目标是让 stand 救车时支点移动更快，`avg_cmd_a` 不再长期停在 `0.03~0.08A`，而是在 ±2deg 扰动时能上到约 `0.12~0.20A`。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00130f` | `0.00200f` | 速度误差 40~60rpm 时，P 项从约 0.05~0.08A 提到 0.08~0.12A |
| `BALANCE_PID_CURRENT_SLEW_A_PER_SEC` | `20.0f` | `40.0f` | balance 路径每 1ms 最大电流步进 0.02A→0.04A，减少救车瞬态削峰 |

### 下一次测试看什么

1. stand 下给 ±2deg 左右扰动：I12 `avg_cmd_a` 应明显高于上一版，最好能短时到 `0.12~0.20A`。
2. I11 `avg_actual_rpm` 应更快追向 I9/I10，不再长期只有目标的约 1/3。
3. 若出现高频抖动或轮子来回啸叫，先把 Kp 回到 `0.0016~0.0018`，不要先动角度环。
4. 若仍明显软且无抖动，再把 Kp 往 `0.0025` 小步加；`0.0030` 作为上限验证点。

## 2026-07-08 VOFA BALANCE_CASCADE 精简为 12 路

### 修改

- 删除当前阶段用处不大的 `position_m / accel_mps2 / position_error_m / velocity_error_mps`。
- `VOFA_OUTPUT_CHANNEL_COUNT`：`16U -> 12U`。

### 当前通道

| I | 含义 |
|---:|---|
| I0 | `theta_deg` |
| I1 | `theta_ref_deg` |
| I2 | `speed_theta_ref_deg` |
| I3 | `velocity_mps` |
| I4 | `velocity_ref_mps` |
| I5 | `balance_ref_rpm` |
| I6 | `avg_ref_rpm` |
| I7 | `avg_actual_rpm` |
| I8 | `avg_cmd_a` |
| I9 | `fault` |
| I10 | `chassis_mode` |
| I11 | `zero_trim_deg` |

## 2026-07-08 08:55 新数据：Kp/slew 生效，走路仍卡在 launch 摆动

### 数据切片

- `vofa+.csv` 为拼接文件：
  - 前 3797 行仍是 2026-07-07 21:12 的旧 16 路数据截前 12 路。
  - 新 12 路数据从 CSV 第 3799 行开始，共 4460 行，约 `89.20s`。
- 新段通道为当前 12 路 `BALANCE_CASCADE`。

### stand 救车结论

- fault=3 有一段 `15.36~18.74s`，在 stand 且 theta 已大幅跑飞；这段不用于 Kp/slew 判断。
- 只看 upright stand（`mode=0,fault=0,|theta|<8deg`）：

```text
dur=72.28s
theta_mean=-0.818deg
avg |cmd|=0.033A, p95=0.055A, max |cmd|=0.270A
|bref|>10rpm 时 actual/ref median=0.71, mean=0.66
```

- 与 21:12 旧数据对比，速度内环明显变硬：
  - 旧数据最大 `avg_cmd_a≈0.145A`，这轮最大 `0.270A`。
  - 旧数据强扰动时实际轮速约目标 1/3；这轮 `|bref|>10rpm` 时中位数约 0.71。
  - 典型大误差行按 `Kp=0.002 + FF` 计算与实测 cmd 基本吻合，说明 Kp/slew 修改确实生效。

### forward 结论

- forward 只发生一段：`69.52~81.18s`，约 `11.68s`。
- 步态重触发工作了：共 13 次 launch，基本每 `0.9~1.0s` 一发。
- 但走路仍没有持续起来：

```text
forward v_range=-0.096~+0.204m/s
|v|>0.15m/s 只有 0.12s，占 forward 1.0%
launch 内 avg_actual_rpm 末端多为 +3~14rpm，轮子没有充分破摩擦
```

- 关键现象：launch 中 `theta_ref=+1.4deg`，车身很快甩到 +1.2deg：

```text
多数 launch: theta>=1.2deg 只需 0.04~0.08s
theta>=1.6deg 多在 0.10~0.26s
theta>=2.0deg 多在 0.26~0.38s
```

- 但 launch 保持到 `0.40s` 时，车身已经接近/超过目标角，直立环轮速命令反而变小；退出 launch 后 tref 跳回 -0.8 附近，才突然打出 +90~100rpm 捕捉车身，随后 theta 又甩到 -2.5deg 左右，形成“前甩-反捕捉-回摆”的摆动，而不是持续前滚。

### 下一步建议

1. 暂时不要继续加速度内环 Kp；这轮强扰动已能到 `0.20~0.27A`，继续加可能只会让 launch 退出后的捕捉更猛。
2. 优先缩短 launch：`BALANCE_MODE_LAUNCH_TIME_SEC 0.40 -> 0.25`，让车身刚越过分离角就进入轮子追身阶段，避免在 +2deg 附近等太久。
3. 或者更正式地加“theta 达阈值提前退出 launch”（例如相对平衡点前倾约 `1.6deg`），比固定时间更稳；但先用 `0.25s` 验证方向更快。
4. 若缩短后仍只摆不走，再考虑调整 retrigger dwell 或 launch bias；现在不先降 bias，因为当前核心问题更像退出太晚。

## 2026-07-08 启摆救车：提高角速度 D，并输出 P/D 分量

### 判断

- 用户观察“还是没有一个启摆救车的过程”成立：上一轮 Kp/slew 让大误差时轮速跟随变强，但早期仍主要靠角度 P 项，角速度 D 项太小。
- 用 08:55 新数据反推：

```text
stand_all: |P| mean≈16.4rpm, |D| mean≈1.6rpm
early |theta-tref|=0.2~1.0deg: |P| mean≈18.5rpm, |D| mean≈1.6rpm
典型启摆: e=0.49deg 时 P≈17rpm, D≈6rpm; e=0.81deg 时 P≈29rpm, D≈7rpm
```

- 这说明控制器还是“等角度偏出来后再救”，不是靠角速度抢先移动支点。

### 修改

| 参数/位置 | 旧 | 新 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.400f` | `0.800f` | 启摆早期把 D 分量翻倍，让轮子更早抢支点 |
| `BALANCE_CASCADE` VOFA | 12 路 | 13 路 | 加 `theta_rate_dps / angle_p_ref_rpm / rate_d_ref_rpm`，直接看启摆救车是不是由 D 提前触发 |

### 当前 VOFA 通道

| I | 含义 |
|---:|---|
| I0 | `theta_deg` |
| I1 | `theta_ref_deg` |
| I2 | `theta_rate_dps` |
| I3 | `speed_theta_ref_deg` |
| I4 | `velocity_mps` |
| I5 | `balance_ref_rpm` |
| I6 | `angle_p_ref_rpm` |
| I7 | `rate_d_ref_rpm` |
| I8 | `avg_ref_rpm` |
| I9 | `avg_actual_rpm` |
| I10 | `avg_cmd_a` |
| I11 | `fault` |
| I12 | `chassis_mode` |

### 下一次测试看什么

1. stand 轻推刚启摆时，I7 `rate_d_ref_rpm` 应先明显抬起，I5/I8 随之提前给轮速，而不是等 I0 偏到 1deg 以后才动作。
2. 若出现细碎高频抖动/轮子啸叫，说明 D 过大，先回 `0.6`；不要动 Kp。
3. 若启摆仍慢但不抖，再试 `D=1.0`；若 D 项已经很大但实际轮速 I9 跟不上，再回头看内环/电流限制。

## 2026-07-08 启摆救车：小步提高角度 P

### 判断

- 仅加 D 后，启摆初段仍可能不够“主动抢支点”：`Kp=35rpm/deg` 时，`0.3deg` 只给约 `10.5rpm`，`0.5deg` 只给约 `17.5rpm`，在当前摩擦带/内环响应下体感仍偏软。
- 本轮先小步提高角度 P，不再继续加轮速内环 Kp。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `35.00f` | `45.00f` | 小角度初偏时更早给轮速目标，配合 D=0.8 做启摆救车 |

### 下一次测试看什么

1. 轻推刚开始，I6 `angle_p_ref_rpm` 应比上一版更早更大；I7 `rate_d_ref_rpm` 负责抢先，I6 负责持续推支点。
2. 若出现低频来回摆大，先把 P 回到 `40`；若是细碎高频抖，优先把 D 回到 `0.6~0.7`。

## 2026-07-08 激进提高角度 P

### 判断

- 09:07 数据显示小误差段仍偏软：

```text
|e| 0.2~0.5deg: |P|mean=12.3rpm, |actual|mean=5.8rpm
|e| 0.5~1.0deg: |P|mean=25.8rpm, |actual|mean=14.8rpm
```

- 用户接受激进调法，本轮直接把角度 P 提到能在小角度启摆时明显推支点。

### 修改

| 参数 | 旧值 | 新值 |
|---|---:|---:|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `45.00f` | `65.00f` |

### 预期

- `0.3deg` 初偏：约 `19.5rpm`。
- `0.5deg` 初偏：约 `32.5rpm`。
- `1.0deg` 初偏：约 `65rpm`。
- 若出现低频大摆/过零后反冲，先回 `55~60`；若出现高频细抖，先降 D 而不是降 P。

## 2026-07-08 继续激进验证 P/D 方向

### 数据依据

- 09:14 数据显示轮速跟随已不是主瓶颈：

```text
actual/ref(|bref|>10rpm) median=0.86, mean=0.79
```

- 但小误差启摆段目标轮速仍偏小：

```text
|e| 0.2~0.3deg: bref mean=16.3rpm, actual mean=9.5rpm
|e| 0.3~0.5deg: bref mean=25.5rpm, actual mean=17.3rpm
|e| 0.5~1.0deg: bref mean=45.8rpm, actual mean=38.9rpm
```

- 因此本轮目的不是一次调到最优，而是证明“更大的直立 P/D 能否让启摆时支点提前明显移动”。

### 修改

| 参数 | 旧值 | 新值 |
|---|---:|---:|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `65.00f` | `90.00f` |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.800f` | `1.200f` |

### 预期与回退

- 预期：轻推刚启摆时，I6/I7 更早抬起，I8 更早到 `40~80rpm`，I9 实际轮速随即跟动，体感出现“轮子抢支点”。
- 若方向有效但过猛：回收到 `P=80, D=1.0`。
- 若低频大摆/过零反冲明显：优先降 P。
- 若细碎高频抖/啸叫明显：优先降 D。

## 2026-07-08 极大提高直立角度 P 验证支点前窜

### 数据依据

- 09:23 数据按直立救车重新判断：支点方向基本正确，`err` 与 `I8/I9` 大多同号，不是 forward mode 问题。
- 小误差启摆段仍偏软：

```text
|e| 0.2~0.5deg: ref mean≈30rpm, actual mean≈21~24rpm, cmd mean≈0.04A
|e| 0.5~1.0deg: ref mean≈55~69rpm, actual mean≈44~62rpm
D 项均值只有约 3~5rpm, 主要还是 P 在决定支点前窜强度
```

- 因此本轮直接把直立角度 P 加到很大，用来验证小角度时支点是否能明显窜到重心前方，并促成 `theta_rate` 反号回甩。

### 修改

| 参数 | 旧值 | 新值 |
|---|---:|---:|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `90.00f` | `180.00f` |

### 预期与观察

- `0.3deg` 初偏给约 `54rpm`，`0.5deg` 给约 `90rpm`，`1.0deg` 给约 `180rpm`。
- 约 `1.8deg` 就会接近 `320rpm` 轮速上限，应该能明显看出支点抢出去的趋势。
- 下一次 VOFA 重点看：`|e|<1deg` 时 `I8/I9` 是否提前变大，以及随后 `theta_rate` 是否快速反号回甩。
- 若出现低频大摆或过零后反冲过猛，先把 P 回收到 `140~150`；若出现细碎高频抖或啸叫，再降 D。

## 2026-07-08 09:39 数据：P=180 有支点感觉但过猛，开始回收

### 数据结论

- `P=180,D=1.2` 后支点前窜感觉已经出来，但数据进入明显限幅摆动：

```text
active rows=1588
I8/I5 打到 ±320rpm: 1446 行
avg_cmd_a 打到 ±0.35A: 1204 行
theta range=-19.4~+20.7deg
theta_rate range≈±550dps
zero crossings=311
```

- 第一拍启控时 `err≈1.96deg` 直接给 `I8=320rpm`，随后几十毫秒内连续过零；说明方向有效，但 `P=180` 已经过激。
- 小误差分箱已被高速过零污染：`|e|<1deg` 时角速度均值达到几百 dps，目标轮速也常常饱和，不能再按“启摆偏软”解释。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `180.00f` | `150.00f` | 保留支点前窜感觉，同时减少打满限幅和过零反冲 |

### 下一次观察

- `0.3deg` 初偏约 `45rpm`，`0.5deg` 约 `75rpm`，`1.0deg` 约 `150rpm`。
- 重点看 `I8/I9` 是否仍能在 `|e|<1deg` 提前冲出去，同时 `theta` 是否不再被甩到 `±15deg` 以上。
- 若仍大幅低频摆/连续打满，下一步把 P 收到 `130~140`；若支点感觉明显消失，再回 `160`。

## 2026-07-08 打开 STAND 漂移 stop-brake 触发

### 数据依据

- 09:57 新数据全程 `chassis_mode=0`，没有 `FORWARD/BACKWARD -> STAND` 切换。
- 原逻辑只在行走模式切回 `STAND` 时启动 `balance_stop_brake_timer_sec`，所以这组手推/小速度漂移数据里 stop-brake 没有工作。
- 小速度段 `|v|max≈0.128m/s`，但 `I3 speed_theta_ref` 只有约 `0.3~0.5deg` 量级，`I8/I9` 与速度同号，说明普通 stand 速度项太温柔，支点没有被强制安排到能让重心回甩的位置。

### 修改

| 项 | 旧逻辑 | 新逻辑 |
|---|---|---|
| `STAND` stop-brake 触发 | 只靠模式切换打开 timer | `STAND` 下 `|v| >= 0.04m/s` 时刷新 stop-brake timer |
| 新阈值 | 无 | `BALANCE_MODE_STOP_BRAKE_STAND_ON_MPS = 0.04f` |

### 预期

- 小速度手推/漂移也会进入姿态域刹车，`I3` 应明显大于原来的普通 `KV=3` 阻尼量。
- 以 `v=0.10m/s` 估算，新增 brake 项约 `0.8deg`，叠加普通速度项后应接近 `1deg` 量级。
- 下一次数据重点看：`I3` 是否随 `I4` 反号放大，`theta_rate` 是否更快回甩，`I8/I9` 是否出现更强的支点前窜/减速响应。

## 2026-07-08 10:09 数据：救车姿态过快翻向，轮速拟合滞后

### 数据结论

- `STAND` 漂移 stop-brake 已经工作：`|v|>=0.04m/s` 的 1493 行里，`I3 speed_theta_ref` 与 `I4 velocity` 全部反号。
- 但救车变成持续震荡并最终 fault=3：

```text
fault0 rows=1727, first fault idx=1727, theta=35.2deg, rate=642dps
theta range=-17.1~+23.6deg
velocity range=-0.868~+0.908m/s
theta zeroCross=320, velocity zeroCross=318
I8/I5 打到 ±320rpm: 1419/1727
avg_cmd_a 打到 ±0.35A: 1270/1727
```

- `I8 avg_ref_rpm` 与 `I9 avg_actual_rpm` 拟合不好：

```text
|ref|avg=278.7rpm, |actual|avg=90.7rpm
MAE=276.7rpm, RMSE=309.0rpm
同拍 sign same/opp=857/733
最佳相关在 actual 滞后 ref 约 2 个 VOFA 点(约 40ms), corr≈0.832
ref 饱和 1419 行中, actual 同号且 >=250rpm 只有 7 行
```

- 解释：姿态目标 `I3` 和轮速目标 `I8` 翻向太快、常常打满；实际轮速约滞后 40ms，等它追上时目标又换向，于是救车从阻尼变成注入能量。

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `BALANCE_PID_SPEED_THETA_REF_SLEW_DEG_PER_SEC` | 无 | `20.0f` | 对最终 `speed_theta_ref_deg` 做斜率限制，避免 `I3` 一拍翻满 |
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00200f` | `0.00250f` | 小/中误差下提高实际轮速贴目标能力 |
| `BALANCE_PID_CURRENT_LIMIT_A` | `0.35f` | `0.40f` | 目标长期饱和时给轮速内环更多力矩余量 |
| `BALANCE_PID_CURRENT_SLEW_A_PER_SEC` | `40.0f` | `60.0f` | 缩短电流建立时间，减少目标翻向后的响应迟滞 |

### 下一次观察

- `I3` 不应再在相邻 VOFA 点大幅从 `+3deg` 翻到 `-3deg`，最大变化应接近 `0.4deg/20ms`。
- `I8` 打满比例应明显下降；若仍长期 `±320rpm`，说明直立环/姿态目标仍过猛，需要继续降姿态目标变化率或收直立 P。
- `I9` 对 `I8` 的同拍 sign opposite 比例应下降；最佳相关滞后应从约 2 个 VOFA 点往 1 个点或更少移动。
- 若出现救车明显变迟钝、支点抢不出去，再把姿态斜率从 `20` 提到 `30deg/s`，不要先继续加 Kp。

## 2026-07-08 10:18 数据：减速改善后先降低 D 压高频

### 数据结论

- 减速效果明显好转且没有 fault：

```text
fault=0 全程
velocity range=-0.266~+0.330m/s
I8/I5 打到 ±320rpm: 4/1531
cmd>=0.39A: 14/1531
I3 相邻最大变化≈0.42deg, 姿态目标限斜率生效
```

- 轮速拟合比 10:09 明显改善：

```text
active |ref|avg=33.7rpm, |actual|avg=19.3rpm
MAE=25.9rpm, RMSE=39.5rpm
最佳滞后约 1 个 VOFA 点, 比上一轮约 2 个点更好
```

- 剩余问题是高频翻向：`theta_rate/D` 的过零次数约 1024，`I8 ref` 过零约 702，而 `I3` 过零约 339。说明姿态目标已经被压住，高频主要从角速度 D 项进入直立环输出。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `1.200f` | `0.900f` | 降低角速度项对 `I8` 的高频翻向驱动 |

### 下一次观察

- 其他参数保持不变，只看降 D 的影响。
- 重点看 `I8 ref zeroCross`、`cmd zeroCross` 是否下降，`I9` 与 `I8` 的反号比例是否减少。
- 若高频明显下降但救车变钝，再考虑 `D=1.0` 或保持 `D=0.9` 并小幅提高姿态斜率；若仍高频明显，再降到 `D=0.8`。

## 2026-07-08 10:31 数据：手推大动量下位移偏长，加入低速安静死区并加大刹车姿态

### 数据结论

- 本轮的大速度脉冲来自手推扰动，不是控制器自激脉冲。
- 救车方向正确：`|v|>=0.2m/s` 时 `I3 speed_theta_ref` 与速度反号 `580/0`。
- 轮速跟随在大目标段已可用：

```text
50~150rpm: act/ref median≈0.71
150~300rpm: act/ref median≈0.66
饱和段: act/ref median≈0.87
```

- 问题集中在手推大动量下收速距离偏长，仍像靠前后摆动慢慢耗能：

```text
v≈+0.60~0.69m/s: 收到 |v|<0.05 约 2.5~2.9s, 位移 0.85~0.94m
v≈-0.58~-0.65m/s: 收到 |v|<0.05 约 2.8~3.0s, 位移 0.79~0.98m
```

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.0f` | `1.5f` | 大速度手推时允许更大刹车倾角，更快消能 |
| `BALANCE_MODE_STOP_BRAKE_QUIET_V_MPS` | 无 | `0.08f` | 小速度进入安静窗口 |
| `BALANCE_MODE_STOP_BRAKE_QUIET_ANGLE_DEG` | 无 | `0.60f` | 小角度进入安静窗口 |
| `gimbal_task.c` | 小速度下仍会给速度/刹车姿态 | `STAND` 下同时满足 `|v|<0.08m/s` 且 `|theta-base_ref|<0.60deg` 时，最终 `speed_theta_ref_deg=0` | 低速小角度不救车，只靠直立环，减少前后抖动 |

### 下一次观察

- 低速附近 `I3` 应回到 0 附近，`I8/cmd` 高频翻向应减少。
- 手推到 `|v|>0.2m/s` 时，`I3` 仍应与速度反号，且可比上一轮更大；看收速时间和位移是否下降。
- 若低速开始漂移不回零，说明安静窗口太大，可把 `QUIET_V` 从 `0.08` 收到 `0.06`；若仍抖，可把 `QUIET_ANGLE` 从 `0.60` 放到 `0.80`。

## 2026-07-08 回撤 stop-brake 倾角限幅

### 现场反馈

- `TREF_LIMIT=1.5deg` 后抖动更明显。
- 判断：死区可能仍不够大，但更大的刹车倾角会加重前后摆动和过零反冲；本轮先单独回撤倾角，避免同时改多个变量。

### 修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.5f` | `1.0f` | 先验证抖动是否主要由过大的刹车姿态导致 |

### 下一次观察

- 死区参数暂不变：`QUIET_V=0.08m/s`、`QUIET_ANGLE=0.60deg`。
- 若抖动明显下降但低速仍不够安静，再单独放大死区；若抖动仍明显，再把 `QUIET_ANGLE` 提到 `0.80deg` 或 `QUIET_V` 提到 `0.10m/s`。

## 2026-07-08 撤销低速死区思路，重新加大刹车倾角

### 现场反馈

- 保留低速小角度死区后仍然抖，说明“低速小角度不救车”没有解决当前主问题。
- 当前判断：抖动更可能来自能量消除不够直接，低速死区反而可能让速度/姿态在边界附近来回切换；本轮撤销死区思路。

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_QUIET_V_MPS` / `QUIET_ANGLE` | `0.08m/s` / `0.60deg` | 删除 | 不再在低速小角度强制 `I3=0` |
| `gimbal_task.c` 低速死区逻辑 | 满足死区时 `speed_theta_ref_deg=0` | 删除 | 避免救车姿态在死区边界断续 |
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.0f` | `1.3f` | 中间值验证：比 1.0 更快消能，比 1.5 更保守 |

### 下一次观察

- 重点看手推后收速位移是否缩短，以及 `I3` 是否连续反向而不是被死区切断。
- 若 1.3 仍抖但收速变快，下一步优先平滑/限斜率或收 D，而不是回死区。
- 若 1.3 仍不够快且没有明显恶化，再试 `TREF_LIMIT=1.5`。

## 2026-07-08 checkpoint 后加入 stop-brake 方向锁

### 代码基线

- 已先提交并推送当前调参状态：`db45922 Checkpoint balance tuning state`。

### 判断

- 当前主要矛盾不是 CAN 反馈率，也不是单纯继续加 `Kp/Kd`。
- 数据里普通小角度区间姿态追踪尚可，但救车/减速段 `I3 speed_theta_ref_deg` 容易跟随速度符号频繁翻向，车身实际姿态跟不上，表现为靠前后抖动慢慢耗能。
- 因此本轮目标是让救车方向在一个刹车段内保持一致，同时保留低速退出、反向释放和姿态追踪保护，避免“单一方向一直加速然后倒下”。

### 修改

| 项 | 旧逻辑 | 新逻辑 | 目的 |
|---|---|---|---|
| stop-brake 触发 | `STAND` 下 `|v|>=0.04m/s` 每拍刷新窗口 | `|v|>=0.10m/s` 才进入方向锁刹车段 | 小噪声/低速不频繁点亮救车 |
| stop-brake 方向 | 每拍用当前 `measured_velocity_mps` 算 `-Kv*v` | 进入刹车段时锁定 `balance_stop_brake_dir=sign(v)` | 防止速度过零/噪声导致目标角快速反向 |
| 低速退出 | `|v|<=0.02m/s` 持续 `0.15s` | `|v|<=0.05m/s` 持续 `0.15s` | 更早退出，减少过零后反冲 |
| 反向保护 | 无单独方向释放 | 若 `v * brake_dir <= -0.08m/s`，立即释放锁定 | 真反向运动时不继续按旧方向救 |
| 姿态追踪保护 | 目标角只限幅到 `1.3deg` | 最终候选目标角限制在实际姿态 `±1.8deg` 内，且不允许反向贡献 | 车身追不上时不继续把 `I1` 甩远 |

### 下一次观察

- `I3` 在一次救车中应更连续，不应在速度零点附近高频翻向。
- `I1 theta_ref` 与 `I0 theta` 的差值不应长期超过 `1.8deg`；若仍超，说明直立环/轮速执行还跟不上。
- 看手推后减速位移是否缩短，同时注意过零后是否少了前后抖动。
- 若救车变钝但明显更稳，可把 `LOCK_ON` 从 `0.10` 降到 `0.08`；若仍抖，可把 `OFF` 提到 `0.06` 或把 `TRACK_ERR` 降到 `1.5`。

## 2026-07-08 11:20 数据：走的时候抖，压直立环高频轮速输出

### 数据结论

- 末尾 `theta=-74deg` 是手拿起车辆，不作为控制失败判断。
- 排除手拿起段后，救车方向本身基本正确：`|v|>=0.10m/s` 时 `I3` 几乎都与速度反号。
- 走/滑行时抖的主要来源不是 `I3` 方向错，而是直立环输出轮速目标高频反向：

```text
稳定移动段(|theta|<8, |v|>0.04):
I8 sign flips = 1109
cmd sign flips = 1579
I8 dI8 p95 ≈ 374.5rpm/20ms

正向滚动(v>0.10):
I8 sign flips = 437
cmd 饱和 ≈ 32.8%
```

- 典型正向滚动窗口中，速度仍同向，但 `I8` 会在 `+320 -> -100 -> +300 -> -80rpm` 来回跳，说明直立环在目标角附近过冲并反复拉扯轮速环。

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.900f` | `0.700f` | 降低角速度 D 对轮速目标的高频注入 |
| `BALANCE_PID_BALANCE_SPEED_REF_SLEW_RPM_PER_SEC` | 无 | `6000.0f` | 限制直立环输出轮速目标斜率，约 `120rpm/20ms` |
| `balance_speed_ref_rpm` | 直接送入左右轮目标 | 经过 `Chassis_StepTowards` 斜率限制后再使用 | 减少 `I8/cmd` 高频反向 |
| stop-brake 追踪保护 | 只保护刹车目标角候选值 | 最终 `theta_ref` 也限制在 `theta ± 1.8deg`，并回写 `I3` | 保证 VOFA 看到的 `I3/I1` 就是真正进入直立环的目标 |

### 下一次观察

- 重点看 `I8 sign flips`、`cmd sign flips`、`I8 dI8 p95` 是否明显下降。
- 正向滚动窗口中 `cmd` 饱和比例应从 30% 量级下降。
- 若抖动明显下降但救车变慢，可把 `BALANCE_PID_BALANCE_SPEED_REF_SLEW_RPM_PER_SEC` 提到 `8000`；若仍抖，先继续降到 `D=0.60`，不要急着加倾角。

## 2026-07-08 回滚直立环输出斜率限制

### 现场反馈

- 上一版改完后“根本立不起来”。
- 判断：直接限制 `balance_speed_ref_rpm/I8` 的斜率会削弱直立环最核心的快速支点移动能力；最终 `theta_ref` 跟随实际角的保护也会在救车触发时把直立环变软。

### 回滚

| 项 | 回滚前 | 回滚后 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.700f` | `0.900f` | 先恢复上一版可直立的阻尼 |
| `BALANCE_PID_BALANCE_SPEED_REF_SLEW_RPM_PER_SEC` | `6000.0f` | 删除 | 不再限制直立环轮速目标斜率 |
| `balance_speed_ref_rpm` 斜率限制 | 启用 | 删除 | 保留直立环快速救车能力 |
| 最终 `theta_ref` 追踪保护 | `theta ± 1.8deg` | 删除 | 避免救车时目标被实际角拖软 |

### 下一步方向

- 不再直接限直立环输出斜率。
- 若继续处理走动抖动，应优先从“目标角生成更平滑/更少高频刺激”或“分段降低 stop-brake 激活侵入性”下手，而不是卡死直立环输出。

## 2026-07-08 前进改为平滑目标倾角，不再用 launch 阶跃前倾

### 依据

- `process` 里已经验证过：小固定前倾 `0.4deg` 会被静摩擦带吃掉；但当前 launch 的 `theta_ref` 阶跃会造成“前甩-反捕捉-回摆”，走起来像在抖。
- 本轮按新的现场判断：前进不再用一拍 `launch_bias` 去甩车身，而是直接控制目标倾角，并把目标倾角平滑推过去。

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_LAUNCH_ENABLE` | `1U` | `0U` | 关闭前进进入时的阶跃前倾 |
| `BALANCE_MODE_LAUNCH_RETRIGGER_ENABLE` | `1U` | `0U` | 关闭低速自动补 launch |
| `BALANCE_MODE_DRIVE_THETA_REF_ENABLE` | `1U` | `0U` | 前进不再由速度误差生成额外倾角 |
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `-0.8f` | `0.8f` | 前进目标角改为相对 stand 前倾 `+1.6deg` |
| `BALANCE_MODE_BACKWARD_BIAS_DEG` | `-0.8f` | `-2.4f` | 后退给对称后倾 `-1.6deg` |
| `BALANCE_MODE_BIAS_SLEW_DEG_PER_SEC` | 无 | `2.0f` | 前进/后退目标角平滑变化，不再阶跃 |
| `gimbal_task.c` | `mode_bias` 直接取宏值 | drive 模式下用 `balance_mode_bias_slew_deg` 斜坡到目标角；回 stand 立即回 stand bias | 前进平滑，停车干脆 |

### 下一次观察

- 发 `1` 后，`I1 theta_ref` 应从 `-0.8deg` 平滑爬到 `+0.8deg`，约 `0.8s` 完成，而不是一拍跳到 `+1.4deg`。
- 重点看车能否在接近 `+0.8deg` 后破静摩擦开始滚动；如果仍只是换姿势站住，下一步把 forward 目标角小幅加到 `+1.0deg`。
- 若开始滚但速度过大或抖，先降低目标角到 `+0.6deg` 或降低斜率到 `1.5deg/s`，不要恢复 launch。

## 2026-07-08 前进目标角改为速度 PI 输出

### 现场判断

- 固定 `+0.8deg` 前进目标角本质上仍是“锁死倾角”，速度起来后不会自动回收，太快时也不会自动反向减速。
- 内层已经有角度 PD：`theta_error -> balance_speed_ref_rpm`。不需要再套第二个角度 PID。
- 需要的是外层速度 PI：`velocity_ref - velocity -> drive_theta_ref`，由速度误差动态决定目标倾角。

### 修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `0.8f` | `BALANCE_MODE_STAND_BIAS_DEG` | 前进不再固定锁倾角 |
| `BALANCE_MODE_BACKWARD_BIAS_DEG` | `-2.4f` | `BALANCE_MODE_STAND_BIAS_DEG` | 后退不再固定锁倾角 |
| `BALANCE_MODE_DRIVE_THETA_REF_ENABLE` | `0U` | `1U` | 允许行走速度外环输出目标倾角 |
| `BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC` | `0.08f` | `0.15f` | 前进/停止速度目标更跟手，但仍不是阶跃 |
| `BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS` | 无 | `6.00f` | 速度 P 输出行走倾角 |
| `BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S` | 无 | `0.80f` | 慢积分补静摩擦/零点残差 |
| `BALANCE_MODE_DRIVE_SPEED_INTEGRAL_LIMIT_M_S` | 无 | `0.50f` | 积分最多贡献约 `0.4deg` |
| `BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG` | 无 | `1.60f` | 行走目标倾角限幅，避免重新变成猛 launch |
| `gimbal_task.c` | drive 时 `speed_theta_ref=0` | drive 时用速度 PI 计算 `speed_theta_ref` | 目标角随速度误差自动加/收/反向 |

### 预期

- 发 `1` 后 `I3 speed_theta_ref` 应随 `velocity_ref` ramp 平滑增加，最大约 `+1.6deg`，不是固定锁死。
- 车开始前进后，`I4 velocity_mps` 接近目标速度时，`I3` 应自动回收；如果速度超过目标，`I3` 应减小甚至反向。
- 发 `0` 后 drive PI 积分清零，回到 stand bias，剩余速度由 stand stop-brake 处理。

## 2026-07-08 12:45 数据：直走摇晃与直立受扰摇晃属于同一振荡链路，但直走外环放大明显

### 数据结论

- 本次 `vofa+.csv` 共 `2377` 行，`13` 路输出，全程 `fault=0`。
- 分段：
  - `0~544`：`mode=0`，`10.90s`
  - `545~1718`：`mode=1`，`23.48s`
  - `1719~2376`：`mode=0`，`13.16s`
- 用户观察“直走摇晃严重，像直立状态受外力干扰后的摇晃”，数据支持这个判断：两者底层都是 `theta/theta_ref` 误差和角速度引起 `P/D -> I8 -> cmd` 正负快速切换。
- 但 `mode=1` 直走段明显更严重，因为速度外环持续注入目标角需求，车身实际追不上后，内层直立环进入大幅饱和救车状态。

### 关键指标

| 场景 | `theta RMS` | `rate RMS` | `|theta_ref-theta| MAE` | `I8-I9 MAE` | `cmd` 饱和 |
|---|---:|---:|---:|---:|---:|
| 直走 `mode=1, |theta|<8` | `4.80deg` | `380.6dps` | `4.04deg` | `232.5rpm` | `69.2%` |
| 直走且移动 `mode=1, |v|>0.04, |theta|<8` | `4.83deg` | `385.3dps` | `4.09deg` | `235.0rpm` | `70.0%` |
| 直立受扰 `mode=0, |v|>0.04, |theta|<8` | `1.49deg` | `103.5dps` | `0.55deg` | `86.2rpm` | `14.1%` |
| 直立静止 `mode=0, |v|<=0.04, |theta|<8` | `0.91deg` | - | `0.30deg` | - | - |

### 判断

- 直立受扰时也有高频翻向，但角度追踪还可以：`|theta_ref-theta| p95 = 2.59deg`。
- 直走时角度追踪已经坏掉：`|theta_ref-theta| p95 = 7.69deg`，说明车身不是在稳定跟随目标角，而是在目标附近大幅来回甩。
- 直走时 `I8` 与 `cmd` 高度同向：`corr(I8, cmd)=0.963`，说明电流环在努力执行。
- 但 `I8` 与实际轮速 `I9` 相关性很低：`corr(I8, I9)=0.208`，且 `I8-I9 MAE=232.5rpm`，说明轮速执行跟不上目标轮速，目标轮速本身也在快速正负切换。
- 代表窗口 `13.40~14.70s` 中，`theta_ref` 只在约 `-1.3~+0.4deg` 附近变化，但实际 `theta` 已经甩到约 `-13~+12deg`，随后 `P_ref/D_ref/I8/cmd` 反复打满正负。

### 下一步方向

- 不再直接限制直立环输出 `I8` 斜率；之前已验证会削弱支点快速移动，导致”根本立不起来”。
- 优先让行走速度外环在车身追踪差时让位：
  - 当 `|theta_ref - theta| > 2.0~2.5deg` 时，暂停或衰减速度 PI 输出，让内层直立环先收住车身。
  - 降低 `BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS` 和 `BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S`，避免速度外环把目标角推得太急。
  - 对 `I4 velocity_mps` 或速度误差做低通/死区，减少速度过零时 `I3 speed_theta_ref` 频繁翻向。
- 当前核心判断：直走摇晃和直立受扰摇晃是同一个内层振荡链路，但直走段的主要触发器是速度外环太积极，车身未稳时仍继续要速度。

## 2026-07-08 12:45 数据根因定位：角度环增益已超稳定域 8 倍，进入 ~12Hz 饱和继电器极限环

### 数据结论（vofa+.csv，2377 行 13 路，全程 fault=0）

- **全链路同频翻向，锁死在同一个极限环上**：

```text
mode=1 直走段（23.5s）符号翻转率完全一致：
theta_rate: 12.0 次/s    angle_P_ref: 12.0 次/s    rate_D_ref: 12.0 次/s
avg_ref(I8): 12.0 次/s   avg_cmd: 12.0 次/s         theta: 11.4 次/s
```

- 所有量以 ~11.6Hz 同步振荡——不是噪声，是闭环自激。这与 10:09/10:18/10:31/11:20 数据中反复出现的”高频翻向/饱和”同源，但本次振荡频率/幅度已完全稳定成持续极限环。

- **执行延迟 ~40ms，正好是这个频率下的致命相位滞后**：

```text
corr(I8, I9) vs lag:
  0ms → 0.235    20ms → 0.791    40ms → 0.909 (peak)
  60ms → 0.633   100ms → -0.545
```

- 实际轮速 `I9` 滞后目标轮速 `I8` 约 40ms。11.6Hz 周期为 86ms，40ms 延迟 ≈ **167° 相位滞后**——控制量到达时车身已经翻到另一边，每个周期都在注入能量而不是阻尼。这与 10:09 数据里”最佳相关在 actual 滞后 ref 约 2 个 VOFA 点” 一致，说明这个执行延迟一直存在，并未因后续调参改善。

- **角度环已经完全饱和成继电器**：

```text
mode=1 段 |angle_P_ref| 平均 1047rpm（320rpm 限幅的 3.3 倍）
mode=1 段 avg_cmd 饱和比例 69.2%
mode=1 段 |theta_ref - theta| MAE = 4.04deg, p95 = 7.69deg
```

- P 项绝大部分时间顶死 `±320rpm` 限幅，环路等效成”±320rpm 方波 + 40ms 延迟”。继电器+延迟必然产生极限环（描述函数法），频率和幅度（theta ±8~13deg、cmd 饱和 69%）都与理论吻合。

- **刚度已经超出物理需求 6~10 倍**：

| 版本 | 角度P × 轮速Kp | 等效刚度 | 结果 |
|---|---:|---:|---|
| P35 × 0.0013（07-06 基线） | 0.045 A/deg | **物理需求** | 连续站立 86~114s，最稳版本 |
| P150 × 0.0025（当前） | **0.375 A/deg** | **需求的 8.3 倍** | 12Hz 极限环 |

- 07-06 已推导过维持直立的重力刚度需求约 `0.035~0.065 A/deg`。当前角度环刚度被抬了 8 倍，而执行延迟（40ms 未变）一点没减。过高增益 + 固定相位滞后 = 必然不稳定。

- **直走更糟只是因为多了一个激励源**：

```text
mode=0 静止段（2-10s）：theta RMS = 0.54~0.83deg（可接受）
mode=0 受扰段（|v|>0.04）：theta RMS = 1.49deg
mode=1 直走段：theta RMS = 8.26deg（坏 5~15 倍）
```

- 速度外环（`DRIVE_SPEED_KP=6 deg/(m/s)`）不停挪 `theta_ref`，等于持续踢一个本来就临界振荡的内环。内环极限环稳定后，外环问题才有意义。

### 为什么会走到这一步

**复盘 07-08 调参路径**：P 35→45→65→90→180→150，追的是”启摆时支点抢出去的手感”。但”支点慢”的真凶是 **07-07 诊断里已经指出、至今没做的那件事：CAN 速度反馈真实刷新率从未实测**（process 2987 行原话：*”CAN 反馈刷新率未知（最大嫌疑）...这是整个调参大厦一个未验证的地基”*）。

- 40ms 的执行滞后如果主要来自反馈率/滤波/CAN 链路，那么加外环 P 永远补不了它，只会把振荡频率推高、把饱和比例推满。
- 这和自平衡车调参的通用经验一致：一致的快速振荡首先修环路延迟，其次才是降 Kp、再补 Kd（参考：Hackaday Wobble Taming 案例、PD≈简化LQR 的刚度/阻尼/相位裕度权衡）。
- 另外 `D=0.9` 在 12Hz、40ms 延迟下已经不提供阻尼：D 项和 P 项同频同步翻向（均 12 次/s），延迟超过 1/4 周期后 D 反而注能——这是”降 D 到 0.7 立不起来、回 0.9 也不好”的原因：**D 的正负作用取决于 P 定下的振荡频率，P 不降，动 D 无解。**

### 下一步优先级（按序执行，不跳步）

1. **先把角度环退回稳定域**：
   - `BALANCE_PID_ANGLE_KP_RPM_PER_DEG: 150.00 -> 50.00`
   - `BALANCE_PID_RATE_KD_RPM_PER_DPS: 0.900 -> 0.450`
   - 保留已加硬的内环 `WHEEL_SPEED_PID_KP=0.0025 / SLEW=60 / LIMIT=0.4A`，所以不必回到 35——等效刚度 50×0.0025=0.125 A/deg 仍是老基线的 2.8 倍，”支点感”不会全丢。
   - **预期**：12Hz 振荡消失、cmd 饱和率从 69% 掉到 <15%、theta RMS 从 8deg 降到 1~2deg。
   
2. **补做欠账的 CAN 反馈率实测**：
   - `VOFA_DEBUG_STAGE = VOFA_DEBUG_STAGE_MOTOR_FEEDBACK`，看 `rx_count` 每 20ms 增量。
   - 若反馈只有 50~100Hz（每 20ms 增量只有 1~2），那 40ms 滞后有一半来自反馈，任何外环调参都在给一个旧影像做控制——该先修反馈（提高上报率或换高频估速），而不是调 P。
   - 若反馈率够（每 20ms 增量 ≥15），再继续下一步。

3. **减延迟而不是加增益**：
   - 反馈率确认够之后，再看 `LEFT/RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA=0.20`（约 4ms）和 `BALANCE_PID_VELOCITY_LPF_ALPHA=0.15` 这些滤波是否能放松，把 40ms 压到 20ms 以内。
   - 再考虑在稳定域内小幅提 P（如 50→60→70），但必须同时监控 `corr(I8,I9)` 的最佳滞后——若滞后又回 40ms 且振荡复现，停止加 P。

4. **直走问题挂起**：
   - 内环极限环没消之前，12:45 数据对速度外环（`DRIVE_SPEED_KP=6 / KI=0.8`）没有评价意义。
   - 内环稳定后再测直走，并按本节末尾已提出的方向加”`|theta_ref-theta|>2deg` 时速度外环让位”的门控。

### 一句话总结

**这不是又一个符号/结构问题，是纯粹的增益越界——07-08 追”启摆手感”把角度环刚度抬到物理需求的 8 倍，而真正限制启摆速度的 40ms 执行延迟（很可能是没测过的 CAN 反馈率）一直没动。先退 P、再测反馈率、再减延迟，顺序不能反。**

## 2026-07-08 执行：角度环退回稳定域，并切换 VOFA 测电机反馈率

### 修改

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `150.00f` | `50.00f` | 先把角度环从 12Hz 饱和极限环退回稳定域 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.900f` | `0.450f` | 降低在 40ms 执行滞后下的高频注能风险 |
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_BALANCE_CASCADE` | `VOFA_DEBUG_STAGE_MOTOR_FEEDBACK` | 下一轮先实测 CAN/电机反馈刷新率 |

### 下一次采数看法

- 本轮数据不再按 13 路 balance cascade 解析。
- `MOTOR_FEEDBACK` 通道顺序：
  - `I0 left_enabled`
  - `I1 right_enabled`
  - `I2 left_can_speed_rpm`
  - `I3 right_can_speed_rpm`
  - `I4 left_feedback_current_a`
  - `I5 right_feedback_current_a`
  - `I6 left_rx_count`
  - `I7 right_rx_count`
  - `I8 last_can_id`
  - `I9 last_can_dlc`
  - `I10 can_rx_count`
  - `I11 fault`
- 重点看 `I6/I7/I10` 每 `20ms` 的增量：
  - 若单电机 `rx_count` 每帧只增加 `1~2`，反馈率大约 `50~100Hz`，40ms 滞后很可能来自反馈链路。
  - 若每帧增加明显更高，再回头看滤波和轮速执行延迟。
- 先验证反馈率，不急着评价直走速度外环；直走外环的门控让位等内环不再极限环后再做。

## 2026-07-08 执行：切回前进定位专用 VOFA 输出

### 依据

- `MOTOR_FEEDBACK` 数据已验证反馈率足够：左右电机 `rx_count` 每 `20ms` 约增加 `20`，单电机反馈约 `1000Hz`，总 CAN 约 `2000Hz`。
- 因此前进“不好”的下一步不再继续看反馈率，而是回到姿态级联数据，重点定位：
  - 速度外环是否把 `theta_ref` 推得太急；
  - 实际 `theta` 是否跟不上 `theta_ref`；
  - `I8 avg_ref_rpm` 与 `I11 avg_actual_rpm` 是否继续大误差/饱和；
  - 左右轮是否有明显不一致。

### 修改

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `VOFA_DEBUG_STAGE` | `VOFA_DEBUG_STAGE_MOTOR_FEEDBACK` | `VOFA_DEBUG_STAGE_BALANCE_CASCADE` | 切回前进/姿态级联定位 |
| `VOFA_OUTPUT_CHANNEL_COUNT` for `BALANCE_CASCADE` | `13U` | `16U` | 增加速度外环和左右轮差分观测量 |

### 新 VOFA 16 路通道

| 通道 | 含义 |
|---:|---|
| `I0` | `theta_deg` 实际车身倾角 |
| `I1` | `theta_ref_deg` 最终目标倾角 |
| `I2` | `theta_rate_dps` 角速度 |
| `I3` | `speed_theta_ref_deg` 速度外环贡献的目标倾角 |
| `I4` | `velocity_ref_mps` 速度目标 ramp 后值 |
| `I5` | `velocity_mps` 当前估计速度 |
| `I6` | `velocity_err_mps = velocity_ref - velocity` |
| `I7` | `balance_ref_rpm` 直立环输出轮速目标 |
| `I8` | `angle_p_ref_rpm` 角度 P 项 |
| `I9` | `rate_d_ref_rpm` 角速度 D 项 |
| `I10` | `avg_ref_rpm` 左右轮平均目标转速 |
| `I11` | `avg_actual_rpm` 左右轮平均实际转速 |
| `I12` | `avg_cmd_a` 左右轮平均电流命令 |
| `I13` | `actual_diff_lr_rpm` 左右实际轮速差 |
| `I14` | `fault` 故障码 |
| `I15` | `chassis_mode` 模式 |

### 下一份数据的判断顺序

1. 先看 `mode=1` 前进段 `I4/I5/I6`：若 `velocity_ref` 平滑但 `velocity_err` 长期大，说明速度跟不上目标。
2. 再看 `I3/I1/I0`：若 `I3` 不大但 `I0` 甩得很大，是内层姿态/轮速执行问题；若 `I3` 频繁翻向或贴限幅，是速度外环刺激问题。
3. 再看 `I10/I11/I12`：若 `avg_ref` 与 `avg_actual` 长期差很大且 `avg_cmd` 饱和，是轮速执行/力矩不足或目标太急。
4. 最后看 `I13`：若左右实际轮速差很大，前进抖动里可能混入偏航/左右轮不一致；若 `I13` 很小，则主要还是前后方向控制问题。

## 2026-07-08 14:36 数据：发 1 不破静摩擦，发 0 能压停但会反向位移

### 现场现象

- 发 `1` 后车身会前倾，但小车不主动前进。
- 需要手推一下才能克服静摩擦，之后可以动起来。
- 发 `0` 后能停车，但停车后会产生反方向位移，不能快速原地停住。

### 数据结论

- 本次 `vofa+.csv` 为 16 路 `BALANCE_CASCADE drive-diagnosis` 输出，共 `5187` 行，约 `103.74s`，全程 `fault=0`。
- `mode=1` 前进段共三段：

| 段 | 时间 | 时长 | 估计位移 | 现象 |
|---|---:|---:|---:|---|
| #0 | `18.28~63.48s` | `45.22s` | `+1.401m` | 中间有手推/大速度段 |
| #1 | `70.28~81.16s` | `10.90s` | `+0.169m` | 多数时间卡住，偶发小移动 |
| #2 | `88.76~98.42s` | `9.68s` | `+0.166m` | 多数时间卡住，偶发小移动 |

### 发 1 不前进的证据

- 在 `mode=1 && vref>0.12 && |v|<0.03` 的卡住段：

```text
rows = 1896, duration = 37.92s
velocity_ref mean = +0.198m/s
velocity mean ≈ -0.001m/s
velocity_err mean ≈ +0.199m/s
speed_theta_ref mean = +1.53deg, max = +1.60deg
avg_ref_rpm RMS = 16.4rpm
avg_actual_rpm RMS = 4.7rpm
avg_cmd_a RMS = 0.056A
cmd saturation = 0.0%
```

- 判断：速度外环已经把 `I3 speed_theta_ref` 顶到 `+1.6deg`，但车身实际追到了这个目标附近，直立环输出轮速目标变小，`I10 avg_ref_rpm` 和 `I12 avg_cmd_a` 都不足以破静摩擦。
- 这不是轮速环爆了。全段 `corr(I10, I11)=0.931`，`cmd` 饱和仅 `0.1%`，说明轮速执行明显比 12:45 的饱和极限环状态健康很多。

### 代码对应原因

- 当前前进 base rpm 使用实际轮速：

```c
drive_base_rpm = 0.5f * (left_rpm + right_rpm);
```

- 因此静止时 `drive_base_rpm=0`。发 `1` 后主要靠 `theta_ref` 前倾来启动；一旦车身追到目标角附近，直立环输出也接近 0，轮子没有持续前进前馈，静摩擦破不了。
- 所以现场表现就是“发 1 只是前倾站住，要手推一下才走”。

### 发 0 反向位移的证据

| 停车段 | 停前速度 | 同方向位移 | 反方向位移 | 结论 |
|---|---:|---:|---:|---|
| #0 `63.50~70.26s` | `+0.058m/s` | `+0.410m` | `-0.182m` | 有较大扰动/回摆 |
| #1 `81.18~88.74s` | `+0.039m/s` | `+0.092m` | `-0.265m` | 过零后继续反向走 |
| #2 `98.44~103.72s` | `+0.005m/s` | `+0.008m` | `-0.264m` | 几乎原地停下后又倒走 |

- #1/#2 更说明问题：车不是停不下来，而是速度很快过零，随后进入 `-0.07~-0.10m/s` 左右的反向慢走。
- #2 停车后典型片段：

```text
t+0.0s: v=-0.01, I3=+0.04
t+0.8s: v=-0.02, I3=+0.07
t+1.8s: v=-0.10, I3=+0.34
t+2.4s: v=-0.09, I3=+1.06
t+3.0s: v=-0.08, I3=+1.03
```

- 判断：发 `0` 后速度能压到小速度，但过零后 stop/stand 逻辑继续给出会造成反向位移的姿态/轮速作用，导致“停车后倒走一段”。

### 当前主要矛盾

1. **前进启动缺少破静摩擦的轮速/电流前馈**：仅靠 `+1.6deg` 目标倾角会前倾站住，但不给足轮速/力矩。
2. **停车缺少过零后的锁停/保持逻辑**：速度过零且进入小速度区后，仍然允许控制继续把车拖向反方向。

### 下一步建议

- 前进：低速/静止且 `mode=1` 时，增加小的 `velocity_ref` base rpm 或静摩擦 kick，不再只用 `actual_base`。
- 停车：加”过零锁停/小速度停止保持”，当速度过零且 `|v|<0.03~0.05m/s` 后，立即退出刹车目标角，冻结/重置位置参考，避免 stand 位置环或 stop-brake 把车继续拖反向。

## 2026-07-08 执行：切换到”目标速度做 base”架构

### 问题本质

当前使用”实际轮速做 base”架构：

```c
drive_base_rpm = 0.5f * (left_rpm + right_rpm);  // 静止时=0，破不了静摩擦
```

导致：
1. **启动问题**：静止时 `drive_base_rpm=0`，启动完全靠倾角（`speed_theta_ref ≈ +1.6deg`），但车身追到目标角后直立环输出变小，轮速不足以破静摩擦 → 发 1 只是前倾站住，需要手推才能走
2. **停车反向位移**：停车过零后实际轮速变负 → `drive_base_rpm` 变负 → 继续拖车倒走 0.2~0.3m

### 解决方案

改用”目标速度做 base + 直立环做修正”架构，让前进 base 由目标速度决定，直立环只负责姿态修正。

### 修改

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_DRIVE_USE_ACTUAL_BASE` | `1U` | `0U` | 切换到目标速度做 base |

修改后的 base 计算（已存在代码，只是之前被 `#if` 关闭）：

```c
drive_base_rpm = BALANCE_PID_VELOCITY_SIGN * 
                 balance_velocity_ref_ramp_mps / 
                 BALANCE_RPM_TO_MPS;
```

### 预期效果

1. **启动**：发 1 后 `balance_velocity_ref_ramp_mps` 立即从 0 ramp 到 0.2m/s → `drive_base_rpm` 给出前进轮速（≈62rpm）→ 配合倾角快速破静摩擦，无需手推
2. **停车**：发 0 后 `balance_velocity_ref_ramp_mps` ramp 回 0 → `drive_base_rpm=0` → 停在原地，不再因实际轮速变负而倒走
3. **直走稳定性**：因角度环 P 已从 150 降到 50（退回稳定域），12Hz 极限环已消失，速度外环不再踢临界振荡的内环

### 架构说明

左右轮目标：

```c
left_ref_rpm  = balance_speed_ref_rpm + drive_base_rpm + stop_brake_rpm + turn_yaw
right_ref_rpm = balance_speed_ref_rpm + drive_base_rpm + stop_brake_rpm - turn_yaw
```

- `balance_speed_ref_rpm`：直立环输出，左右同向（前后平衡）
- `drive_base_rpm`：前进 base，左右同向（目标速度）
- `turn_speed_ref_rpm`：转向，左右反向（差动转向）

这是经典的”前馈 + 反馈”结构：
- 前馈（`drive_base_rpm`）：直接给出目标速度对应的轮速，破静摩擦、维持匀速
- 反馈（`balance_speed_ref_rpm`）：根据姿态偏差修正，实现平衡和加减速

### 下一步

烧录测试，观察：
1. 发 1 是否立即启动，无需手推
2. 发 0 是否原地停车，无反向位移
3. 直走是否平稳（P=50 已退回稳定域，应无 12Hz 振荡）

## 2026-07-08 数据：目标速度 base 被直立环完全抵消，车不动

### 现象

发 1 后车仍然不动，77.9秒前进段实际速度只有 0.002m/s。

### 数据分析

mode=1 段统计（77.9秒）：

| 量 | 值 | 说明 |
|---|---:|---|
| `velocity_ref (I4)` | 0.193 m/s | 目标速度正常 |
| `velocity (I5)` | **0.002 m/s** | 实际几乎不动 |
| `speed_theta_ref (I3)` | +1.46° | 速度外环输出 |
| `theta_ref (I1)` | +0.66° | 最终目标角 |
| `theta (I0)` | **-0.55°** | 实际车身后仰 |
| `balance_ref (I7)` | **-59 rpm** | 直立环输出负向 |
| `avg_ref (I10)` | **+0.35 rpm** | 最终轮速目标≈0 |

**关键**：
- 前进 base 应该给 `vref=0.2m/s → +62rpm`
- 但直立环输出 `-61rpm`（因为车身后仰，想拉回来）
- **两者抵消 → 最终轮速目标≈0 → 车不动**

### 根因

**车身重心在轮轴后面，自然后仰 -4.4°**（mode=0 静止段测得）

当前前进模式参数：
- `BALANCE_MODE_FORWARD_BIAS_DEG = -0.8°`（前进姿态偏置）
- `DRIVE_SPEED_KP = 6 deg/(m/s)`（速度外环增益）
- 0.2m/s 目标 → `speed_theta_ref = 1.2°`

**最终目标角**：`theta_ref = -0.8 + 1.2 = +0.4°`（太小！）

车身重心让它自然后仰 -4.4°，目标只有 +0.4°，**误差 ≈ -5°**

角度环 P=50 输出：`balance_ref = -50 × 5 = -250rpm`（限幅到约 -60rpm）

**这个 -60rpm 抵消了前进 base +62rpm，车根本不动。**

### 为什么之前"实际轮速做 base"能动

```c
drive_base = 0.5 * (left_rpm + right_rpm);  // 跟随实际
```

一旦手推破静摩擦，实际轮速变正 → `drive_base` 跟随变正 → `balance` 只做小幅修正，不会完全抵消。

**但现在"目标速度做 base"**：
- `drive_base = +62rpm`（固定目标）
- 车不动 → 车身后仰误差大 → `balance = -61rpm`（抵消）
- **前馈和反馈打架**

### 解决方案

**加大前进模式的姿态偏置，抵消重心偏差：**

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `-0.8f` | `4.0f` | 前进时车身目标前倾 +4°，抵消重心导致的 -4.4° 自然后仰 |

修改后：
- `theta_ref = +4.0(mode_bias) + 1.2(speed) = +5.2°`
- 车身追到 +5.2° 附近，明显前倾
- 角度环误差变小，`balance_ref` 不再输出 -60rpm
- 前进 base 不再被抵消，车能动

### 架构正确性验证

- 极性没有反：如果反了，mode=0 静止段车根本站不起来（实际能站 -4.4°）
- 架构没问题：`balance_ref` 左右同向（前后平衡），`turn` 左右反向（差动转向）
- 问题是数值：mode_bias 太小，无法抵消重心偏差，导致前馈和反馈抵消

### 预期效果

发 1 后：
- `theta_ref ≈ +5°`（明显前倾）
- 车身追到前倾姿态
- `balance_ref` 接近 0（误差小）
- 前进 base +62rpm 直接驱动轮子
- 车立即启动

## 2026-07-08 调整：恢复角度环响应速度（"启摆救车"手感）

### 问题

上一版本将角度环 P 从 150 降到 50（降 3 倍），虽然消除了 12Hz 极限环，但响应变慢——车身倾斜时轮速输出只有之前 1/3，失去了"启摆时支点快速抢出去救车"的手感。

### 调整

在稳定域内提高增益，平衡稳定性和响应速度：

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `50.00f` | `70.00f` | 恢复部分响应速度，仍在稳定域内 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.450f` | `0.650f` | 恢复角速度阻尼 |

### 等效刚度对比

| 版本 | 角度P × 轮速Kp | 等效刚度 | 说明 |
|---|---:|---:|---|
| 07-06 基线 (P=35) | 35 × 0.0013 | 0.045 A/deg | 物理需求，能站 86~114s |
| 12:45 振荡 (P=150) | 150 × 0.0025 | 0.375 A/deg | 需求的 8.3 倍，12Hz 极限环 |
| 上一版 (P=50) | 50 × 0.0025 | 0.125 A/deg | 需求的 2.8 倍，稳但慢 |
| **当前 (P=70)** | 70 × 0.0025 | **0.175 A/deg** | **需求的 3.9 倍，响应快又稳** |

### 预期

- 保持稳定：P=70 仍远低于 P=150 的饱和区，不会重现 12Hz 振荡
- 恢复响应：等效刚度 0.175 A/deg 比 P=50 的 0.125 提高 40%，"启摆救车"手感回来
- 前进能动：配合 `FORWARD_BIAS=+4.0°`，前进 base 不再被抵消

## 2026-07-08 数据：能前进但停车失控，速度压不下来

### 现象

- 前进能动了（velocity 平均 0.075 m/s，虽然只有目标 0.15 m/s 的一半）
- 车身前倾到 +2.45°（不再后仰）
- **但停车时速度不降反升**：0.4 → 0.7 → 0.9 m/s，直到失控

### 典型停车失控数据（53.48s）

| 时间 | vref | v | theta | theta_ref | err | balance_ref | avg_ref |
|---|---:|---:|---:|---:|---:|---:|---:|
| t+0.0s | 0.0 | 0.37 | 4.17° | 2.89° | +1.28° | +111 rpm | +111 rpm |
| t+0.2s | 0.0 | 0.44 | 5.01° | 2.49° | +2.52° | +187 rpm | +187 rpm |
| t+0.6s | 0.0 | 0.60 | 5.03° | 2.40° | +2.63° | +187 rpm | +187 rpm |
| t+1.0s | 0.0 | 0.72 | 5.79° | 2.40° | +3.39° | +235 rpm | +235 rpm |
| t+1.4s | 0.0 | 0.88 | 8.91° | 2.40° | +6.51° | +320 rpm | +320 rpm（饱和）|

**停车后速度从 0.37 → 0.88 m/s，车身从 4° → 9°，完全失控加速。**

### 根因分析

**小角度时速度外环权重不足，压不住角度环的加速输出。**

停车时逻辑：
1. `vref = 0`（目标速度0）
2. 实际 `v = 0.4~0.8 m/s`（速度误差大）
3. 速度外环输出：`speed_theta_ref = 6 × (0 - 0.8) = -4.8°`（后仰减速）
4. 但 `mode_bias = +4.0°`（FORWARD_BIAS，仍在 mode=1）
5. **最终目标角**：`theta_ref = 4.0 - 4.8 = -0.8°`（应该后仰）

**但数据显示 `theta_ref = 2.4°`（仍然前倾！），说明速度外环的 -4.8° 没有完全体现。**

同时：
- 实际车身 `theta = 4~9°`（前倾）
- 角度环看到前倾，输出正向加速（追赶重心，物理上对的）
- 但这让速度更大 → 车身更前倾 → 更加速 → **正反馈发散**

**问题**：
1. **FORWARD_BIAS 太大**（+4.0°）：停车时仍在 mode=1，bias 没有切换到 STAND
2. **速度外环增益太小**（KP=6）：速度误差 -0.8m/s 只给 -4.8° 修正，抵消不了 +4° 的 bias

### 解决方案

同时修改两个参数，让速度外环能压住速度：

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `4.0f` | `1.5f` | 降低前进姿态偏置，减少停车时的目标前倾角 |
| `BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS` | `6.00f` | `15.00f` | 加大速度外环增益，让速度误差能有效修正姿态 |

修改后：
- 启动时：`theta_ref = 1.5(bias) + 3.0(速度0.2×15) = +4.5°`（仍有足够前倾启动）
- 停车时速度误差 -0.8m/s：`speed_theta_ref = 15 × (-0.8) = -12°`
- **最终目标角**：`theta_ref = 1.5 - 12 = -10.5°`（强制后仰减速）
- 角度环看到实际 +4° 而目标 -10.5°，误差 +14.5°，输出大的负向减速
- **速度能压下来**

### 预期效果

- 启动：bias 1.5° + 速度外环 3° = 4.5° 前倾，足够破静摩擦
- 前进：速度外环主导姿态，bias 只是小偏置
- **停车：速度外环的 -12° 完全压制 bias +1.5°，强制后仰减速，速度能压到 0**

## 2026-07-08 数据：mode=0 静止时持续倒退，速度压不住

### 现象

- mode=1 前进基本正常（theta 平均 1.68°，velocity 平均 0.11 m/s）
- **但 mode=0 静止时出现持续倒退**：v ≈ -0.22 m/s，持续约 2 秒
- 最大倒退速度 -0.226 m/s，发生在 t=73.5s

### 典型倒退段数据（73.18~74.98s，mode=0）

| 时间 | v | theta | theta_ref | err | balance_ref | avg_ref | avg_actual (电流) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 73.18s | -0.218 | +0.343° | +1.376° | -1.033° | -68.6 rpm | -68.6 rpm | -0.009 A |
| 73.58s | -0.205 | +0.481° | +1.403° | -0.922° | -67.4 rpm | -67.4 rpm | -0.054 A |
| 74.28s | -0.200 | +0.597° | +1.505° | -0.908° | -61.0 rpm | -61.0 rpm | -0.046 A |
| 74.98s | -0.188 | +0.799° | +1.585° | -0.786° | -52.8 rpm | -52.8 rpm | -0.054 A |

**关键**：
- 车以 -0.22 m/s 匀速倒退（不是加速，是没刹住）
- 轮速目标 `avg_ref ≈ -67rpm`（负向/后退）
- **实际电流只有 0.04~0.06A，几乎没有输出**
- 车倒退不是控制驱动的，而是**重力/惯性导致的自由滑行**

### 根因分析

**mode=0 静止模式使用位置环，速度阻尼太弱，无法快速刹住倒退。**

mode=0 时的控制逻辑：
```c
speed_theta_ref = BALANCE_POSITION_KP × position_error +
                  BALANCE_POSITION_KV × speed_error + ...
```

当前参数：
- `BALANCE_POSITION_KP = 0.8 deg/m`
- `BALANCE_POSITION_KV = 3.0 deg/(m/s)`（速度阻尼）
- `BALANCE_MODE_STAND_BIAS = -0.8°`

倒退段分析：
- 车倒退 v = -0.22 m/s
- 已倒退约 2 米（位置误差）
- `speed_error = vref - v = 0 - (-0.22) = +0.22 m/s`
- 位置项贡献：`0.8 × 2.0 = +1.6°`
- **速度阻尼贡献：`3.0 × 0.22 = +0.66°`**（太小！）
- 总修正：`+2.2°`
- **最终目标角**：`theta_ref = -0.8 + 2.2 = +1.4°`

**问题**：
- 实际车身 `theta ≈ +0.5°`（前倾不够）
- 误差 `= 0.5 - 1.4 = -0.9°`（实际比目标后仰）
- 角度环输出：`70 × (-0.9) = -63rpm`（负向）
- **负向输出是让轮子后退，这在车已经倒退时是错的方向！**

**根本问题**：
1. **速度阻尼 KV=3 太小**：倒退 -0.22m/s 只给 +0.66° 修正，不够
2. **STAND_BIAS = -0.8° 太负**：拖累最终 theta_ref，让它只有 +1.4°
3. **实际车身前倾不够**（+0.5°），追不上目标 +1.4°
4. **角度环输出负向**（后退），无法刹住倒退，反而维持倒退状态

**物理逻辑**：
- 车倒退时，要刹住，需要让车身**充分前倾**（如 +3~4°）
- 轮子加速前进，追赶往后倒的重心
- 但现在目标只有 +1.4°，实际只有 +0.5°
- 角度环看到"后仰不足"，输出负向，让车继续倒退维持平衡

### 解决方案

**增大速度阻尼和静止偏置，让倒退时能快速给出大的前倾修正：**

| 项 | 修改前 | 修改后 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_STAND_BIAS_DEG` | `-0.8f` | `0.0f` | 提高静止模式的基准角度，不拖累 theta_ref |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `3.00f` | `12.00f` | 增大速度阻尼 4 倍，让速度误差能快速修正姿态 |

修改后，倒退 -0.22 m/s 时：
- `speed_error = +0.22 m/s`
- **速度阻尼贡献：`12 × 0.22 = +2.64°`**（增强 4 倍）
- 位置项贡献：`0.8 × 2.0 = +1.6°`
- 总修正：`+4.24°`
- **最终目标角**：`theta_ref = 0.0 + 4.24 = +4.24°`

如果实际车身能达到 `theta ≈ +4.5°`：
- 误差 `= 4.5 - 4.24 = +0.26°`（实际比目标更前倾）
- 角度环输出：`70 × 0.26 = +18rpm`（正向前进）
- **轮子加速前进，追赶重心，刹住倒退**

### 预期效果

- **静止模式**：速度阻尼增强 4 倍，倒退时能快速给出大的前倾修正，角度环输出正向，刹住倒退
- **启动/前进不受影响**：STAND_BIAS 只在 mode=0 生效，mode=1 用的是 FORWARD_BIAS=1.5°
- **停车过零不再大幅倒退**：过零后立即切换到强阻尼的位置环，快速刹住

## 2026-07-08 调整：降低直走目标速度

### 判断

- 最新数据中直走段 `velocity_ref` 为 `0.20m/s`，实际速度一度到 `0.33~0.34m/s`，主观表现为直走太快。
- 当前速度外环已经能在超速时给出反向姿态修正，优先先降低前进 base 目标，不继续加大姿态环或刹车逻辑。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_DRIVE_VELOCITY_MPS` | `0.20f` | `0.12f` | 降低直走目标速度，base rpm 约从 `62rpm` 降到 `37rpm`，同时保留启动时 `speed_theta_ref` 触及 `1.6deg` 的破静摩擦能力 |

### 下一步测试

- 烧录后测试 `发 1 -> 直走 -> 发 0`。
- 观察 `I4 velocity_ref_mps` 应稳定在约 `0.12m/s`。
- 重点看 `I5 velocity_mps` 是否仍明显超过 `0.20m/s`；若仍偏快，再考虑降低 `FORWARD_BIAS` 或提高/放宽前进速度外环制动权限。

## 2026-07-08 数据：降速后前进倾角偏小，稍微加大

### 数据结论

- 最新 `vofa+.csv` 为 16 路 `BALANCE_CASCADE`，共 `4560` 行，约 `91.2s`，全程 `fault=0`。
- `mode=1` 前进段共 `5` 段，累计约 `35.08s`。
- 降速后 `velocity_ref` 已降到约 `0.10~0.12m/s`，但前进段实际车身倾角均值只有约 `1.8~2.0deg`，主观表现为倾角偏小。
- 各前进段 `theta_ref` 都曾碰到 `3.00deg` 上限，说明启动/补速时倾角权限也略保守。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_FORWARD_BIAS_DEG` | `1.5f` | `1.7f` | 小幅提高前进巡航基准前倾角 |
| `BALANCE_PID_THETA_REF_LIMIT_DEG` | `3.0f` | `3.2f` | 允许启动/补速时最终目标倾角略高一点，避免一到 `3.0deg` 就顶死 |

### 下一步测试

- 烧录后继续测试 `发 1 -> 直走 -> 发 0`。
- 观察 `I0 theta_deg` 前进段均值是否从约 `2deg` 提到 `2.2~2.4deg`。
- 观察 `I5 velocity_mps` 是否更接近 `0.12m/s`，同时确认不会重新冲到 `0.25~0.30m/s`。

## 2026-07-08 数据：停车仍慢，确认不是左右轮速度环控不住

### 数据结论

- 最新 `vofa+.csv` 共 `3312` 行，约 `66.24s`；`fault` 从约 `59.66s` 后出现，前面的行走/停车段可用于判断。
- `mode=1` 前进共 `3` 段，速度目标约 `0.11m/s`，实际速度均值约 `0.14~0.16m/s`，峰值约 `0.28~0.31m/s`。
- 三次发 `0` 后，速度降到连续 `|v|<0.05m/s` 约需要 `1.94~2.52s`，停车位移约 `0.25~0.42m`，主观表现为停车偏慢。

### 左右轮/轮速环判断

- 非故障前进段：`avg_ref-avg_actual` 绝对误差平均约 `8.9rpm`，`actual_diff_lr` 绝对值平均约 `3.3rpm`，`avg_cmd` 最大约 `0.158A`，未接近 `0.40A` 限流。
- 非故障静止/停车段：`avg_ref-avg_actual` 绝对误差平均约 `8.1rpm`，`actual_diff_lr` 绝对值平均约 `1.8rpm`，同样无电流饱和。
- 判断：不是 left/right 速度环控不住，也不是左右轮明显不一致；底层轮速基本在跟上层给的目标。

### 停车慢的直接证据

- 停车窗口里 `I3/I1` 已经给了后仰目标，`I1` 经常顶到 `-3.2deg` 附近。
- 但 `I10 avg_ref_rpm` 大多数时间仍为正，典型停车段 `avg_ref` 约 `42~54rpm`，`avg_actual` 约 `39~51rpm`。
- 也就是说上层为了把车身拉到后仰姿态，轮子仍在较长时间顺着前进方向追平衡点；停车慢主要是高层停车策略问题，不是轮速 PID 失控。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_GAIN` | `0.0f` | `0.30f` | 在 stop-brake 窗口叠加很小的轮级反向速度前馈；`v=0.30m/s` 时约为 `-28rpm`，用于缩短停车位移，但避免大力反刹破坏直立 |

### 下一步测试

- 烧录后重复 `发 1 -> 发 0`。
- 重点看停车段 `I10 avg_ref_rpm` 是否比本次更快下降，`dur_to_|v|<0.05` 是否从 `2s` 量级缩短。
- 同时观察是否出现明显过零反冲；若反冲变大，先把 `STOP_BRAKE_GAIN` 降到 `0.15~0.20`。

## 2026-07-08 回滚：取消轮级反向 stop-brake 前馈

### 现场判断

- 轮级反向速度前馈会在姿态还没跟上时直接给反向轮速，存在把车身推倒/摔车风险。
- 当前停车慢先不通过 `STOP_BRAKE_GAIN` 解决，继续只保留姿态目标刹车和静止阻尼。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_GAIN` | `0.30f` | `0.0f` | 取消轮级反向前馈，避免停车时反向轮速直接破坏直立 |

## 2026-07-09 硬件变更适配：后方循迹模块 + 顶部相机/激光笔，首轮参数调整

### 硬件变化与物理影响

- 车体后方（低位悬臂支架）加装循迹模块 PCB；云台上方加装 MaixCAM 2 相机 + 激光笔。
- 三个物理后果：
  1. **重心前后偏移**（循迹模块在轮轴后方悬出）→ 自然平衡角改变，幅度未知，可能超出旧零点自学习 ±2deg 权限。旧数据曾测得裸车自然后仰 -4.4deg，加装后必须重新收敛零点。
  2. **重心升高 + 总质量增加**（相机/激光笔在最顶端）→ m·g·h 上升：同样角度误差需要更大回复力矩；摆变慢（自然频率下降）反而放宽相位裕度；正压力增大 → 静摩擦带变宽；同速度下动能更大 → 超速更难回收。
  3. 云台负载变化（相机挂在 yaw 电机上）本轮不动，只调平衡侧。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_ZERO_TRIM_LIMIT_DEG` | `2.0f` | `4.0f` | 新自然平衡角偏移未知，放宽自学习权限防止 trim 顶死 |
| `BALANCE_ZERO_TRIM_RATE_PER_SEC` | `0.02f` | `0.06f` | 时间常数 50s→~17s，首次上电静立 ~1min 内收敛到新零点 |
| `BALANCE_PID_CURRENT_LIMIT_A` | `0.40f` | `0.50f` | m·g·h 上升，同角度误差需要更大力矩；电机限 1.65A 余量充足 |
| `WHEEL_SPEED_FF_STATIC_A` | `0.018f` | `0.022f` | 变重后正压力/静摩擦上升约同比例 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.650f` | `0.750f` | 顶部质量动量更大，摆变慢后 D 上调风险低 |
| `BALANCE_SPEED_GUARD_ON_MPS` | `0.40f` | `0.35f` | 动能变大回收更难；07-07 曾两次 0.383 擦线未触发 |

### VOFA 通道变更（BALANCE_CASCADE 16 路）

- `I13` 从 `actual_diff_lr_rpm` 换为 `zero_trim_deg`（本轮关键观测量：零点收敛过程）。左右轮差分本轮不看，需要时再换回。

### 上电测试顺序（按序执行，不跳步）

1. **先只测静立收敛**：上电扶正放手，站 60~90s 不发指令。看 I13 `zero_trim` 是否单调收敛到一个稳定值：
   - 若稳定在 |trim| < 3.5deg → 零点吸收成功，记录该值，下轮把它并入 `BALANCE_ANGLE_OFFSET_DEG`，再把 RATE/LIMIT 收回 0.02/2.0。
   - 若 trim 顶死在 ±4.0 → 重心偏移超权限，直接改 `BALANCE_ANGLE_OFFSET_DEG`（当前 -2.0，往 trim 顶死方向加 trim 值）后重测。
2. 静立稳后轻推测扰动恢复：I12 `avg_cmd_a` 允许到 0.5A；若出现高频细抖/啸叫，先把 D 回 0.65，不要动 P。
3. 最后测 `发1 -> 直走 -> 发0`。变重后启动可能更难破摩擦（FF 已 +20%，必要时 0.022→0.026）；停车距离可能变长（先记录数据，不急着加 STOP_BRAKE_KV）。
4. 若直走前倾明显不足/过大：重心变化会改变巡航所需倾角，观察 I3/I1 后再动 `FORWARD_BIAS`（当前 1.7）。

### 回退顺序

- 高频抖：D 0.75→0.65。
- 救车过猛/过零反冲变大：CURRENT_LIMIT 0.50→0.40。
- 零点收敛后：RATE 0.06→0.02、LIMIT 4.0→2.0（防止长期运行 trim 被缓慢扰动带偏）。

## 2026-07-09 数据：mode=0 后走/前走的主因在 stand 外环目标过激

### 数据结论

- 最新 `vofa+.csv` 全程 `mode=0`、`fault=0`，约 `83.78s`。
- 轮速内环基本在执行上层目标：5s 后 `avg_ref` 与 `avg_actual` 最佳相关约 `0.945`，滞后约 `20ms`；`|avg_ref|>20rpm` 时目标/实际同号约 `97.6%`。
- Pitch 跟踪本身也有相关性：`theta_ref` 与 `theta` 最佳相关约 `0.980`，滞后约 `20ms`；但移动时 `|theta-theta_ref|` 平均约 `0.72deg`，95% 约 `1.45deg`。
- 后走时典型状态：`v<0`，`theta_ref` 被推到 `+2.8~+3.2deg`，实际 `theta` 只有 `+1.6~+2.1deg`；角度环为了追更大的前倾目标输出负向轮速，于是 `avg_actual` 也跟着负向，车继续后走。

### 判断

- 问题不是轮速内环控不住，而是 `mode=0` 的位置/速度外环把 `theta_ref` 推得太远。
- 之前把 `BALANCE_POSITION_KV` 从 `3` 拉到 `12` 是为了解决倒退刹不住；新数据证明 `12` 已经过激，会让目标 pitch 超过实际可跟踪量，形成来回游走。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `12.00f` | `7.00f` | 按 `v≈0.18m/s` 时目标超前约 `0.9deg` 反推，降低 stand 速度阻尼，避免目标 pitch 过冲 |
| `BALANCE_POSITION_THETA_REF_LIMIT_DEG` | 无 | `2.20f` | 给静止模式单独限幅，避免 stand 外环直接顶到全局 `±3.2deg` |

### 下一步测试

- 只测 `mode=0` 扶正放手，不发 `1`。
- 重点看：
  - 后走/前走时 `I1 theta_ref` 是否不再频繁顶到 `±3.2deg`，应主要落在 `±2.2deg` 内。
  - `I0 theta` 与 `I1 theta_ref` 的误差是否从移动时 `0.7~1.5deg` 降下来。
  - `I10 avg_ref` 和 `I11 avg_actual` 是否仍同向跟随；若跟随正常但车仍走，继续调 stand 外环，不动轮速 PID。
  - 若变成明显刹不住、速度长期单向漂，再把 `BALANCE_POSITION_KV` 从 `7` 小步加到 `8~9`，不要回到 `12`。

## 2026-07-09 数据：stand 外环收住后，直立环 Kp 偏软

### 数据结论

- 最新 `vofa+.csv` 共 `2726` 行，约 `54.52s`，全程 `mode=0`、`fault=0`。
- 轮速内环仍然基本跟随：`avg_ref` 与 `avg_actual` 最佳相关约 `0.962`，滞后约 `20ms`。
- 移动段 `|v|>0.05m/s` 时，`|theta-theta_ref|` 平均约 `1.07deg`，95% 约 `1.80deg`。
- 快速前走段 `v>0.10m/s`：`theta` 均值约 `-0.85deg`，`theta_ref` 均值约 `-2.36deg`，实际 pitch 落后目标约 `1.51deg`；轮速目标/实际约 `92.5/80.7rpm`，底层仍在跟。
- `avg_cmd` 最大约 `0.418A`，未打满 `0.50A`；`avg_ref` 只有约 `1.04s` 接近 320rpm 上限。

### 判断

- 上一轮把 stand 外环目标收回后，问题从“目标 pitch 过激”转为“实际 pitch 追不上目标”。
- 这次更像直立角度环 Kp 偏软：目标已经给了，轮速内环也在执行，但支点速度/电流力度不足以快速把车身追到目标角。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `70.00f` | `90.00f` | 将等效刚度从 `70*0.0025=0.175A/deg` 提到 `0.225A/deg`，先补约 29% 直立力度；不直接回到已振荡过的 `150` |

### 下一步测试

- 继续只测 `mode=0` 扶正放手。
- 重点看：
  - 移动段 `|theta-theta_ref|` 是否从 `1deg+` 降到 `0.6~0.8deg`。
  - `I10 avg_ref` 是否更快拉开，但不要长时间贴 `±320rpm`。
  - `I12 avg_cmd` 是否仍不长时间贴 `±0.50A`。
  - 若出现高频细抖/啸叫，优先把 Kp 回 `80` 或 D 从 `0.75` 回 `0.65`，不要动轮速 PID。

## 2026-07-09 数据：Kp=90 有效，继续小步加直立刚度

### 数据结论

- 最新 `vofa+.csv` 共 `1227` 行，约 `24.54s`，全程 `mode=0`、`fault=0`。
- 与上一轮相比，直立环 Kp 从 `70` 加到 `90` 后明显有效：
  - 移动段 `|v|>0.05m/s` 的 `|theta-theta_ref|` 平均从约 `1.07deg` 降到约 `0.67deg`。
  - 快速段 `|v|>0.10m/s` 的 `|theta-theta_ref|` 平均约 `0.75deg`，仍有继续加刚度空间。
- 底层未打满：
  - `avg_ref` 最大约 `159rpm`，未接近 `320rpm`。
  - `avg_cmd` 最大约 `0.282A`，未接近 `0.50A`。
  - 轮速跟随相关约 `0.930`，滞后约 `20ms`，仍是执行上层目标为主。

### 判断

- Kp=90 方向正确，但快走/倒走时 pitch 仍落后目标约 `0.7~0.8deg`。
- 由于电流和轮速目标都没有接近上限，可以继续补直立环 Kp。
- 不直接回到 `150`：此前 `150*0.0025=0.375A/deg` 已出现振荡；本轮只加到 `110*0.0025=0.275A/deg`。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `90.00f` | `110.00f` | 继续提高直立刚度，目标是把移动段 pitch 误差从 `0.7deg` 量级压到 `0.5deg` 左右 |

### 下一步测试

- 继续只测 `mode=0` 扶正放手。
- 重点观察：
  - `I0 theta` 是否更贴近 `I1 theta_ref`。
  - `I10 avg_ref` 峰值是否仍不长时间贴 `±320rpm`。
  - `I12 avg_cmd` 是否仍不长时间贴 `±0.50A`。
  - 若出现明显高频细抖/啸叫，先把 Kp 回 `100`，不要再加。

## 2026-07-09 数据：Kp=110 过界，原地模式需要限速和降 D

### 数据结论

- 最新 `vofa+.csv` 共 `1660` 行，约 `33.20s`，全程 `mode=0`、`fault=0`。
- 活动段约 `26.12s`，期间出现两类问题：
  1. **低频大幅前后走/跑飞**：`vmax≈0.99m/s`，`avg_ref` 贴到 `±320rpm` 约 `3.10s`，占活动段约 `11.9%`。
  2. **电机高频细抖**：`theta_rate` 主频约 `6.16Hz`，`cmd` 围绕均值换向约 `13.7次/s`；14s 与 22.9s 附近 `rate_d_ref` 大幅正负跳。
- 轮速内环仍在跟：`avg_ref -> avg_actual` 相关约 `0.941`，滞后约 `20ms`。所以大幅前走不是底层不听话，而是上层已经把轮速目标推到很大。
- 24s 附近典型跑飞：`theta_ref=-2.20deg`，实际 `theta` 从 `+0.52deg` 快速冲到 `+11deg`，`avg_ref=+320rpm`，速度接近 `0.9m/s`。这是直立环饱和追姿态，支点速度过大导致整车大幅前走。

### 判断

- `Kp=110` 已经越过当前机械状态的稳定边界；不能再继续加 Kp。
- 高频细抖主要由 D 项/角速度快速摆动放大；需要降低 D。
- 原地模式不应该允许直立环给到 `±320rpm` 的走车速度；需要 stand 专用速度上限，把大扰动限制在可控范围内。

### 参数/代码修改

| 项 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_ANGLE_KP_RPM_PER_DEG` | `110.00f` | `100.00f` | 回退到 90 和 110 之间，保留刚度但离振荡边界远一点 |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.750f` | `0.600f` | 降低角速度噪声/快速摆动对电机电流的高频注入 |
| `BALANCE_POSITION_THETA_REF_LIMIT_DEG` | `2.20f` | `2.00f` | 原地模式目标角略收紧，减少非最小相位初段支点大幅冲出 |
| `BALANCE_PID_STAND_SPEED_LIMIT_RPM` | 无 | `220.0f` | `mode=0` 单独限制直立环轮速目标，避免原地模式贴 `±320rpm` 跑飞 |

### 下一步测试

- 只测 `mode=0`，不要发 `1`。
- 重点看：
  - `I10 avg_ref_rpm` 是否不再长时间贴 `±320rpm`，应被限制在约 `±220rpm`。
  - 电机细抖是否减轻，`I12 avg_cmd_a` 不应频繁快速正负跳。
  - 若仍大幅前后走但不高频抖，再调 stand 外环；若高频抖仍明显，继续降 D 到 `0.50`。

## 2026-07-09 数据：原地仍漂，先并入重心/零点 bias

### 数据结论

- 最新 `vofa+.csv` 共 `5615` 行，约 `112.30s`；`fault=3` 从第 `5182` 行开始，约 `103.64s`，触发时 `theta=-30.73deg`。
- 保护前轮速内环仍在执行：末段已被 stand 上限限制到 `avg_ref≈-220rpm`，`avg_actual≈-220rpm`，所以不是“轮速没跟上”导致的主问题。
- 低速近似静止窗口（`|v|<0.03m/s`、`|theta_rate|<10dps`、`|avg_actual|<5rpm`）：
  - `theta` 中位数约 `+0.75deg`，均值约 `+0.52deg`。
  - `theta_ref` 中位数约 `+0.76deg`，均值约 `+0.52deg`。
  - `zero_trim` 中位数约 `+0.07deg`，末值仅 `+0.244deg`。
- 更严格静止窗口（`|v|<0.015m/s`、`|theta_rate|<5dps`、`|avg_actual|<3rpm`）仍给出 `theta/theta_ref` 中位数约 `+0.75deg`。

### 判断

- 数据支持用户现场判断：现在还有重心/机械零点 bias 没配准。
- 这次不继续加直立环 Kp；`Kp=110` 已经出现高频细抖和大幅走车，当前应先把自然平衡点挪回控制坐标零点。
- 由于 `theta = raw - BALANCE_ANGLE_OFFSET_DEG - zero_trim`，当前低速平衡点约为 `+0.75deg`，且当时已有 `zero_trim≈+0.07deg`；等效固定 offset 欠补约 `+0.8deg`。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_ANGLE_OFFSET_DEG` | `-2.0f` | `-1.2f` | 将最新低速静立窗口的约 `+0.8deg` 残余重心/零点偏置并入固定零点，下次上电不再主要依赖 RAM trim |

### 下一步测试

- 只测 `mode=0` 扶正放手，不发 `1`。
- 看 `I0 theta` 在轮子接近不动时是否从 `+0.7~1.5deg` 回到 `0deg` 附近。
- 看 `I13 zero_trim` 是否只慢慢学到小残差（理想稳定在 `|trim|<0.5deg`）；若又单向涨到 `>1deg`，说明固定 offset 仍需再并一次。
- 若改后变成反方向长期漂移，先把 offset 往回退半度（例如 `-1.5f`），不要再加 Kp。

## 2026-07-09 数据：offset 生效，剩余问题是静立电机高频细抖

### 数据结论

- 最新 `vofa+.csv` 共 `4811` 行，约 `96.22s`，全程 `mode=0`。
- `fault=3` 在约 `92.12s` 触发；触发前 `70~90s` 已经能稳定静立：
  - `theta` 均值约 `+0.20deg`，RMS 约 `0.22deg`。
  - `theta_ref` 均值约 `+0.20deg`。
  - `v≈0`，实际轮速均值接近 0。
  - `zero_trim` 最后约 `+0.10deg`。
- 因此上一轮 `BALANCE_ANGLE_OFFSET_DEG=-1.2f` 方向正确：静止窗口从此前 `theta≈+0.75deg` 收到本次 `+0.2deg` 附近，不再优先改 offset。
- 用户反馈“电机抖动声明显”，数据对应：
  - `70~90s` 静立段 `avg_ref` RMS 约 `12rpm`。
  - `avg_cmd` RMS 约 `0.057A`。
  - `I8/I9/I10/I12` 翻向约 `19~21次/s`。
  - `rate_d_ref` RMS 约 `10rpm`，说明高频主要由陀螺 D 项和小轮速目标的静摩擦前馈共同放大。
- 摔倒前 `91.96~92.10s` 是突发大扰动/支点冲出：`theta` 从 `1.9deg` 到 `28deg`，`avg_ref` 被 stand 上限压在 `+220rpm`，`avg_actual≈134~184rpm`，`cmd≈0.11~0.26A`。这不支持继续加 Kp；当前先压静立高频。

### 参数/代码修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.600f` | `0.500f` | 降低陀螺角速度噪声进入轮速目标的高频分量 |
| `WHEEL_SPEED_FF_STATIC_RAMP_RPM` | 无 | `30.0f` | 30rpm 以下静摩擦前馈线性爬坡，避免原地小目标一上来就敲满 `0.022A` |
| `Chassis_SpeedFeedforward()` | `abs(ref)>0` 即给完整静摩擦前馈 | `static_ff *= abs_ref / 30rpm`（仅 30rpm 以下） | 保留大目标破静摩擦能力，同时降低静立细抖声 |

### 下一步测试

- 只测 `mode=0` 静立 60~90s。
- 重点听电机细抖声是否明显下降。
- 看 `I12 avg_cmd_a` 静立 RMS 是否从约 `0.057A` 降到 `0.03~0.04A` 附近。
- 看 `I10 avg_ref_rpm` 是否仍在小范围翻向，但电流输出不再跟着硬敲。
- 若抖声明显下降但抗扰变钝，再把 `BALANCE_PID_RATE_KD` 小回调到 `0.55`；若仍抖，再把 `WHEEL_SPEED_FF_STATIC_RAMP_RPM` 提到 `40rpm`，不要加 Kp。

## 2026-07-09 数据：低速抖声压制后又后走，stand 外环仍偏激

### 数据结论

- 最新 `vofa+.csv` 共 `2337` 行，约 `46.74s`，全程 `mode=0`，`fault=0`。
- 不是纯单向后滑，而是前后游走且偏后走：
  - `v<-0.05m/s` 共约 `14.86s`。
  - `v>0.05m/s` 共约 `10.56s`。
- 后走段关键状态：
  - `theta_ref` 均值约 `+1.32deg`，实际 `theta` 均值约 `+0.92deg`。
  - 实际 pitch 比目标少约 `0.4deg`，角度 P 项平均约 `-39rpm`。
  - D 项平均约 `+9rpm`，合成 `avg_ref≈-31rpm`；`avg_actual≈-32rpm`，轮速内环在执行。
- 所以这次不是轮速环不听，也不是零点没修好，而是 `mode=0` 的位置/速度外环又把 `theta_ref` 推得略超过车身可跟踪量，导致角度环长期给后退轮速。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.80f` | `0.50f` | 降低位置回中项，避免小位移积累把目标 pitch 推远 |
| `BALANCE_POSITION_KV_DEG_PER_MPS` | `7.00f` | `5.00f` | 降低原地速度阻尼目标角，减少非最小相位阶段继续带车后走 |
| `BALANCE_POSITION_THETA_REF_LIMIT_DEG` | `2.00f` | `1.60f` | 限制 stand 外环最大目标角，防止目标长期跑在实际车身前面 |

### 下一步测试

- 继续只测 `mode=0`。
- 重点看后走时 `I1 theta_ref` 是否从 `+1.3~2.0deg` 收到约 `+0.8~1.4deg`。
- 看 `I0 theta` 与 `I1 theta_ref` 的误差是否不再长期保持 `-0.4deg` 左右。
- 若仍偏后走但不再大幅游走，优先把 `BALANCE_POSITION_KP` 再降到 `0.30`；若变成刹不住慢漂，再把 `KV` 小回调到 `5.5`，不要动直立 Kp。

## 2026-07-09 数据：KP=0.5/KV=5/limit=1.6 后原地静立显著改善

### 数据结论

- 最新 `vofa+.csv` 共 `4185` 行，约 `83.70s`，全程 `mode=0`，`fault=0`。
- 除前 5s 放手/初始扰动外，后续基本纯静立：
  - `quiet` 窗口累计约 `80.32s`。
  - `theta` 均值约 `+0.055deg`，中位数约 `+0.032deg`。
  - `theta_ref` 中位数约 `+0.032deg`。
  - `|v|` RMS 约 `0.002m/s`，几乎不移动。
  - `avg_ref` RMS 约 `4.32rpm`，`avg_actual` RMS 约 `0.87rpm`。
  - `avg_cmd` RMS 约 `0.027A`，比上一版静立抖声对应的 `0.057A` 明显下降。
- 后走基本被消掉：
  - `v<-0.05m/s` 仅 `0.70s`。
  - `v>0.05m/s` 仅 `0.06s`。
- `zero_trim` 从 0 慢慢学到约 `+0.15deg`，属于小残差，不需要再大改 `BALANCE_ANGLE_OFFSET_DEG`。

### 判断

- `BALANCE_POSITION_KP=0.5 / KV=5 / theta_ref_limit=1.6` 是目前最稳的原地模式组合。
- 上一轮“后走”根因确认为 stand 外环目标过激；收软后已解决。
- 低速静摩擦前馈斜坡 + `D=0.5` 对电机细抖也有效，`avg_cmd` RMS 已降到约一半。

### 决定

- 本轮不改参数，先保留当前配置。
- 下一步只做两类验证：
  1. 长站 `mode=0` 90~120s，确认 `zero_trim` 是否稳定在 `+0.1~0.2deg` 附近，不再继续单向爬。
  2. 轻推扰动恢复，确认不会因为 stand 外环收软而“慢慢漂走”；若轻推后回中太慢，再只把 `BALANCE_POSITION_KP` 从 `0.50` 小回调到 `0.60`，不要动直立 Kp。

## 2026-07-09 数据：手推后走起来仍有抖，根因在 stand 受扰刹车侵入性

### 数据结论

- 最新 `vofa+.csv` 共 `5468` 行，约 `109.36s`，全程 `mode=0`、`fault=0`，不是 `mode=1` 前进数据。
- 静立仍然很好：`quiet` 窗口约 `97.02s`，`theta` RMS 约 `0.087deg`，`v` RMS 约 `0.002m/s`，`avg_cmd` RMS 约 `0.026A`。
- 手推移动段累计约 `6.72s`，主要集中在 `17~33s`：
  - `I1/I3 theta_ref` 方向基本正确，不是速度目标乱翻；翻向仅约 `0.45次/s`。
  - 但 `I7/I10 avg_ref` 翻向约 `22.8次/s`，`I12 cmd` 翻向约 `26次/s`，说明移动时内层轮速目标/电流有明显高频拉扯。
  - 典型手推正向段 `v≈+0.17m/s` 时，`theta_ref≈-1.85deg`、`theta≈-1.24deg`；反向回弹段 `v≈-0.10m/s` 时，`theta_ref≈+1.0deg`、`theta≈+0.6~0.8deg`。
- 因此这次不是“发 1 前进外环抖”，而是 `mode=0` 受扰恢复：普通 stand 速度项叠加 stop-brake 姿态域刹车，轻推后目标角偏激，车身追上后产生反向回弹。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_LOCK_ON_MPS` | `0.10f` | `0.14f` | 小速度手推先只靠普通 stand 阻尼，避免一过 `0.10m/s` 就进入强姿态刹车 |
| `BALANCE_MODE_STOP_BRAKE_KV_DEG_PER_MPS` | `8.0f` | `6.0f` | 降低 stop-brake 额外目标角，减少回弹 |
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.3f` | `1.0f` | 回到 07-08 验证过更保守的刹车倾角上限；`1.5deg` 曾明显加重抖动 |

### 下一步测试

- 继续 `mode=0` 长站后轻推，不发 `1`。
- 重点看轻推后：
  - `I1 theta_ref` 峰值是否从约 `±1.8deg` 降到 `±1.3~1.5deg`。
  - `I10/I12` 高频翻向是否减少，电机声音是否更顺。
  - 回中距离可以略长一点，但不应继续前后点头。
- 若仍抖但回中很快，下一步继续把 `STOP_BRAKE_TREF_LIMIT` 降到 `0.8deg`；若变成推一下滑很远，再把 `LOCK_ON` 回到 `0.12`。

## 2026-07-09 数据：38deg 为手动放倒，真正问题是 stand 目标角压迫后仰

### 数据结论

- 最新 `vofa+.csv` 共 `6350` 行，约 `127.00s`，全程 `mode=0`。
- `121.74s` 后 `fault=3`、`theta≈38deg` 是手动放倒触发保护，不作为控制器自行摔倒证据。
- 放倒前有效数据约 `121.74s`：
  - quiet 窗口约 `21.86s`，`theta` 均值约 `+0.176deg`，`theta_ref` 均值约 `+0.184deg`，`zero_trim` 均值约 `+0.031deg`。
  - `100~120s` quiet 段 `theta≈+0.42deg`、`theta_ref≈+0.39deg`、`trim≈+0.11deg`，有小残余后仰偏置，但不是几度级零点跑飞。
- 移动段才是主要问题：
  - 后退段累计约 `46.22s`，`v≈-0.167m/s`，`theta_ref≈+1.37deg`，实际 `theta≈+0.65deg`。
  - 前进段累计约 `26.56s`，`v≈+0.199m/s`，`theta_ref≈-1.46deg`，实际 `theta≈-0.75deg`。
  - 轮速内环在执行：后退段 `avg_ref≈-51.8rpm`、`avg_actual≈-51.4rpm`，不是轮速跟不上。
- 关键异常：当前 `BALANCE_POSITION_THETA_REF_LIMIT_DEG=1.6`，但数据里 `I1/I3` 多次到 `±2.6deg`。原因是普通 stand 外环先限到 `±1.6deg`，后面 stop-brake 再叠加 `±1.0deg`，总目标角绕过了 stand 限幅，形成用户说的“偏移压迫加大、往后仰”。

### 参数/代码修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---:|---:|---|
| `BALANCE_POSITION_KP_DEG_PER_M` | `0.50f` | `0.30f` | 降低位置回中对目标 pitch 的持续压迫，后退时不要长时间顶着 `+1.5~2.6deg` 后仰目标 |
| stop-brake 后总目标角 | stand 外环 `±1.6deg` 后又叠加 stop-brake，实际可到 `±2.6deg` | stop-brake 叠加后，`drive_bias_mode==0` 时再次用 `BALANCE_POSITION_THETA_REF_LIMIT_DEG` 限幅 | 保留刹车方向，但不允许静立模式目标角超过原地限幅 |

### 下一步测试

- 仍然只测 `mode=0`：静立 20s 后轻推/放手，不主动放倒。
- 重点看：
  - `I1/I3 theta_ref` 峰值应被压回 `±1.6deg` 内，不再出现 `±2.6deg`。
  - 后退时 `theta_ref-theta` 不应长期维持约 `+0.7deg` 压迫差。
  - 回中距离可能稍长，但后仰感和电机拉扯声应下降。
- 若变成推一下滑很远，再把 `BALANCE_POSITION_KV` 从 `5.0` 小回调到 `5.5`；若仍然后仰压迫，再考虑把 stand 限幅从 `1.6` 降到 `1.4`，不要加直立 Kp。

## 2026-07-09 结构调整：mode 与 Application 同级，循迹走 UART1

### 结构决定

- 新增根目录 `mode/`，和 `Application/`、`Device/` 同级，不放在 `Application` 内。
- 模式文件拆分为：
  - `mode_control.c/.h`
  - `mode_stop.c`
  - `mode_forward.c`
  - `mode_backward.c`
  - `mode_turn_left.c`
  - `mode_turn_right.c`
  - `mode_tracking.c`
- `MODE_TRACKING.c/.h` 是四轮车旧版 PWM 循迹逻辑，本轮不接入平衡车控制链。

### 串口分配

- VOFA 保持原接口：`UART6/USART6`，发送和接收回调都只走 `bsp_uart6`。
- 循迹模块使用 `UART1/USART1`，在 `Device/track` 解析 `$...#` 数据帧，再由 `mode_tracking` 转换成平衡车的高层命令。
- `UART1` 不再给 VOFA 注册回调，避免循迹和 VOFA 抢同一个 RX callback。

### 控制接口

- 上层可用数字 `5` 或文本 `track` / `tracking` / `line` 进入循迹模式。
- 循迹模式不直接输出四轮车 PWM，也不直接写电机电流；只输出：
  - `velocity_mps`
  - `bias_deg`
  - `turn_rpm`
  - `drive_enabled`
- 平衡、速度、转向混控仍由 `gimbal_task.c` 的现有平衡车控制链执行。

### 构建验证

- 使用 `D:\MAKE\make.exe -j8` 完成全量构建，生成 `build/spi_bmi088.elf/.hex/.bin`。
- 修正 Makefile object 命名：不再把所有源文件压平成 `build/<basename>.o`，而是用源路径生成唯一 object 名，例如 `mode/mode_tracking.c -> build/mode_mode_tracking.o`。
- 目的：避免 Windows 大小写不敏感时旧四轮 `MODE_TRACKING.c` 被误当成 `mode/mode_tracking.c` 编译。
- 增量构建再次运行结果为 `make: Nothing to be done for 'all'.`

## 2026-07-09 VOFA 临时切换：输出循迹 8 路模拟量

### 修改

- VOFA 物理接口不变，仍然使用 `UART6/USART6`。
- 暂时关闭原平衡车 `BALANCE_CASCADE` 回传，把 `VOFA_DEBUG_STAGE` 切到 `VOFA_DEBUG_STAGE_TRACK_ADC`。
- VOFA CSV 现在输出 8 路循迹 ADC：
  - `track_x1_adc`
  - `track_x2_adc`
  - `track_x3_adc`
  - `track_x4_adc`
  - `track_x5_adc`
  - `track_x6_adc`
  - `track_x7_adc`
  - `track_x8_adc`

### 恢复方法

- 若要恢复原平衡车回传，把 `Inc/config.h` 里的：

```c
#define VOFA_DEBUG_STAGE                VOFA_DEBUG_STAGE_TRACK_ADC
```

改回：

```c
#define VOFA_DEBUG_STAGE                VOFA_DEBUG_STAGE_BALANCE_CASCADE
```

### 构建验证

- 使用 `D:\MAKE\make.exe -j8` 构建通过，生成 `build/spi_bmi088.elf/.hex/.bin`。

## 2026-07-09 数据：stand 外力停车变钝，恢复昨天 stop-brake 权限

### 数据结论

- 最新 `vofa+.csv` 全程 `mode=0`，不是 `forward -> stand` 切换问题。
- 外力推起来后，`avg_ref` 与 `avg_actual` 基本同向跟随，`avg_ref` 多次贴 `±220rpm` stand 限幅；所以不是轮速环控不住。
- 失败段里 `theta_ref/speed_theta_ref` 已经与速度反号，但由于 `LOCK_ON=0.14m/s` 触发偏晚，车身在进入强刹车前已经被推到较大姿态；随后直立环只能追支点救姿态，表现为停车距离长甚至 fault。
- 昨天 `process` 已确认：`stand` 收到外力也必须触发姿态域 stop-brake，不能只靠普通 stand 速度项慢慢蹭。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_LOCK_ON_MPS` | `0.14f` | `0.10f` | 回到昨天方向锁基线，更早进入 stand 外力刹车窗口 |
| `BALANCE_MODE_STOP_BRAKE_KV_DEG_PER_MPS` | `6.0f` | `8.0f` | 恢复姿态域刹车力度，让车身更快转到减速侧 |
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.0f` | `1.3f` | 介于昨天验证过的 `1.0~1.5deg` 之间，增加消能但避免过猛 |

### 注意

- 当前代码已经在 stop-brake 叠加后，对 `drive_bias_mode==0` 再次用 `BALANCE_POSITION_THETA_REF_LIMIT_DEG=1.6deg` 限总目标角。
- 因此本轮不会回到之前 `stand 外环 ±1.6deg + brake ±1.0deg = ±2.6deg` 的压迫后仰问题；刹车项主要作用在更早触发和更快顶到 stand 允许的目标角。

### 下一步测试

- 只测 `mode=0`，不要发前进后退。
- 先轻推，再中等力度推，不要直接大力推到 `|v|>0.5m/s`。
- 重点看：
  - `I3 speed_theta_ref` 是否在 `|v|≈0.10m/s` 时立刻反号并接近 `±1deg`。
  - `I5 velocity_mps` 从 `0.15~0.25m/s` 回到 `|v|<0.05m/s` 是否明显快于上一版。
  - 如果出现低速来回点头，再把 `TREF_LIMIT` 回 `1.0`，不要动轮速 PID。

## 2026-07-09 数据：角度/轮速目标提前衰减，stop-brake 改为短时更用力

### 数据结论

- 最新 `vofa+.csv` 仍是 `mode=0` 外力推车测试。
- `|avg_ref|>40rpm` 的窗口里，`avg_ref-avg_actual` 绝对误差均值约 `25.8rpm`，RMS 约 `32.3rpm`，说明轮速实际值经常没追到目标。
- 多个片段能看到目标轮速先冲到 `50~100rpm`，实际轮速只到目标的 `20%~60%` 时，上层目标已经开始回落；用户现场说的“轮子转速软、到一半就衰减”成立。
- 失败前典型片段：`theta_ref` 从 `-0.44 -> -1.60deg` 受 `20deg/s` 斜坡限制，`avg_ref` 顶到 `220rpm` stand 上限，随后车身角度继续放大。问题不是单纯轮速 PID 失控，而是 stop-brake 阶段目标角/轮速权限仍偏保守。

### 参数/代码修改

| 项 | 旧值/旧逻辑 | 新值/新逻辑 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00250f` | `0.00300f` | 轮速内环 P 小加一档，减少目标到一半实际追不上就松手的软感 |
| stop-brake 总目标角限幅 | 仍用普通 stand `±1.6deg` | stop-brake 激活时临时放到 `±2.0deg` | 普通静立不变，外力推车时给姿态刹车多一点角度权限 |
| `speed_theta_ref` 斜坡 | `20deg/s` | stop-brake 时 `45deg/s` | 让刹车目标更快到位，减少 60~100ms 内继续被推远 |
| stand 轮速上限 | stop-brake 也被 `220rpm` 限制 | stop-brake 时临时 `280rpm` | 避免救车时轮速目标刚起来就被截断 |
| 电流权限 | 普通 balance `0.50A / 60A/s` | stop-brake 时临时 `0.60A / 100A/s` | 给短时涌流留余量，仍低于电机侧 `1.65A` |

### 下一步测试

- 只测 `mode=0` 外力推车，不发前进。
- 重点看 `I3` 是否能更快到 `±1.6~2.0deg`，`I10 avg_ref` 是否能短时超过旧的 `±220rpm`，`I11 avg_actual` 是否明显更快贴近目标。
- 如果低速静立抖声明显变大，先把 `WHEEL_SPEED_PID_KP` 回到 `0.00270~0.00280`；如果只是在大推时仍不够刹，再优先把 stop-brake speed limit 从 `280` 小加到 `300`。

## 2026-07-09 数据：加力后抖动明显，关键是 recover 过早切掉 stop-brake

### 数据结论

- 最新 `vofa+.csv` 共 `4422` 行，约 `88.44s`，`mode=0` 有效数据约 `82.90s`。
- 上一版新权限确实生效：
  - `theta_ref` 已到 `±2.0deg`。
  - `avg_ref` 短时到 `280rpm`。
  - `cmd_a` 峰值到约 `±0.48A`。
- 但抖动明显变大：全段 `cmd_a` RMS 约 `0.151A`，移动段 `cmd` 翻向约 `20次/s`。
- 失败前关键片段：
  - `82.38s`: `theta=1.31deg`、`theta_ref=-1.28deg`、`avg_ref=276rpm`，stop-brake 仍在工作。
  - `82.40s`: `theta=2.32deg` 后，代码里的 `BALANCE_MODE_BRAKE_RECOVER_ANGLE_DEG=2.0deg` 把 `stop_brake_active` 关掉，`avg_ref` 回落到普通 stand 的 `220rpm`。
  - 随后 `theta` 从 `2.32deg` 一路涨到 `29deg`，触发 `fault=3`。
- 结论：不是 bias 完全不够，而是刚需要继续压住停车 bias 时，recover 保护过早退出了 stop-brake。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `WHEEL_SPEED_PID_KP_A_PER_RPM` | `0.00300f` | `0.00250f` | 回退上一版内环加硬，先压住高频抖动 |
| `BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG` | `2.0f` | `2.2f` | 按现场判断，小幅加大停车姿态 bias 权限 |
| `BALANCE_MODE_STOP_BRAKE_THETA_SLEW_DEG_PER_SEC` | `45.0f` | `30.0f` | 减少目标角快速抽动导致的电机敲击 |
| `BALANCE_MODE_STOP_BRAKE_CURRENT_LIMIT_A` | `0.60f` | `0.50f` | 回到普通限流，避免高频时电流声过硬 |
| `BALANCE_MODE_STOP_BRAKE_CURRENT_SLEW_A_PER_SEC` | `100.0f` | `60.0f` | 回到普通电流斜率，降低抖动声 |
| `BALANCE_MODE_BRAKE_RECOVER_ANGLE_DEG` | `2.0f` | `6.0f` | 不再刚过 `2deg` 就切掉 stop-brake，让停车 bias 能持续到中等倾角 |

### 下一步测试

- 仍然只测 `mode=0` 外力推车。
- 重点看失败前 `I10 avg_ref` 是否能在 `theta=2~6deg` 继续保持 stop-brake 权限，而不是立刻回到 `220rpm`。
- 同时听电机抖动是否比上一版下降；若仍抖，下一步不加 Kp，优先继续降低 `THETA_SLEW` 或缩小 `TRACK_ERR`。

## 2026-07-09 数据：纠正为后倾，stop-brake 后仰目标过强且抖动大

### 数据结论

- 最新 `vofa+.csv` 共 `3045` 行，约 `60.90s`，全程 `fault=0/mode=0`。
- 数据中有一段 `theta=-17deg` 附近但 `avg_ref=0/cmd=0`，应为非 balance 输出/手扶状态，不用于闭环调参。
- 排除零输出后，闭环有效数据约 `2879` 行：
  - 静立窗口 `theta` RMS 约 `0.222deg`，说明零点本身不是主要问题。
  - 移动段 `cmd` 翻向约 `26.7次/s`，电机抖动确实很大。
  - 后倾窗口（按历史符号 `theta< -1deg`）中，`theta_ref` 平均约 `-1.69deg`、中位约 `-1.86deg`，多次贴到 `-2.2deg`。
- 结论：用户纠正后，问题不是 bias 不够，而是 stop-brake 把目标角压得太后，车身追到后倾侧后被 D/P 内环高频拉扯。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.500f` | `0.400f` | 降低角速度 D 项注入，压电机高频抖声 |
| `BALANCE_MODE_STOP_BRAKE_KV_DEG_PER_MPS` | `8.0f` | `6.0f` | 降低速度转姿态刹车增益，减少后倾侧压迫 |
| `BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG` | `1.3f` | `0.9f` | 限制额外 stop-brake 姿态量 |
| `BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG` | `2.2f` | `1.7f` | 最终目标角不再长期顶到 `-2.2deg` 后倾 |
| `BALANCE_MODE_STOP_BRAKE_THETA_SLEW_DEG_PER_SEC` | `30.0f` | `20.0f` | 目标角变化更慢，减少冲击 |
| `BALANCE_MODE_STOP_BRAKE_TRACK_ERR_DEG` | `1.8f` | `1.2f` | stop-brake 目标更贴近当前车身，避免目标角跑得太远 |

### 下一步测试

- 只测 `mode=0` 外力推车。
- 重点看后倾窗口 `theta< -1deg` 时，`theta_ref` 是否从 `-1.8~-2.2deg` 收到约 `-1.0~-1.7deg`。
- 看 `I10/I12` 翻向和电机抖声是否明显下降；若停车又变钝，再只小回调 `STAND_TREF_LIMIT 1.7 -> 1.9`，不要直接回到 `2.2`。

## 2026-07-09 整车正方向翻转（陀螺 pitch + 编码器 + 电流 + 差速全链路取反）

### 目标

把平衡车"正方向"整体反号：原正方向变负、负方向变正。物理行为完全不变
（倒立摆镜像对称），只是坐标约定翻转——翻转后 `forward` 指令会朝原来的
"后退"方向走，所有遥测符号相对旧 CSV 反号。

### 原理与改动清单（全部集中在 `Inc/config.h`，共 9 个宏）

方向约定只在两个边界进出：IMU→姿态（`ins_task.c`）和 CAN 轮端
（`bsp_can.c` 的反馈解析 + `Motor_SetCurrent`）。级联内部全部工作在
归一化域且奇对称（launch/stop-brake/前馈都按速度方向取号），零点自学习、
守卫、限幅全是绝对值逻辑，所以只翻边界宏即可，控制代码一行不动：

| 宏 | 旧值 | 新值 | 说明 |
|---|---:|---:|---|
| `BALANCE_ACCEL_ANGLE_SIGN` | `-1` | `+1` | 加速度角反号 |
| `BALANCE_GYRO_SIGN` | `-1` | `+1` | 陀螺角速度同步反号，两源一致性保持 |
| `BALANCE_ANGLE_OFFSET_DEG` | `-1.2` | `+1.2` | θ=raw×SIGN−OFFSET，角度域翻转 offset 必须跟着反号 |
| `LEFT_WHEEL_FEEDBACK_SIGN` | `-1` | `+1` | 左轮转速/电流反馈 |
| `RIGHT_WHEEL_FEEDBACK_SIGN` | `+1` | `-1` | 右轮转速/电流反馈 |
| `LEFT_WHEEL_CURRENT_SIGN` | `-1` | `+1` | 左轮电流指令（与反馈成对翻，内环负反馈保持） |
| `RIGHT_WHEEL_CURRENT_SIGN` | `+1` | `-1` | 右轮电流指令 |
| `BALANCE_PID_LEFT_YAW_SIGN` | `-1` | `+1` | 差速转向：轮极性翻转后须跟翻，保持 turn>0 物理旋向不变 |
| `BALANCE_PID_RIGHT_YAW_SIGN` | `+1` | `-1` | 同上（yaw 陀螺 `CHASSIS_YAW_GYRO_SIGN` 不动） |

### 明确不改的（查证过）

- `BALANCE_PID_POLARITY / POSITION_SIGN / VELOCITY_SIGN / WHEEL_SPEED_PID_POLARITY`：
  归一化域内的相对符号，随边界一起镜像，保持 `+1`。
- `CHASSIS_YAW_GYRO_SIGN / YAW_RATE_BIAS`：绕竖直轴旋转不受前后翻转影响。
- 各 bias/速度常数（`FORWARD_BIAS/LAUNCH_BIAS/DRIVE_VELOCITY/TRACKING_*`）：
  定义在约定域（+ = 前进），语义随新方向自动生效，数值不动。
- `motor->angle / encoder_raw`：只写不读；`Motor_SetAngle` 无调用点。
- `global_balance_accel_forward_ms2`：仅进遥测通道，无控制消费者。
- 旧云台宏（`PITCH_POLARITY` 等）：无 .c 消费者，死代码。

### 影响与上电检查项

1. **零点自学习**：RAM trim 掉电清零，新会话从 `OFFSET=+1.2` 基准重新收敛，
   首次上电静立 ~1min 再动车。
2. **B 命令**：手动 bias 在角度域，旧经验值现在要**反号**输入。
3. **VOFA 对比**：新 CSV 的 theta/v/rpm/cmd 全部与旧数据反号，跨会话对比时注意。
4. 上电顺序验证：先手持验证 θ 符号（车头朝新正方向压低应读 +θ）、
   推轮验证 v 符号（朝新正方向滚动应读 +rpm/+v），再解锁闭环；
   最后各试一次 forward/left，确认走向与旋向符合新约定。

## 2026-07-09 数据：一直往前走停不住，纠正不再重试轮级反刹

### 数据结论

- 最新 `vofa+.csv` 全程 `mode=0/fault=0`，`vref=0`，说明这是 stand 外力/停车问题，不是前进指令残留。
- 有效闭环段里，速度为正时 `theta_ref` 已经给到负向，速度为负时 `theta_ref` 已经给到正向；也就是说姿态刹车方向没有错。
- 但移动段里 `avg_ref/avg_actual` 仍经常跟着实际速度同向跑。现场表现就是“目标角在刹，但轮子为了追姿态仍顺着车走”，所以只继续加 `theta_ref` 权限会变成前后摆和抖动。
- 复查 07-07/07-08 记录：轮级反向 stop-brake 已被验证会和姿态域刹车打架，姿态还没跟上时直接给反向轮速有摔车/反冲风险；因此不再重试 `STOP_BRAKE_GAIN` 路线。
- 当前只能沿已验证路线小步修正：姿态域 stop-brake 保持，若停车变钝，只小回调 `STAND_TREF_LIMIT 1.7 -> 1.9`，不要回到 `2.2`，也不要重新打开轮级反刹。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_MODE_STOP_BRAKE_GAIN` | `0.15f` | `0.0f` | 撤销误改，轮级反向前馈不再尝试 |
| `BALANCE_MODE_STOP_BRAKE_LIMIT_RPM` | `45.0f` | `160.0f` | 反刹关闭后恢复名义值，当前不参与输出 |
| `BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG` | `1.7f` | `1.9f` | 按上一节建议，只小幅增加姿态域停车权限，避免重新顶到 `2.2deg` |
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.400f` | `0.350f` | 压低角速度 D 的高频注入，先减轻电机抖动声 |

### 下一步测试

- 只测 `mode=0` 外力推车，不发前进后退。
- 看 `I1/I3 theta_ref` 是否允许到约 `±1.9deg`，但不再出现 `±2.2deg` 长时间压迫。
- 看 `I10/I11` 是否因为姿态目标更早到位而更快回落；如果仍一直往前走，下一步优先查 stop-brake 激活/退出状态和姿态跟踪滞后，不走轮级反刹。
- 如果电机抖动明显下降但停车仍慢，再评估 `TRACK_ERR` 或 `STOP_BRAKE_KV`，仍保持 `STOP_BRAKE_GAIN=0`。

## 2026-07-09 16:05 数据分析：直立停车 = 匀速爬行，根因是零点误差 ~0.9°，不是刹车问题

### 数据事实（vofa+.csv, 6922 帧 / 138.4s, 全程 mode=0, b760ac8 翻转后固件）

- **积分位移 +8.0 m**：v 均值 +0.058 m/s，几乎所有 2s 窗口 v>0——"停不住"的主体是
  单向匀速爬行（stick-slip：静摩擦粘住 → 被内环微抖破开 → 爬 → 偶尔再粘住），不是振荡。
- **爬行段 719 帧：θ 均值 −0.687°，v 均值 +0.072 m/s**。匀速时车身真实近垂直
  ⇒ 测量零点在已学 trim（−0.15~−0.20）之外还差约 **−0.7°**，总零差 ≈ **−0.9°**。
  方向与今天加装（车尾循迹 + 顶部相机/激光）动重心一致。
- **trim 自学习被门控锁死**：门开占空比仅 **18.8%**——爬行时 v=0.07>0.03、轮 21rpm>5
  把门关死（要先停住才能学零点，零点不学就停不住的死锁）；静立时 12Hz 微抖
  （rate p80=15.4dps）又被 |rate|<10 门再砍 47%。按当前门/速率仿真积分 −0.226，
  与实测最终 trim −0.197 吻合，机制闭合。放宽门后占空比 63.8%，138s 内可收敛
  （无反馈上界仿真 −1.72）。
- 26.5s / 74s 两次大波动是**外力推扰**（40ms 内 v 跳 0.6m/s，0.1A 电流给不出 15m/s²），
  推后 ~3.5s 停住、冲出 ~2.5m，theta_ref 顶 ±1.6/±1.9 限幅工作正常——
  **姿态域刹车链路没问题，别再动刹车参数**。电流全程仅 0.1% 帧触 ±0.5A 限。
- fault=3 只在 132.7s 倒下后出现（θ=−33°角度保护），正常。
- 结构性缺口：stand 积分被强制清零（gimbal_task.c ~1297，14:49 死锁教训，维持不动）；
  位置项 KP=0.3deg/m 且位置测量/参考均 ±1m 饱和 → 上限 0.6°，顶不过 0.9° 零差；
  稳态便落在 Kv·v ≈ 残余零差 → v≈+0.07 无限爬。

### 参数修改（已应用；`RATE_PER_SEC` 保留 0.06 不动）

| 参数 | 现值 | 建议 | 原因 |
|---|---:|---:|---|
| `BALANCE_ANGLE_OFFSET_DEG` | `1.2f` | `0.35f` | 并入已学 trim(−0.197) + 爬行残差(−0.69)，即 config 既定"稳定值并回 OFFSET"流程 |
| `BALANCE_ZERO_TRIM_GATE_V_MPS` | `0.03f` | `0.15f` | 让 trim 在慢速爬行时也能学，解掉死锁 |
| `BALANCE_ZERO_TRIM_GATE_WHEEL_RPM` | `5.0f` | `50.0f` | 同上（爬行轮速 ~21rpm） |
| `BALANCE_ZERO_TRIM_GATE_RATE_DPS` | `10.0f` | `30.0f` | 静立微抖 p80=15.4dps，现门砍掉一半学习时间 |
| `BALANCE_ZERO_TRIM_RATE_PER_SEC` | `0.06f` | 可保留或 `0.10f` | τ=17s(门内) 已够；加快则注意与直立环解耦 |

- 之后若要"推不走的停车刚度"：`BALANCE_POSITION_KP_DEG_PER_M 0.30 → 1.5~2.5` 小步试；
  注意 Kv 注释（21:12 说回 3.0）与宏（5.0）不一致，若现慢速极限环按注释降 Kv。
  **不要**开 `BALANCE_POSITION_KI`（14:49 前馈死锁教训）。

### 验证方法

- 刷新后静置 2min 看 I5(v)/I13(trim)：v 应在 ±0.01 内、积分位移 <0.2m、
  trim 稳定后不再单调走；再推一把确认恢复不劣化。
- trim 稳定值若非 ~0，把它再并回 OFFSET，掉电重启即从正确基准出发。

## 2026-07-09 16:37 数据分析：静立已稳，剩余问题是 18Hz 高频细抖

### 数据事实

- 最新 `vofa+.csv` 共 `7902` 帧，约 `158.0s`，全程 `mode=0/fault=0`。
- 前 `0~5s` 为手扶/放倒/释放过程，`theta` 到过 `-20deg`，不用于调参。
- 从 `t>=5s` 且 `|theta|<3deg` 的闭环稳定段看：
  - `v avg≈0.0000m/s`，积分位移约 `-0.001m`，已经不再单向爬行。
  - `theta avg≈0.0002deg`，`theta RMS≈0.0755deg`，95% 落在约 `±0.11deg`。
  - `trim` 仅在 `-0.001~+0.003deg`，说明 `BALANCE_ANGLE_OFFSET_DEG=0.35` 已经贴近真实零点。
  - `|v|>0.015m/s` 的稳定段为 0 帧，stop-brake 没有被真正测试到，本轮不要改刹车参数。
- 剩余问题是静立细抖：
  - `theta_rate RMS≈19.5dps`，`cmd RMS≈0.0355A`。
  - `angle_p RMS≈7.45rpm`，`rate_d RMS≈6.82rpm`，P/D 两项都在约 18Hz 细翻向。
  - 速度不漂、角度不偏，属于“站得住但电机有细声”，不是零点或刹车问题。

### 参数修改

| 参数 | 旧值 | 新值 | 原因 |
|---|---:|---:|---|
| `BALANCE_PID_RATE_KD_RPM_PER_DPS` | `0.350f` | `0.300f` | 降低角速度 D 对 18Hz 细抖的注入；优先降噪，不动零点和 stop-brake |

### 下一步测试

- 仍先只测 `mode=0` 静立 2min。
- 看 `cmd RMS` 和现场电机细声是否下降，同时确认 `theta RMS` 不明显放大、`v` 不重新单向爬。
- 若降噪明显但抗扰变软，再小回调到 `0.32`；若仍有明显高频声，下一步考虑给 `theta_rate` 做轻滤波，而不是继续改 stop-brake。

## 2026-07-09 循迹短断线处理：破损黑线不立刻停车

### 修改

- 增加 `TRACKING_LOST_HOLD_MS = 300U` 和 `TRACKING_LOST_HOLD_VELOCITY_MPS = 0.06f`。
- `mode_tracking` 只有在已经见过有效黑线后，短暂丢线才进入 hold：
  - `0~300ms`：继续低速前进，沿用上一帧 `turn_rpm`，用于跨过短断线/破损黑线。
  - `>300ms`：恢复原安全逻辑，速度置 `TRACKING_LOST_VELOCITY_MPS=0`，bias 回 stand。
- 刚进入循迹但还没见过线时，不盲走，仍直接停车等待线。

### 测试重点

- 用 `track/mode 5` 触发循迹后，先确认压在线上能正常前进。
- 用 1~2cm 断线测试：应连续跨过去，不出现立刻刹停。
- 真正离线超过约 300ms 后仍应停车；若是 UART 数据完全断流，则约 `120ms timeout + 300ms hold` 后停车。

## 2026-07-09 17:12 VOFA 循迹灰度数据：开头稳定段是 A 点黑线/黑带

### 赛道结构按现场图片理解

- A/B 是横向黑色标记；压到 A 或 B 时 8 路探头会同时看到黑线。
- 中间导航线是窄黑线；正常循迹时只有局部几个通道为黑线，其余通道为白底/地面。
- CSV 最后 `line>=6120` 附近全通道接近 `3900~3970`，用户已说明最后白线不准，本轮不作为白底标定。

### 数据分段

- `line 3~850`：A 点黑线/黑带，8 路稳定高值，不是白底。
  典型黑线中值约：
  `x1=2870, x2=2859, x3=2782, x4=2901, x5=2741, x6=2648, x7=2739, x8=2646`
- `line 851~6119`：实际循迹段。黑线所在通道仍表现为高值；未压线通道是低值。
  按活动段低分位估计白底基准：
  `x1=929, x2=1210, x3=1176, x4=1222, x5=901, x6=737, x7=893, x8=613`
- `line 6120~6407`：尾部全通道接近满量程，忽略，不参与标定。

### 代码修改

- `Device/track/track.c` 的 `WHITE_BASE` 改为本次活动段白底低值。
- 黑线判定改为 `adc - WHITE_BASE > 400`，且总黑线强度 `<700` 时认为丢线。
- A/B 横向黑带处理：若 `>=7` 路同时判黑，且 8 路 ADC 极差 `<900`，返回 `error=0`，按居中直行处理。

### 复盘重点

- 旧理解“开头稳定段是白线”是错的；它是 A 点黑线/黑带。
- 旧 `WHITE_BASE={2365,1808,1481,1502,1569,1757,2070,2523}` 来自旧环境，会让 A 点全黑算出约 `+0.68` 的偏向，也会漏掉不少中间真实黑线。
- 新逻辑严格按图片赛道：横向黑带直行，窄黑线按局部高值算偏差，短断线交给 lost-hold。

## 2026-07-09 循迹测试 VOFA 通道

`VOFA_DEBUG_STAGE_TRACK_ADC` 扩展为 16 通道，前 8 路仍保持原始灰度，后 8 路用于判断循迹闭环：

| 通道 | 含义 |
|---|---|
| I0~I7 | `track_x1_adc` ~ `track_x8_adc` |
| I8 | `track_error`，左偏为正、右偏为负；A/B 横向黑带返回 `0` |
| I9 | `lost`，`1` 表示当前 `Track_GetError()` 判丢线 |
| I10 | `chassis_mode`，循迹应为 `5` |
| I11 | `velocity_mps`，循迹目标前进速度/短断线保持速度 |
| I12 | `turn_rpm`，循迹转向差速指令 |
| I13 | `drive_enabled`，`1` 表示底盘走行命令有效 |
| I14 | `frame_count`，循迹 UART 已解析帧数 |
| I15 | `rx_byte_count`，循迹 UART 接收字节数 |

测试时发送 `track` / `mode 5` 进入循迹；若 I14/I15 增长但 I9 长时间为 1，优先看 I0~I7 与白底基准/阈值；若 I8 正负方向反了，改权重符号而不是改串口或 VOFA。

## 2026-07-09 循迹速度调整：采用行走档速度幅值

- `mode 1` = 前进，`velocity_mps = +BALANCE_MODE_DRIVE_VELOCITY_MPS = +0.12m/s`。
- `mode 2` = 后退，`velocity_mps = -BALANCE_MODE_DRIVE_VELOCITY_MPS = -0.12m/s`。
- 之前循迹基速 `TRACKING_BASE_VELOCITY_MPS = 0.08m/s`，同样 `turn_rpm` 下路径曲率更大，现场会显得左右旋转敏感。
- 曾改为 `TRACKING_BASE_VELOCITY_MPS = BALANCE_MODE_DRIVE_VELOCITY_MPS`，即循迹前进基速使用 `0.12m/s`。
- 后续因左右转/循迹过敏，已在下一节回到 `0.08m/s`。

## 2026-07-09 左右转/循迹降敏：先慢转，不做 yaw 闭环

### 判断

- `left/right` 当前不是车体角速度闭环，而是固定差速轮速指令：
  `mode left -> turn_rpm=+BALANCE_MODE_TURN_SPEED_RPM`，
  `mode right -> turn_rpm=-BALANCE_MODE_TURN_SPEED_RPM`。
- 循迹也是 `track_error -> turn_rpm -> 左右轮差速`，没有 yaw rate 闭环。
- 因此现场“左右越跑越快/循迹左右太敏感”先按开环差速过猛处理，不急着加 yaw 闭环。

### 参数修改

| 参数 | 旧值 | 新值 | 目的 |
|---|---:|---:|---|
| `BALANCE_MODE_TURN_SPEED_RPM` | `45.0` | `22.0` | 手动左右转慢下来 |
| `TRACKING_BASE_VELOCITY_MPS` | `0.12` | `0.08` | 循迹前进回慢速 |
| `TRACKING_TURN_KP_RPM_PER_ERROR` | `9.0` | `4.5` | 降低循迹转向比例 |
| `TRACKING_TURN_KD_RPM_PER_ERROR_RATE` | `0.45` | `0.12` | 降低误差跳变带来的甩头 |
| `TRACKING_TURN_LIMIT_RPM` | `38.0` | `20.0` | 限制最大差速转向 |

### 下一步看数

- VOFA I12 `turn_rpm` 应明显少撞 `±20rpm` 限幅。
- 如果仍然敏感，优先再降 `TRACKING_TURN_LIMIT_RPM 20 -> 15` 或 `Kp 4.5 -> 3.5`。
- 如果转不过弯，再只小回 `TURN_LIMIT`，不要一次把 Kp/Kd 全抬回去。

## 2026-07-09 18:41 数据分析：停止震动的三个来源（bias 单拍阶跃 / Kv=5.0 越界 / 循迹丢线抖动）

### 数据事实（本轮 CSV 是指令侧，非平衡内环）

- 最新 `vofa+.csv` 为 `TRACK_ADC` 16 通道，10663 帧 ≈ 213.3s；I11/I12 是**指令**速度/差速，
  没有 theta/电流通道，平衡域结论靠机理与历史数据交叉。
- 固件版本判定：循迹段 turn 指令 41% 时间打满 `±38rpm`、LEFT 段恒 `45rpm`——
  这是降敏参数（Kp3.5/Kd0.05/limit15、TURN_SPEED=16）刷入**之前**的固件；
  `t=120.14s` 处 frame_count 19670→22，录制中间重启过一次。
- 模式时间线：TRACK 3 段共 80.8s，其后 FWD×2 / BWD×2 / LEFT×1，全部以 STAND 收尾，共 8 次停止。
- 每次停止都是指令硬阶跃：`+0.12→0`、`+0.08→0`、turn `+45→0`，一帧内到零，指令侧无斜坡。
- 循迹段丢线标志严重抖动：80.8s 内 lost 段 145 个（大量 60~140ms 闪断），
  速度指令阶跃 329 次；`>300ms` 丢线停车后常被**单帧**"找回"触发整车重启动，
  几十 ms 后再丢→再停，以 0.5~1s 周期反复顿挫。

### 机理：一次冲击激励 + 一个欠稳定外环 + 循迹的周期性重激励

1. **bias 单拍阶跃（冲击源）**：`gimbal_task.c` 983~992 行，`drive_enabled=1` 时
   bias 走 2°/s 斜坡；**`=0`（停止）那一拍直接赋值**。FWD 停止 = theta_ref 在
   1ms 内掉 1.7°（TRACK 停止掉 1.2°）；直立环 Kp=100rpm/°，等效 +170rpm 轮速
   阶跃，电流数 ms 内顶到限幅附近。speed_theta_ref 路径有 20°/s 斜率器管着，
   唯独 bias 在停止时绕过了所有斜率器。BWD 的 bias=0，无此冲击。
2. **stand 外环 Kv=5.0 停止激励下进极限环（震动主体）**：07-07 21:12 已实证
   Kv=5.0 越过稳定边界——stop 后 ±0.1m/s 慢速极限环摆 9s、大激励后 +0.39↔-0.38
   发散向后摔，当时决定回撤 3.0（db45922 落实）。今天翻转后重调走了 3→12→7→5：
   静立"显著改善"只说明振幅在静摩擦带内；停止带着 0.10~0.12m/s + 刹车瞬态进来，
   把外环踢出摩擦带就回到 21:12 的极限环。16:05 条目留过预警
   （"若现慢速极限环按注释降 Kv"）。**BWD 停止没有 bias 冲击也震，指向的正是这条。**
3. **循迹丢线/找回无去抖（重激励源）**：`mode_tracking.c` 丢线>300ms 停车后，
   单帧找回即全量重启动（bias 0→1.2 斜坡 + 速度 0→0.08 斜坡），几十 ms 后又
   丢线"停止"（bias 再一拍掉 1.2°）……每 0.5~1s 给车一记，这就是循迹停车段的持续抽动。

### 修改建议（#1/#2 已应用；#3 循迹找回确认按用户指示暂缓）

| # | 位置 | 修改 | 目的 |
|---|---|---|---|
| 1 | `BALANCE_POSITION_KV_DEG_PER_MPS` | `5.00f → 3.00f` | 回 21:12 实证稳定域，消停止后极限环；零点已修到 ~0.1° 残差，Kv=3 下爬行项 ≤0.03m/s，且 trim 门控已放宽能继续学掉 |
| 2 | `gimbal_task.c` bias 停止路径 + 新宏 `BALANCE_MODE_BIAS_STOP_SLEW_DEG_PER_SEC 20.0f` | `drive_enabled=0` 时 bias 也走 StepTowards（20°/s），1.7° 用 85ms 放完 | 消停止那一拍的 theta_ref 阶跃冲击；与 stop-brake THETA_SLEW 同速，不拖慢刹车 |
| 3 | `mode_tracking.c` + 新宏 `TRACKING_REFIND_CONFIRM_MS 120U` | 完全停车（丢线>300ms）后须连续见线 ≥120ms 才重新起步；确认期内维持停车指令、丢线计时不清零 | 消单帧找回引发的起步/急停循环（传感器 ~7.5ms/帧，120ms≈16 帧确认） |

不动的：stop-brake 全套（链路已验证）；`RATE_KD=0.30`（若 Kv 降后停止仍有余摆，
按 16:37 预案回 0.32）；循迹转向参数（3.5/0.05/15 已降敏、尚未实测）；
`POSITION_KP=0.30`（加劲抗推是另一件事，见 16:05 条目）。
另：LEFT 停止的 turn 45→0 冲击在已改的 TURN_SPEED=16 下自然缩小近 3 倍，本轮不单独处理。

### 验证方法

- 停止测试把 `VOFA_DEBUG_STAGE` 切回 `BALANCE_CASCADE`，录 FWD/BWD 各 2 次停止：
  - I1(theta_ref) 停止拍不再出现台阶（bias 斜坡化后应为 ≤20°/s 斜线）；
  - 停止后 1.5s 内 I5(v) 包络单调收敛进 ±0.02，不再有 ±0.05~0.1 的等幅摆；
  - I12(cmd) 停止瞬间不再打到限幅。
- 循迹停车段（丢线>300ms）应停稳不抽动，找回后 ~120ms 才起步。

## 2026-07-09 19:14 循迹极性翻转后数据：极性确认，线速度偏快，转向权限不足

### 数据结论

- 最新 `vofa+.csv` 共 5439 帧，`mode=5` 循迹 1457 帧。
- 翻转权重后，现场表现与数据一致，循迹极性按当前方向保留。
- 循迹段丢线比例约 `422/1457 = 29%`，比翻转前明显改善，但仍需要慢速留余量。
- `I11 velocity_mps` 均值约 `0.072m/s`，接近原 `TRACKING_BASE_VELOCITY_MPS=0.08`，现场判断偏快。
- `I12 turn_rpm` 已多次撞 `±15rpm`：`>=14.9rpm` 正向 99 帧、负向 55 帧。说明当前主要不是误差没出来，而是转向限幅偏小；需要降低前进速度，同时提高转向权限。

### 参数修改

| 参数 | 旧值 | 新值 | 目的 |
|---|---:|---:|---|
| `TRACKING_BASE_VELOCITY_MPS` | `0.08f` | `0.06f` | 循迹线速度降低，给转向更多时间 |
| `TRACKING_LOST_HOLD_VELOCITY_MPS` | `0.06f` | `0.04f` | 短断线保持也降速，避免丢线段冲太远 |
| `TRACKING_TURN_KP_RPM_PER_ERROR` | `3.5f` | `4.5f` | 同等误差下更快打方向 |
| `TRACKING_TURN_KD_RPM_PER_ERROR_RATE` | `0.05f` | `0.08f` | 适度增加误差变化响应 |
| `TRACKING_TURN_LIMIT_RPM` | `15.0f` | `24.0f` | 放开转向差速上限 |
| `weights` | `{-5,-3,-2,-1,+1,+2,+3,+5}` | `{-7,-4,-2,-1,+1,+2,+4,+7}` | 加大左右边缘探头权重，偏到边缘时更强修正 |

本轮仍不动直立环、stop-brake、手动 left/right 差速符号。

### 速度跟踪观测修正

上一版 `TRACK_ADC` VOFA 的 `I11 velocity_mps` 是 `Mode_GetCommand()` 输出的目标速度，
不是实际车速；因此不能用那份数据判断“实际速度是否拟合目标速度”。为下一轮测试，
保留 `I12 turn_rpm` 不变，并把末 3 路改成速度闭环观测：

| 通道 | 含义 |
|---|---|
| I11 | `cmd_velocity_mps`，循迹模式原始目标速度 |
| I12 | `turn_rpm`，循迹转向差速指令 |
| I13 | `actual_velocity_mps`，轮速换算后的实际车速 |
| I14 | `ref_velocity_mps`，速度斜坡后的实际外环目标 |
| I15 | `velocity_err_mps = ref - actual` |

下一轮判断速度是否控住，以 `I14` 对 `I13` 为准，不再看 `I11` 单独下结论。

## 2026-07-09 循迹速度偏高修正：降 tracking bias，并记忆丢线前策略

### 影响范围

- `TRACKING_FORWARD_BIAS_DEG` 只由 `mode_tracking` 使用，只影响 `mode=5` 循迹时的
  `bias_deg` 输出；普通 `mode=0` 直立、`mode=1/2` 前进后退、直立 PID 参数都不改。
- 它会改变“循迹时的直立目标角”，这是为了把实际速度 `I13` 从被 bias 顶高的状态拉回
  速度目标 `I14` 附近。

### 修改

| 位置 | 修改 | 目的 |
|---|---|---|
| `TRACKING_FORWARD_BIAS_DEG` | `1.2f -> 0.4f` | 降低循迹持续前压，避免 `I13 actual` 长期约为 `I14 ref` 的 1.8 倍 |
| `mode_tracking.c` | 增加 `tracking_last_velocity_mps`、`tracking_last_bias_deg` | 记住上一帧有效循迹策略 |
| `mode_tracking.c` 丢线分支 | 短丢线时不清 `tracking_ready/integral/filter`，沿用上一帧 `turn/velocity/bias`，速度受 `TRACKING_LOST_HOLD_VELOCITY_MPS=0.04` 限制 | 断线/破损时保持上一时刻循迹思路，避免单帧丢线导致策略重置 |

### 下一轮验证

- `mode=5` 下看 `I13 actual_velocity_mps` 是否从 `0.10~0.20` 降向 `I14 ref≈0.06`。
- 短丢线时 `I12 turn_rpm` 应保持上一方向，不再重置为 0 后重新找线。
- 若仍速度过高，继续优先降 `TRACKING_FORWARD_BIAS_DEG`，不动普通直立环。

## 2026-07-10 循迹降敏与终点停车锁存修复

### Codex provider 前置条件检查

- 未读取或记录任何 API Key 内容。
- 当前进程、用户级、系统级均未检测到 `OPENAI_API_KEY`。
- 当前自定义 provider 已使用 `wire_api = "responses"`，但尚未配置
  `env_key = "OPENAI_API_KEY"`，且 `requires_openai_auth` 仍为 `true`。
- 撤销旧 Key、签发新 Key、为新 Key 开通 `codex-auto-review`（或改为人工审批）以及
  重启后确认 403 消失，都属于服务端/账户操作，本次固件修改无法代办，仍待完成。

### 代码修改

| 位置 | 修改 | 目的 |
|---|---|---|
| `Device/track/track.c` | 灰度权重 `{-18,-9,-3,-1,+1,+3,+9,+18}` 改为 `{-7,-4,-2,-1,+1,+2,+4,+7}` | 缩小边缘误差幅值，降低大转向指令出现频率 |
| `TRACKING_TURN_KP_RPM_PER_ERROR` | `12.0f -> 4.0f` | 降低比例转向增益 |
| `TRACKING_TURN_KD_RPM_PER_ERROR_RATE` | `0.08f -> 0.0f` | 暂停微分项，避免误差跳变放大 |
| `TRACKING_TURN_LIMIT_RPM` | 保持 `24.0f` | 保留必要的最大转向权限 |
| `TRACKING_FORWARD_BIAS_DEG` | 保持 `0.4f` | 等新固件速度数据后再判断是否降到 `0~0.1deg` |
| `TRACKING_BLOCK_DONE` | 每拍明确输出 `velocity_mps=0`、`turn_rpm=0`、stand bias、`drive_enabled=0`，随后立即 `return` | 终点停车状态锁存，不再落入丢线或正常循迹分支 |
| `Mode_Set()` | `Mode_TrackingReset()` 改为只在进入循迹模式的模式边沿调用 | 重复发送 `mode 5` 不能解锁；必须先退出、再重新进入循迹才复位 |

### 原故障数据证据（修改前基线，不能视为本版实测）

- `vofa+.csv` 共 5425 帧，其中循迹 3696 帧。
- 两个循迹段都出现了恰好 150 帧的 `I11 cmd_velocity_mps=0`；按 20ms/帧计算均为
  3.0s，随后在 `mode=5` 未退出的情况下重新输出前进速度，和旧代码
  `TRACKING_BLOCK_DONE` 继续落入正常循迹分支完全一致。
- 有效见线帧中，`I8 track_error` 范围 `-5.942~+5.716`，绝对值均值 `1.247`；
  `I12 turn_rpm` 有 115/2395 帧（4.80%）撞到 `|turn|>=23.9rpm`。
- 旧参数移动段中：`I13 actual_velocity` 均值 `0.0496m/s`，
  `I14 ref_velocity` 均值 `0.0449m/s`，`I15 velocity_err` 均值 `-0.0048m/s`、
  绝对值均值 `0.0504m/s`。该数据采集于本次改权重/KP/KD/停车逻辑之前，
  因此不据此继续修改固定前倾量。

### 编译检查

使用 ARM GCC 10.3-2021.10、项目实际宏和完整 include 路径，以
`-fsyntax-only -Wall -Werror` 检查，以下文件全部 0 错误、0 警告：

- `Device/track/track.c`
- `mode/mode_tracking.c`
- `mode/mode_control.c`
- `Application/ins/ins_task.c`
- `Application/gimbal/gimbal_task.c`

### 待上车验证

- 正常循迹观察 `I8 track_error`、`I12 turn_rpm`，统计本版撞 `+/-24rpm` 的比例。
- 经过终点标记后等待超过 3s，确认 `I11=0`、`I12=0` 且始终不重新前进。
- 停车锁存后重复发送 `mode 5`，确认不能解锁；先退出循迹再进入，确认状态机复位。
- 重新记录本版 `I13 actual_velocity`、`I14 ref_velocity`、`I15 velocity_err`，
  再决定是否将 `TRACKING_FORWARD_BIAS_DEG` 从 `0.4f` 降到 `0~0.1f`。

## 2026-07-10 一维 yaw 云台接入 mode 与终点激光时序

### C 板激光接口

- 本地大疆 C 板官方示例 `20.standard_robot/bsp/boards/bsp_laser.c` 使用
  `TIM3_CH3` 控制板载激光；对应管脚为 `PC8`，`CCR=ARR` 开、`CCR=0` 关。
- 当前工程新增 `bsp_laser.c/.h`，并在 `tim.c/.h` 和 `spi_bmi088.ioc` 中补齐
  `TIM3_CH3 / PC8` 的 10kHz PWM 配置（84MHz / 8400）。
- 上电初始化时 PWM 比较值保持 0，激光默认关闭，不再上电常亮。

### 终点激光时序

- `ModeCommand_t` 新增 `laser_enabled`，普通模式和循迹正常行驶/冲线阶段均为 0。
- 检测终点横向标记后先按原逻辑以 `0.04m/s` 冲线 500ms；进入
  `TRACKING_BLOCK_STOPPED` 后，车辆输出零速度、零转向和 stand bias，同时激光打开。
- `TRACKING_TARGET_BLOCK_STOP_MS=3000ms` 到时进入 `TRACKING_BLOCK_DONE`，立即关闭激光，
  并继续锁存停车；提前退出循迹或发生其他模式切换也会因命令清零而关闭激光。
- `Application/gimbal/gimbal_task.c` 的 1ms 电机安全任务读取 mode 命令并执行
  `BSP_LaserOn/Off()`，mode 层不直接依赖 HAL/TIM。

### 一维 yaw QD4310

- yaw 电机现有配置：`can.id=005`，CAN 1Mbps，命令帧 `0x405`、反馈帧 `0x505`。
- 电机侧参数记录为：速度 PID `0.00300/0.000390/0`，角度 PID `1200/0/0`，
  `limit.speed=250rpm`，`limit.current=1.65A`；STM32 不在启动时重写电机内部配置。
- `bsp_can` 新增独立 `gimbal_yaw_motor` 和 `can_gimbal_rx_count`，删除旧代码把
  `yaw_motor` 别名到左轮的危险兼容宏；反馈 ID 0x505 不再污染左右轮实例。
- 新增 `mode/mode_gimbal.c/.h`，提供 `RELAX/HOLD/AIM` 三种状态。控制器使用 QD4310
  编码器角度和速度反馈，首次进入 HOLD 捕获当前位置为中心；AIM 的目标接口为相对中心
  `Mode_GimbalSetTargetOffsetDeg()`，并限制在 `+/-120deg`。
- 视觉模块本轮不接入；`Device/vision` 不调用 yaw 目标接口，后续融合只需把视觉换算后的
  yaw 偏移送入 mode 层，不进入 1ms 电流内环。
- yaw 电机启动时只发送 enable 并接收反馈；当前电流命令编译开关关闭，mode 层仍可进入
  HOLD 并计算控制量，但不会向 ID 0x05 发送任何 `Motor_SetCurrent()` 帧。

### 构建检查

- ARM GCC 10.3-2021.10 以 `-fsyntax-only -Wall -Werror` 检查 `main/tim/bsp_laser/`
  `bsp_can/mode_gimbal/mode_control/mode_tracking/gimbal_task/chassis_serial_cmd/ins_task`，
  全部通过。
- 独立目录 `build/codex_verify` 完整编译 63 个 C 文件、启动汇编并链接成功；ELF 大小：
  text 50724B、data 176B、bss 27248B。链接符号确认包含 `MX_TIM3_Init`、
  `BSP_Laser*`、`Mode_Gimbal*` 和 `gimbal_yaw_motor`。
- Keil `spi_bmi088.uvprojx` XML 解析通过，Makefile 和 Keil 工程都已加入
  `bsp_laser.c`、`mode_gimbal.c`。

### 待上车验证

- 上电确认激光保持关闭；未到终点时任何循迹/丢线状态都不能点亮。
- 经过终点标记：500ms 冲线后停车并点亮激光，约 3.0s 后关闭；继续等待确认车辆和激光
  都不重新启动。退出再进入循迹后，确认终点状态机可以重新执行一次。
- 确认 CAN 收到 0x505 且 `gimbal_yaw_motor` 反馈更新；手动转动云台时角度/速度应更新，
  电机保持使能但不能主动回正，也不能出现 ID 0x05 的电流命令帧。
- 后续允许电流输出时，再先以 0.30A 做小角度回正测试；若误差继续增大，立即急停并翻转
  `GIMBAL_YAW_CONTROL_SIGN`，确认极性后再接入 AIM 目标。

### 2026-07-10 yaw 仅使能、禁止电流命令

- yaw 电机仍在启动时发送 `Motor_Enable(0x05)`，并继续解析 `0x505` 反馈。
- 新增 `GIMBAL_YAW_CURRENT_COMMAND_ENABLE=0`；该配置会在编译期移除 yaw 的
  `Motor_SetCurrent()` 调用，因此不会发送非零电流，也不会发送 0A 保活电流帧。
- `mode_gimbal` 暂时只计算 HOLD/AIM 目标和控制量，待确认允许输出后再显式打开该开关。

# MaixCAM2 bidirectional UART and UDP telemetry (2026-07-11)

- The MCU transport remains USART6 on PG14 TX and PG9 RX at 115200 8N1.
- MaixCAM2 sends ASCII target lines as `dx,dy,valid\n`. `dx` and `dy` must be
  within `-2048..2048`, and `valid` must be either 0 or 1.
- Vision parsing now has three results: valid target, invalid target candidate,
  and non-vision command. A line whose first non-space character is numeric or
  a sign and which contains a comma is always consumed by the vision parser.
- Invalid candidates such as `5,0,2`, `5000,0,1`, and `5,` are counted and
  discarded. They cannot fall through to chassis mode or wheel-speed parsing.
- Existing bare `0..5`, English mode names, bias, PID, and speed commands remain
  compatible because non-vision lines continue through the original command
  queue.
- STM32 telemetry to MaixCAM2 remains the existing VOFA FireWater numeric CSV,
  with the Git channel order and 20 ms output period unchanged. MaixCAM2 forwards
  valid telemetry to PC UDP port 5005; the PC bridge forwards it to VOFA+ port
  5006 and relays commands through MaixCAM2 UDP port 5007.
- `Vision_GetSnapshot()` and `INS_GetVisionTarget()` expose a coherent target
  snapshot for diagnostics or later integration. No chassis, yaw, motor, or
  laser control consumes this snapshot in the current firmware.
- Verification completed with a clean ARM GCC build and link: text 52088 B,
  data 180 B, bss 27284 B. The vision parser also passed `-Wall -Wextra
  -Werror`, and a Cortex-M Thumb emulation test returned 0 for valid boundary
  frames, malformed numeric candidates, snapshot preservation, and legacy
  command classification. The sibling Python project passed `py_compile`, all
  9 unit tests, and a bidirectional UDP telemetry/command loopback.

## 2026-07-13 yaw Kp下调

- 实际生效的云台yaw外环比例增益 `GIMBAL_YAW_KP_A_PER_DEG` 由 `0.036` 下调为 `0.030 A/deg`，降低约16.7%。
- `KI=0.007 A/(deg*s)`、`KD=0.0025 A/rpm`、静摩擦前馈、`0.30A`限流和电流斜率保持不变，便于单独判断Kp变化。

### 实车回退

- `Kp=0.030 A/deg` 实车出现大幅低频晃动，说明降低比例刚度后，现有KI和静摩擦前馈相对过强。
- 立即回退到上一版 `GIMBAL_YAW_KP_A_PER_DEG=0.036 A/deg`；本轮不同时改KI/KD，先恢复已知基线。

### 积分与静摩擦前馈降能量

- 现场确认晃动幅度会持续增大，更符合积分累积叠加静摩擦前馈反复换向注能，不再继续降低Kp。
- 保留 `Kp=0.036 A/deg` 和 `Kd=0.0025 A/rpm`。
- `Ki` 由 `0.007` 降至 `0.002 A/(deg*s)`，积分电流上限由 `0.015` 降至 `0.006A`，同时降低累积速度和最终可注入能量。
- 静摩擦前馈由 `0.008` 降至 `0.004A`，减少误差换向时的固定推力；仍保留少量破静摩擦能力，避免直接关闭后小误差完全不动。

### 最新VOFA严重晃动根因：电流反向过慢

- 最新 `vofa+.csv` 共8008帧；有效AIM段1184帧、23.68秒。
- AIM段目标角范围仅 `35.85~65.10deg`，实际yaw却冲到 `3.65~96.28deg`，转速范围 `-106.4~+130.7rpm`，属于严重越过目标，不是小幅视觉噪声。
- 决定性样本：`yaw_rpm=+123.9rpm、yaw_err=-2.55deg` 时，P+D期望已经是负向制动，但VOFA实际 `yaw_cmd=+0.041A`；另一帧 `yaw_rpm=+130.7rpm、yaw_err=+4.35deg`，D项也应使合成输出为负，实际仍为 `+0.035A`。
- 控制公式和D符号正确，滞后来自 `GIMBAL_YAW_CURRENT_SLEW_A_PER_SEC=2.0`：电流由正向翻到负向需要约100ms以上，高速时足以继续越过几十度，随后反方向重复，形成大幅摆动。
- `GIMBAL_YAW_CURRENT_LIMIT_A` 从 `0.30A` 收紧到 `0.18A`，降低加速能量；`GIMBAL_YAW_CURRENT_SLEW_A_PER_SEC` 从 `2.0` 提高到 `20.0A/s`，使过零后的制动电流能在约20ms量级建立。
- Kp/Kd保持 `0.036/0.0025`，Ki/积分上限/静摩擦前馈保持降低后的 `0.002/0.006/0.004`，本轮只修正执行器制动滞后。

### 固定目标自激与保守首测参数

- 重要时间关系：上述8008帧CSV采集早于 `0.18A/20A/s` 固件生成，因此它不能证明20A/s新斜率仍会晃，只能证明旧2A/s换向过慢。
- 两段视觉断流且目标角固定的数据仍发生自激：`age=161~642ms` 时目标恒 `57.713deg`，实际yaw约 `68→13→94→45deg`；另一段目标恒 `56.538deg`，实际约 `58→3.65→92→42deg`。视觉跳动不是这两段自激的主因。
- 原代码在 `VISION_YAW_FRAME_TIMEOUT_MS=150ms` 后只清积分，仍持续追旧目标，直到1000ms才RELAX。现改为超过150ms立即 `GIMBAL_MODE_RELAX`，撤销yaw电流；保留的停止超时同步缩短为180ms。
- 为首次安全验证采用保守机械环：`Kp=0.024A/deg`、`Ki=0.0005A/(deg*s)`、`Kd=0.0035A/rpm`、积分上限 `0.002A`、静摩擦前馈 `0.002A`。
- yaw误差限幅和单帧视觉角修正均由8deg收紧为4deg；电流限幅由0.18A进一步收紧为0.12A，电流斜率保持20A/s以保证及时反向制动。

## 2026-07-13 USART6反向灌电与发车复位修复

- 复位链路判断：轮控启动造成Maix供电轨瞬时下陷时，STM32 PG14仍保持UART空闲高电平，可能经Maix B1 RX保护结构反向灌电；这属于软件触发负载和引脚状态、电气通路完成反灌。
- PG14不再与PG9一起常驻`GPIO_MODE_AF_PP + GPIO_PULLUP`。PG9 RX持续工作，PG14默认使用`GPIO_MODE_ANALOG + GPIO_NOPULL`保持高阻。
- `BSP_UART6_SendBytes()`只在一帧DMA开始前将PG14临时切到UART推挽，UART TC确认最后停止位发送完成后立即恢复高阻。
- INS遥测和BT任务状态原先会无锁访问同一个`tx_buf/tx_busy`。发送检查、缓冲复制和DMA启动现使用PRIMASK组成原子事务，避免高优先级INS抢占覆盖活动DMA缓冲。
- 新增`BSP_UART6_MuteTxForMs()`：可中止当前反向DMA、保持RX有效并在保护期内拒绝新TX。
- 电机进入平衡PID、进入`BALANCE_MOTION_STARTING`以及任务1至5启动时，反向UART静默`800ms`，覆盖轮电流建立阶段；保护结束后任务状态会自动重发。
- 24路VOFA CSV由`20ms/50Hz`降为`100ms/10Hz`，预计串口占用从约66.5%降至约13.3%，避免Maix画面掉帧时UART积压。
- 验证：13项Python回归通过；ARM GCC完整编译、链接、HEX/BIN生成通过，`text=72028B, data=184B, bss=28240B`。
- 实车首测仍应先悬空车轮。分别验证平衡使能、任务1发车、任务2发车；若Maix仍复位，断开`PG14->B1`复测并用示波器观察Maix 5V/3.3V轨，软件高阻不能代替最终的串联电阻或断电高阻缓冲器。

## 2026-07-13 循迹5中途退出与空白断线修正

- 最新 `vofa+.csv` 的三段 `mode=5` 中 `turn_ref` 始终为0，说明车辆一直处于起步直行，未真正进入按线转向。前两段宽横线最多识别到5/6路，而旧门槛要求7路；第三段始终为 `mask=255/count=8`，无法满足离开起始横线的连续300ms条件。
- CSV中的三次 `mode 5 -> 0` 均跨采集断档；最后一次还同时从20ms采样切换到100ms，不能当作同一运行中的正常状态跳变。
- 找到明确的软件退出链：视觉侧相机帧超时曾调用 `safe_stop()`，随后发送 `stop\n`，STM32会停止任务并切换到 `mode=0`。现改为运行时视觉丢失只发送无效V帧：任务1及任务2/3到中央前继续循迹，瞄准/射击阶段原地等待恢复，任务4/5返B时停车等待重锁；用户主动STOP和程序真正退出仍发送安全停止。
- `TRACKING_BLOCK_BLACK_COUNT_MIN` 从7降为实测5，并保留40ms连续确认；4路普通线不会触发宽横线。标记判定只拒绝小于 `-0.015m/s` 的明确后退，小范围轮速过零抖动不再反复清除确认窗口。
- 路线保持 `起步直行 -> A宽横线 -> 循迹到中央标记 -> 打靶/放行 -> 循迹到B宽横线 -> 停稳`。跨越A横线期间固定直走，避免横线边缘不对称产生错误转向。
- 白色间断处先完整沿用上一有效 `turn_rpm`，随后按上一方向扩大找线；超过1.5s后仍以 `0.04m/s` 低速找线，不再原地零速。重新见线的100ms确认期间继续输出找线动作，不插入停车脉冲。
- TARGET_AIM VOFA在原24路后追加：`I24 route_phase`、`I25 task_id`、`I26 task_phase`、`I27 fault`、`I28 estop`、`I29 motor_feedback_ok`、`I30 motor_enabled_ok`。下一次可区分串口STOP、姿态保护、电机反馈故障和路线正常停车。
- 按现场要求将 `GIMBAL_YAW_KP_A_PER_DEG` 从保守值 `0.024` 小幅增加到 `0.028A/deg`；`Ki=0.0005`、`Kd=0.0035`、`0.12A`限流和 `20A/s` 电流斜率不变。
- 验证：course tracking、task control、square mode、target aim、gimbal aim主机测试通过；视觉侧101项测试通过；ARM GCC完整编译链接通过，`text=72456B, data=184B, bss=28240B`。

## 2026-07-13 中央停车后直接打靶

- 保持原中央标记停车动作、`500ms` runout、停稳控制和位置保持逻辑不变。
- 任务2至5检测到中央标记并由原路线状态机进入 `TARGET_WAIT_SETTLE` 后，任务控制立即进入 `LASER_DWELL` 并打开激光，不再等待视觉锁定、`aim_ready`、云台反馈或额外的 `motion_settled` 条件。
- 激光持续完整3秒；首次打靶过程中视觉条件变化不再中断或清零计时。3秒结束后关闭激光，由视觉侧锁存本次环数并触发对应WAV播报。
- 急停、底盘故障、电机反馈异常或电机未使能仍具有最高优先级，会立即关闭激光；这些保护不改变停车动作。
- 验证：task control与course tracking主机测试通过；视觉环数、WAV播报和协议37项测试通过；ARM GCC完整编译链接并生成HEX/BIN，`text=72424B, data=184B, bss=28240B`。

## 2026-07-13 循迹禁止中途HOLD

- 修复循迹数据超过 `120ms` 未刷新时直接输出停车命令的问题。未到宽黑标记时，短时断流继续沿用上一速度/转向，随后进入原有丢线找线逻辑，不再回到 `HOLD`。
- 刚进入循迹、等待首批稳定传感器帧期间保持直行，避免发车后先出现一次停车脉冲。
- 任务或视觉侧将 `route_drive_enabled` 清零时，只允许在中央标记停车/射击阶段和B点终点阶段执行停车；处于A到中央、中央到B的循迹阶段时继续找线。
- 中央标记和B点宽黑线的原runout、制动、位置保持及停稳判定均未修改。人工 `STOP`、硬件按键急停、姿态越界、电机反馈和使能故障仍可立即停车。
- 新增 `tests/host/competition_route_static_test.c` 完整题目静态仿真：任务5从A直行出发，经过起始宽线、普通弯线、白色间断和传感器超时，到中央完成两次完整3秒打靶，再经历视觉丢失、断线找线并到B宽线停稳。
- 仿真结果：`STATIC SIM PASS`，全程 `midroute_hold=0`、`mode_exit=0`、`shots=2`、`milestones=0x0F`。说明状态机在无硬件扰动条件下不会中途退出循迹或进入HOLD；实车急停/故障仍需结合VOFA的 `I27..I30` 判断。

## 2026-07-13 循迹阶段pitch失效死锁修复

- 根因是任务状态的seqlock读取使用无限自旋。优先级4的 `Gimbal_Task` 在 `Task_Update()` 中将发布序列置为奇数后，可能被优先级5的 `INS_Task` 抢占；`INS_Task` 的VOFA/状态上报调用 `Task_GetStatus()` 后会永久等待偶数序列，高优先级读者因此饿死低优先级写者。
- 死锁后INS姿态更新和Gimbal直立控制均无法继续调度，所以pitch保持旧值、电机控制停止刷新，表现为循迹运行一段后卡死且直立环失效。
- 删除 `Task_GetOutput()`、`Task_GetStatus()` 的所有无限自旋。任务状态改为双缓冲发布：1kHz状态计算和非活动槽复制保持中断开启，只在单字节活动索引提交与读取快照时使用FreeRTOS短临界区；主机测试构建使用空锁宏。
- 本轮只修改任务状态并发保护，不修改UART、UDP、DMA、视觉协议、循迹路线和停车动作。
- 旧24列VOFA的最后循迹段还显示 `motion=STARTING(1)` 持续约1.86秒，实速最高仅 `0.027m/s`，没有达到 `0.035m/s` 的启动退出门槛。旧逻辑到2秒会无条件转入 `BRAKING/HOLD`，形成另一条确定的中途停车路径。
- 对 `CHASSIS_MODE_TRACKING` 单独取消该自动HOLD：启动超时后转入 `RUNNING` 并继续接受循迹速度；其他模式仍保留原超时制动。中央和B点通过 `drive_requested=0` 触发的原停车动作不变。
- 验证：并发/启动超时策略6项测试、task control、course tracking和任务5完整静态仿真全部通过；ARM GCC完整链接并生成HEX/BIN，`text=72520B, data=184B, bss=28280B`。

## 2026-07-13 中央目标点六路确认

- 中央目标标记门槛 `TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN` 从5路收紧为6路；5路持续亮不会触发目标停车，6路达到原连续确认时间后才进入目标runout。
- A点和B点宽黑线门槛、中央/B停车动作、UART/UDP通信均不修改。
- 验证：5路拒绝/6路确认边界测试、course tracking、任务5完整静态仿真及ARM构建通过，固件 `text=72536B, data=184B, bss=28280B`。
- 针对实车中途误停车并开激光，中央标记进一步要求左右4路传感器中各至少亮3路、车辆实际向前，并将连续确认由80ms提高到150ms；瞬时6路及偏向一侧的 `4+2` 分布不会触发打靶。
- 误判保护修改后course tracking、任务5完整静态仿真和ARM构建通过，固件 `text=72616B, data=184B, bss=28280B`。

## 2026-07-13 循迹提速

- 正常循迹基速由 `0.12` 提高到 `0.15m/s`，起步找线和急弯速度由 `0.10` 提高到 `0.12m/s`。
- 白色间断的保持/重捕速度由 `0.06` 提高到 `0.08m/s`，长时间找线速度由 `0.04` 提高到 `0.06m/s`。
- 中央目标与B点runout继续保持 `0.04m/s`，不改变原停车动作和停车距离；通信不修改。
- 现场随后明确“速度不够”是转向追踪不上，上述线速度调整立即撤回到原值；未进入最终固件参数。
- 改为循迹专用转向增强：Kp `10→14rpm/error`、限幅 `32→40rpm`、误差LPF旧值权重 `0.65→0.40`，循迹转向斜率 `160→260rpm/s`。其他模式仍使用原 `160rpm/s`。
- 验证：course tracking、任务5完整静态仿真及ARM构建通过，固件 `text=72664B, data=184B, bss=28280B`。

## 2026-07-13 打靶后离场动力增强

- 打靶后 `TARGET_DEPART` 不再直接依赖普通循迹输出，改为固定直行 `0.14m/s`，直到车辆产生有效前进并完成原1.2秒离场窗口。
- 仅在目标离场重新起步时，将静摩擦助推从普通 `1.10°/2°每秒` 提高到 `1.80°/4°每秒`；进入 `WAIT_TARGET_EXIT` 后立即恢复普通循迹助推。
- 不启用全局launch，不修改中央/B停车、正常循迹、通信和安全保护。
- 验证：离场专用速度断言、course tracking、任务5完整仿真和ARM构建通过，固件 `text=72776B, data=184B, bss=28280B`。

## 2026-07-13 白色间断速度连续化

- 原黑线基速 `0.12m/s` 切到短空白 `0.06m/s`、长空白 `0.04m/s`，目标速度落差过大，导致前后倾变化和重捕线动力衔接差。
- 改为黑线 `0.12`、短空白保持 `0.10`、扩大寻线 `0.09`、长空白 `0.08m/s`，仍保留随丢线时间逐级减速，但不再骤降一半以上。
- 中央/B点runout和停车动作、打靶后专用离场、通信均不修改。
- 验证：course tracking、任务5完整静态仿真及ARM构建通过，固件 `text=72776B, data=184B, bss=28280B`。

## 2026-07-13 停车后退与离场静摩擦修正

- 停车制动原先在速度绝对值低于 `0.005m/s` 后仍需连续30ms才捕获HOLD，车辆通常先穿过零点，因此出现可见后退。现提前为低于 `0.010m/s` 连续5ms即捕获位置保持；零交叉保护仍保留。
- 打靶后离场助推由 `1.8°/4°每秒` 提高到项目历史实车验证的 `2.2°/8°每秒`，最终pitch参考仍受原 `3.2°` 总限幅和姿态安全保护约束。
- 不修改中央/B点runout、正常循迹速度、UART/UDP通信。

## 2026-07-13 任务期间激光常开

- 任务1至5从 `Task_Start` 到人工STOP/HOME之间，任务输出和实际激光均保持开启，不再因循迹、等待移靶、射击计时结束、视觉丢失或到B完成而关闭。
- 人工STOP/HOME、急停、底盘故障、电机反馈/使能异常仍强制关闭激光；通信协议不修改，状态帧中的laser字段同步反映常开状态。
- 修正主机测试仍以 `laser_enabled` 判断进入射击计时阶段的旧逻辑，改为精确检测 `LASER_DWELL` 阶段切换；激光常开不改变两次完整3秒计时。
- ARM固件构建产物确认：`text=72760B, data=184B, bss=28280B`；任务控制与任务5仿真源通过严格语法检查，`git diff --check` 无空白错误。

## 2026-07-13 循迹速度小幅回调

- 正常黑线循迹由 `0.12` 小幅下调到 `0.11m/s`，打靶后目标离场由 `0.14` 下调到 `0.13m/s`。
- 白色间断继续保持 `0.10/0.09/0.08m/s`，因此黑线到空白的速度差进一步减小；转向增益、中央/B点停车、激光常开和UART/UDP通信均不修改。

## 2026-07-13 循迹比例增益加大

- 循迹专用转向 `Kp` 从 `14` 提高到 `16rpm/error`，改善弯道追线不足。
- 转向限幅 `40rpm`、循迹转向斜率 `260rpm/s`、线速度、停车和通信均保持不变，便于单独验证比例增益效果。

## 2026-07-13 打靶后柔和起步

- 打靶停车后的目标离场助推建立斜率从 `8` 降到 `4deg/s`，避免直立车体突然前倾、冲出循迹线。
- 保留 `2.2deg` 助推上限和 `0.13m/s` 离场速度，仍提供克服静摩擦所需的起步动力；循迹Kp、停车动作和通信不修改。

## 2026-07-13 任务激光立即常开

- 修复任务状态虽请求激光常开，但实际输出仍等待 `TRACKING` 模式、平衡电机反馈和电机使能全部就绪，且任务启动函数末尾无条件执行一次 `LaserOff`，导致现场观察到激光不亮的问题。
- 任务1至5启动成功后立即执行 `BSP_LaserOn()`；任务活动期间不再受模式切换和电机就绪门控影响。
- 人工 `STOP/HOME`、急停和底盘故障仍会关闭激光；UART、UDP、DMA及通信协议不修改。

### 激光PWM二次核查

- 初始化顺序为 `MX_TIM3_Init -> BSP_LaserInit -> FreeRTOS`，硬件映射为 `PC8/AF2/TIM3_CH3`，PWM1高极性，链路配置正确且无其他外设复用PC8。
- 开启占空比改为 `CCR3=ARR+1`，保证PWM1持续高电平；若首次PWM启动未成功，后续开启请求会自动重试初始化。
- `BSP_LaserIsOn()` 不再只返回软件标志，而是同时回读 `TIM3 CEN`、`CC3E` 和 `CCR3`；VOFA的 `gimbal_laser_on` 因此反映实际寄存器输出状态。
- SET校准激光允许在底盘急停且静止时开启，仍要求底盘无故障、处于STAND且任务未运行；任务激光逻辑及通信协议不修改。

## 2026-07-13 循迹Kp回调

- 实车反馈循迹比例增益偏强，将循迹专用转向 `Kp` 从 `16` 回调到 `14rpm/error`。
- 转向限幅 `40rpm`、转向斜率 `260rpm/s`、路线速度、停车、激光和通信保持不变。
