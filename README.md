# STM32F407VET6 LCD、触控与 QD4310 测试工程

> 当前启用完整TFT UI和正式任务流程：三个任务成功后开始累计充电，30 s时先关闭PB1，
> 再输出一次500 ms的PB0发射脉冲。临时手动测试代码仍保留在
> `CANNON_MANUAL_TEST_MODE=1U`分支，但当前默认值为`0U`。

当前默认目标为魔女科技 STM32F407VET6 v5.5 开发板和 34 针 2.8 寸
`ILI9341 + XPT2046` 屏幕。仓库保留当前裸机 LCD/触控/QD4310 控制目标、MaixCAM2
箱面视觉/TOF服务及其最小依赖，不包含旧 HAL、FreeRTOS、Keil、平衡车或循迹工程。

## 构建

需要 GNU Arm Embedded Toolchain、CMake 和 Ninja：

```powershell
cmake --preset debug
cmake --build --preset debug

cmake --preset release
cmake --build --preset release
```

产物位于 `build/cmake/Debug` 或 `build/cmake/Release`：

- `stm32f407ve_lcd.elf`
- `stm32f407ve_lcd.hex`
- `stm32f407ve_lcd.bin`
- `stm32f407ve_lcd.map`

链接配置严格按 VET6：512 KiB Flash、128 KiB SRAM、64 KiB CCM；当前不用
CCM，也没有整屏帧缓冲。

## 界面

主界面包含：

- 50–100 cm设定距离、视觉TOF换算的靶心距离`DIST`和“距离定位”；有效TOF原始值表示箱体前沿距离，靶心水平距离在其基础上增加25 cm，
  `DIST`断流500 ms后显示`-- cm`；
- 固定靶、自动搜索、暂时关闭的往复扫描入口，以及电机页和主题页入口；
- 状态、yaw/pitch实际角和目标角；点击主界面的YAW或PITCH卡片可输入带符号目标角；
- 独立数字键盘页，非法距离或越界角度不会提交；
- 电机页按状态提供 Enable、Scan、Stop Scan、Disable，当前不可用操作会变灰且不发命令。

界面预览：[`主界面`](docs/ui_main_preview.png) / [`发射控制页`](docs/ui_cannon_preview.png) /
[`充电控制页`](docs/ui_charge_preview.png) /
[`电机页`](docs/ui_motor_preview.png) /
[`主题页`](docs/ui_theme_preview.png) / [`距离键盘`](docs/ui_distance_preview.png) /
[`角度键盘`](docs/ui_angle_preview.png)。

触摸事件进入 8 槽静态命令 FIFO。发射与充电控制页的本地命令由组合层直接驱动PB0/PB1，
其余未接管命令仍进入`Src/control_port.c`的UI演示弱端口。以后接入其它控制层时，
用强符号实现 `control_port_submit()`，并通过
`lcd_demo_app_set_runtime()`推送状态、方位角和仰角（单位：度）。

## 分层

- `bsp/bsp_board_f407ve.*`：25 MHz HSE、168 MHz 时钟及 1 ms 单调时基；
- `bsp/bsp_cannon_relay.*`：PB0 发射、PB1 充电继电器，高有效；初始化后两路均关闭；
- `bsp/bsp_lcd_fsmc.*`：FSMC 引脚、地址和总线写入；
- `bsp/bsp_can1.*`：PB8/PB9 上的 CAN1 裸机轮询收发；
- `bsp/bsp_uart_bridge.*`：DAP虚拟串口与ESP8266 USART3的中断调试桥；
- `bsp/bsp_xpt2046_io.*`：触摸 GPIO 软件 SPI；
- `Device/ili9341/`、`Device/xpt2046/`、`Device/qd4310/`：器件协议和状态；
- `Application/control/`：无 MCU/Device依赖的距离标定和 yaw/pitch位置PID算法；
- `Application/ui/`：无 MCU/RTOS 依赖的界面状态、事件和静态字体；
- `Src/lcd_demo_app.c`：组合端口，不承载控制算法。

详细引脚、时序、NC 引脚和实机检查项见
[`docs/hardware_mapping.md`](docs/hardware_mapping.md)。触摸不准时先修改
`Inc/lcd_demo_config.h` 的 `TOUCH_RAW_X_LEFT/RIGHT` 和
`TOUCH_RAW_Y_TOP/BOTTOM`，不要在上层交换坐标补偿。

运行契约、命令 FIFO 和脏区策略见
[`docs/architecture.md`](docs/architecture.md)。

## QD4310 双轴控制（Scan暂时关闭）

当前双轴映射为 yaw ID `0x00`、pitch ID `0x01`，CAN1 为 1 Mbit/s。上电完成CAN初始化后
固件自动发送双轴Enable；两轴均收到本次Enable后的新应答，才以当前yaw位置作为yaw相对0°，
并以实测原始编码器`0xEE77`作为pitch水平0°。随后两个100 Hz位置PID都启动并输出电流，
默认目标均为0°，持续保持角度。点击主界面YAW/PITCH卡片可分别设置目标：yaw和pitch均为
`-50..+50°`；重新点击`ENABLE / HOME`会重新建立yaw零点，并继续使用
固定pitch水平零点和当前目标。TFT持续刷新双轴实际角/目标角。当前
`GIMBAL_SCAN_ENABLED=0`：STM32的Scan按钮显示`SCAN OFF`，本地及TCP
`SWEEP`命令均不会启动控制，也不会发送扫描电流；`ZERO CURRENT`仍可人工向两轴发送0 A。

双轴位置PID直接发送QD4310 `0x03`电流命令。yaw参数为
`Kp=0.080 A/°、Ki=0.002 A/(°·s)、Kd=0.006 A/rpm`，
误差限幅8°、积分限幅0.010 A，总电流限幅0.70 A；yaw相对上电零点的硬边界为±55°。
pitch采用简化保守位置PID参数
`Kp=0.010 A/°、Ki=0.0002 A/(°·s)、Kd=0.0005 A/rpm`，死区0.20°、积分区1.0°，电流限幅
0.30 A、相对角硬边界 ±90°；手动pitch目标范围为 ±50°，距离定位内部目标上限为±85°。
两个目标和两轴命令/反馈电流均通过
现有遥测输出。

当前按钮显示`ZERO CURRENT`，点击后停止两个PID并分别发送0 A。双轴继续保持Enable；只有点击
`DISABLE` 才向两轴发送 Disable。控制故障不会自动 Disable；故障时
停止双轴控制并尝试向两轴各发送一次 0 A，保持 Enable。MCU复位后无法证明电机真实使能状态，
因此固件重新发送Enable并等待本次新应答，不把旧缓存状态伪装成已确认。
当 yaw 实际角超过 ±55°、pitch相对角超过 ±90°、任一轴反馈超过 100 ms未更新，
或者任一轴电流帧无法排队时会显示
`GIMBAL CONTROL ERROR`。原 fault code `1..5`保持不变，新增 `6=pitch反馈超时`、
`7=pitch控制器越界/输入无效`、`8=pitch电流帧排队失败`。

板载 TJA1050 必须通过跳帽连接 PB8/PB9，并确保 QD4310 供电、CANH/CANL、
通信地和终端电阻正确。两台电机的 CAN ID 必须不同；ID 映射在
`Inc/lcd_demo_config.h` 中修改。

点击“距离”进入数字键盘设置50～100 cm；“距离定位”直接使用该设定值调用距离弹道公式，
以地面0 cm为目标强制选择低弹道调整pitch，不依赖VIS/TOF距离。定位成功后自动开始30 s充电，累计完成后不再
等待双轴到位，立即自动击发一次。为保持JX1协议兼容，线上命令名仍为`FIRE_DISTANCE`，它读取
当前保存的设定距离；`SET_DISTANCE`可更新该值。

“固定靶”和“自动搜索”继续读取500 ms内最新VIS帧中的`DISTANCE_VALID` TOF距离；有效原始距离
作为箱体前沿距离，限定为50～100 cm。弹道求解使用的靶心水平距离为箱体前沿距离加250 mm，即75～125 cm，
`DIST`显示该靶心距离。根据实测`DIST=100 cm`时弹丸只到达“箱体前沿37 cm”的位置，先统一成
靶心坐标`37+25=62 cm`，得到高弹道水平进度比`q=62/100=0.62`。两者用
`d_virtual=d_target/q×1.013`求新的高根；1.013是用于增加水平射程的乘法裕量，不是固定Pitch偏置。
若虚拟距离超过当前模型的可达包络，则使用约49.26°的最大射程角。
该修正沿弹道距离映射变化，不是统一加减固定Pitch角；不计弹丸直径和额外净空余量。无数据、距离无效、超时
或越界时显示`WAIT DIST 50-100CM`并保持原pitch目标。固定靶定位成功后同样开始30 s充电并在
累计完成时立即自动击发。
JX1命令在原有枚举尾部追加`SET_YAW_TARGET`和`SET_PITCH_TARGET`，`value`按带符号
`int16`解析；非法范围返回`INVALID`，命令FIFO满返回`FULL`。

“距离定位”和“固定靶”成功设置pitch目标，或“自动搜索”命令被接受后，都会进入发射控制页、
立即开始一轮新的充电并将计时清零。线上的`START`和TFT电机页`ENABLE / HOME`都只执行双轴
Enable/Home，不再作为电磁炮任务入口。PB0/PB1均为高电平导通、低电平关断；初始化完成后两路
均为低。三个任务入口成功后PB0保持低、PB1拉高，按主循环单调时基累计充电30 s；时间到后先将
PB1拉低，随后三个任务都立即把PB0拉高500 ms，不再等待角度、视觉、TOF或稳定条件。手动发射
同样要求本轮30 s充电已经完成。

充电控制页的页面、文字和按钮保持原样。发射控制页显示本轮充电累计和PB0发射脉冲倒计时。
发射控制页或充电控制页的`BACK`、“立即断开充电”、
STOP或DISABLE都会将PB1拉低并保留本轮已累计的充电时间；再次点击“开始充电”从暂停点继续，
不重新计时。重新点击“距离定位”“固定靶”或“自动搜索”才开始新任务、清零并重新累计30 s。
充电、暂停和发射均为非阻塞状态机，软件保证PB0/PB1不会同时为高。当前没有充满反馈、电压/电流
保护，30 s只是软件定时，不能替代外部硬件保护。首次上板应断开功率回路，只测上电两路低、
三个入口启动后PB1高、BACK后PB1低、累计30 s后PB1先低再PB0高以及所有取消路径。

“自动搜索”现已实现发挥部分第（1）问的本地非阻塞流程，且不受已关闭的往复`SWEEP`入口影响：
yaw目标先以8°/s在相对零点±50°内缓慢扫描。相机固定在Yaw轴上，因此收到每个新的普通
`X_VALID`后，都把当前像素误差作为相对视轴误差，以`-0.020°/px`换算并累加到上一条Yaw命令目标；
单帧最多修正2°，命令推进速度限制为3°/s。目标在画面右侧时修正负Yaw，目标在画面左侧时修正
正Yaw，直到进入画面中心±8 px；命令目标不会回写成当前电机反馈角，连续搜索/视觉目标更新也不会
反复清空Yaw积分。进入中心后保持最后命令目标，不继续到端点，也不进行第二遍回扫和两点标定。
锁定后只有连续2个同方向新样本都超过±16 px才重新修正，单帧离群点不会解锁；短暂无效帧在现有
500 ms目标丢失时间内保持Yaw，避免视觉抖动使机构反复启停。随后才消费
TOF，并与“固定靶”共用“靶心距离=箱体前沿距离+25 cm”、36.5 cm靶心、`q=0.62`实测高弹道
及1.013倍射程裕量
距离映射；“距离定位”单独使用人工
设定距离和地面0 cm低弹道模型。视觉端只有3个不同的
新样本连续落在该范围才置
`AIM_STABLE`，但当前单程锁定不要求`X_FINAL/AIM_STABLE`。锁定后目标移出±16 px或视觉连续失效
500 ms时返回搜索，不会启动二次标定；这些状态不再延迟30 s截止
时的击发。累计到30 s后固定先关PB1，再自动产生一次PB0高电平500 ms脉冲。
整个事务使用120 s总截止时间，充电暂停时间不计入截止时间。视觉/双轴故障、超时、
`ZERO CURRENT`、`DISABLE`或“立即断开发射”都会取消自动事务并保持PB0为低；任务取消时正在进行
的充电也会暂停并将PB1拉低。自动流程运行时手动“发射”和其它定位命令被禁用，完成或取消后
恢复。当前源码保留单程锁定、±50°搜索、相对像素误差闭环、±8/±16 px迟滞死区和带1.013倍
射程裕量的高弹道版本；运动机构、相机、TOF及真实落点仍需现场联调。

## ESP8266串口调试桥

当前调试固件把板载DAP虚拟串口`COM3`与ESP8266做双向字节桥接：

```text
电脑COM3 <-> DAP <-> USART1 PA9/PA10 <-> STM32
         <-> USART3 PB10/PB11 <-> ESP8266
```

两路串口均为115200、8N1，收发使用中断和两个静态有界环形缓冲，不占用主循环。
在串口终端发送`AT\r\n`可直接读取ESP8266响应。DAPLink目标复位会通过板级NRST
同时重启STM32和ESP8266；ESP8266 Boot ROM固定以74880波特率输出，因此115200终端
在`ready`之前看到乱码是正常现象。本桥只用于联调，接入正式TCP协议后应由ESP8266
Device状态机接管USART3，不再透明转发原始AT字节。

## MaixCAM2视觉、调试画面与TCP交接

当前正式入口为`maixcam/main.py`。它先监听`0.0.0.0:9000`，再初始化640×480相机、
箱面中心算法和PMOD_TOF100。当前联调配置`DISPLAY_ENABLED=True`，会在MaixVision/板载屏
显示相机调试画面，但任务输入仍使用STM32本地TFT；比赛无屏运行时可改回`False`。任何时刻
只能运行一个`main.py`实例，否则第二个实例会遇到9000或VO显示资源冲突。程序不占用9001。

每3个RGB帧产生一份视觉/TOF样本，并通过现有ESP8266 TCP连接发送单槽Latest Value
`JX1|VIS`帧。字段为`x`（像素中轴，无效=-1）、`d`（毫米，无效=65535）、`f`
（bit0=X_VALID、bit1=X_FINAL、bit2=DISTANCE_VALID、bit3=AIM_STABLE）和`w`（图像宽度）。
链路按样本`frame`去重，不把TOF间隔内重复传入的同一字典当成多个新样本。整帧继续使用
JX1序号、显式长度和CRC；STM32只在CRC、字段范围和flags全部有效时更新数据。
当前版本把有效TOF原始距离视为箱体前沿距离，增加25 cm后得到靶心水平距离并显示到主界面`DIST`卡片；500 ms没有新VIS帧即清除有效状态，
避免沿用旧测距。普通状态下只有用户点击“固定靶”时才会消费当前有效距离并更新pitch目标；
`VIS`本身仍是状态帧而非击发命令，只有已经由用户启动的120 s自动搜索事务可以消费它来调节
双轴；任务击发则统一由30 s充电累计截止触发。

`maixcam/tcp_server_test.py`只用于最小链路测试，不能与正式`main.py`同时运行，否则
二者都会绑定9000并触发`Address already in use`。开发联调时，电脑、MaixCAM2和
ESP8266作为STA连接同一2.4 GHz热点；比赛时计划切换为MaixCAM2 AP、ESP8266 STA。
ESP角色保持TCP客户端，MaixCAM2保持服务端。当前热点中的业务端点为
`192.168.215.66:9000`；热点必须保持给MaixCAM2分配该地址。

## 验证边界

历史版本的Debug/Release构建、DAPLink烧录校验、LCD/触摸和双轴Enable/Disable已实机验证。
首次电流环运行约 25 s，yaw 实际范围约 -43.30°..+44.17°，峰值命令电流约
0.155 A，CAN ESR=0。第二圈旧版误报来自启动方向检查重复执行，并非 CAN 故障；
旧位置环提高到 `Kp=0.028` 并增加端点保持后，用户仍观察到约 43°跟随上限。
所有角速度 PI/串级版本均出现明显震荡或卡顿，已被用户否决并删除。稳定位置 PID基线的
CAN记录显示 82/82周期均有新反馈，电流反馈约滞后一周期；末段约
`0.072 A → 1.05 rpm`，但原始转速标准差约 0.665 rpm。当前板上已恢复位置 PID。
诊断版额外在 RAM 中保留最近 256 个、约 2.56 s的控制样本，停止后可由 DAPLink
读取并用 `tools/analyze_yaw_trace.py` 导出 CSV；记录不会改变 CAN帧或 10 ms控制周期。
DAP串口桥已在实机通过`AT`/`OK`往返和DAP复位后的`ready`启动消息验证。ESP8266已用
`CWMODE_DEF=1`和`CWJAP_DEF`保存当前热点，ESP↔MaixCam TCP业务遥测已实测连续发送；
断线后每3秒自动重连，网络状态不会触发电机Disable。
当前自动搜索/击发版本已完成F407身份守卫下的烧录和独立Flash校验，但尚未执行新的机构瞄准和发射动作。
首次动作必须固定机构并确认pitch的`PITCH_HOLD_CURRENT_POLARITY`方向，从当前0.30 A限流
观察零位保持响应；若方向相反，应立即执行`ZERO CURRENT`并修改极性后再测。
