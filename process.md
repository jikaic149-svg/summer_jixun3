# 调试过程记录

> 只保留影响当前 STM32F407VET6 LCD/触控/QD4310 目标的有效事实、用户决策、
> 硬件证据和验证边界。已删除工程的冗长调参历史不再保留。

## 2026-07-21 当前工程基线

- 目标：魔女科技 STM32F407VET6 v5.5 开发板，34针2.8寸
  `ILI9341 + XPT2046` 横屏，双 QD4310 CAN节点。
- 架构：裸机协作式主循环；BSP持有时钟、GPIO、FSMC、CAN、UART和软件SPI，Device持有
  器件协议，`Application/control` 为纯控制算法，`Application/ui` 为纯UI状态。
- 工具链：GNU Arm GCC 10.3.1、CMake、Ninja；目标使用 Cortex-M4F硬浮点 ABI。
- 内存：STM32F407VET6 512 KiB Flash、128 KiB SRAM、64 KiB CCM；当前不用CCM，
  不使用动态内存和整屏帧缓冲。

## LCD与触控

- LCD：FSMC NE1，CMD=`0x60000000`、DATA=`0x60020000`；读时序
  ADDSET/DATAST=`15/60`，写时序=`3/8`；PA15背光高有效。
- XPT2046：PD13=CS，PE0=SCK，PE2=MOSI，PE3=MISO，PE4=PENIRQ；软件SPI，
  PENIRQ低有效。每5 ms读取5组样本做中值滤波，按下/释放各连续3次确认。
- 触摸ADC按12位处理，显式执行命令、BUSY/null拍和转换等待；拒绝全0/全1故障值。
- UI：320×240，使用控件 dirty mask，无全帧缓冲。距离合法范围50–100 cm。
- 用户决策：点击“距离”进入数字设置；点击“距离定位”使用当前设定距离计算并更新Pitch目标，
  成功后进入当前正式30秒充电和自动击发流程，不消费VIS距离。
- 主界面YAW/PITCH卡片显示实际角和目标角，点击后用带符号数字页设置目标；yaw/pitch范围
  均为±50°，Scan入口继续关闭。
- `control_port_submit()`仍是UI演示弱端口：合法命令返回已接受，但不驱动真实发射机构。

## CAN与QD4310

- CAN1使用PB8/PB9和板载TJA1050，APB1=42 MHz，1 Mbit/s；板厂位时序为
  Prescaler=6、BS1=5 TQ、BS2=1 TQ。裸机轮询，不使用中断、DMA或自动重发。
- yaw ID=`0x00`，pitch ID=`0x01`；编译期禁止重复或超出0x0F。
- Enable/Disable标准帧为 `0x400+ID`，DLC=3，数据分别为`01 00 00`和`02 00 00`；
  反馈为`0x500+ID`、DLC至少8。双节点事务各有确认、拒绝、250 ms超时和排队失败出口。
- 当前`GIMBAL_SCAN_ENABLED=0`；TFT/MaixCam Scan入口变灰，本地和TCP命令均不能启动
  往复扫描。CAN初始化成功后自动请求双轴Enable；新应答确认后yaw以当前位置取零，pitch
  使用固定原始值`0xEE77`作为水平零点，并启动两个100 Hz位置PID。
- `ZERO CURRENT`停止两个PID、向两轴分别发送0 A并保持双轴Enable；点击`DISABLE`才发送双轴
  Disable。用户明确否决上电、复位、故障或软件保护自动Disable；控制故障只停止控制
  并尝试向两轴各发送一次0 A，保持Enable。

## yaw当前控制

- 当前不执行Scan，而是在每次Enable确认后运行固定目标位置保持。默认目标0°，主界面或
  JX1可在±50°内修改；修改目标会清零积分项。
- 当前只使用位置PID；所有角速度PI、串级、速度参考、速度前馈、LPF和编码器差分估速
  已按用户实车反馈删除。
- 位置PID参数为`Kp=0.080 A/°、Ki=0.002 A/(°·s)、Kd=0.006 A/rpm`。
- 误差限幅8°、死区0.25°、积分区1.5°、积分限幅0.010 A，总电流限幅0.70 A，
  硬边界±55°。

## pitch当前控制

- 当前不依赖Scan；上电自动Enable确认后即使用独立位置环追踪目标，默认固定水平0°，
  主界面或JX1可在±8°内修改，不跟随yaw。
  `ZERO CURRENT`、控制故障和`DISABLE`都会退出该控制状态。
- 当前参数为`Kp=0.010 A/°、Ki=0.0002 A/(°·s)、Kd=0.0005 A/rpm`，误差限幅8°、死区
  0.20°、积分区1.0°、积分限幅0.010 A、电流限幅0.30 A、相对角硬边界±90°；手动目标
  范围±50°，距离定位内部目标上限±85°。
- 当前极性为`PITCH_HOLD_CURRENT_POLARITY=+1`，尚未实机验证；不得将编译通过写成
  pitch机械方向或闭环稳定性已确认。

## 实测与被否决方案

- 首轮位置环约25 s实测范围`-43.30°..+44.17°`，峰值命令电流约0.155 A，
  CAN ESR=0。
- 稳定位置环CAN基线：82个样本持续811 ms，响应序号每10 ms连续递增；末50点编码器
  差分均速1.048 rpm，CAN转速均值0.988 rpm、标准差0.665 rpm；命令/反馈电流均值
  0.0718/0.0707 A，反馈约滞后一周期。
- 两组原始CAN速度PI和一组50 ms编码器差分串级均出现明显震荡或卡顿；用户最终要求
  删除角速度环。不得再次把这些方案写成当前实现或继续盲调。
- yaw诊断环保留最近256×44 B、约2.56 s样本；新启动清零，停止后保留，
  `tools/analyze_yaw_trace.py` 可导出CSV。

## 实机与构建验证

- DAPLink：CMSIS-DAP FW 1.6.0，SWD 1 MHz，序列号`2024-09-01`。
- 芯片：CPUID=`0x410FC241`，DBGMCU_IDCODE=`0x101F6413`，Flash size=512 KiB。
- LCD、触摸、双轴Enable/Disable及CAN 1 Mbit/s已实机验证；yaw位置环仍需继续按实车结果评估
  端点角和长期稳定性。
- 当前板上DAP串口桥位置PID版 ELF SHA-256=
  `E852FC9F6EE7629DF39E41F39E2D7321AC5954B0247D8610E32AC922AF611CB6`，
  DAPLink/OpenOCD已完成擦写、`Verified OK`并复位运行。

## 2026-07-21 工程目录清理

- 依据：以`CMakeLists.txt`和全新Ninja依赖数据库建立当前目标的实际依赖闭包。
- 删除：旧HAL、FreeRTOS、Keil/MDK、平衡车、IMU、循迹、视觉模型与脚本、旧BSP/Device、
  旧主程序、旧测试、备份、PDF、CSV、压缩包、编辑器配置、缓存和旧构建产物。
- 保留：当前16个目标源文件及头文件、8个必要CMSIS/STM32F407头文件、CMake/启动/链接、
  当前测试与工具、README、架构、硬件映射和UI预览。
- 清理前非Git/构建主体约95 MB；清理后源目录为53个文件、约1.72 MiB。
- 验证：从空`build/`配置并全量构建Debug/Release成功，19项静态测试及3个Python工具
  语法检查通过。Release Flash=17,740 B、RAM=16,224 B；Debug Flash=19,308 B、
  RAM=16,224 B。
- 已知限制：本轮只整理当前LCD/触控/QD4310目标，不修改固件行为，也未重新烧录或重复
  实机测试；被删除内容仍可从Git历史恢复。
- 待确认事项：无。

## 2026-07-21 DAPLink与ESP8266串口联调

- 修改原因：当前固件未启用USART1/USART3，DAP虚拟串口不能直接观察或控制ESP8266。
- 最终决策：增加仅用于联调的115200 8N1中断字节桥；USART1使用PA9/PA10连接DAP，
  USART3使用PB10/PB11经跳帽连接ESP8266。每个方向使用255 B有效容量的静态有界环，
  满载丢弃新字节并计数，不阻塞、不自动重发，也不修改UI/CAN/电机行为。
- 涉及文件：`bsp/bsp_uart_bridge.*`、`Inc/lcd_demo_config.h`、`CMakeLists.txt`、
  `Src/lcd_demo_app.c`、当前静态测试、README和架构/硬件映射文档。
- 验证：DAPLink识别Cortex-M4，CPUID=`0x410FC241`、DBGMCU=`0x101F6413`；运行态
  寄存器确认PA9/PA10和PB10/PB11均为AF7，USART1 BRR=`0x2D9`、USART3
  BRR=`0x16D`、两路CR1=`0x202C`。Debug/Release构建及20项测试通过；Debug
  Flash=20,072 B，Release Flash=18,356 B，RAM均为16,768 B；OpenOCD擦写校验
  `Verified OK`。COM3发送`AT`收到回显和`OK`；
  DAP目标复位后捕获820 B ESP启动输出并收到`ready`，环形缓冲零丢弃。
- 已知限制：ESP Boot ROM的74880启动日志在115200桥上显示乱码并产生帧错误计数；进入
  AT固件后通信正常。透明桥不解析AT/TCP协议，正式通信需由Device状态机替换。
- 待确认事项：ESP AT固件为1.7.4，初始持久状态为`CWMODE=2`；后续联调已用
  `CWMODE_CUR=1`临时切为STA，但尚未写入永久网络配置。

## 2026-07-21 ESP8266与MaixCam TCP联调

- 修改原因：验证开发阶段三节点局域网，以及比赛阶段MaixCam AP/ESP STA方案所需的
  ESP↔MaixCam TCP基础链路。
- 最终决策：开发阶段电脑、MaixCam和ESP8266均作为STA连接同一2.4 GHz热点；比赛阶段
  计划由MaixCam切为AP、ESP保持STA。MaixCam固定为TCP服务器，ESP固定为TCP客户端。
- 涉及文件：新增`maixcam/tcp_server_test.py`；脚本已复制到MaixCam
  `/root/tcp_server_test.py`，测试进程结束后已正常停止。
- 验证：电脑`192.168.84.147`可Ping通MaixCam`192.168.84.217`；ESP使用
  `CWMODE_CUR=1`入网，DHCP地址`192.168.84.250`、RSSI=-52 dBm，Ping MaixCam为
  8 ms。ESP连接电脑临时服务器并完成`HELLO`/`WORLD`双向传输；随后连接MaixCam
  `192.168.84.217:9000`完成初始请求/响应。进一步独立验证两个方向：连接建立后
  MaixCam主动发送12 B `MAIX_TO_ESP\n`，ESP收到完整`+IPD,12`；ESP随后发送12 B
  `ESP_TO_MAIX\n`，MaixCam日志确认原样收到并回复9 B `MAIX_ACK\n`，ESP收到完整
  `+IPD,9`。连接主动关闭后双方均回到空闲，测试服务器已停止。
- 已知限制：上述IP均为本次DHCP租约，不是固定地址；`CUR`模式和热点凭据在复位后
  可能丢失。测试脚本按TCP字节流回包，没有长度、序号、CRC、应用ACK或业务语义。
- 待确认事项：确定比赛用MaixCam AP的SSID、密码、固定IP和端口，再设计正式有界帧协议
  与ESP8266 Device非阻塞状态机。

## 2026-07-21 MaixCam直连VOFA遥测

- 修改原因：用户需要在VOFA中观察双轴状态、编码器、实际/目标角、电流和时间，用于后续
  PID调试；用户否决虚拟串口方案，改为TCP直连。
- 最终决策：STM32 TEL增加双轴原始编码器、反馈有效位、相对启动零点实际角、目标角、
  指令/反馈电流和双轴响应计数；单位为raw、mdeg、mA和ms。MaixCam TCP 9001直接输出
  14路VOFA FireWater可读文本，100 ms一行；只保留时间、双轴状态、原始编码器、
  实际/目标角、指令/反馈电流和电机模式。9001只读且与9000控制链路解耦。原31路JustFloat方案
  因二进制显示乱码和通道过多已被用户否决。
- 涉及文件：`Application/protocol/gimbal_link_protocol.*`、`Src/lcd_demo_app.c`、
  `maixcam/gimbal_app.py`、`maixcam/vofa_protocol.py`、`maixcam/vofa_stream.py`、README。
- 验证：扩展TEL最坏值编码仍不超过ESP 255 B；Debug构建成功，Flash=26,828 B、
  RAM=18,168 B，OpenOCD烧录`Verified OK`；变更前26项测试通过。FireWater版本完成
  Python语法检查和14路文本帧内容验证；新版文件已上传MaixCam `/root`。
- 已知限制：当前只有10 Hz遥测，适合慢速扫描趋势，不足以替代100 Hz控制环原始瞬态；
  VOFA通道名需按固定顺序配置。MaixCam应用需重新运行后才加载新版9001格式。
- 待确认事项：用户在MaixVision重新运行`main.py`，VOFA以TCP Client连接
  `192.168.84.217:9001`、协议选择FireWater并确认14路曲线。

## 2026-07-21 TFT状态与交互优化

- 修改原因：原电机页四个按钮始终可点，Enable/Disable等待被错误显示为扫描，复位后还会
  把未知电机状态显示成`DISABLED`，操作含义和视觉层级不清。
- 最终决策：TFT改为深色仪表盘；电机状态明确为`UNKNOWN / ENABLING / ENABLED /
  SCANNING / STOPPED / DISABLING / DISABLED / FAULT`。按钮随状态启用或变灰；无效点击
  不发布命令，只在底部给出原因。复位默认`UNKNOWN`，仍不自动发送Disable。主页面保留
  高频状态与`MOTOR / THEME`入口；两张320×240 RGB565主题图直接存Flash，不占整屏SRAM。
- 涉及文件：`Application/ui/target_ui.*`、`Src/lcd_demo_app.c`、
  `Application/ui/tft_theme_assets.*`、`maixcam/vofa_protocol.py`、UI预览和项目文档。
- 验证：Debug构建成功，Flash=337,252 B（64.33%），RAM=18,192 B（13.88%）；预览脚本
  已生成并人工检查主页面、电机页和距离页；14路VOFA状态映射完成文本帧检查并已上传
  MaixCam `/root/vofa_protocol.py`。DAPLink/OpenOCD以SWD 1 MHz完成擦写、`Verified OK`和
  目标复位；ELF SHA-256=`B1A27881813B28C47DF88F5B1246471E56C513245D290AAC699385CF1B8514F2`。
  触摸命中、LCD颜色和完整实机状态切换仍待人工确认。
- 已知限制：TFT只有矩形和16 px位图字形，不使用透明度、抗锯齿或整屏帧缓冲；主题图片
  页面只负责浏览，不改变控制页背景。
- 待确认事项：逐项验证四操作语义、按钮禁用提示和状态过渡。

## 2026-07-21 TFT电机页触摸命中修复

- 修改原因：实机反馈`BACK`不方便点击，`ENABLE`偶发无法命中；原`BACK`只有52×22 px，
  四个电机按钮的触摸区与可视矩形完全相同，边缘和按钮间隙不接受触摸。
- 最终决策：`BACK`可视区扩大到80×26 px、独立触摸热区扩大到92×36 px；电机按钮
  可视高度扩大到42 px，并使用不重叠的独立热区覆盖左右和上下间隙。仍要求按下和松开
  属于同一控件，不改变Enable、Scan、Stop或Disable命令语义。
- 涉及文件：`Application/ui/target_ui.c`、`tools/render_ui_preview.py`和电机页预览。
- 验证：Debug构建成功，Flash=337,312 B（64.34%），RAM=18,192 B（13.88%）；布局预览
  已人工检查，触摸热区均在320×240范围内且相互不重叠。DAPLink/OpenOCD以SWD 1 MHz
  擦写并完成`Verified OK`和目标复位；ELF SHA-256=
  `8E05F9E41E49DF542B29E69D642F928EFFB256C077C53A7BD7CE4C8344244EB8`。实机命中率待确认。
- 已知限制：按钮在当前状态不允许操作时仍会变灰，点击只显示原因而不发布命令；这不属于
  触摸丢失。`ENABLE`在Enabled或Stopped时可重复取零，扫描时保持禁用；`DISABLE`除自身
  正在等待应答外始终可人工重发；`STOP SCAN`在双轴已使能后可重复发送双轴0 A。
- 待确认事项：实机连续点击`BACK`和`ENABLE`验证命中率。

## 2026-07-21 Enable后按钮错误锁定修复

- 修改原因：实机出现Enable后无法Disable或Back，用户否决一次性按键行为。
- 根因：Enable/Disable应答可能在相邻两次UI报告之间完成，使Device状态经历
  `CONFIRMED → WAITING → CONFIRMED`；旧的变化检测只比较状态枚举，没有比较
  `expected_enabled`，因此UI错误保留上一次`IDLE`并按旧状态锁定按钮。
- 最终决策：UI报告缓存同时比较yaw/pitch的`expected_enabled`。`DISABLE`在Enable等待期
  也允许人工发出以覆盖事务；`ENABLE / ZERO`在Enabled或Stopped状态允许重新取零；
  `STOP SCAN`在已使能后允许重复发送0 A；电机页和主题页`BACK`改为按下立即返回。
- 涉及文件：`Src/lcd_demo_app.c`、`Application/ui/target_ui.c`、电机页预览。
- 验证：DAP只读现场采样显示CPU持续处于正常`bsp_board_idle()`，XPT2046状态为
  `pressed=0、release_count=3`，排除主循环和触摸释放卡死；同时读到双轴
  `CONFIRMED + expected_enabled=true`而UI仍为`IDLE`，直接确认缓存缺陷。烧录前进一步确认
  yaw扫描`active=false`、指令电流0 A。Debug构建成功，Flash=337,416 B（64.36%）、
  RAM=18,192 B；OpenOCD烧录`Verified OK`并复位。ELF SHA-256=
  `EF7F9FB6FCAF5678876287896A1DA88E273526B58103331B262AC1492EC0ECD3`。
- 已知限制：本轮验证了软件根因和烧录校验，尚待用户连续执行Enable、Disable、Back和
  重复取零确认实机交互。
- 待确认事项：无卡锁后再验证Scan与Stop的连续操作。

## 2026-07-21 TFT主题页辨识度优化

- 修改原因：实机反馈主页面主题入口和主题页导航不明显。
- 最终决策：主页面`THEME`使用独立紫色强调色；主题预览增加40 px深色顶栏、紫色分隔线、
  `THEME 1 / 2`或`THEME 2 / 2`编号，并使用更大的`< BACK`和`NEXT >`按钮。主题图片仍按
  320×240全屏绘制，只由顶栏覆盖40 px，不重新编码图片资产。
- 涉及文件：`Application/ui/target_ui.c`、`tools/render_ui_preview.py`、README和主题页预览。
- 验证：布局预览已人工检查；Debug构建成功，Flash=337,568 B（64.39%）、RAM=18,192 B。
  烧录前DAP只读确认yaw扫描`active=false`且指令电流0 A；OpenOCD烧录`Verified OK`并复位。
  ELF SHA-256=`7D0711C10ACCAD8605184CEA649D7415C85FF37BC9749F9FC8F94070F8C2FD04`。
- 已知限制：顶栏是不透明覆盖，不支持半透明；可视图片高度有40 px被导航占用。
- 待确认事项：实机确认两张图片切换、标题编号和返回命中率。

## 2026-07-21 Theme顶栏按钮命中修复

- 修改原因：实机主题页`BACK`和`NEXT`可见但点击无效。
- 根因：用户按住`NEXT`时DAP现场读取到页面确为Theme、末次触摸坐标为`(294,0)`；触摸
  校准在屏幕最上沿会把y钳位为0，而Theme按钮矩形从y=2开始，导致顶边触摸差2 px未命中。
- 最终决策：`BACK`热区改为`x=0..95,y=0..39`，`NEXT`改为
  `x=224..319,y=0..39`，完整覆盖40 px顶栏；中间标题区相应改为`x=96..223`。
- 涉及文件：`Application/ui/target_ui.c`、`tools/render_ui_preview.py`和主题页预览。
- 验证：Debug构建成功，Flash=337,568 B（64.39%）、RAM=18,192 B；OpenOCD烧录
  `Verified OK`并复位。ELF SHA-256=
  `E010E56D7408DC3121CEF28CCDC524B3E103BB3E6C0A8DF23A02085511E96637`。
- 已知限制：本次只修正顶栏命中边界，不修改全局触摸标定。
- 待确认事项：实机分别点击`BACK`和`NEXT >`确认两侧顶栏命中。

## 2026-07-22 pitch直接电流控制

- 修改原因：用户要求pitch也能直接输出控制，不再只保持Enable。
- 最终决策：保留`ENABLE`仅使能和取零；`START SCAN`后yaw继续往复扫描，pitch使用独立
  100 Hz位置环保持取零位置，并通过ID `0x01`直接发送`0x03`电流。任一轴反馈超过
  100 ms、控制器越界或CAN发送失败时退出双轴控制并尝试向两轴各发一次0 A，仍不自动
  Disable；`STOP SCAN`同样向两轴发送0 A。新增fault code `6/7/8`分别表示pitch反馈
  超时、pitch控制失败和pitch电流帧排队失败。
- 涉及文件：`Application/control/pitch_hold_controller.*`、`Src/lcd_demo_app.c`、
  `Inc/lcd_demo_config.h`、`CMakeLists.txt`、README和架构/硬件映射文档。
- 验证：Debug和Release全量链接成功；Debug Flash=338,492 B（64.56%）、
  RAM=18,224 B（13.90%），Release Flash=335,036 B（63.90%）、RAM=18,216 B（13.90%）。
  `git diff --check`无空白错误。pitch目标角和命令电流已接入现有TEL/VOFA字段；按当前
  物理边界组合的TEL最长约219 B，未超过ESP单帧255 B上限。
- 已知限制：后续已烧录Scan关闭版本，但pitch控制环仍未运行；pitch正电流机械方向、
  0.30 A限流、±10°边界和当前PID参数的稳定性均未验证。
- 待确认事项：首次上机低风险确认`PITCH_HOLD_CURRENT_POLARITY`，观察pitch实际角、
  目标角、命令/反馈电流和fault code，再决定是否调参。

## 2026-07-22 临时关闭Scan

- 修改原因：用户明确要求当前暂时不要Scan，覆盖同日双轴扫描控制的启用计划。
- 最终决策：设置`GIMBAL_SCAN_ENABLED=0`并保留算法源码。TFT和MaixCam的Scan入口显示
  `SCAN OFF`且不发布命令；STM32本地`APP_COMMAND_SWEEP`拒绝执行，TCP `SWEEP`不进入
  FIFO并返回`INVALID`。`ENABLE`仍只使能和取零，不输出运动电流；`ZERO CURRENT`和
  `DISABLE`继续可用。恢复Scan必须由用户再次明确要求并把总开关改为1。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/ui/target_ui.c`、
  `Src/lcd_demo_app.c`、`maixcam/gimbal_ui.py`、UI预览脚本/图片及项目文档。
- 验证：Debug和Release构建成功；Debug Flash=338,416 B、RAM=18,224 B，Release
  Flash=334,844 B、RAM=18,216 B。MaixCam UI和预览脚本通过Python语法检查；主界面、
  电机页预览已确认Scan为灰色不可用状态。
- 已知限制：后续已烧录STM32关闭版本；MaixCam脚本仍未上传设备，但STM32会拒绝其
  `SWEEP`命令，因此远端不能绕过Scan关闭开关。
- 待确认事项：无；保持Scan关闭，等待用户后续明确恢复。

## 2026-07-22 Scan关闭版固件烧录

- 修改原因：用户要求把当前Scan关闭版固件烧录到目标板。
- 最终决策：烧录Debug ELF，不修改配置、不触发Enable/Scan/Disable，也不上传MaixCam。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `23C0321EA02D14F258A89951B8C64571C9D1AAF500FA346B8E2DF6F9C17ADCD0`。
- 验证：DAPLink FW 1.6.0、序列号`2024-09-01`，SWD 1 MHz；识别
  CPUID=`0x410FC241`、DBGMCU_IDCODE=`0x101F6413`、Flash=512 KiB。OpenOCD完成
  Programming、`Verified OK`和目标复位。复位运行500 ms后PC=`0x08001772`；新ELF
  对应的yaw/pitch控制active、双轴命令电流、扫描fault和start pending均为0，随后恢复运行。
- 已知限制：只验证了烧录、CPU运行和控制状态为零；未人工确认LCD画面、触摸、CAN或
  MaixCam显示。
- 待确认事项：保持Scan关闭；如需恢复必须再次明确授权。

## 2026-07-22 Enable新反馈校零

- 修改原因：用户要求Enable后读取电机编码器值，再依据该值校准零点；原实现只检查
  `feedback_valid`，重复Enable时存在使用旧缓存反馈取零的可能。
- 最终决策：发起双轴Enable前分别记录`response_count`基线；只有两轴均确认Enable、反馈
  有效且各自响应计数均不同于本次基线，才保存`last_angle_raw`作为yaw/pitch相对零点。
  原始编码器值继续通过现有TEL遥测输出，角度继续按16位回绕差值换算；Scan保持关闭。
- 涉及文件：`Src/lcd_demo_app.c`、README和架构文档。
- 验证：Debug和Release链接成功；Debug Flash=338,488 B、RAM=18,232 B，Release
  Flash=334,924 B、RAM=18,232 B；`git diff --check`无空白错误。Debug ELF SHA-256=
  `3E2C858366286E7F55097451AD9D53CF675584B8F46D8BB82F2B5CAEF7449DF5`；DAPLink以
  SWD 1 MHz连接，OpenOCD完成`Programming Finished`、`Verified OK`并复位目标。
- 已知限制：已完成烧录校验，尚未人工执行重复Enable取零并观察编码器反馈。
- 待确认事项：烧录后观察两轴响应计数递增、原始编码器值有效且相对角从0°建立。

## 2026-07-22 Enable后手动角度显示

- 修改原因：用户要求Enable后手动转动电机时TFT继续显示相对角；原实现仅在Scan控制运行
  时更新TFT角度，而且电机在空闲Enable状态不主动连续反馈。
- 最终决策：双轴Enable并完成取零后，扫描未运行时每50 ms向yaw/pitch各发送一次0 A
  命令以获取编码器反馈；使用独立响应计数更新两轴相对角和TFT脏区。监测不启动PID、
  不改变Scan关闭配置，也不自动Disable。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、README和架构文档。
- 验证：修改前在双轴`CONFIRMED`且零点有效状态进行DAP只读采样，两轴`response_count`
  在1 s内均保持为3，确认空闲时没有连续反馈。修改后Debug/Release全量链接成功；Debug
  Flash=338,776 B、RAM=18,240 B，Release Flash=335,156 B、RAM=18,240 B；
  `git diff --check`无空白错误。Debug ELF SHA-256=
  `DF0646B05CFFE197F386DF205A38D709C0D342A860F1AE9B92A65011A360376F`；DAPLink以
  SWD 1 MHz连接，OpenOCD完成`Programming Finished`、`Verified OK`并复位目标。Enable后
  DAP只读实测500 ms内两轴`response_count`均增加10，显示响应计数同步；零点有效，当前
  相对角为yaw 11°、pitch 6°，TFT UI模型字段同为11°/6°且状态为`READY/CAN_ENABLED`。
- 已知限制：TFT按整数度显示，TEL/VOFA仍保留毫度单位；已验证UI内存模型，未通过外部
  相机核对屏幕像素，也未确认物理正转对应的角度符号。
- 待确认事项：人工观察屏幕并分别正反转两轴，确认视觉刷新和符号符合机械定义。

## 2026-07-22 pitch水平零点与Enable归位

- 修改原因：用户确认TFT约+6°时炮体水平，要求以该位置作为pitch新零点，并在人工Enable
  后主动回到零点。
- 最终决策：使用现场水平位置的原始编码器`0xEE77`作为固定pitch 0°；旧的“首次Enable
  当前位置再加6°”方案会随启动姿态漂移，已否决。两轴Enable新反馈确认后，yaw仍以当前
  位置取零，pitch立即启动100 Hz位置环回到并持续保持固定水平0°。Scan保持关闭；yaw
  继续50 ms发送0 A监测帧，`ZERO CURRENT`停止pitch保持但不Disable。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、`Application/ui/target_ui.c`、
  README、架构/硬件映射文档和电机页预览。
- 验证：Debug/Release全量链接成功；Debug Flash=339,084 B、RAM=18,248 B，Release
  Flash=335,468 B、RAM=18,240 B；`git diff --check`无空白错误，预览已检查
  `ENABLE / HOME`和`PITCH HOME @ ENABLE`布局。Debug ELF SHA-256=
  `0BB36B9E4D2DB8BF7D4E5623FA405D3F871BDBD3C11475F786800DD0D69E62E7`；OpenOCD以
  SWD 1 MHz完成`Programming Finished`、`Verified OK`并复位目标，随后以当前ELF只读
  `verify_image`再次核对339,084 B烧录段一致。
- 已知限制：尚未执行首次pitch闭环动作；`PITCH_HOLD_CURRENT_POLARITY=+1`的机械方向、
  0.30 A限流和闭环稳定性仍未实机确认。相对水平位超过±10°会停止控制并报错。
- 待确认事项：从水平位附近偏转1–2°后点击`ENABLE / HOME`，确认pitch向0°收敛；若方向
  相反立即点击`ZERO CURRENT`并反转电流极性配置。

## 2026-07-22 当前固件烧录后只读验证

- 修改原因：用户要求继续确认烧录结果，同时明确暂时不要Scan；本轮不发送Enable/Home、
  Scan、Zero Current或Disable命令，也不上传或启动MaixCam程序。
- 最终决策：以当前Debug ELF为片上基准，保持`GIMBAL_SCAN_ENABLED=0`，只通过SWD读取
  运行状态。当前ELF SHA-256=`0BB36B9E4D2DB8BF7D4E5623FA405D3F871BDBD3C11475F786800DD0D69E62E7`。
- 验证：OpenOCD使用VID/PID `0x0416/0x5051`、FW 1.6.0和SWD 1 MHz连接STM32F407，
  当前ELF的`verify_image`执行无错误；CPU持续停留在`bsp_board_idle()`的WFI，硬件/UI启动
  标志均为1、时钟失败标志为0、命令FIFO为0。yaw Scan active=0、start pending=0，pitch
  hold active=0，yaw/pitch软件命令电流均为0。两轴最后反馈的电流raw和速度raw均为0；yaw
  为`CONFIRMED + expected_enabled=true`，pitch为`REJECTED + expected_enabled=true`且状态字
  未置Enable位。当前故障标志为1、fault code=7（pitch控制器更新失败），故障处理已停止
  pitch保持并向两轴尝试发送0 A；500 ms采样内两轴响应计数未继续变化。
- ESP8266链路：本机IPv4为`10.249.6.213`，固件目标`192.168.84.217:9000`当前Ping和TCP
  均不可达。SWD采样显示ESP状态机按`WAIT_WIFI -> RETRY_WAIT -> WAIT_WIFI`继续推进，历史
  error=4（TCP连接被拒绝），不是主循环卡死；未启动服务器或修改网络配置。
- 已知限制：未通过外部相机核对LCD像素，也未执行任何电机动作。fault code 7证明本次运行
  曾进入pitch保持后被安全退出，但缺少故障发生瞬间的角度/速度样本，不能据此判断是机械
  超过±10°边界、反馈瞬态还是其他控制输入异常；当前两轴控制输出均为0。
- 待确认事项：在机械安全条件下先排查pitch状态字和fault code 7原因，再决定是否重新执行
  `ENABLE / HOME`；在用户再次明确授权前继续保持Scan关闭。

## 2026-07-22 上电双轴角度保持与主界面目标设置

- 修改原因：用户要求上电后直接输出控制电流，用PID保持yaw/pitch角度，并允许在主界面
  设置两个目标角；继续明确不要Scan。
- 最终决策：CAN1初始化成功后自动发起双轴Enable，只有两轴本次新反馈均确认Enable后才
  建立零点并启动两个100 Hz位置PID。yaw以每次Enable时的当前位置为0°、目标范围±50°；
  pitch固定以`0xEE77`为水平0°、目标范围±8°。默认目标均为0°。主界面YAW/PITCH卡片
  打开共用带符号数字页；JX1在原枚举尾部追加`SET_YAW_TARGET/SET_PITCH_TARGET`并按
  `int16_t`解析value。`GIMBAL_SCAN_ENABLED=0`保持不变。
- 涉及文件：`Application/control/yaw_scan_controller.*`、
  `Application/control/pitch_hold_controller.*`、`Application/ui/target_ui.*`、
  `Application/ui/app_command_bus.h`、`Application/protocol/gimbal_link_protocol.*`、
  `Src/lcd_demo_app.c`、`Inc/lcd_demo_config.h`、MaixCam镜像UI、预览和项目文档。
- 验证：Debug/Release构建均无警告；Debug Flash=339,924 B、RAM=18,296 B，
  ELF SHA-256=`1F4CA49D66419392F88514617FF9A543C3C4FAE9D7F550F109868E2109858243`；
  Release Flash=336,104 B、RAM=18,288 B，ELF SHA-256=
  `5D4931D5D8156531EBB4E5566C94C6D3DE41013F65E09A77F6B60D09FF01DF5B`。
  Python语法、MaixCam负角/负零/越界/取消、JX1负值和主界面命中区测试通过；按当前物理
  边界构造的最长TEL为225 B，未超过ESP单帧255 B；`git diff --check`无错误。
- 已知限制：本版本未烧录、未执行上电自动动作。上一烧录版本只读现场状态显示pitch
  `REJECTED + expected_enabled=true`，即应答状态bit0未置Enable；历史fault code 7还表明
  pitch控制器曾因越界或输入检查失败退出。不能将本轮构建通过视为pitch机械方向或稳定性
  已确认。
- 待确认事项：固定机构后先读取pitch原始应答`last_status`和相对`0xEE77`角度，确认ID、
  供电和Enable状态；再以低风险方式验证`PITCH_HOLD_CURRENT_POLARITY=+1`及0.30 A限流。
  未确认前不自动烧录上电运动版本。

## 2026-07-22 OpenOCD目标识别复核

- 修改原因：用户指出OpenOCD安装在D盘，需要解释本轮为什么没有直接做目标机读取。
- 最终决策：使用`D:\OPENOCD\xpack-openocd-0.12.0-7\bin\openocd.exe`和CMSIS-DAP只读连接，
  不执行烧录、Enable或电流命令。
- 验证：探针枚举为VID:PID=`0x0D28:0x0204`，SWD DPIDR=`0x6BA02477`；目标CPUID读为
  `0x410CC601`（Cortex-M0+），DBGMCU_IDCODE=`0x00000000`，与本工程STM32F407VET6应有
  的CPUID=`0x410FC241`不匹配，因此当前USB链路不是云台STM32目标。采样后结束OpenOCD会话，
  未对该目标写入寄存器或Flash。
- 已知限制：无法用当前探针链路补读云台pitch的`last_status`、原始角度或CAN寄存器；此前
  pitch `REJECTED + expected_enabled=true`的解释仍以已保存的STM32现场快照为依据。
- 待确认事项：切换到枚举CPUID=`0x410FC241`、DBGMCU有效的云台STM32探针/USB链路后，
  再做只读状态采样。

## 2026-07-22 自动双轴保持版本烧录与CAN现场诊断

- 修改原因：用户明确要求烧录，并指出电脑同时连接了多只探针，前一次选择了错误接口。
- 最终决策：显式选择云台板CMSIS-DAP VID:PID=`0x0416:0x5051`、序列号`2024-09-01`
  （对应COM3），否决自动选中的`0x0D28:0x0204`/COM7。使用SWD 1 MHz烧录当前Debug ELF，
  不修改上电自动Enable和`GIMBAL_SCAN_ENABLED=0`配置。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `1F4CA49D66419392F88514617FF9A543C3C4FAE9D7F550F109868E2109858243`。
- 验证：烧录前确认CPUID=`0x410FC241`、DBGMCU_IDCODE=`0x101F6413`、Flash=512 KiB；
  OpenOCD完成`Programming Finished`、`Verified OK`和目标复位。复位后只读状态显示yaw/pitch
  均为`TIMEOUT`、`response_count=0`，双轴控制器inactive、命令电流0 A。CAN1时钟已开，
  PB8/PB9均为AF9，BTR=`0x00040005`对应1 Mbit/s；TSR显示两帧发送错误，
  ESR=`0x00100030`即LEC=3 ACK Error、TEC=16、REC=0，证明发送端已发帧但总线上无节点ACK。
- 已知限制：本次验证了烧录和无ACK时的0 A安全退出，没有运行PID闭环。当前不能区分电机
  未供电、TJA1050跳帽未接、CANH/CANL/共地问题或总线物理断开；两轴均无ACK，不是单独
  pitch PID参数导致。
- 待确认事项：检查双电机供电、PB8/PB9到TJA1050跳帽、CANH/CANL、通信地和两端120 Ω；
  总线恢复后重新上电，确认两轴Enable新应答再观察PID方向和电流。

## 2026-07-22 CAN恢复后的pitch控制保护诊断

- 修改原因：用户反馈CAN通信恢复后屏幕显示`CONTROL ERROR`。
- 最终结论：CAN已正常工作，pitch状态为`CONFIRMED`、status=`0x03`、反馈计数持续递增；
  当前pitch原始角`0xE545`，固定水平零点`0xEE77`，回绕差值为-2354 counts、约
  `-12.93°`。固件pitch硬边界为±10°，因此控制器返回失败并置fault code=7，随后停止
  双轴PID并发送0 A；不是CAN错误。
- 验证：COM3对应的`0416:5051`探针SWD只读采样确认`response_count≈2500`、
  `s_enable_zero_valid=true`、yaw/pitch控制器均已停止、软件命令电流均为0 A。
- 已知限制：当前机械位置超出保护边界，固件不会为了回零而越过±10°保护；不能仅靠重启
  消除故障。
- 待确认事项：先在机械安全条件下将pitch手动移回相对水平±10°内，再点击`ENABLE / HOME`；
  若确需从约-13°自动回零，须用户明确授权后再评估扩大保护边界并重新烧录。

## 2026-07-22 pitch高频震荡保守调参

- 修改原因：用户反馈pitch PID自行高频震荡并跑出保护边界；现场采样确认旧版pitch曾
  进入控制但随后触发fault code=7。
- 最终决策：只调整pitch，不改变yaw参数、100 Hz周期或±10°硬保护。pitch P/I/D由
  `0.028/0.002/0.004`分别降为`0.014/0.0005/0.0015`，对速度反馈增加一阶低通
  `α=0.15`；启动和改目标时清除积分及滤波状态。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/pitch_hold_controller.*`、
  README、架构和过程文档。
- 验证：Debug/Release构建无警告；Debug Flash=339,956 B、RAM=18,296 B，ELF SHA-256=
  `4A70ABBDCD028CA82F30BD3E2D063016A380482547AB2C5FD15C98A2617A334E`；Release Flash=
  336,132 B、RAM=18,296 B，ELF SHA-256=`D1E093A583E69D656A47C7BEAD9896D7E981B6AE9E015FEEA3B7290B0D324DF5`；
  `git diff --check`通过。
- 已知限制：新调参版本尚未烧录，尚无实机稳定性证据；当前板上仍是旧参数版本。调参
  不能替代pitch机械方向、重力负载和零点位置确认。
- 待确认事项：先将pitch移回±10°内，在机械安全条件下烧录新Debug ELF并观察角度、速度、
  命令电流和fault code；若仍震荡，再按实测波形继续降低P或调整速度滤波，而不是盲目
  放宽硬边界。

## 2026-07-22 pitch保守调参版烧录

- 修改原因：用户明确要求烧录高频震荡保守调参版。
- 最终决策：使用已确认的云台CMSIS-DAP COM3（`0416:5051`、序列号`2024-09-01`），烧录
  Debug ELF，不改yaw参数、不发送额外控制命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `4A70ABBDCD028CA82F30BD3E2D063016A380482547AB2C5FD15C98A2617A334E`。
- 验证：OpenOCD确认STM32F407并完成`Programming Finished`、`Verified OK`和复位；复位后
  yaw/pitch均`CONFIRMED`且反馈计数递增。采样时pitch约+2°，但fault code仍为7、两个
  控制器inactive、命令电流为0 A，说明启动初始采样曾超出±10°保护，故障锁存而非CAN失败。
- 已知限制：新调参环尚未获得连续实机稳定性证据；必须在pitch位于±10°内点击
  `ENABLE / HOME`后才能开始新一轮观察。
- 待确认事项：机械固定、清除故障后观察pitch实际角、速度和命令电流；若仍高频震荡，再
  基于波形继续调整，不绕过硬边界。

## 2026-07-22 pitch参数简化再调

- 修改原因：用户反馈上一版加入速度滤波后效果仍不好，并要求主要优化参数、不要复杂化。
- 最终决策：保留原有100 Hz位置环结构，撤销pitch滤波状态；仅使用保守P/I，D置零。
  当前参数为`Kp=0.010 A/°、Ki=0.0002 A/(°·s)、Kd=0`，死区0.20°、积分区1.0°，
  yaw参数、±10°保护和电流限幅不变。该条覆盖上一条`α=0.15`滤波方案。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/pitch_hold_controller.*`、
  README、架构和过程文档。
- 验证：Debug/Release构建无警告；Debug Flash=339,924 B、RAM=18,296 B，ELF SHA-256=
  `8A8726BD6780548790C4A7A335C60ECC826E05EE3CE5BDAD8BF2C6AD123181ED`；Release Flash=
  336,100 B、RAM=18,288 B，ELF SHA-256=`32B9C40F74148B0919526D9245C88722ADD3DD7CE0B16AE93AC4AAE6A9114169`；
  OpenOCD完成Debug版`Programming Finished`、`Verified OK`和复位。复位后2.5 s只读采样中
  pitch控制器持续active、fault=0、反馈速度为0，角度原始值稳定在`0xEE3B`，相对水平
  约-0.33°；命令电流仅由约5.07 mA缓慢升至5.20 mA，未跑出保护；`git diff --check`通过。
- 已知限制：SWD低频采样只能确认未跑出和无故障，不能证明机械高频振幅完全消失；现场
  触感和声音仍需用户确认。
- 待确认事项：观察P/I无D版本的机械振动；若仍存在明显高频抖动，优先继续降低Kp或Ki，
  不恢复复杂滤波、不放宽硬边界。

## 2026-07-22 pitch小速度阻尼

- 修改原因：用户要求增加速度阻尼，同时保持参数方案简单。
- 最终决策：不增加滤波或额外状态，只将pitch速度反馈阻尼设为
  `Kd=0.0005 A/rpm`，即输出中保留`-Kd×speed_rpm`；该值为早期震荡版0.004的八分之一。
  `Kp=0.010`、`Ki=0.0002`、死区0.20°、100 Hz周期及保护保持不变。
- 涉及文件：`Inc/lcd_demo_config.h`、README、架构和过程文档。
- 验证：Debug/Release构建无警告；Debug Flash=340,132 B、RAM=18,304 B，ELF SHA-256=
  `A178E48F47650E30174529B56802968683F80972993C361B6D6CEE36E898931B`；Release Flash=
  336,316 B、RAM=18,296 B，ELF SHA-256=`36EC217C1D045A080983AC95EF91D8BA2327EDFFA9677133CD8FF12C062736D7`；
  `git diff --check`通过。
- 已知限制：该小阻尼版尚未烧录；当前板上仍运行Kd=0版本，不能把构建通过视为阻尼效果
  已实机确认。
- 待确认事项：用户确认后烧录，并对比相同小角度扰动下的回零速度、过冲和机械高频振动。

## 2026-07-22 简单Kd版稳定快照烧录与测试

- 修改原因：烧录前发现工作区曾并发出现目标斜坡/电流斜坡状态；用户要求控制不要复杂，
  因此以简单PID源代码建立不可变快照后再烧录。
- 最终决策：移除pitch目标斜坡和电流斜坡，仅保留
  `Kp=0.010`、`Ki=0.0002`、`Kd=0.0005 A/rpm`、死区0.20°、积分区1.0°；yaw和
  100 Hz周期不变。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd_kd0005.elf`，SHA-256=
  `B8297DB3DF6822D6E087C33F89FE2A74A05BABCA7F88A62F9637015EBC30AAAA`；Release当前ELF
  SHA-256=`BC63C6EA9B1C23B0D73A6F7D0FA8751D2F5353797E74B39B4994703D8617D6D2`。
- 验证：Debug/Release清理构建无警告，Debug Flash=339,948 B、RAM=18,296 B；使用快照
  OpenOCD完成`Programming Finished`、`Verified OK`和复位。复位后约1.5 s至2.5 s采样中
  pitch active、fault=0、反馈速度为0，原始角约`0xEDFF`（相对水平约-0.66°），命令
  电流约8.8–8.9 mA，未出现超界；`git diff --check`通过。
- 已知限制：采样间隔为500 ms，不能完全排除更高频的机械振动；尚未做目标阶跃响应测试。
- 待确认事项：用户现场观察声音和触感；若仍有高频抖动，只在当前简单结构上继续微调
  Kp/Ki/Kd，不再引入额外斜坡或滤波层。

## 2026-07-22 pitch硬保护边界调整

- 修改原因：用户要求pitch硬保护阈值从±10°扩大到±20°，以允许更大初始偏差自动回零。
- 最终决策：仅将`PITCH_HOLD_HARD_LIMIT_DEG`改为`20.0°`；pitch目标输入仍限制±8°，
  yaw硬边界仍为±55°，PID参数和100 Hz周期不变。
- 验证：Debug/Release清理构建无警告；Debug Flash=339,948 B、RAM=18,296 B，ELF SHA-256=
  `A24EB4A2FC5E2DE5111DBF1C9938166D7C0A327DF849A5CF4B6D54DF4829174D`；Release Flash=
  336,104 B、RAM=18,288 B，ELF SHA-256=`F65CF9FAC9A3A6CB171EBB7F7FA28F117F3D6345794A4C12ED58A11D64859253`；
  `git diff --check`通过。
- 已知限制：新阈值版本尚未烧录；扩大边界会允许更大pitch机械运动，必须在机构安全、
  线缆和限位确认后再烧录。
- 待确认事项：用户明确烧录后，先在低目标角和低风险条件下观察pitch回零方向、峰值电流和
  是否再次触发fault code=7。

## 2026-07-22 pitch20版本烧录确认

- 修改原因：用户明确允许烧录当前Debug版本。
- 最终决策：使用正确的CMSIS-DAP `VID:PID=0x0416:0x5051`（序列号`2024-09-01`）和D盘
  OpenOCD，烧录`build/cmake/Debug/stm32f407ve_lcd_pitch20.elf`对应的Debug镜像；不发送
  Enable、Scan或额外电流命令。
- 验证：目标识别为STM32F407，`device id=0x101F6413`、Flash=512 KiB；OpenOCD报告
  `Programming Finished`、`Verified OK`并完成复位。板上已写入ELF SHA-256=
  `A24EB4A2FC5E2DE5111DBF1C9938166D7C0A327DF849A5CF4B6D54DF4829174D`。
- 已知限制：烧录完成后工作区又生成了新的`stm32f407ve_lcd_pitch20_target20.elf`，当前工作区
  源码配置为pitch硬保护±30°、目标范围±20°，其SHA-256=`159CC8ED90F2E1723F9BE12B69D62A53ABA77A367F409B57E0DEAF9B8243E860`，该版本未写入板上。
- 待确认事项：上电后先在低目标角、机构固定和线缆安全条件下观察pitch回零方向、峰值电流和
  fault code；如需把工作区最新±30°版本烧录，须再次明确确认对应镜像。

## 2026-07-22 pitch目标与保护范围最终烧录

- 修改原因：用户将最新需求改为pitch目标范围±20°、硬保护范围±30°，并明确允许烧录。
- 最终决策：`PITCH_TARGET_LIMIT_DEG=20.0f`、`PITCH_HOLD_HARD_LIMIT_DEG=30.0f`；
  PID参数保持`Kp=0.010`、`Ki=0.0002`、`Kd=0.0005`，死区0.20°，yaw不变。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd_pitch20_target20.elf`，SHA-256=
  `159CC8ED90F2E1723F9BE12B69D62A53ABA77A367F409B57E0DEAF9B8243E860`；Release当前ELF
  SHA-256=`A88231736F41E13501BBA1468430634F2220D895B58EABE14170D70630263595`。
- 验证：Debug/Release清理构建无警告，Debug Flash=339,948 B、RAM=18,296 B；OpenOCD
  确认STM32F407并完成`Programming Finished`、`Verified OK`和复位。复位后只读状态显示
  pitch active、fault=0、状态`CONFIRMED`、反馈持续递增，采样角约+0.45°，命令电流约-4.6 mA。
- 已知限制：±30°硬保护允许更大机械运动；尚未做±20°目标阶跃和极限位置实机验证。
- 待确认事项：机械安全条件下先用小目标角观察回零方向和峰值电流，再逐步使用±20°目标，
  若方向或振动异常立即`ZERO CURRENT`。

## 2026-07-22 烧录镜像归属复核

- 复核原因：烧录后工作区又生成了不同SHA-256的Debug镜像，需要区分板上镜像和工作区镜像。
- 已确认：本轮直接执行的OpenOCD写入使用`A24EB4A2FC5E2DE5111DBF1C9938166D7C0A327DF849A5CF4B6D54DF4829174D`，日志明确包含`Programming Finished`和`Verified OK`。
- 未确认：随后尝试只读核对工作区`159CC8ED90F2E1723F9BE12B69D62A53ABA77A367F409B57E0DEAF9B8243E860`时，CMSIS-DAP暂时不可用，不能据此断言该新镜像已经覆盖板上版本。
- 待确认事项：探针重新可用后再执行只读`verify_image`；在此之前以A24镜像作为已验证板上基线，不继续重复烧录。

## 2026-07-22 最终pitch范围确认与烧录阻塞

- 修改原因：用户最终确认pitch目标范围±20°、硬保护范围±30°，并延续烧录授权。
- 最终配置：`PITCH_TARGET_LIMIT_DEG=20.0f`、`PITCH_HOLD_HARD_LIMIT_DEG=30.0f`；TFT、JX1/TCP命令校验和控制器均引用同一配置。Scan保持关闭，yaw和100 Hz周期不变。
- 验证：Debug/Release均为`ninja: no work to do`且`git diff --check`通过；待烧录快照`build/cmake/Debug/stm32f407ve_lcd_pitch20_target20.elf`的SHA-256=`159CC8ED90F2E1723F9BE12B69D62A53ABA77A367F409B57E0DEAF9B8243E860`。
- 阻塞：本次OpenOCD未找到`VID:PID=0x0416:0x5051`的正确CMSIS-DAP，Windows只检测到错误探针COM7/`0x0D28:0x0204`；初始化在写Flash前失败，未烧录任何内容。
- 待确认事项：重新连接COM3/`0x0416:0x5051`探针后继续烧录该不可变快照；不得改用COM7。

## 2026-07-22 pitch目标与电流平滑

- 修改原因：用户反馈简化PID的实际效果仍不好；只继续降低增益会同时降低回零能力，无法处理
  Enable回零和目标切换产生的阶跃冲击。
- 最终决策：保留100 Hz位置环、`Kp=0.010`、`Ki=0.0002`和`Kd=0.0005`，不增加串级速度环。
  控制器启动时从当前实际角建立内部参考，再以6°/s逼近设置目标；电流命令以0.40 A/s限速，
  电流换向前先归零。yaw参数、Scan关闭、±10°硬保护和0.30 A pitch电流限幅均不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/pitch_hold_controller.*`、README和
  架构文档。
- 验证：修改前通过正确的`0416:5051`探针进行20点、50 ms间隔只读采样，pitch反馈计数每点
  增加6，静态角稳定在约-0.3296°、反馈速度为0，反馈电流约2.44..4.27 mA，说明本轮针对瞬态
  而非CAN丢帧。数值边界检查确认内部目标最大变化0.06°/周期、电流变化不超过0.004 A/周期。
  Debug/Release全量构建无警告，Flash分别为340,132/336,316 B，RAM分别为18,304/18,296 B；
  Debug SHA-256=`A178E48F47650E30174529B56802968683F80972993C361B6D6CEE36E898931B`，
  Release SHA-256=`36EC217C1D045A080983AC95EF91D8BA2327EDFFA9677133CD8FF12C062736D7`；
  `git diff --check`通过。
- 已知限制：平滑版尚未烧录，编译和静态采样不能证明机械振幅、过冲或长期稳态已改善；SWD间歇
  halt采样只用于确认静态状态，不作为高频波形测量。
- 待确认事项：机械固定并确保pitch位于±10°内后烧录Debug版，分别测试Enable回零、0°到±2°
  小阶跃和静态保持；根据实测只调整目标速率、电流斜率或P/I/D中的一项。

## 2026-07-22 38 cm距离定位

- 修改原因：用户提供地面落点`0° -> 67.5 cm`、`-15° -> 78 cm`和炮口高度12 cm，要求
  距离模式瞄准38 cm，并明确允许pitch超过原±20°限制。
- 最终决策：按用户确认采用两点实测直线`pitch=(67.5-distance)/0.7`，不采用理想抛体角；
  38 cm目标为向下`+42.14°`，38–100 cm映射范围为`+42.14°..-46.43°`。pitch目标范围扩大
  到±50°、硬保护扩大到±55°。用户随后明确本模块只负责调节角度，因此删除到位等待和
  击发端口调用；`APP_COMMAND_FIRE_DISTANCE`/JX1 `FIRE_DISTANCE`仅为兼容保留的定位命令名。
- 涉及文件：`Application/control/cannon_aim_model.*`、`Src/lcd_demo_app.c`、
  `Inc/lcd_demo_config.h`、TFT/MaixCam距离输入、CMake、README和架构/硬件映射文档。
- 验证：Debug/Release链接无警告；Debug Flash=340,200 B、RAM=18,296 B，Release
  Flash=336,280 B、RAM=18,288 B。边界计算得到38/67.5/78/100 cm分别对应
  `+42.142857/0/-15/-46.428571°`；MaixCam Python语法和`git diff --check`通过。
- 已知限制：未烧录、未执行±42°以上机械动作，也未进行落点复测；两点直线在38 cm和
  78–100 cm区间均属外推。当前范围明确不包含任何击发动作。
- 待确认事项：首次测试必须固定机构并准备断电，先逐级验证±20°以外的机械行程、线缆、
  pitch极性和稳定性，再用地面落点修正直线标定。

## 2026-07-22 距离定位纯角度版烧录

- 修改原因：用户明确要求烧录当前“只负责调节角度、不控制击发”的Debug固件。
- 最终决策：只使用Windows枚举为COM3的CMSIS-DAP `VID:PID=0x0416:0x5051`，烧录当前
  `build/cmake/Debug/stm32f407ve_lcd.elf`；不使用同时在线的其他探针，不发送额外角度或电流命令。
- 涉及产物：Debug ELF SHA-256=
  `79A18408D7C538175D4C80C11CC0C7E1C08A853A26A9D75E62224A0300C12E7E`。
- 验证：OpenOCD只读识别CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、Flash=512 KiB；
  探针FW=1.6.0、序列号=`2024-09-01`。写入日志明确包含`Programming Finished`、
  `Verified OK`和目标复位。首次带序列号过滤的连接未匹配设备且在写Flash前退出；随后以
  唯一`0416:5051`严格过滤完成识别和烧录。
- 已知限制：已验证写入和Flash内容一致，尚未在屏幕输入距离，也未执行大角度运动或地面落点测试。
- 待确认事项：固定机构并准备断电，从接近67.5 cm的小目标变化开始验证方向，再逐级扩大到38 cm；
  本版本不包含任何击发控制。

## 2026-07-22 距离定位范围最终改为50–100 cm

- 修改原因：用户最终要求设置距离只允许50–100 cm，点击“距离定位”后自动通过现有PID调整
  pitch电机；此前允许38 cm输入的扩展被取消。
- 最终决策：STM32触摸界面、JX1命令校验和MaixCam镜像界面统一接受50–100 cm；继续使用
  `pitch=(67.5-distance)/0.7`，对应pitch目标范围为`+25.00°..-46.43°`。定位命令直接调用
  `pitch_hold_controller_set_target()`，由现有100 Hz位置PID持续调节和保持，不包含击发动作。
- 涉及文件：`Inc/lcd_demo_config.h`、TFT/MaixCam界面、预览、README、架构和过程文档。
- 验证：49/50/100/101 cm边界检查分别为拒绝/接受/接受/拒绝；50/67.5/78/100 cm分别得到
  `+25/0/-15/-46.428571°`。Debug/Release链接无警告，Flash分别为340,200/336,272 B，
  RAM分别为18,296/18,288 B；Debug SHA-256=
  `14206DC6ECB4158F98630C3832DC2EF8B7D4F144E77C9B9C45E376A9A0774B02`。
- 已知限制：本条新镜像尚未烧录；板上仍是上一条38–100 cm版本。尚未实机执行距离定位或
  ±20°以外动作，两点直线在78–100 cm区间仍为外推。
- 待确认事项：用户明确烧录后写入新Debug镜像；首次实机从接近67.5 cm的小目标变化开始，
  确认pitch方向和稳定性后再逐级扩大距离变化。

## 2026-07-22 50–100 cm距离定位版烧录阻塞

- 修改原因：用户明确要求烧录最新50–100 cm距离定位Debug固件。
- 最终决策：仍只允许使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`；不改用当前在线的
  COM5/`0x0D28:0x4001`探针。
- 涉及产物：待烧录Debug ELF SHA-256=
  `14206DC6ECB4158F98630C3832DC2EF8B7D4F144E77C9B9C45E376A9A0774B02`。
- 验证：Debug构建为`ninja: no work to do`；Windows仅枚举COM5，未枚举`0416:5051`。
  OpenOCD使用严格VID/PID过滤后报告`unable to find a matching CMSIS-DAP device`，在目标识别和
  写Flash前退出。
- 已知限制：本次未写入、未擦除、未复位目标；板上仍运行上一版38–100 cm固件。
- 待确认事项：重新连接COM3/`0416:5051`后继续烧录同一不可变Debug镜像，不使用COM5。

## 2026-07-22 50–100 cm距离定位版烧录完成

- 修改原因：COM3正确探针恢复连接后，用户再次明确要求继续烧录。
- 最终决策：严格使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`写入上一条不可变Debug镜像；
  不使用COM5，不发送额外距离、角度或电流命令。
- 涉及产物：Debug ELF SHA-256=
  `14206DC6ECB4158F98630C3832DC2EF8B7D4F144E77C9B9C45E376A9A0774B02`。
- 验证：OpenOCD只读识别CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、Flash=512 KiB；
  探针FW=1.6.0、序列号=`2024-09-01`。烧录日志明确包含`Programming Finished`、
  `Verified OK`和目标复位。
- 已知限制：已验证Flash写入一致，但尚未通过屏幕提交50–100 cm距离，也未验证PID大角度运动
  或实际地面落点；78–100 cm仍是两点直线外推。
- 待确认事项：固定机构并准备断电，从接近67.5 cm的距离开始验证pitch方向和稳定性，再逐步
  扩大到50 cm和100 cm；本版本不包含击发控制。

## 2026-07-22 距离定位pitch符号取反并烧录

- 修改原因：用户实机确认距离定位产生的pitch目标需要整体增加负号，并明确要求修改后烧录。
- 最终决策：只对距离定位映射增加`CANNON_DISTANCE_PITCH_POLARITY=-1`，手动pitch目标符号、
  PID结构和电机电流极性均不变。最终公式为`pitch=(distance-67.5)/0.7`，50/67.5/78/100 cm
  分别映射为`-25/0/+15/+46.428571°`。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/cannon_aim_model.c`、README、架构、
  硬件映射和过程文档。
- 验证：Debug/Release链接无警告，Flash分别为340,200/336,272 B，RAM分别为18,296/18,288 B；
  四个边界/标定点数值检查通过。Debug ELF SHA-256=
  `F058650A60D84DD689D4BCE65A83569B69F1632B90680DE6705984F71850852D`。
- 烧录：严格使用COM3/CMSIS-DAP `0416:5051`；只读识别CPUID=`0x410FC241`、DBGMCU ID=
  `0x101F6413`、Flash=512 KiB。OpenOCD日志明确包含`Programming Finished`、`Verified OK`和复位。
- 已知限制：已验证代码、边界数值和Flash一致；尚未从屏幕提交距离，也未观察取反后PID机械方向、
  大角度稳定性或实际落点。
- 待确认事项：固定机构并准备断电，先输入接近67.5 cm的距离确认取反方向正确，再逐步测试50 cm
  和100 cm；本版本不包含击发控制。

## 2026-07-22 距离定位改为理想抛体双解

- 修改原因：用户否决此前两点直线和整体取反方向，最终确认距离定位必须按物理抛体双解计算；
  距离仍限50–100 cm，模块仍只调整pitch PID目标，不控制击发。
- 最终决策：炮口高12 cm，以0°水平落点67.5 cm反推等效初速度。对每个距离同时计算低、
  高两组发射角，默认采用低弹道；仅当低解严格大于`+5°`时，改用同距离的高弹道解。
  pitch物理角按向上为正；手动pitch输入仍限±50°，距离定位内部目标上限±85°，硬保护±90°。
  旧的线性公式和距离定位整体取反方案均不再生效。
- 涉及文件：`Application/control/cannon_aim_model.c`、`Inc/lcd_demo_config.h`、
  `Src/lcd_demo_app.c`、`Application/ui/target_ui.c`、README、架构和硬件映射文档。
- 验证：50–100 cm全部51个整数输入的判别式、弹道方程残差、解选择和±85°目标边界检查通过；
  50/67/67.5/78/85 cm选低解，分别约为`-6.10/-0.15/0/+2.98/+4.82°`，86/100 cm
  因低解约`+5.07/+8.49°`而切换高解，目标约为`+76.98/+74.67°`。Debug/Release均链接
  成功；Flash分别为341,048/337,096 B，RAM分别为18,400/18,392 B。Debug ELF SHA-256=
  `1D4D4FF63241A2E43420B93935B41E5837F8CF444F5AE936C0098FAF99701B6D`，Release ELF
  SHA-256=`5B60322D0834C0A260071C9499D2887EB7C2CC08A6E7307C496D1B318225A8E0`；
  `git diff --check`通过。
- 已知限制：这是忽略空气阻力、发射速度变化和机构误差的理想模型；此前`-15° -> 78 cm`
  实测点不再作为线性标定依据。高弹道约77°尚未实机验证机械行程、稳定性或实际落点。
- 待确认事项：本条双解镜像尚未烧录，板上仍是上一版线性取反固件。若后续明确要求烧录，
  应固定机构并准备断电，先从50–85 cm低弹道小角度验证，再单独确认86–100 cm高弹道行程。

## 2026-07-22 MaixCam2与板载ESP8266双向无线联调

- 修改原因：用户要求同时测试MaixCam2与开发板板载无线模块的收、发方向，并授权修改目标IP、
  上传和启动MaixCam2正式程序、构建及烧录STM32固件。
- 最终决策：当前2.4 GHz热点中MaixCam2固定使用`192.168.84.66`；ESP8266以TCP客户端连接
  `192.168.84.66:9000`传输JX1业务帧，MaixCam2在`9001`向PC输出FireWater遥测。旧目标
  `192.168.84.217`已取消。正式程序从`/root/main.py`启动；临时测试服务器停止后再占用9000，
  避免两个服务器冲突。
- 涉及文件：`Inc/lcd_demo_config.h`、`maixcam/*.py`、`README.md`、
  `docs/architecture.md`、`docs/hardware_mapping.md`和`process.md`。
- 验证：Debug/Release均构建成功，Flash分别为341,048/337,096 B，RAM分别为18,392/18,384 B；
  Debug ELF SHA-256=`61E2584089538A484E85F13AAA151EB606A44D465E3AF392CBC9777DD98793A2`，
  Release ELF SHA-256=`9C692025A78B7A74A8CAC830C5F28EA5602B5900A0795D9488346B8E522FC892`。
  Debug镜像严格通过COM3/CMSIS-DAP `0416:5051`烧录；识别CPUID=`0x410FC241`、DBGMCU
  ID=`0x101F6413`、Flash=512 KiB，日志包含`Programming Finished`、`Verified OK`和复位。
  本地8个MaixCam Python文件与`/root`对应文件SHA-256逐项一致。临时服务器实测持续收到来自
  ESP8266 `192.168.84.250`的有效JX1 `TEL/DBG`帧，并持续回复`MAIX_ACK`；STM32遥测中的
  `pe`计数随测试回复递增，证明返回字节已由ESP8266送达并被STM32解析。`MAIX_ACK`是故意使用的
  非JX1测试文本，因此这部分`pe`增长属于预期测试现象。正式程序PID 5259启动后同时监听
  9000/9001，9000与`192.168.84.250`保持`ESTABLISHED`；PC `192.168.84.147`从9001连续读取
  8行FireWater遥测，正式日志未出现Python异常。
- 已知限制：当前遥测报告双轴`CAN_ERROR/TIMEOUT`且无有效编码器反馈；这不影响无线TCP双向验证，
  但不能据此宣称电机、PID或距离定位动作已通过。正式程序当前由`nohup`启动，MaixCam2重启后
  是否自动运行未配置、未验证。
- 待确认事项：恢复QD4310供电、CANH/CANL、共地和终端电阻后再验证双轴反馈与pitch动作；若要求
  MaixCam2掉电重启后自动运行，需另行配置并验证正式应用自启动。

## 2026-07-22 无屏箱面视觉、TOF交接与DIST界面

- 修改原因：用户要求在任务UI增加独立distance显示，将
  `box_face_uart_handoff_20260722`并入当前视觉代码，并取消视觉画面UI，只保留赛题所需功能。
- 最终决策：STM32主界面第一行拆为设定距离、视觉实测`DIST`和距离定位三块。MaixCAM2正式
  `main.py`先监听9000，再启动640×480箱面视觉与PMOD_TOF100；`DISPLAY_ENABLED=False`，
  不创建VO显示设备、不绘制相机帧、不监听9001。外部交付的几何、TOF、UART和配置模块复制到
  `maixcam/`；正式链路不启用UART2，而是复用已验证的板载ESP8266 TCP连接。
- 协议：新增原子`JX1|VIS`状态帧，payload为`x`像素、`d`毫米、`f`有效标志和`w`图像宽度，
  继续使用JX1序号、显式长度和CRC。STM32在主循环中以单槽Latest Value消费；flags语义、范围、
  长度或CRC无效时整帧丢弃。500 ms无新帧后清除视觉有效状态，`DIST`显示`-- cm`；VIS不进入
  命令FIFO，也不自动驱动角度或击发。用户点击固定靶时，分派器要求最新样本同时满足
  `X_VALID|X_FINAL`和目标中轴位于画面中心±5%，再读取500～1000 mm有效TOF距离，以0.1 cm
  分辨率复用理想抛体双解和pitch PID；未锁定、未居中、无效、超时或越界时保持原pitch目标。
- 涉及文件：新增`maixcam/box_face_app.py`、`box_face_geometry.py`、`tof100_service.py`、
  `uart_link.py`、`vision_config.py`和`vision_link.py`；修改`maixcam/main.py`、JX1协议、
  `Src/lcd_demo_app.c`、TFT UI、配置、UI预览、README及架构/硬件映射文档。
- 验证：Maix端Python模块语法编译通过；VIS有效/无效样本经Python编码、CRC和流解码往返2例通过；
  Debug/Release完整链接无警告。Debug Flash=342,616 B、RAM=18,424 B、SHA-256=
  `C6EE9DD8D6F73A9A56C626813EEBF14265040091376EB14EEFFFF38FC37F0474`；Release
  Flash=338,536 B、RAM=18,416 B、SHA-256=
  `C71FA8B8559A7020ABA3948698A96D37961A5EC00922B3199E203C0E68F4720B`。新主界面预览已生成并
  目视确认三块卡片无重叠。
- 已知限制：本轮未上传MaixCAM2、未烧录STM32，也未连接相机/TOF做实物运行。RGB与TOF未做
  外参标定，目标未居中时中心TOF可能测到背景；固定靶当前只根据TOF调pitch，不根据视觉x调yaw。
  像素到yaw角度的视场角和方向尚未标定。旧9001 VOFA服务仍保留在调试模块源码中，但正式
  `main.py`不启动它。
- 待确认事项：上传全部`maixcam/*.py`并烧录本次Debug镜像后，确认启动日志含
  `APP_MODE|HEADLESS_BOX_FACE_WITH_TOF_TCP`、`VISION_TCP_READY`和`display=0`；用实物靶验证
  `VISION_TOF`距离、STM32 `DIST`刷新及500 ms断流清空，再标定像素误差到yaw控制方向/比例。

## 2026-07-22 恢复MaixCAM2调试画面并核对DIST界面

- 修改原因：用户覆盖此前无屏决定，当前联调需要相机调试画面，并反馈STM32 TFT未显示新增
  `DIST`卡片。
- 最终决策：当前`DISPLAY_ENABLED=True`，MaixCAM2显示640×480相机帧、识别叠加和TOF调试
  信息；STM32 TFT继续显示任务触控UI，主界面第一行固定为“设定 / DIST / 距离定位”。此前
  `DISPLAY_ENABLED=False`的无屏方向已被否决，仅保留为比赛阶段可选配置。
- 涉及文件：`maixcam/vision_config.py`、`maixcam/main.py`、`maixcam/vision_link.py`、
  `Application/ui/target_ui.c`、`README.md`、`docs/hardware_mapping.md`和`process.md`。
- 验证：14个Maix Python模块AST语法检查通过；有效VIS样本编码为
  `JX1|VIS|7|21|x=320|d=742|f=7|w=640|B356`，CRC、分段流解码和损坏帧拒绝通过；
  Debug/Release构建均为`ninja: no work to do`，`git diff --check`通过；主界面预览确认
  `DIST 74.2cm`卡片无重叠。当前Debug/Release ELF SHA-256分别为
  `C6EE9DD8D6F73A9A56C626813EEBF14265040091376EB14EEFFFF38FC37F0474`和
  `C71FA8B8559A7020ABA3948698A96D37961A5EC00922B3199E203C0E68F4720B`。
- 已知限制：本次恢复显示后的Maix脚本尚未重新上传，包含`DIST`的新STM32镜像尚未重新烧录；
  因此板端仍可能显示旧UI。相机与VO显示资源一次只能由一个Maix程序实例占用，同时运行会触发
  9000端口占用或VO初始化失败。
- 待确认事项：重新上传整个`maixcam`目录并运行唯一实例后，启动日志应包含
  `APP_MODE|BOX_FACE_WITH_TOF_TCP_DEBUG_DISPLAY`、`VISION_TCP_READY`和`display=1`；烧录最新
  STM32 Debug镜像后，即使尚无VIS帧也应先显示`DIST -- cm`，收到有效TOF后再显示实测厘米值。

## 2026-07-22 DIST界面Debug固件烧录完成

- 修改原因：用户明确要求烧录包含三栏主界面和视觉实测`DIST`卡片的最新STM32 Debug固件。
- 最终决策：只使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  以SWD写入`build/cmake/Debug/stm32f407ve_lcd.elf`；未发送电机、角度或击发命令。
- 涉及产物：Debug ELF SHA-256=
  `C6EE9DD8D6F73A9A56C626813EEBF14265040091376EB14EEFFFF38FC37F0474`。
- 验证：烧录前识别Cortex-M4、DBGMCU ID=`0x101F6413`和Flash=`512 KiB`；OpenOCD退出码为0，
  日志明确包含`Programming Finished`、`Verified OK`和目标复位。
- 已知限制：本次仅完成STM32固件烧录，尚未上传当前MaixCAM2调试显示脚本；在未收到有效VIS帧时
  `DIST`应显示`-- cm`，不能据此判定TOF故障。
- 待确认事项：目视确认TFT主界面第一行为“设定 / DIST / 距离定位”；随后上传并运行唯一Maix
  视觉实例，确认`DIST`从`-- cm`更新为实测值。

## 2026-07-22 固定靶取消画面居中限制测试版

- 修改原因：用户要求去掉`WAIT TARGET CENTER`对应的画面中心±5%限制，以便测试TOF距离到
  pitch定位链路。
- 最终决策：固定靶仍要求500 ms内的最新样本带`X_VALID|X_FINAL`，并要求同帧TOF距离有效且在
  500～1000 mm内；不再比较目标中轴与画面中心。识别未锁定提示改为`WAIT TARGET LOCK`。
  yaw和击发行为不变，仍不由固定靶命令控制。此前中心±5%要求已被当前测试决定覆盖。
- 涉及文件：`Src/lcd_demo_app.c`、`Inc/lcd_demo_config.h`、`Application/ui/target_ui.c`、
  `README.md`、`docs/architecture.md`和`docs/hardware_mapping.md`。
- 验证：构造`x=0`、`d=742 mm`、flags=`X_VALID|X_FINAL|DISTANCE_VALID`、宽度640的离中心VIS帧，
  编码/解码和固定靶门控静态检查通过；工程中已无中心容差宏、±5%说明或
  `WAIT TARGET CENTER`残留。Debug/Release完整链接无警告，Flash分别为342,584/338,504 B，
  RAM分别为18,424/18,416 B；ELF SHA-256分别为
  `0E39393530F0B7E7B59A5554BA6338197EA4B7BE877435C081AFCC7781A2BA9E`和
  `7D7DCC61BA947784E16BE4ACB22E7B655F4FBD1FFBA410D720BC1CC88E3854E3`。
- 已知限制：RGB与TOF没有外参标定，TOF仍读取深度矩阵中心1×1单元；目标偏离画面中心时，
  测得距离可能属于背景。必须人工确认TOF中心点落在目标上。
- 待确认事项：本测试版尚未烧录；烧录后用偏离中心但完整识别的目标点击固定靶，确认不再出现
  `WAIT TARGET CENTER`，并从小角度、机构固定且可断电状态验证pitch响应。

## 2026-07-22 取消居中限制测试版烧录完成

- 修改原因：用户明确要求烧录上一条取消画面中心±5%限制的固定靶测试固件。
- 最终决策：只使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  写入当前Debug ELF；未发送电机、角度或击发命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `0E39393530F0B7E7B59A5554BA6338197EA4B7BE877435C081AFCC7781A2BA9E`。
- 验证：烧录前Debug构建为`ninja: no work to do`；OpenOCD识别Cortex-M4、DBGMCU ID=
  `0x101F6413`和Flash=`512 KiB`，退出码为0，日志明确包含`Programming Finished`、
  `Verified OK`和目标复位。
- 已知限制：Flash写入一致已验证，但偏离中心目标的TOF与pitch动作尚需用户在固定机构、可断电
  条件下实测；TOF中心1×1单元仍可能在目标偏离时测到背景。
- 待确认事项：保持目标完整识别但偏离画面中心，点击固定靶应不再出现居中等待；确认`DIST`值
  属于目标表面后，再观察pitch小角度响应。

## 2026-07-22 固定靶纯distance按键测试版

- 修改原因：用户要求暂时关闭全部视觉目标限制，只测试TFT `DIST`数据和“固定靶”按键之间的
  距离定位关系。
- 最终决策：固定靶分派器忽略`X_VALID`、`X_FINAL`和目标中轴位置，只要求VIS样本在500 ms内、
  带`DISTANCE_VALID`且TOF距离在500～1000 mm；任何无数据、超时、无效或越界情况统一提示
  `WAIT DIST 50-100CM`并保持原pitch目标。yaw和击发仍不控制。
- 涉及文件：`Src/lcd_demo_app.c`、`README.md`、`docs/architecture.md`和
  `docs/hardware_mapping.md`。
- 验证：构造`x=-1`、`d=742 mm`、flags仅为`DISTANCE_VALID`、宽度640的VIS帧，协议解码和
  固定靶门控检查通过；Debug/Release完整链接无警告，Flash分别为342,568/338,488 B，RAM分别为
  18,424/18,416 B；ELF SHA-256分别为
  `664B395F2102E87279D17799AE261D7F3DB417720F135AADD525E48EB11AAAC0`和
  `7138F46EA3B679C669CD097B0447CDA4730F51B2BA87F9A032BD4FD4731BE86B`；`git diff --check`通过。
- 已知限制：本测试版不确认TOF测到的是目标，深度中心1×1单元可能落在任意表面；当前MaixCAM2
  9000端口未监听时，STM32不会收到新VIS帧，仍会显示距离等待。
- 待确认事项：本测试版尚未烧录；烧录并运行Maix正式`main.py`后，先确认`DIST`显示50～100 cm
  实测值，再点击固定靶观察pitch目标更新。

## 2026-07-22 纯distance按键测试版烧录完成

- 修改原因：用户明确要求烧录上一条固定靶纯distance测试固件。
- 最终决策：只使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  写入当前Debug ELF；未发送电机、角度或击发命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `664B395F2102E87279D17799AE261D7F3DB417720F135AADD525E48EB11AAAC0`。
- 验证：烧录前Debug构建为`ninja: no work to do`；OpenOCD识别Cortex-M4、DBGMCU ID=
  `0x101F6413`和Flash=`512 KiB`，退出码为0，日志明确包含`Programming Finished`、
  `Verified OK`和目标复位。
- 已知限制：当前MaixCAM2若未运行正式`main.py`并监听9000，STM32仍不会收到新的距离帧；
  固定靶按键会正确保持原pitch并提示`WAIT DIST 50-100CM`。
- 待确认事项：先观察`DIST`是否显示50～100 cm实测值；有值后点击固定靶，确认pitch目标按该值更新。

## 2026-07-23 PB0发射继电器、模式后置发射页与36 cm固定靶

- 修改原因：用户要求使用光耦继电器控制发射，并明确固定靶落点为离地36 cm；距离定位仍打地面。
  用户否决主界面独立“发射控制”按键，要求距离定位、固定靶或自动搜索之后再进入发射页；充电
  本轮暂不实现。
- 最终决策：PB0为低有效发射继电器，PB1为低有效充电继电器；两路在应用初始化前配置为低速
  推挽输出并先写无效高电平。只有发射页可发布本机`FIRE_PULSE/FIRE_ABORT`命令，发射脉冲为
  100 ms、非阻塞、回绕安全，脉冲期间拒绝第二次发射，也可人工提前断开。PB1没有使能调用，
  始终保持断开。距离定位成功后按地面0 cm目标进入发射页；固定靶使用最新有效TOF水平距离，
  按离地36 cm靶心的低弹道目标进入发射页；自动搜索命令被接受后进入发射页。任何定位失败均
  留在主界面。JX1命令枚举和协议未增加发射命令。
- 涉及文件：新增`bsp/bsp_cannon_relay.*`和`docs/ui_cannon_preview.png`；修改CMake、配置、
  `Src/lcd_demo_main.c`、`Src/lcd_demo_app.c`、弹道模型、UI命令/页面、预览工具、README、架构和
  硬件映射文档。
- 验证：Debug/Release均完整链接无警告；Flash分别为344,284/339,788 B，RAM分别为
  18,440/18,424 B。Debug ELF SHA-256=
  `71ABDE7720792DB63570273AF1115FD65BE460F84F6A895C3FC06CB6A4FF694F`，Release ELF SHA-256=
  `101A2FB0B1A463071FD06B78E1ACF86B334C637A36A65914467CFFF2CCB35D18`。50～100 cm全部整数距离
  的36 cm靶心低弹道残差小于`1e-4 cm`，pitch范围`+30.188°..+33.861°`；地面模式的既有
  50/67/78/85/86/100 cm结果仍为约`-6.10/-0.15/+2.98/+4.82/+76.98/+74.67°`。
  静态检查确认无PB1使能调用、主界面无独立发射按键，三种模式只在命令接受分支打开发射页；
  主界面和发射页预览已目视确认无重叠，`git diff --check`通过。
- 已知限制：本轮未烧录、未连接继电器或功率回路。低有效极性、100 ms触点动作和外部10 kΩ
  上拉均待实物确认；复位期间GPIO仍为高阻。100 ms截止依赖主循环推进，不是独立硬件单脉冲，
  MCU若在PB0吸合后停机，软件无法保证按时断开。自动搜索当前仍由弱演示端口立即接受，并不表示
  搜索算法或目标锁定已经完成。
- 待确认事项：先断开功率回路，示波器确认复位/空闲PB0、PB1为高，三种入口跳页符合预期，
  单击发射只产生约100 ms低脉冲且立即断开可提前结束；确认无误后再接光耦输入。充电时序、
  充满反馈、发射互锁和自动搜索完成条件留待后续单独定义。

## 2026-07-23 PB0发射控制Debug固件烧录完成

- 修改原因：用户明确要求烧录包含PB0发射继电器、模式后置发射页和36 cm固定靶弹道的最新固件。
- 最终决策：严格使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  以SWD 1 MHz写入当前Debug ELF；未使用其它探针，未发送充电、发射、角度或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `71ABDE7720792DB63570273AF1115FD65BE460F84F6A895C3FC06CB6A4FF694F`。
- 验证：烧录前Debug构建为`ninja: no work to do`；目标识别CPUID=`0x410FC241`、DBGMCU ID=
  `0x101F6413`、Flash=512 KiB。OpenOCD日志明确包含`Programming Finished`、`Verified OK`和目标复位。
- 已知限制：已验证Flash写入与ELF一致，但尚未验证PB0/PB1实物极性、空闲电平、100 ms脉宽、
  继电器触点或功率负载；复位后固件仍按既有逻辑自动请求双轴Enable。
- 待确认事项：断开功率回路，用示波器确认PB0/PB1空闲为高、发射PB0为约100 ms低脉冲且PB1
  始终为高，再连接光耦输入并验证三种模式到发射页的跳转。

## 2026-07-23 发射页字体补齐、距离定位角度取反并烧录

- 修改原因：发射控制页新增中文未进入LCD字体子集，实屏显示为问号；用户同时确认距离定位
  提交给pitch控制器的弹道角必须增加负号。
- 最终决策：字体生成字符集补齐发射控制页全部文案并重新生成`ui_font.c`。距离定位仍按地面
  0 cm求低/高弹道，但将最终选中的弹道角整体乘以`-1`后提交；固定靶仍按离地36 cm低弹道
  直接提交，不取反。此前“双解输出不取反”的方向已被本次用户决定覆盖。未修改PB0发射脉冲、
  PB1充电关闭状态、页面跳转或其它控制逻辑。
- 涉及文件：`tools/generate_ui_font.py`、`Application/ui/ui_font.c`、
  `Application/control/cannon_aim_model.c`、README、架构和硬件映射文档。
- 验证：静态检查确认`target_ui.c`的75个中文字符均存在于生成字体，发射页所需字形全部覆盖；
  Debug/Release均链接无警告，Flash分别为344,948/340,456 B，RAM分别为18,440/18,424 B。
  50/67/78/85/86/100 cm距离定位分别提交约
  `+6.10/+0.15/-2.98/-4.82/-76.98/-74.67°`，固定靶函数路径未取反。Debug/Release ELF
  SHA-256分别为`AB2750676FCD4CDAFAC56ECD982DCBAAC294E025FC80F295F8DD141B18104C61`和
  `DB82EBD032834DCA95E7EE275411AD4E827578FF4D3F3F7648EC9D3EFE160E3`；`git diff --check`通过。
  严格使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，以SWD写入
  Debug ELF；OpenOCD识别Cortex-M4、DBGMCU ID=`0x101F6413`、Flash=512 KiB，日志包含
  `Programming Finished`、`Verified OK`和目标复位。未发送充电、发射、角度或电流命令。
- 已知限制：字体覆盖和Flash写入已验证，但发射页实屏显示、距离定位取反后的机械方向、稳定性
  及实际地面落点尚未实测；复位后固件仍按既有逻辑自动请求双轴Enable。
- 待确认事项：进入发射页确认中文不再显示问号；固定机构并准备断电，从接近67 cm的小角度开始
  确认距离定位符号，再逐步测试其它距离。固定靶36 cm落点方向应保持原行为。

## 2026-07-23 PB0发射继电器极性取反

- 修改原因：用户要求将发射继电器控制逻辑取反。
- 最终决策：只把PB0发射继电器由低有效改为高有效，空闲输出低、发射时输出约100 ms高脉冲，
  “立即断开发射”恢复低电平；此前PB0低有效方向已被用户否决。PB1充电继电器仍为低有效、
  空闲高且没有业务入口，发射状态机、页面逻辑和脉宽均不变。
- 涉及文件：`Inc/lcd_demo_config.h`、README、架构、硬件映射和过程文档。
- 验证：Debug/Release均完整链接无警告，Flash分别为344,948/340,460 B，RAM分别为
  18,440/18,424 B；配置检查为`CANNON_FIRE_RELAY_ACTIVE_LOW=0`、
  `CANNON_CHARGE_RELAY_ACTIVE_LOW=1`，未发现残留的PB0低有效波形说明，`git diff --check`通过。
  Debug/Release ELF SHA-256分别为`CAC6755F4E44F83EAF4DBCA7A671364BFB9C17CA1940237F83C126BA4544F5CC`
  和`0FD69FC0D653FE428A02A0A31E2E2E48F890668177B568F592967A834812D8BB`。
- 已知限制：本版本尚未烧录，板上仍运行上一版PB0低有效镜像；PB0高有效实物极性和100 ms脉宽
  尚未用示波器确认。复位阶段PB0为高阻，高有效模块需外接约10 kΩ下拉避免误吸合。
- 待确认事项：确认继电器功率回路已断开后再烧录；烧录后先验证PB0空闲约0 V、发射时约100 ms
  高脉冲，再连接光耦输入和负载。

## 2026-07-23 PB0高有效发射版本烧录完成

- 修改原因：用户明确要求烧录上一条PB0发射极性取反版本。
- 最终决策：严格使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  写入当前Debug ELF；未发送充电、发射、角度或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `CAC6755F4E44F83EAF4DBCA7A671364BFB9C17CA1940237F83C126BA4544F5CC`。
- 验证：烧录前Debug构建为`ninja: no work to do`且`git diff --check`通过；OpenOCD识别
  Cortex-M4、DBGMCU ID=`0x101F6413`和Flash=512 KiB，日志包含`Programming Finished`、
  `Verified OK`和目标复位。复位后只读GPIOB寄存器为MODER=`0x00AA0285`、OTYPER=`0`、
  PUPDR=`0x00500106`、IDR=`0x0000FFFA`、ODR=`0x00000002`，确认PB0/PB1均为推挽输出，
  PB0下拉且空闲低，PB1上拉且空闲高。
- 已知限制：已验证镜像写入和空闲GPIO状态，但没有触发发射，因此PB0约100 ms高脉冲、光耦输入、
  继电器触点和功率负载仍未实测；复位后固件仍按既有逻辑自动请求双轴Enable。
- 待确认事项：保持功率回路断开，用示波器确认点击发射时PB0产生约100 ms高脉冲、“立即断开发射”
  可提前恢复低电平，再连接光耦和负载。

## 2026-07-23 用户确认低触发并恢复PB0低有效固件

- 修改原因：用户明确确认实物继电器为低电平触发，否决上一版PB0高有效配置。
- 最终决策：PB0发射继电器恢复低有效，空闲高、发射约100 ms低脉冲、“立即断开发射”恢复高；
  PB1继续低有效且不启用。收到纠正后先通过GPIOB BSRR把仍运行高有效镜像的PB0强制写高至断开态，
  再构建并烧录低有效固件。此前PB0高有效方向已被否决，不得再次采用。
- 涉及文件：`Inc/lcd_demo_config.h`、README、架构、硬件映射和过程文档；烧录产物为
  `build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `AB2750676FCD4CDAFAC56ECD982DCBAAC294E025FC80F295F8DD141B18104C61`。
- 验证：Debug/Release均完整链接无警告，Flash分别为344,948/340,456 B，RAM分别为
  18,440/18,424 B，Release ELF SHA-256=
  `DB82EBD032834DCA95E7EE275411AD4E827578FF4D3F3F7648EC9D3EFE160E3`，`git diff --check`通过。
  COM3/CMSIS-DAP烧录日志包含`Programming Finished`、`Verified OK`和目标复位；未发送充电、
  发射、角度或电流命令。复位后只读GPIOB寄存器为MODER=`0x00AA0285`、OTYPER=`0`、
  PUPDR=`0x00500105`、IDR=`0x0000FFFB`、ODR=`0x00000003`，确认PB0/PB1均为推挽输出、
  上拉且空闲高。
- 已知限制：已验证最终镜像写入和空闲GPIO状态，但没有触发实际发射；约100 ms低脉冲、光耦输入、
  继电器触点和功率负载仍未实测。复位阶段GPIO为高阻，PB0/PB1仍需外接约10 kΩ上拉。
- 待确认事项：保持功率回路断开，用示波器确认点击发射时PB0产生约100 ms低脉冲，点击
  “立即断开发射”可提前恢复高电平，再连接负载。

## 2026-07-23 发挥部分第（1）问双扫描自动瞄准与单次击发

- 修改原因：用户要求在尽量保持视觉端和MCU原代码完整的前提下，实现一键自动搜靶并在30 s内
  击中；允许第一次扫描锁定、第二次扫描校准。固定靶/自动靶按离地36 cm落点，原“距离定位”
  仍打地面，PB0继电器继续低电平触发，PB1充电暂不处理。
- 最终决策：复用现有`VIS`帧、双轴位置PID、固定靶弹道和PB0脉冲，不新增JX1命令。MaixCAM2
  对每3个RGB帧产生的样本按`frame`去重，3个不同的`X_FINAL`样本连续处于中心±8 px才置
  `AIM_STABLE` bit3。MCU使用`SEARCH/CALIBRATE_A/CALIBRATE_B/ALIGN/SETTLE/FIRING`
  单枚举状态机：yaw在±30°粗搜，粗锁后用约4°局部基线现场标定像素/度和符号，按
  `yaw_delta=-pixel_error/slope`居中；TOF按36 cm靶高更新pitch。视觉稳定、双轴误差均≤1°并
  再保持300 ms后只输出一次100 ms低脉冲。30 s总截止时间不因丢靶回扫而延长；丢靶、比例无效、
  故障、STOP、DISABLE或人工断开发射均不击发并恢复PB0高。取消在进入命令FIFO前即执行，
  即使FIFO满也不延迟断开发射。自动状态中UI禁用手动发射和其它定位命令。
- 涉及文件：`maixcam/vision_config.py`、`maixcam/vision_link.py`、
  `Application/protocol/gimbal_link_protocol.*`、`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、
  `Application/ui/target_ui.*`、`tools/generate_ui_font.py`、`Application/ui/ui_font.c`、README、
  架构和硬件映射文档。
- 验证：Debug/Release从清理状态完整链接且无警告，最终增量复核链接同样通过；Debug
  Flash=347,652 B、RAM=18,472 B，Release Flash=342,788 B、RAM=18,464 B。Debug/Release
  ELF SHA-256分别为`A379C22949AA5F90FCCFDD8D926865157A11E649AD3C6D0AC5E37FEAC3BBC5B5`和
  `A3E1953B89DAA939DA28ADE0E59295FDBAF8164FB23AB2E9EA97F60FDAE39786`。Python检查通过：全部
  Maix脚本语法、同帧去重、3个新样本稳定门限、无效样本清零、bit3编码、局部标定端点平移、
  UI中文字体全覆盖、36 cm/30 s/PB0低有效/100 ms截止和校准负号不变量；`git diff --check`通过。
- 已知限制：本版本未上传MaixCAM2、未烧录STM32，也未在运动机构、相机/TOF、继电器或功率回路
  上执行自动击发。编译和静态测试不能证明相机目标在±2°局部移动中始终可见、实测比例可靠、
  双轴可在30 s内收敛、36 cm实际落点正确或PB0触点确为单脉冲。软件100 ms截止仍依赖MCU主循环。
- 待确认事项：首次联调断开功率回路并固定机构，依次验证粗搜±30°、局部校准、遮挡/断网/轴故障/
  STOP/DISABLE/人工断开均无低脉冲；最后用逻辑分析仪确认满足全部稳定条件后PB0只产生一次约
  100 ms低脉冲且总时间≤30 s，再接负载测试36 cm落点。

## 2026-07-23 PB0低有效脉冲延长至500 ms

- 修改原因：用户实测原100 ms低电平时间太短，继电器没有响应，要求延长PB0触发时间。
- 最终决策：只将统一配置`CANNON_FIRE_PULSE_MS`从100 ms改为500 ms；手动发射和自动击发
  共用同一非阻塞截止时间。PB0仍低有效、空闲高，脉冲期间拒绝第二次发射，“立即断开发射”、
  STOP、DISABLE和故障仍可提前恢复高电平；PB1保持未使用。此前100 ms时长已被本次决定覆盖。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/ui/target_ui.c`、UI预览工具与图片、README、
  架构和硬件映射文档。
- 验证：Debug/Release均无警告链接成功；Debug Flash=347,652 B、RAM=18,472 B，Release
  Flash=342,788 B、RAM=18,464 B。Debug/Release ELF SHA-256分别为
  `DBA3BECA423AFB2E0FA3CC56831954F965BC10CEE0E2843CE618A34776F86738`和
  `8C268B838B2F6BED09BE6C5061CB9CC7D70F1DE343F784522CCAA77D7654CF10`。静态检查确认PB0低有效
  配置未变，代码仍以`now_ms + CANNON_FIRE_PULSE_MS`截止，自动击发会为完整500 ms预留30 s内
  时间；发射页预览确认`PB0 / 500 ms`无溢出，`git diff --check`通过。
- 已知限制：本轮未烧录、未触发PB0，也未验证500 ms能否使实物光耦继电器可靠动作。若500 ms
  仍完全无响应，更可能需要检查PB0实际电平、共地、光耦输入接法、模块供电和有效极性，而不能
  继续仅凭软件无限延长。
- 待确认事项：保持功率回路断开，烧录后用示波器或逻辑分析仪确认PB0空闲为高、发射时连续低约
  500 ms且人工断开可提前恢复高；再观察继电器指示灯和触点动作，确认后才能连接负载。

## 2026-07-23 PB0 500 ms与自动瞄准Debug固件烧录

- 修改原因：用户明确要求烧录上一条PB0低有效500 ms版本。
- 最终决策：严格使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，
  以SWD 1 MHz写入当前Debug ELF；不使用其它探针，不发送充电、发射、距离或角度命令。镜像包含
  双扫描自动瞄准代码和PB0 500 ms低脉冲配置，MaixCAM2脚本本轮未上传。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `DBA3BECA423AFB2E0FA3CC56831954F965BC10CEE0E2843CE618A34776F86738`。
- 验证：烧录前只读识别探针FW=1.6.0、目标CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、
  Flash=512 KiB。OpenOCD写入后独立执行`verify_image`，校验347,652字节成功且退出码0，并复位
  运行。复位2.5 s后只读GPIOB寄存器：MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x00500105`、
  IDR=`0x0000FFFB`、ODR=`0x00000003`，确认PB0/PB1均为推挽输出、上拉且空闲高。
- 已知限制：Flash内容和继电器空闲电平已验证，但本轮没有触发PB0，因此500 ms实际低脉冲、光耦
  指示、继电器触点和负载仍未验证。目标复位后沿用现有自动双轴Enable行为。
- 待确认事项：保持功率回路断开，点击发射并用示波器确认PB0连续低约500 ms；确认“立即断开发射”
  能提前恢复高电平及继电器可靠动作后，再连接负载。自动瞄准实测还需上传匹配的MaixCAM2脚本。

## 2026-07-23 PB1手动充电控制页

- 修改原因：用户要求新增充电UI界面并控制PB1。
- 最终决策：主界面新增“充电”入口，独立页面提供“开始充电”和“立即断开充电”。开始后低有效
  PB1持续为低，人工断开、返回主界面、STOP或DISABLE均立即恢复高；取消命令在进入FIFO前先
  执行，即使队列已满也不延迟断开。PB0/PB1在组合层互锁：充电期间拒绝其它业务及手动/自动
  击发，发射脉冲或自动瞄准期间拒绝开始充电。未增加JX1命令、定时器、DMA、IRQ或自动充电逻辑。
- 涉及文件：`Application/ui/app_command_bus.h`、`Application/ui/target_ui.*`、
  `Src/lcd_demo_app.c`、`tools/generate_ui_font.py`、`Application/ui/ui_font.c`、
  `tools/render_ui_preview.py`、UI预览图片、README、架构和硬件映射文档。
- 验证：Debug/Release均无警告链接成功，Flash分别为349,360/344,116 B，RAM分别为
  18,472/18,464 B；ELF SHA-256分别为
  `2110439C9454EB931DB265105AF64BF8247D7EA3AE147EAB4A08ACCFA1255E42`和
  `95999A0F596654C5C78347F0EC187A8FEEF5835F9F0C5F6B502AD2ADDFB27D6E`。机器码复核确认
  `bsp_cannon_relay_set_charge()`使用PB1掩码和低有效配置，开始/断开分别传入true/false；中文
  字体覆盖检查、主界面与充电页预览目视检查及`git diff --check`均通过。
- 已知限制：本版本尚未烧录，也未在实物上触发PB1；没有充满反馈、电压/电流保护或自动截止，
  不能由软件判断电容已经充满。持续充电必须依赖人工断开或外部硬件保护。
- 待确认事项：断开充电功率回路后烧录，先用示波器确认PB1空闲约3.3 V、开始充电后持续约0 V，
  人工断开、返回、STOP和DISABLE均恢复高，并确认任何时刻PB0/PB1不会同时为低，再连接负载。

## 2026-07-23 PB1充电控制页Debug固件烧录

- 修改原因：用户明确要求烧录包含PB1手动充电页和PB0/PB1互锁的最新固件。
- 最终决策：使用CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，以SWD 1 MHz
  写入当前Debug ELF；未发送发射、充电、角度、电流或其它业务命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `2110439C9454EB931DB265105AF64BF8247D7EA3AE147EAB4A08ACCFA1255E42`。
- 验证：烧录前构建为`ninja: no work to do`且`git diff --check`通过；OpenOCD识别探针FW 1.6.0、
  Cortex-M4、DBGMCU ID=`0x101F6413`和Flash=512 KiB，日志包含`Programming Finished`与
  `Verified OK`。复位运行后再次独立校验349,360字节成功；只读RCC/GPIOB寄存器为
  AHB1ENR=`0x0010001B`、MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x00500105`、
  IDR=`0x0000FFFB`、ODR=`0x00000003`，确认PB0/PB1均为推挽输出、上拉且空闲高。
- 已知限制：本轮只验证Flash一致性和空闲GPIO状态，没有触发PB0/PB1，因此充电页实屏操作、PB1
  持续低电平、断开路径、光耦输入和继电器触点仍未实测；RESET阶段GPIO仍会短暂回到高阻输入。
- 待确认事项：保持功率负载断开，用示波器确认开始充电后PB1持续为低，人工断开、返回、STOP和
  DISABLE均恢复高，并确认PB0/PB1互锁后再连接负载。

## 2026-07-23 自动搜索改为低速首遍记录、第二遍回扫并提高yaw Kp

- 修改原因：用户明确要求自动搜索先缓慢扫动并记录目标位置，继续扫到端点后第二遍反向回来，
  再微调停止；同时要求yaw的Kp小幅提高。
- 最终决策：不改MaixCAM2识别和`VIS`协议，只修改STM32自动状态机。yaw目标按既有
  `YAW_SCAN_TARGET_SPEED_DPS=8°/s`斜坡移动，不再直接跳到±30°端点；第一遍首次收到新鲜
  `X_VALID`时只记录实际yaw并继续到当前端点，随后反向回扫，沿回扫方向依次到达粗锁点两侧
  约2°并采集两组新鲜`X_FINAL`，再沿用比例标定、像素闭环、36 cm固定靶pitch、稳定门控和
  PB0单次500 ms低脉冲。回扫尚未到达标定点时不执行500 ms丢靶重启，到点后重新开始丢靶计时。
  yaw位置Kp由0.028提高为0.032 A/°，Ki、Kd、100 Hz周期、0.70 A限流和所有保护边界不变。
- 涉及文件：`Src/lcd_demo_app.c`、`Inc/lcd_demo_config.h`、README、架构、硬件映射和本过程记录；
  MaixCAM2源代码未修改。
- 验证：Debug/Release完整链接成功且无警告，Flash分别为350,040/344,672 B，RAM分别为
  18,488/18,480 B；ELF SHA-256分别为
  `809042FC7B9AEE60B80C77D0FFF2C2EAC51085BAB3A562F4A8DE5F7AF188C996`和
  `C35979F99529B36D31015598F5162CE75E28866D449E5DC76F7B4ECE639CAA83`。边界静态检查覆盖粗锁点
  -30°、-29°、-10°、0°、10°、29°、30°和两个扫描方向，确认A/B标定点均在±30°内、总基线
  不小于2°且顺着端点返回方向依次经过；60°全幅目标斜坡为7.5 s。`git diff --check`通过。
- 已知限制：本版本尚未烧录，也未在真实机构上确认8°/s目标斜坡的实际跟随速度、第二遍视觉
  重捕获或Kp提高后的过冲。Kp提高会将8°限幅误差下的纯比例分量从约0.217 A提高到约0.248 A，
  仍受0.70 A总限流保护，但不能以编译结果代替机械稳定性验证。
- 待确认事项：固定机构并断开发射/充电功率回路后烧录，从小角度启动自动搜索，确认第一遍见靶
  不停车、到端点才反向、第二遍在记录位置附近减速校准且无明显振荡；随后再验证稳定和PB0脉冲。

## 2026-07-23 自动任务累计充电90秒后击发

- 修改原因：用户确认继电器为高电平导通、低电平关断，并要求按任务启动键后自动充电90 s再
  发射；充电可随时暂停，充电UI不改。
- 最终决策：线上`START`是完整任务启动入口：立即开始新一轮充电并重新Enable/Home，双轴取得
  本次新反馈和零点后自动进入两遍搜索。直接点击“自动搜索”要求双轴已就绪，也会新开充电和搜索；
  TFT电机页`ENABLE / HOME`改用本地`MOTOR_ENABLE`，只操作电机。此前“只有自动搜索才是任务启动”
  的中间方向已被覆盖。上电PB0/PB1均低；任务接受后PB1高、PB0低并清零累计时间。主循环非阻塞
  累计90 s，暂停时PB1低且不累计，恢复时从原累计值继续。累计完成先写PB1低，随后仅在既有
  视觉/TOF/双轴稳定门控满足时让PB0高500 ms。手动发射在本轮充电完成前被拒绝；自动任务截止
  时间为120 s，暂停时间不计入。充电页布局、文字、按钮和预览未改。
- 涉及文件：`Application/ui/app_command_bus.h`、`Application/ui/target_ui.c`、
  `Inc/lcd_demo_config.h`、`bsp/bsp_cannon_relay.c`、`Src/lcd_demo_app.c`、README、
  `docs/architecture.md`、`docs/hardware_mapping.md`和本过程记录。
- 验证：Debug/Release从清理状态完整链接且无警告；Debug Flash=350,792 B、RAM=18,504 B，
  Release Flash=345,364 B、RAM=18,496 B。ELF SHA-256分别为
  `C4F2BAF51553A6187F2FEBB4661CCBD7FECEBD61E8FE6998FCB731835BF8F5A5`和
  `207EB1A03443D0839B780021D13B8ECED7AF16C46B3322823D92269BFF0C3563`。静态检查覆盖高有效极性、
  上电两路关闭、START/本地Enable分流、暂停累计不清零、90 s前禁止击发、充满先关PB1以及瞄准
  稳定门控；暂停分段累计模型通过，`git diff --check`通过。
- 已知限制：本版没有充满传感器、电压/电流保护或继电器触点反馈；90 s为软件累计时间。暂停时
  视觉搜索和对准仍继续，仅冻结充电累计及自动任务截止。PB1拉低与PB0拉高之间没有额外延时。
- 待确认事项：烧录记录见下一条，MaixCAM2脚本仍未上传。仍需断开功率回路后用示波器验证
  任务启动后PB1高、暂停期间PB1低且恢复后累计不清零、90 s到达时PB1先低再PB0高约500 ms。

## 2026-07-23 90秒自动充电版本Debug固件烧录

- 修改原因：用户明确要求烧录当前90 s自动充电、可暂停和高有效继电器版本。
- 最终决策：使用系统唯一枚举的COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号
  `2024-09-01`，以SWD写入当前Debug ELF并复位运行；未发送充电、发射、距离、角度或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，最终SHA-256=
  `C4F2BAF51553A6187F2FEBB4661CCBD7FECEBD61E8FE6998FCB731835BF8F5A5`。
- 验证：OpenOCD识别CMSIS-DAP FW 1.6.0、Cortex-M4、DBGMCU ID=`0x101F6413`和512 KiB Flash；
  写入日志包含`Programming Finished`与`Verified OK`。复位后再次独立校验350,792字节成功。
  运行2.5 s后只读GPIOB：MODER=`0x00AA0285`、OTYPER=`0x00000000`、PUPDR=`0x0050010A`、
  IDR=`0x0000FFD8`、ODR=`0x00000000`，确认PB0/PB1均为推挽下拉输出且当前输出低。
- 已知限制：只验证了Flash一致性和上电后PB0/PB1低电平；没有触发任务、充电暂停或发射，也没有
  验证光耦、继电器触点和功率负载。固件复位后仍沿用既有双轴自动Enable行为。
- 待确认事项：保持功率回路断开，按任务启动键并用示波器完成PB1高电平90 s、暂停/恢复累计、
  PB1先低后PB0高约500 ms的全时序验证，确认无同时高电平后再连接负载。

## 2026-07-23 三个定位入口启动充电并自动击发

- 修改原因：用户纠正电磁炮任务入口：距离定位、固定靶和自动搜索都应自动开始充电计时；发射页
  `BACK`应停止充电；累计充电90 s后自动发射。此前将线上`START`设为完整任务入口的方向被否决。
- 最终决策：`START`及TFT的`ENABLE / HOME`恢复为只操作双轴。距离定位或固定靶成功提交pitch、
  或自动搜索成功启动后，均清零累计并将PB1拉高。发射页和充电页`BACK`都发布
  `CHARGE_ABORT`，将PB1拉低但保留累计值；充电页再次开始时继续累计。累计满90 s后先将PB1拉低；
  距离定位/固定靶在双轴误差≤1°后自动输出一次PB0高脉冲500 ms，自动搜索仍需既有视觉、TOF、
  双轴稳定门控。PB0脉冲期间拒绝三个入口重新开启PB1；STOP、DISABLE或立即断开发射取消待击发
  状态。UI布局、文字和预览未改。
- 涉及文件：`Application/ui/app_command_bus.h`、`Application/ui/target_ui.c`、
  `Src/lcd_demo_app.c`、README、`docs/architecture.md`、`docs/hardware_mapping.md`和本过程记录；
  MaixCAM2代码与JX1线上枚举值未改。
- 验证：Debug/Release从清理状态完整链接且无警告；最终Debug Flash=350,788 B、RAM=18,504 B，
  Release Flash=345,384 B、RAM=18,496 B。ELF SHA-256分别为
  `8CA592A7E2F0610BD27754598F4DA8F5386F20B959287CA2FBA054F0387216D1`和
  `74EE2C6F67CD3E23A06BDF1765E093ABBEB09ED3ABD2C3522FDB52A0EBF1444C`。暂停分段模型确认
  12+18+25+35 s有效充电累计为90 s，47 s暂停不计入；`git diff --check`通过。
- 已知限制：距离定位和固定靶在90 s时若双轴尚未进入±1°，会等待到位后再发射，不会脱离角度
  门控强制击发。没有充满传感器、电压/电流保护或继电器触点反馈。
- 待确认事项：烧录记录见下一条；仍需分别验证三个入口、两个`BACK`、暂停恢复累计和
  PB1低→PB0高500 ms的完整时序。

## 2026-07-23 三个定位入口版本Debug固件烧录

- 修改原因：用户明确要求烧录当前由距离定位、固定靶和自动搜索启动充电及自动击发的版本。
- 最终决策：使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、序列号`2024-09-01`，通过SWD
  写入当前Debug ELF并复位运行；未发送距离、固定靶、自动搜索、充电、发射、角度或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `8CA592A7E2F0610BD27754598F4DA8F5386F20B959287CA2FBA054F0387216D1`。
- 验证：OpenOCD识别CMSIS-DAP FW 1.6.0、Cortex-M4、DBGMCU ID=`0x101F6413`和512 KiB Flash；
  写入日志包含`Programming Finished`与`Verified OK`，复位后独立校验350,788字节成功。运行2.5 s
  后只读GPIOB：MODER=`0x00AA0285`、OTYPER=`0x00000000`、PUPDR=`0x0050010A`、
  IDR=`0x0000FFD8`、ODR=`0x00000000`，确认PB0/PB1当前均输出低。
- 已知限制：已验证Flash一致性和上电空闲电平，没有实际触发三个入口、BACK暂停、90 s累计或
  PB0脉冲，也未验证光耦、继电器触点和功率负载。
- 待确认事项：保持功率回路断开，分别操作三个入口和两个`BACK`，用示波器验证暂停恢复累计以及
  PB1累计高90 s后先低、PB0再高约500 ms；确认互锁后再连接负载。

## 2026-07-23 90秒截止立即击发、发射页计时与Debug固件烧录

- 修改原因：用户反馈上一版充电停止后没有发射，并最终确认距离定位、固定靶和自动搜索都应在
  有效充电累计满90 s时自动发射，不再等待角度到位或视觉、TOF、双轴稳定门控；发射页还需同时
  显示充电计时和发射计时。
- 最终决策：三个任务入口成功后都跳转发射页、清零累计并拉高PB1。发射页显示
  `CHG mm:ss/01:30`和PB0脉冲的0.1 s倒计时；`BACK`拉低PB1并暂停累计。累计满90 s时先拉低
  PB1，再由三个任务共用的待击发步骤立即拉高PB0 500 ms，不检查角度、视觉、TOF或稳定状态。
  STOP、DISABLE、故障和立即断开发射仍取消待击发；人工发射成功时同步清除自动待击发标志，
  避免同周期产生第二个脉冲。此前“90 s后继续等待瞄准门控”的方案已被本次决定覆盖。
- 涉及文件：`Src/lcd_demo_app.c`、`Application/ui/target_ui.*`、`tools/render_ui_preview.py`、
  `docs/ui_cannon_preview.png`、README、`docs/architecture.md`、`docs/hardware_mapping.md`和本记录。
- 验证：Debug/Release均链接成功且无警告；Debug Flash=351,356 B、RAM=18,512 B，Release
  Flash=346,332 B、RAM=18,504 B。ELF SHA-256分别为
  `88533A5DDEF17C82D65F19868651D04BF150CF11AEF73E4E5FB54A3227474252`和
  `8836D0EC56DAEA07EEB946EF7A62A4E4AA2AC8DFF42167FA3803C0B24417AEA6`；`git diff --check`通过。
  使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、探针FW 1.6.0和序列号`2024-09-01`写入Debug
  ELF，OpenOCD识别Cortex-M4、DBGMCU ID=`0x101F6413`、Flash=512 KiB，日志包含
  `Programming Finished`与`Verified OK`。复位后独立校验351,356字节成功；运行2.5 s后只读
  GPIOB得到MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x0050010A`、IDR=`0x0000FFD8`、
  ODR=`0x00000000`，确认PB0/PB1当前均输出低。
- 已知限制：已验证构建、Flash一致性和上电空闲电平，尚未实际等待90 s或测量PB1低到PB0高的
  时序，也未验证光耦、继电器触点和功率负载；90 s仍是软件累计时间，没有充满、电压或电流反馈。
- 待确认事项：保持功率回路断开，分别启动三个任务并测试`BACK`暂停/恢复，用逻辑分析仪确认
  每次有效累计满90 s时均先出现PB1下降沿，再出现且只出现一次约500 ms PB0高脉冲，确认互锁后
  再连接功率负载。

## 2026-07-23 固定靶与自动搜索视觉距离增加12 cm并烧录

- 修改原因：用户要求固定靶和自动搜索在`DIST`原始测距基础上增加12 cm，并将校正值同时用于
  弹道计算和距离显示；要求修改后直接烧录。
- 最终决策：JX1 `VIS`协议和MaixCAM2发送值保持原始毫米单位不变。STM32应用组合层只对带有
  `DISTANCE_VALID`的视觉距离增加`120 mm`，并保存为本机最新视觉距离；STM32主界面`DIST`、
  固定靶和自动搜索统一消费该校正值。最终有效范围仍为50～100 cm，因此对应原始TOF范围变为
  380～880 mm；手工输入的距离定位继续直接使用输入值，不增加偏置。加法前检查16位上界，不能
  安全校正的帧会清除本机距离有效标志。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、README、
  `docs/architecture.md`、`docs/hardware_mapping.md`和本记录；MaixCAM2代码与线上协议未修改。
- 验证：Debug/Release均完整链接成功且无警告；Debug Flash=351,396 B、RAM=18,512 B，Release
  Flash=346,384 B、RAM=18,504 B。ELF SHA-256分别为
  `A3A8815B2E4F3426FE8199E8DDA18CE1E869FB0EFA3F9B1B88733346D64585CC`和
  `F7A92445D38F1A839AC07CAC53535075C84498D3C58F9D9FBBCBBDDCED33E0B9`；静态边界复核确认
  380/880 mm分别映射为500/1000 mm，手工距离命令仍直接进入原计算函数，`git diff --check`通过。
  使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号`2024-09-01`烧录最终Debug ELF；
  OpenOCD识别Cortex-M4、DBGMCU ID=`0x101F6413`和512 KiB Flash，日志包含
  `Programming Finished`与`Verified OK`。复位后独立校验351,396字节成功；运行2.5 s后只读
  GPIOB得到MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x0050010A`、ODR=`0x00000000`，确认
  PB0/PB1当前均输出低。
- 已知限制：尚未向已烧录固件发送实测`VIS`帧，也未目视确认TFT校正值或观察固定靶/自动搜索的
  pitch目标；构建、烧录和静态边界检查不能替代TOF安装偏置的现场测量。
- 待确认事项：用已知距离目标核对原始TOF与STM32 `DIST`应相差12.0 cm，并分别进入固定靶和
  自动搜索确认二者使用相同校正距离；首次动作继续保持机构固定和功率回路断开。

## 2026-07-23 固定靶与自动搜索pitch角取反并烧录

- 修改原因：用户要求将上一版固定靶和自动搜索计算出的pitch角取反，并随后明确要求烧录。
- 最终决策：只修改固定靶共用弹道函数：仍按离地36 cm靶心求低弹道，但在提交pitch控制器前
  将结果整体乘以`-1`。固定靶按钮与自动搜索均复用该函数，因此同时生效；手工距离定位原有地面
  弹道及取反逻辑不变。此前“固定靶低弹道直接提交、不取反”的方向已被本次决定覆盖。
- 涉及文件：`Application/control/cannon_aim_model.c`、README、`docs/architecture.md`、
  `docs/hardware_mapping.md`和本记录。
- 验证：校正距离50/67.5/75/100 cm的固定靶pitch分别为
  `-33.86057/-30.82084/-30.33490/-30.85221°`。Debug/Release均链接成功且无警告；Debug
  Flash=351,428 B、RAM=18,512 B，Release Flash=346,472 B、RAM=18,504 B；ELF SHA-256分别为
  `1A6880C112FB7C5490F91BB0443EFAD90254E9706A9475F0135261899DDEB13E`和
  `0BB67B64064BE21A63AEDA81894F92F3563CED49611B380FA7FACC1835C36798`，`git diff --check`通过。
  使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号`2024-09-01`烧录Debug ELF；
  OpenOCD识别Cortex-M4、DBGMCU ID=`0x101F6413`和512 KiB Flash，日志包含
  `Programming Finished`与`Verified OK`。复位后独立校验351,428字节成功；运行2.5 s后只读
  GPIOB得到MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x0050010A`、ODR=`0x00000000`，确认
  PB0/PB1当前均输出低。
- 已知限制：尚未在机构上触发固定靶或自动搜索并观察pitch实际运动方向，也未验证取反后的36 cm
  落点；数值、构建与Flash一致性不能替代机械方向和弹道实测。
- 待确认事项：固定机构并断开充电/发射功率回路，用接近67.5 cm的目标先确认pitch向负角方向
  运动且稳定，再逐步测试50～100 cm和实际36 cm落点。

## 2026-07-23 距离定位改读视觉DIST并瞄准36 cm高点

- 修改原因：用户发现视觉`DIST=86 cm`时距离定位几乎没有改变pitch；源码确认旧逻辑读取左侧手工
  “设定”值而非视觉`DIST`。用户最终要求距离定位和自动搜索都读取`DIST`并射向高处。
- 最终决策：`FIRE_DISTANCE`与`FIXED_TARGET`在应用组合层合并为同一定位路径：读取500 ms内
  最新有效视觉距离，经既有`+120 mm`校正后检查50～100 cm范围，再按离地36 cm靶心求低弹道并
  将pitch角整体取反。自动搜索继续在对准阶段调用同一个36 cm弹道函数。视觉距离无效、断流或
  越界时保持原pitch目标，不启动充电和待击发。手工距离输入、UI与`SET_DISTANCE`保留兼容，但
  三种任务均不消费该值；JX1仍保留`FIRE_DISTANCE`枚举名，其`value`不再决定距离，因此删除该
  命令的旧手工距离范围校验。此前“距离定位读取手工值并瞄准地面0 cm”的方向已被本次决定覆盖。
- 涉及文件：`Src/lcd_demo_app.c`、README、`docs/architecture.md`、
  `docs/hardware_mapping.md`和本记录；JX1帧格式、枚举数值、VIS协议与MaixCAM2代码未修改。
- 验证：源码静态链路确认`FIRE_DISTANCE`和`FIXED_TARGET`共用`s_latest_vision.distance_mm`、
  50～100 cm校验与`request_fixed_target_aim_cm()`；自动搜索继续调用该函数。校正距离
  50/67.5/75/100 cm的pitch保持`-33.86057/-30.82084/-30.33490/-30.85221°`。Debug/Release
  均链接成功且无警告；Debug Flash=351,172 B、RAM=18,512 B，Release Flash=346,120 B、
  RAM=18,504 B；ELF SHA-256分别为
  `EFBEDEE56DB6BCE496185006321BA0F4F1F9BCDAAB6ACC937A32DCD68828B00A`和
  `E11B5DD18A2DBF4DC5DEC13D3FE0CAE99AE470ABC6F8AD550B98F8E1B8EAF005`，`git diff --check`通过。
- 已知限制：本版本尚未烧录；板上仍是上一版“固定靶/自动搜索取反，但距离定位读取手工设定值”
  的Debug固件。尚未用真实VIS帧验证距离定位的校正值、pitch运动或36 cm落点。
- 待确认事项：用户明确要求烧录后，再保持机构固定且断开充电/发射功率回路，确认主界面`DIST`
  有效时点击距离定位会把pitch目标更新到约-34°～-30°；`DIST -- cm`时不得启动充电。

## 2026-07-23 视觉DIST高点距离定位版本Debug固件烧录

- 修改原因：用户明确要求烧录上一条“距离定位、固定靶和自动搜索均读取校正DIST并瞄准离地
  36 cm高点”的最新固件。
- 最终决策：使用COM3/CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`，以SWD写入当前Debug ELF并复位运行；未主动发送定位、角度、充电或发射命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `EFBEDEE56DB6BCE496185006321BA0F4F1F9BCDAAB6ACC937A32DCD68828B00A`。
- 验证：烧录前Debug构建为`ninja: no work to do`且`git diff --check`通过；OpenOCD识别
  Cortex-M4、DBGMCU ID=`0x101F6413`和512 KiB Flash，日志包含`Programming Finished`与
  `Verified OK`。复位后独立校验351,172字节成功；运行2.5 s后只读GPIOB得到
  MODER=`0x00AA0285`、OTYPER=`0`、PUPDR=`0x0050010A`、ODR=`0x00000000`，确认PB0/PB1
  当前均输出低。
- 已知限制：已验证Flash一致性和上电空闲电平，尚未实际输入VIS距离、点击距离定位、观察pitch
  运动或验证36 cm落点，也未触发90 s充电和PB0脉冲。
- 待确认事项：保持机构固定且断开充电/发射功率回路，确认`DIST`有效时距离定位目标约为
  -34°～-30°；`DIST -- cm`时不得启动任务。确认方向和稳定性后再进行落点测试。

## 2026-07-24 TFT手动充电、Pitch与发射临时测试模式

- 修改原因：用户要求暂时隐藏原UI，只保留充电、Pitch角度设置、发射和计时；发射不再由计时
  触发，充电和发射改为人工控制。用户随后明确要求不删除原逻辑，只做简单测试。
- 最终决策：新增编译开关`CANNON_MANUAL_TEST_MODE`并默认置`1U`。测试模式上电直接进入精简页，
  只显示Pitch实际/目标角、有效充电累计、PB0脉冲倒计时、充电启停和500 ms手动发射/提前停止。
  PB1由人工开始/停止，累计90 s不自动关断也不触发PB0；PB0可不经充电计时直接人工触发，若PB1
  正在导通则先关PB1再开PB0，继续保持两路继电器互锁。三个原任务入口在测试模式下不再自动开始
  充电，两个原自动击发点均被模式分支禁止。完整UI、90 s充电截止和自动击发实现保留在`0U`分支。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/ui/target_ui.c`、`Src/lcd_demo_app.c`、
  `Application/ui/ui_font.c`、`tools/generate_ui_font.py`、README和本记录；公共命令枚举、队列、无线
  协议、引脚、极性、控制周期和发射脉宽未改。
- 验证：当前`1U`模式Debug/Release完整链接成功且无警告，Flash分别为350,968/345,928 B，RAM
  分别为18,512/18,496 B；额外以`CANNON_MANUAL_TEST_MODE=0U`独立配置并完整链接Debug成功且无
  警告，Flash=351,328 B、RAM=18,512 B，确认原模式仍可恢复。字体生成脚本通过Python语法检查，
  `git diff --check`通过。
- 已知限制：尚未烧录或触发真实PB0/PB1；软件构建不能证明继电器、电源和发射机构安全。计时只
  累计PB1实际接通时长，停止充电或手动发射期间暂停，显示上限为99:59。手动发射仍是既有500 ms
  有界脉冲，不是按住持续导通；PB0脉冲期间开始充电会返回忙。
- 待确认事项：断开充电与发射功率回路后烧录，依次确认Pitch设置、PB1人工启停和暂停累计、未满
  90 s也能手动触发、充电中点击发射时PB1先低且PB0仅高约500 ms。测试结束后将
  `CANNON_MANUAL_TEST_MODE`改回`0U`并重新构建，恢复原完整UI和自动逻辑。

## 2026-07-24 TFT手动测试模式Debug固件烧录

- 修改原因：用户要求烧录当前手动测试模式，并特别强调必须识别F407目标，不能写错芯片。
- 最终决策：只使用COM3对应的CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`和SWD 1 MHz。写入前在同一OpenOCD会话强制检查`DBGMCU_IDCODE & 0xFFF ==
  0x413`，确认属于STM32F405/407/415/417系列且Flash容量为512 KiB后，才写入当前Debug ELF；
  未使用COM5探针，未发送充电、Pitch或发射命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `3CC2FDC6D566E0DEF3414AFBCE33DD243DCCA4913DBFB47A7AAA1311796B0141`，有效镜像350,968 B，
  `CANNON_MANUAL_TEST_MODE=1U`。
- 验证：烧录前Debug构建为`ninja: no work to do`；只读识别CPUID=`0x410FC241`、
  DBGMCU ID=`0x101F6413`、Flash=512 KiB。写入会话报告擦写393,216 B并校验350,968 B成功；
  随后独立重连执行`verify_image`再次校验350,968 B成功，两个写入/校验会话最终退出码均为0。
  复位运行2.5 s后GPIOB `MODER=0x00AA0285`、`OTYPER=0`、`PUPDR=0x0050010A`、
  `ODR=0x00000000`，确认PB0/PB1当前均输出低，读取后恢复CPU运行。
- 已知限制：仅验证目标身份、Flash一致性和上电空闲电平；未操作精简TFT页面，也未验证Pitch动作、
  PB1人工启停、累计计时或PB0的500 ms实际脉冲，不能替代断开功率回路后的示波器测试。
- 待确认事项：保持功率回路断开，按精简页依次验证Pitch设置、PB1开始/停止与暂停累计、未满90 s
  手动发射，以及充电中发射时PB1先低、PB0只高约500 ms。

## 2026-07-24 TFT手动测试模式Debug固件烧录

- 修改原因：用户要求烧录当前手动测试模式，并特别要求确认目标为STM32F407。
- 最终决策：严格使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`，仅向只读识别为CPUID=`0x410FC241`、DBGMCU IDCODE=`0x101F6413`、Flash
  512 KiB的F407目标写入当前Debug ELF；未发送Pitch、充电或发射命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `3CC2FDC6D566E0DEF3414AFBCE33DD243DCCA4913DBFB47A7AAA1311796B0141`，有效烧录段
  350,968 B，`CANNON_MANUAL_TEST_MODE=1U`。
- 验证：烧录前构建为`ninja: no work to do`。首次以SWD 1 MHz擦除时CMSIS-DAP发生命令错位，
  OpenOCD在写入前报告失败；复查确认应用区已擦空。随后在同一`0x413`目标上将SWD降至500 kHz，
  完成`Programming Finished`、`Verified OK`和复位。复位运行2.5 s后独立`verify_image`核对
  350,968字节成功，CPUID/DBGMCU ID再次为`0x410FC241/0x101F6413`，GPIOB ODR=`0x00000000`，
  确认当前PB0/PB1均为低。
- 已知限制：只验证了目标身份、Flash一致性、程序能够运行至应用区及PB0/PB1当前空闲电平；尚未
  触摸操作Pitch、充电或发射，也未验证继电器触点、负载和500 ms实际脉宽。
- 待确认事项：断开功率负载，先目视确认精简TFT页，再用示波器依次验证PB1人工启停、暂停累计、
  未满90 s手动击发和充电中击发时PB1先低、PB0仅高约500 ms，确认后再连接负载。

## 2026-07-24 自动扫描YAW驱动力与弹道标定宏整理

- 修改原因：用户要求阅读当前自动扫描逻辑，提高YAW力度但不引入震荡或加快扫描，并把水平射击
  标定参数放到文件开头，便于直接修改宏。
- 最终决策：保留自动状态机按`YAW_SCAN_TARGET_SPEED_DPS=8°/s`推进目标、100 Hz位置PID和
  0.70 A总限流，不增加速度环、前馈、滤波、目标加速度或额外状态。YAW `Kp`由0.080提高到
  0.090 A/°，8°误差经0.25°死区后的最大比例项由0.620提高到0.6975 A；同时将原始速度阻尼
  `Kd`由0.004提高到0.005 A/rpm以抑制过冲，`Ki=0.002 A/(°·s)`及所有保护边界不变。将炮口
  高度12.0 cm、0°水平落地点67.5 cm、低弹道切换阈值5.0°和固定靶高度36.0 cm四个宏集中到
  `Inc/lcd_demo_config.h`开头；名称和值及弹道算法均未改变。
- 涉及文件：`Inc/lcd_demo_config.h`、README和本记录；自动扫描状态机、控制器代码、公共API、
  CAN协议、控制周期、电流限幅和手动测试模式开关均未修改。
- 验证：当前`CANNON_MANUAL_TEST_MODE=1U`的Debug/Release完整链接成功且无警告，Flash分别为
  350,968/345,928 B，RAM分别为18,512/18,496 B；额外以测试开关`0U`完整链接原UI Debug成功，
  Flash=351,328 B、RAM=18,512 B。三者SHA-256分别为
  `E82D8861F3A31532994260B66C8845ED91EBEB1B1D0F90442BD3CC20B20550AC`、
  `C64BCBBDFE329F8E85213DB5787A9BE2B77497B0FB8C9A2D1C065AB3D3705DD9`和
  `958EF90FA477E5D81EB9648DE0F71073D64D301DEAFBA87138378DB05AA214D5`；`git diff --check`通过。
- 已知限制：增益修改尚未烧录或进行机构扫描，构建与静态电流边界不能保证实机绝对无震荡。
  增大驱动力会提高实际机械加速度，但命令目标速度仍严格为8°/s；当前板上仍是上一版0.080/0.004
  固件。临时手动测试模式仍默认开启，精简TFT不显示自动搜索入口。
- 待确认事项：用户明确要求烧录后，先固定机构并断开发射/充电功率回路，从小角度自动搜索开始，
  观察实际角、原始转速和命令电流；若仍有过冲或振动，优先小步增加Kd或回退Kp，不提高8°/s速度
  和0.70 A限流。

## 2026-07-24 当前正式系统逻辑汇总文档

- 修改原因：用户要求将当前全部正式逻辑和公式汇总到一个Markdown，同时明确排除最新临时手动
  测试UI。
- 最终决策：新增`CURRENT_SYSTEM_LOGIC.md`，以当前STM32F407VET6和MaixCAM2源码为准，按
  `CANNON_MANUAL_TEST_MODE=0U`记录完整TFT、充电和发射基线；只说明当前默认测试开关与已烧录
  镜像可能不同，不展开临时精简UI。文档区分Maix当前`main.py`视觉/TOF/TCP入口和未运行的
  `gimbal_app.py`控制台，并如实记录当前统一90 s充电完成路径先于自动搜索稳定门控执行，因此
  三个正式任务最终都会在充电完成时直接输出500 ms发射脉冲。
- 涉及文件：新增`CURRENT_SYSTEM_LOGIC.md`并更新本记录；业务源码、配置、构建选项和板上固件均
  未修改。
- 验证：逐项回查时钟、引脚、QD4310换算、双轴PID、弹道、任务状态机、JX1/ESP、正式UI、
  Maix箱面识别、TOF和TCP源码；关键常量自动核对无缺失，20个源码索引路径均存在，Markdown共有
  26个成对代码围栏且无行尾空白，`git diff --check -- process.md`通过。此次仅修改文档，未重新构建
  或烧录。
- 已知限制：文档描述的是关闭临时测试开关后的正式源码基线，不等同于当前默认`1U`配置或板上
  临时测试镜像；视觉、机械方向、继电器时序和实际弹道均未因本次文档整理获得新的硬件验证。
- 待确认事项：恢复正式模式时将`CANNON_MANUAL_TEST_MODE`改为`0U`，重新构建并在功率回路断开
  条件下验证；若正式发射必须受视觉和双轴稳定门控，应另行调整统一自动击发路径，不能仅依赖
  现有`AUTO_AIM_SETTLE`分支。

## 2026-07-24 恢复完整TFT UI并改为累计充电30秒击发

- 修改原因：用户结束临时手动测试，要求恢复原本完整UI和正式逻辑，并将计时自动发射改为30秒。
- 最终决策：将`CANNON_MANUAL_TEST_MODE`默认值从`1U`恢复为`0U`，重新启用完整首页、Motor、充电、
  发射、Theme、距离和角度输入页面及正式任务状态机；将`CANNON_CHARGE_DURATION_MS`由90000 ms改为
  30000 ms。保留既有软件累计语义、暂停/恢复、PB1先关闭再触发PB0、PB0高500 ms、PB0/PB1互锁
  和统一计时截止立即击发行为，未增加视觉或双轴稳定门控。
- 涉及文件：`Inc/lcd_demo_config.h`、`CURRENT_SYSTEM_LOGIC.md`和本记录；未修改公共API、JX1协议、
  引脚、继电器极性、控制周期、状态机代码或弹道公式。
- 验证：面向STM32F407VET6的Debug/Release均以`STM32F407xx`、Cortex-M4F和
  `STM32F407VETX_FLASH.ld`从头完整构建成功且无警告。Debug Flash/RAM为351,328/18,512 B，
  Release为346,320/18,504 B；配置静态约束`120000 > 30000 + 300 + 500`成立，正式逻辑汇总中的
  现行90秒参数已同步为30秒。
- 已知限制：本次只完成源码、文档和主机侧构建验证，未烧录，也未在硬件上操作完整UI、等待30秒、
  测量PB1/PB0时序或验证继电器与功率负载。30秒仍是软件累计时间，不代表电容已实际充满。
- 待确认事项：如需烧录，应先只读确认目标DBGMCU ID末12位为`0x413`且Flash为512 KiB，再向
  STM32F407VET6写入本次Debug镜像；保持功率回路断开，验证累计30秒时PB1先下降且PB0只高约500 ms。

## 2026-07-24 完整UI与30秒自动击发Debug固件烧录

- 修改原因：用户在恢复正式UI并完成构建后明确要求直接烧录，同时要求目标必须识别为F407。
- 最终决策：只使用COM3对应CMSIS-DAP HID `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`，在写入会话中强制检查CPUID=`0x410FC241`、`DBGMCU_IDCODE & 0xFFF == 0x413`
  和Flash容量512 KiB后，向STM32F407VET6写入本次Debug ELF；未发送Pitch、充电、发射或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `111603127F35BF39105709805AEE777015CDEE6F09231A9984386C6AF36D36DE`，有效镜像351,328 B，
  `CANNON_MANUAL_TEST_MODE=0U`、`CANNON_CHARGE_DURATION_MS=30000U`。
- 验证：探测和写入均确认Cortex-M4F、DBGMCU ID=`0x101F6413`和Flash=512 KiB。首次以开发版
  OpenOCD、SWD 500 kHz写入时发生CMSIS-DAP命令错位；第二次改用稳定版、100 kHz后又因探针
  检测到外部复位而中断，以上两次均未作为成功。最终显式固定HID后端，使用SWD 100 kHz并在同一
  会话校验DEV_ID和Flash容量后，分步完成整片擦除与ELF写入。独立重连`verify_image`确认351,328
  字节完全一致；复位运行2.5 s后PC=`0x08005858`，GPIOB `MODER=0x00AA0285`、`ODR=0`，确认
  PB0/PB1均为输出低，随后恢复运行。
- 已知限制：已确认目标身份、Flash一致性、程序可运行和继电器上电空闲电平；尚未触摸操作完整UI、
  实际累计30秒、测量PB1下降到PB0上升的时序、PB0的500 ms脉宽或验证继电器触点与功率负载。
- 待确认事项：保持功率回路断开，通过完整UI启动正式任务并用示波器确认PB1累计高30秒后先下降，
  PB0随后只高约500 ms；确认软件互锁和外部电气保护后再连接功率负载。

## 2026-07-24 Yaw位置环Kp回退

- 修改原因：用户实机反馈当前Yaw `Kp=0.090 A/°`过大。
- 最终决策：只将`YAW_SCAN_KP_A_PER_DEG`回退为`0.080 A/°`。保留`Ki=0.002 A/(°·s)`、
  `Kd=0.005 A/rpm`、0.25°死区、8°误差限幅、0.010 A积分限幅、0.70 A总限流、100 Hz周期和
  所有状态机。8°限幅误差扣除死区后的最大纯比例项由0.6975 A降为0.620 A；此前提高到0.090的
  方向已被本次实机反馈否决。
- 涉及文件：`Inc/lcd_demo_config.h`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录；未修改
  Yaw控制器代码、Pitch参数、CAN协议、任务逻辑、完整UI或30秒自动击发逻辑。
- 验证：面向STM32F407VET6的Debug/Release均重新完整链接成功且无警告，Flash分别为
  351,328/346,320 B，RAM分别为18,512/18,504 B。
- 已知限制：本次仅完成源码和构建验证，尚未烧录；板上仍运行上一版`Kp=0.090 A/°`固件，构建
  通过不能证明`Kp=0.080`在实际机构上已经消除过冲或振动。
- 待确认事项：用户明确要求烧录后，再以小角度目标阶跃观察实际角、转速和命令电流；若仍偏强，
  应根据波形继续小步降低Kp，而不是同时改动Ki、Kd、速度或电流限幅。

## 2026-07-24 距离定位与高处靶弹道分流

- 修改原因：用户明确距离定位应根据人工设定距离调整Pitch并打地面靶子；固定靶和自动搜索打
  高处靶子。
- 最终决策：`APP_COMMAND_FIRE_DISTANCE`读取命令携带的当前设定距离50..100 cm，调用
  `cannon_aim_pitch_for_distance()`按目标高度0 cm计算普通弹道，不再要求或消费VIS距离。
  `APP_COMMAND_FIXED_TARGET`和自动搜索继续读取增加120 mm后的有效VIS/TOF距离，并调用
  `cannon_aim_pitch_for_fixed_target()`按当前`CANNON_FIXED_TARGET_HEIGHT_CM=36.0 cm`强制选择低弹道。
  三个任务成功后开始新的30秒累计充电并自动击发的正式流程不变。
- 涉及文件：`Src/lcd_demo_app.c`、`Application/protocol/gimbal_link_protocol.h`、`README.md`、
  `CURRENT_SYSTEM_LOGIC.md`和本记录；未修改弹道公式、视觉距离校正、自动搜索状态机、继电器时序
  或公共命令枚举和值。
- 验证：面向STM32F407VET6的Debug/Release均从头完整构建成功且无警告，Flash分别为
  351,584/346,588 B，RAM分别为18,512/18,504 B；静态回查确认距离定位只调用地面弹道入口，
  固定靶和自动搜索只调用36 cm高处靶入口。
- 已知限制：仅完成源码、文档和构建验证，尚未烧录或实测落点；板上仍是上一版
  `Kp=0.090 A/°`且距离定位使用VIS距离的固件。弹道参数仍依赖实际炮口高度和水平射程标定。
- 待确认事项：用户明确要求烧录后，先断开充电和发射功率回路，分别设置50/75/100 cm验证距离
  定位Pitch，再给固定靶和自动搜索输入有效VIS距离，确认两类任务得到不同的地面/36 cm目标角。

## 2026-07-24 Yaw Kp与地面/高处靶分流版本烧录

- 修改原因：用户要求完成Yaw Kp回退和距离定位弹道分流后直接烧录。
- 最终决策：只使用COM3对应CMSIS-DAP HID `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`，以SWD 100 kHz连接；写入前在同一会话强制检查
  `DBGMCU_IDCODE & 0xFFF == 0x413`和Flash容量512 KiB，只向确认的STM32F407VET6写入当前Debug
  ELF。未发送Pitch、Yaw、充电、发射或电流命令。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `52BAAF65F6A765B96C71F4C613F02CB09CC4F68F3559063F0D5AAF4BCE02362B`，有效镜像351,584 B；
  包含`YAW_SCAN_KP_A_PER_DEG=0.080f`、距离定位使用设定距离/地面0 cm弹道、固定靶和自动搜索使用
  校正VIS距离/36 cm高处靶弹道，以及完整UI和30秒自动击发。
- 验证：写入前及独立校验会话均识别CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`和Flash
  512 KiB。分步整片擦除、写入351,584字节并在写入会话校验成功；独立重连`verify_image`再次
  确认351,584字节完全一致。复位运行2.5 s后PC=`0x0800086E`，GPIOB `MODER=0x00AA0285`、
  `OTYPER=0`、`PUPDR=0x0050010A`、`ODR=0`，确认PB0/PB1当时均为输出低，随后恢复运行。
- 已知限制：已确认目标身份、Flash一致性、程序运行和继电器上电空闲电平；尚未操作完整UI、实测
  Yaw `Kp=0.080`响应、地面/36 cm两类Pitch目标、30秒累计或PB0的500 ms脉宽，也未验证实际落点、
  继电器触点和功率负载。
- 待确认事项：保持功率回路断开，先做小角度Yaw阶跃观察过冲；再分别设置50/75/100 cm验证地面
  靶Pitch，并以有效VIS距离验证固定靶和自动搜索的36 cm高处靶Pitch；最后用示波器确认PB1累计
  30秒后先下降、PB0只高约500 ms。

## 2026-07-24 固定靶与自动搜索改为高弹道并烧录

- 修改原因：用户实测炮口低于36 cm高处靶时，原低弹道近似从下向上直射，弹丸被远处高物体
  拦截，要求固定靶和自动搜索重新计算为抛射轨迹。
- 最终决策：只修改固定靶共用弹道求解的选根方式。`cannon_aim_pitch_for_fixed_target()`继续使用
  校正VIS/TOF距离和`CANNON_FIXED_TARGET_HEIGHT_CM=36.0 cm`，但由强制低根改为强制高根；固定靶
  和自动搜索因共用该函数同步生效。距离定位仍使用人工设定距离和地面0 cm普通弹道，Yaw
  `Kp=0.080 A/°`、完整UI、30秒累计充电、PB0高500 ms及继电器互锁均未修改。
- 涉及文件：`Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和
  本记录；未修改公共API、命令枚举、状态机、视觉距离校正、目标高度、炮口高度或水平射程标定。
- 验证：36 cm高处靶在50/67.5/75/100 cm的高弹道Pitch分别为
  `-81.7804/-78.7523/-77.4098/-72.6435°`，均在内部±85°限位内，代回轨迹的目标高度均为
  36.00000 cm；理论最高点约98.47～104.98 cm。STM32F407VET6 Debug/Release全量构建成功且无
  警告，Flash分别为351,584/346,592 B，RAM分别为18,512/18,504 B。
- 烧录：Debug ELF SHA-256=`74182714E37281CE128A0950AB4720D7F5B2668516AE1468973570F0F498C68C`。
  使用COM3对应CMSIS-DAP HID、SWD 100 kHz；写入前和独立校验均确认CPUID=`0x410FC241`、
  DBGMCU ID=`0x101F6413`及Flash=512 KiB。整片擦除、写入和会话内校验351,584字节成功，独立
  重连`verify_image`再次完全一致。复位运行2.5 s后PC=`0x08002D1C`、GPIOB
  `MODER=0x00AA0285`、`ODR=0`，确认PB0/PB1当时均为输出低，随后恢复运行。
- 已知限制：尚未实测Pitch机械行程、抛物线净空和36 cm真实落点。50 cm目标约-81.78°，距离
  ±85°内部限位仅约3.22°，必须防止机构碰撞。现有统一30秒击发路径仍不等待Pitch实际到位，
  大角度运动若30秒内未收敛仍会按时击发；本次未改变该既有行为。
- 待确认事项：断开充电和发射功率回路并固定机构，先从100 cm的约-72.64°目标开始，逐步验证
  75、67.5、50 cm的Pitch方向、行程和稳定性；确认无碰撞且30秒内到位后，再用无弹/低能方式
  验证轨迹最高点和遮挡净空，最后连接实际负载。

## 2026-07-24 高弹道回退为36.5厘米靶心低弹道并烧录

- 修改原因：用户实测强制高根虽然飞得高但落点太近；随后明确靶子位于36.5 cm高箱子上，要求
  根据炮口12 cm、0°平射落点67.5 cm重新计算，只比第一次36 cm公式略高并避免打到箱体。
- 最终决策：否决固定靶/自动搜索强制高根方案，恢复原低弹道根；将
  `CANNON_FIXED_TARGET_HEIGHT_CM`由36.0改为36.5 cm。中途考虑的固定额外+5°偏置未烧录并已从
  源码删除。距离定位继续使用人工设定距离和地面0 cm普通弹道；Yaw `Kp=0.080 A/°`、完整UI、
  30秒累计充电、PB0高500 ms和继电器互锁不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/cannon_aim_model.c`、`README.md`、
  `CURRENT_SYSTEM_LOGIC.md`和本记录；未修改公共API、任务状态机、视觉距离校正、炮口高度或平射
  距离标定。
- 验证：按当前平射模型，50/67.5/75/100 cm对应Pitch为
  `-34.33869/-31.21731/-30.70442/-31.16016°`，代回对应距离的理论高度均为36.50000 cm；相对
  原36.0 cm低弹道只增加约0.37～0.48°。STM32F407VET6 Debug/Release全量构建成功且无警告，
  Flash分别为351,584/346,588 B，RAM分别为18,512/18,504 B。
- 烧录：Debug ELF SHA-256=`D8D92095D64EFD6A380D692A6189A122B30D1FDE8CA3BAC5F2A9AA98F0E1398B`。
  使用COM3对应CMSIS-DAP HID、SWD 100 kHz；写入前和独立校验均确认CPUID=`0x410FC241`、
  DBGMCU ID=`0x101F6413`及Flash=512 KiB。首次整片擦除后因外部复位在写入前中断，未计为成功；
  重连确认同一F407后跳过重复擦除，写入并校验351,584字节成功。独立`verify_image`再次完全一致；
  复位运行2.5 s后PC=`0x08001210`、GPIOB `MODER=0x00AA0285`、`ODR=0`，确认PB0/PB1均低。
- 已知限制：模型只保证在TOF靶距位置达到36.5 cm，不含空气阻力、弹丸差异和安全净空余量；如果
  靶心位于箱体后方，箱体前沿比靶心更靠近炮口，仅知道箱高仍不能证明弹道不会碰前沿，还需要
  箱体深度或前沿到靶心的水平距离。现有30秒截止仍不等待Pitch实际到位。
- 待确认事项：先断开功率回路验证50/67.5/75/100 cm目标角；低能实测是否擦碰箱体。若仍碰前沿，
  测量箱体前沿到靶心的水平距离，再按前沿36.5 cm加实际安全余量求最小可行弹道，不再盲加角度。

## 2026-07-24 箱体前沿25厘米映射、Pitch净空约束与烧录

- 修改原因：用户确认原视觉距离增加12 cm代表箱体前沿到靶心的水平偏移，
  实际应改为25 cm；靶心和箱体前沿均离地36.5 cm，不计弹丸直径，并要求Pitch按
  物理关系稍微抬高，不统一叠加固定角度。原12 cm和新25 cm是替换关系，不叠加为37 cm。
- 最终决策：VIS/TOF保存原始箱体前沿距离 $d_f=50..100$ cm，TFT `DIST`显示
  $d_t=d_f+25$ cm的靶心距离。`cannon_aim_pitch_for_fixed_target()`分别求出在 $d_t$处命中
  36.5 cm靶心的低弹道角和在 $d_f$处至少到36.5 cm的前沿净空低弹道角，选两者
  中较高者后整体取反作为Pitch目标。固定靶和自动搜索同步生效；距离定位仍使用
  人工设定50..100 cm和地面0 cm弹道。Yaw `Kp=0.080 A/°`、完整UI、30秒累计充电自动击发、
  PB0/PB1时序和所有任务状态机不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`Application/control/cannon_aim_model.c/.h`、
  `Src/lcd_demo_app.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录；未修改公共命令枚举、CAN/TCP协议、
  PID参数、继电器极性、控制周期或计时语义。
- 验证：箱体前沿距离50/67.5/75/100 cm对应最终Pitch为
  `-34.3387/-31.2173/-31.1602/-34.0348°`；理论前沿弹心高度为
  `36.500/36.500/37.119/41.188 cm`，靶面高度为`41.507/37.247/36.500/36.500 cm`。
  面向STM32F407VET6的Debug/Release全量构建成功且无警告，Flash分别为351,796/346,736 B，
  RAM分别为18,512/18,504 B；Debug ELF为ARMv7E-M、Thumb-2、VFPv4-D16和hard-float ABI。
- 烧录：Debug ELF SHA-256=`CF65A6A67838E57CCB3AA459AB1174CE2B4CE63C37AD546B101203F15DDDFDF0`，
  有效Flash镜像351,796 B。使用FW 1.6.0、序列号`2024-09-01`的CMSIS-DAP HID和SWD 100 kHz；
  写入前及独立校验会话均确认CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、
  `DEV_ID=0x413`和Flash=512 KiB。首次整片擦除后，从`0x08000000`启动的RAM写入算法超时，
  OpenOCD退出码1，未计为成功；重连确认同一F407且Flash首字为`0xFFFFFFFF`后，跳过
  重复擦除完成写入。独立重连`verify_image`退出码0；复位运行2.5 s后PC=`0x0800139C`，
  GPIOB `MODER=0x00AA0285`、`OTYPER=0`、`PUPDR=0x0050010A`、`ODR=0`，确认PB0/PB1均为输出低。
- 已知限制：固定初速下，50 cm和67.5 cm前沿距离无法同时满足“前沿不低于36.5 cm”和
  “25 cm后精确命中36.5 cm靶心”；当前映射优先前沿净空，因此理论落点分别高于靶心
  约5.01 cm和0.75 cm。模型不含空气阻力、实际弹丸差异或额外净空余量；现有30秒截止仍不等待
  Pitch实际到位。
- 待确认事项：保持功率回路断开，先分别向固定靶/自动搜索输入50/67.5/75/100 cm的箱体
  前沿TOF，确认Pitch目标和机械方向；再用低能方式验证前沿净空和真实落点，最后检查
  30秒充电、PB1先关和PB0高500 ms时序。

## 2026-07-24 固定靶与自动搜索切换25厘米映射高弹道并烧录

- 修改原因：用户实机反馈前一版“两个低弹道角取较高者”仍射得更低，明确要求采用高射轨道。
- 最终决策：固定靶和自动搜索继续使用`d_target=d_box-front+25 cm`和36.5 cm靶心，但
  `cannon_aim_pitch_for_fixed_target()`现在强制选择弹道方程高根。前一版低弹道净空取大方案
  被本次实机反馈否决。距离定位、Yaw PID、完整UI、30秒自动击发和继电器逻辑不变。
- 涉及文件：`Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录；
  未修改公共API、协议、任务状态机、距离偏移、靶心高度或弹道标定。
- 验证：箱体前沿50/67.5/75/100 cm对应靶心75/92.5/100/125 cm，Pitch高根为
  `-77.3861/-74.0982/-72.6061/-67.0546°`；理论最高点为102.395/99.796/98.439/92.495 cm。
  STM32F407VET6 Debug/Release全量构建成功且无警告，Flash分别351,672/346,600 B，RAM分别18,512/18,504 B。
- 烧录：Debug ELF SHA-256=`8E61DA0762968A7D77B8625BC580F7EEC3422BE6380170A5EF764D161D83F91B`，
  有效Flash镜像351,672 B。CMSIS-DAP HID使用SWD 100 kHz；写入前和独立校验都确认
  CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、DEV_ID=`0x413`和Flash=512 KiB。首次擦除后的
  RAM写入算法在`0x08000000`超时并退出1，未计为成功；重连同一F407确认Flash首字为
  `0xFFFFFFFF`后跳过重复擦除完成写入。独立`verify_image`退出0；复位运行后
  GPIOB `MODER=0x00AA0285`、`ODR=0`，PB0/PB1均为输出低。
- 已知限制：高根理论最高点约92.5..102.4 cm，必须确认Pitch机械行程、上方净空和真实落点；
  模型不含空气阻力和弹丸差异，30秒截止仍不等待Pitch实际到位。
- 待确认事项：断开功率回路并固定机构，先从100 cm前沿距离对应的`-67.05°`验证机械方向和行程，
  再逐步测试75/67.5/50 cm，确认不碰机械限位后再做低能落点测试。

## 2026-07-24 自动搜索一次锁定、±50度与高弹道射程实测修正烧录

- 修改原因：自动搜索旧版在相对零点±30°内首遍见靶后仍继续到端点并二次回扫标定，现场时间
  不足；用户要求改为相对零点±50°且一次锁定。随后实测TFT `DIST=100 cm`时，高弹道高度足够，
  但弹丸只到达箱体前沿37 cm的位置；用户明确指出观测距离也要加25 cm靶心偏移，并要求快速
  计算后烧录。
- 最终决策：新增`AUTO_AIM_SINGLE_PASS_LOCK=1U`，保留旧`CALIBRATE_A/B/ALIGN`源码供开关恢复。
  当前搜索以8°/s在相对零点±50°内进行，普通`X_VALID`即可控制方向；目标进入中心±8 px立即
  锁住当前Yaw，不等待端点、不二次回扫、不要求`X_FINAL/AIM_STABLE`。高弹道观测和目标统一到
  靶心坐标：`d_obs=37+25=62 cm`、`q=62/100=0.62`；固定靶/自动搜索使用
  `d_virtual=d_target/q`求高根。若虚拟距离超过当前模型最大可达距离163.52 cm，则使用
  49.2606°最大射程角。距离定位的人工设定距离/地面0 cm公式、Yaw/Pitch PID、完整UI、30 s
  充电自动击发和继电器逻辑未修改。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、
  `Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录；
  未修改MaixCAM2代码、JX1协议、公共命令枚举、控制周期、PID参数、继电器引脚或极性。
- 验证：TFT靶心距离75/92.5/100/125 cm的Pitch目标分别为
  `-68.0306/-59.8421/-53.3454/-49.2606°`，最后一点因虚拟距离201.61 cm超过包络而采用最大
  射程角。面向STM32F407VET6的Debug/Release均从清理状态无警告构建成功，Flash分别为
  351,856/346,564 B，RAM分别为18,512/18,496 B；Debug ELF SHA-256=
  `0C6182EDD8DBD9A069EFAC5964BB58ABAA0A4BFA20866E2079F73D96B43BDCE6`。
- 烧录：只使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`和SWD 100 kHz。写入前及独立校验均识别CPUID=`0x410FC241`、DBGMCU ID=
  `0x101F6413`、`DEV_ID=0x413`和Flash=512 KiB；OpenOCD报告`Programming Finished`、
  `Verified OK`，独立`verify_image`再次确认351,856字节完全一致。复位运行2.5 s后
  PC=`0x080013BA`、GPIOB `MODER=0x00AA0285`、`ODR=0`，PB0/PB1均为输出低，随后恢复运行。
- 已知限制：当前射程修正只有一个实测点，假设高弹道水平进度可按0.62统一缩放；该假设尚未
  通过第二个距离验证。标定后可精确求解的TFT靶心距离上限约101.38 cm，更远请求只能输出最大
  射程角，不能承诺命中。30 s截止仍不会等待Pitch或一次锁定实际完成。
- 待确认事项：优先复测`DIST=100 cm`的新落点；记录箱体前沿落点后仍按`落点+25 cm`换到靶心
  坐标。再补测一个较近距离，用两点判断0.62比例是否恒定，并确认±50°搜索方向、中心一次锁定
  和目标丢失后返回搜索均符合实机。

## 2026-07-24 自动搜索视觉负反馈与高弹道射程微调烧录

- 修改原因：MaixVision日志中有效目标`cx`从455.22持续增至570.32并从画面右侧丢失，说明STM32
  旧像素误差到Yaw命令的映射形成正反馈；用户同时要求高射轨道再稍微远一点。
- 最终决策：只将`AUTO_AIM_PIXEL_ERROR_YAW_POLARITY`由`+1`改为`-1`，使目标在画面右侧时转向
  负Yaw、在左侧时转向正Yaw；`YAW_SCAN_CURRENT_POLARITY`和Yaw PID（含Ki）均不改。MaixVision
  现有`x`和`frame_width`字段足以表达误差，未修改视觉端或JX1协议。固定靶/自动搜索的高弹道
  虚拟距离由`d_t/0.62`改为`d_t/0.62×1.005`，用0.5%乘法射程裕量轻微降低高根角度，不叠加
  固定Pitch值。一次锁定、相对零点±50°、完整UI、30 s自动击发、距离定位地面弹道及继电器逻辑不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`既有映射消费者、
  `Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录。
- 验证：TFT靶心距离75/92.5/100/125 cm的Pitch目标分别为
  `-67.8867/-59.5529/-52.5146/-49.2606°`，最后一点仍采用最大射程角。STM32F407VET6
  Debug/Release均从清理状态无警告构建成功，Flash分别为351,864/346,576 B，RAM分别为
  18,512/18,496 B；`git diff --check`通过。Debug ELF SHA-256=
  `0D14183BAFF87DE02FCE2C0439AA5E2CBA47D38A33AFA8D6EC0B75E308461935`。
- 烧录：只使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`和SWD 100 kHz。写入会话内强制确认CPUID=`0x410FC241`、DBGMCU ID=
  `0x101F6413`、`DEV_ID=0x413`和Flash=512 KiB后写入Debug ELF；写入393,216 B并校验有效镜像
  351,864 B成功。独立重连`verify_image`再次完全一致；运行2.5 s后PC=`0x08002E06`、GPIOB
  `MODER=0x00AA0285`、`OTYPER=0`、`PUPDR=0x0050010A`、`ODR=0`，PB0/PB1均为输出低。
- 已知限制：`DEV_ID=0x413`硬件ID覆盖STM32F405/407/415/417系列，F407VET6的具体料号同时依据
  512 KiB容量、开发板标识和工程配置确认。0.62比例及1.005裕量仍依赖单个实测点，不能替代多个
  距离的落点标定；100 cm靶心角仅比上一版降低约0.831°，125 cm请求仍受最大射程包络限制。
- 待确认事项：现场先确认目标在画面右侧时`cx`向320减小并能一次锁定；再复测`DIST=100 cm`
  落点，若仍偏近，记录新的箱体前沿落点并小步调整乘法裕量，不修改Yaw电机极性或盲目增大积分项。

## 2026-07-24 高弹道射程裕量提高至1.010并烧录

- 修改原因：用户实测1.005倍射程裕量版本后要求弹丸再远一点。
- 最终决策：只将`CANNON_HIGH_ARC_RANGE_MARGIN`由1.005提高到1.010，使固定靶和自动搜索继续按
  `d_v=d_t/0.62×1.010`求高弹道；仍是距离域乘法映射，不增加固定Pitch偏置。视觉Yaw负反馈、
  PID、距离定位地面弹道、一次锁定、±50°搜索、完整UI、30 s自动击发和继电器逻辑均不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`和本记录。
- 验证：TFT靶心距离75/92.5/100/125 cm的Pitch目标分别为
  `-67.7420/-59.2563/-51.3932/-49.2606°`；100 cm比1.005版再降低约1.121°，125 cm仍采用
  最大射程角。STM32F407VET6 Debug/Release从清理状态无警告构建成功，Flash分别为
  351,864/346,576 B，RAM分别为18,512/18,496 B；`git diff --check`通过。Debug ELF SHA-256=
  `238356B1CC2396E7299D6BC064063A982FF902AEF24E37F6F0DC73F95F381BB5`。
- 烧录：使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号`2024-09-01`
  和SWD 100 kHz；写入会话及独立校验均确认CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、
  `DEV_ID=0x413`和Flash=512 KiB。写入393,216 B、校验有效镜像351,864 B成功，独立重连
  `verify_image`再次完全一致；运行2.5 s后PC=`0x080036EA`，GPIOB `MODER=0x00AA0285`、
  `OTYPER=0`、`PUPDR=0x0050010A`、`ODR=0`，PB0/PB1均为输出低。
- 已知限制：0.62比例仍只有一个实测点；100 cm靶心请求已接近163.52 cm理论最大虚拟距离，裕量
  达到约1.0138后也会被钳到-49.2606°最大射程角，继续增大宏将不再增加该距离的理论射程。
- 待确认事项：优先复测`DIST=100 cm`真实落点；若仍偏近，应记录新落点并重估0.62比例，而不是
  继续越过最大射程包络盲增裕量。

## 2026-07-24 自动搜索Yaw视觉迟滞死区（未烧录）

- 修改原因：用户要求先保留已烧录的1.010射程版本，再在发射自动瞄准中增加Yaw旋转死区，避免
  机构移动时视觉抖动使Yaw反复启停、无法稳定瞄准。
- 最终决策：保留现有`AUTO_AIM_X_TOLERANCE_PX=8 px`作为进入锁定阈值，新增
  `AUTO_AIM_X_RELEASE_TOLERANCE_PX=16 px`作为退出阈值。一次锁定首次进入中心±8 px后保持当前
  Yaw，锁定期间只在偏差超过±16 px时恢复旋转；偶发无效帧沿用现有500 ms目标丢失时间，在超时前
  保持Yaw。复用已有`coarse_locked`标志，不新增状态机。Yaw PID/极性、MaixVision/JX1协议、
  1.010高弹道、距离定位、完整UI、30 s自动击发和继电器逻辑均不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`Src/lcd_demo_app.c`、`README.md`、
  `CURRENT_SYSTEM_LOGIC.md`和本记录。
- 验证：面向STM32F407VET6的Debug/Release均从清理状态无警告构建成功，Flash分别为
  351,944/346,668 B，RAM分别为18,512/18,496 B；`git diff --check`通过。新Debug ELF SHA-256=
  `DD397429EC1A3041A700C8033784FB3C3E50EACF526391F0F51616394A35F515`。
- 烧录状态：按用户指定顺序，板上仍保持上一条已独立校验的1.010射程版本，SHA-256=
  `238356B1CC2396E7299D6BC064063A982FF902AEF24E37F6F0DC73F95F381BB5`；本条死区版本未烧录。
- 已知限制：±8/±16 px是当前640 px图像宽度下的初始迟滞参数，只完成源码和构建验证；尚未用
  真实机构确认抖动幅度、中心误差和500 ms丢失保持是否合适。
- 待确认事项：用户要求烧录死区版后，观察目标跨越±8 px时是否停止、在8..16 px范围内是否保持、
  超过±16 px时是否恢复正确方向旋转，以及短暂无效帧是否不再触发立即搜索。

## 2026-07-24 1.010射程与Yaw迟滞死区合并版本烧录

- 修改原因：用户确认将已构建的±8/±16 px Yaw迟滞死区与1.010高弹道射程版本一起烧录。
- 最终决策：烧录上一条已验证Debug镜像；未再修改控制参数、协议、状态机、继电器或发射时序。
- 涉及产物：`build/cmake/Debug/stm32f407ve_lcd.elf`，SHA-256=
  `DD397429EC1A3041A700C8033784FB3C3E50EACF526391F0F51616394A35F515`，有效镜像351,944 B。
- 验证：烧录前Debug/Release均为`ninja: no work to do`且`git diff --check`通过。只使用COM3对应
  CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号`2024-09-01`和SWD 100 kHz；写入会话
  及独立校验均确认CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、`DEV_ID=0x413`和Flash=
  512 KiB。写入393,216 B并校验有效镜像351,944 B成功，独立重连`verify_image`再次完全一致；
  运行2.5 s后PC=`0x08002E52`，GPIOB `MODER=0x00AA0285`、`OTYPER=0`、`PUPDR=0x0050010A`、
  `ODR=0`，PB0/PB1均为输出低。
- 已知限制：已验证目标身份、Flash一致性、程序运行和继电器上电空闲状态；尚未用真实视觉抖动
  验证±8/±16 px迟滞效果，也未执行30 s充电或发射动作。
- 待确认事项：实机观察目标进入±8 px后Yaw是否稳定保持、8..16 px内是否不反复启动、超过±16 px
  是否按正确方向恢复旋转，并复测`DIST=100 cm`的1.010高弹道落点。

## 2026-07-24 高弹道1.013、Yaw Kd提高及合并烧录

- 修改原因：用户要求弹丸再远一些，同时反馈自动搜索反复移动锁定，要求小步提高Yaw速度阻尼并与
  射程修改一起烧录。最新MaixVision日志中正常`BLOB_GUIDE`序列也出现
  `dx=1.80→-19.73→84.28`、`7.83→-13.97→78.48`等反复越过中心的周期性变化。
- 最终决策：将`CANNON_HIGH_ARC_RANGE_MARGIN`由1.010提高到1.013；TFT靶心距离100 cm的高弹道
  Pitch由-51.3932°降到-50.2424°，虚拟距离163.39 cm，仍未超过163.52 cm模型最大射程。
  将`YAW_SCAN_KD_A_PER_RPM`由0.005提高到0.006 A/rpm；`Kp=0.080`、`Ki=0.002`、0.70 A限流、
  ±8/±16 px迟滞、视觉极性、一次锁定、30 s击发和继电器逻辑均不变。
- 涉及文件：`Inc/lcd_demo_config.h`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`、
  `docs/architecture.md`和本记录。
- 验证：TFT靶心距离75/92.5/100/125 cm的Pitch目标分别为
  `-67.6547/-59.0745/-50.2424/-49.2606°`。STM32F407VET6 Debug/Release从清理状态无警告构建
  成功，Flash分别为351,944/346,668 B，RAM分别为18,512/18,496 B；`git diff --check`通过。
  Debug ELF SHA-256=`2CA000F13909F33C12E25B098A97F759CB173228600513F1FA748E8C4854B613`。
- 烧录：只使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`和SWD 100 kHz；写入会话及独立校验均确认CPUID=`0x410FC241`、DBGMCU ID=
  `0x101F6413`、`DEV_ID=0x413`和Flash=512 KiB。写入393,216 B、校验有效镜像351,944 B成功；
  独立重连`verify_image`再次完全一致。运行2.5 s后PC=`0x08002E52`，GPIOB
  `MODER=0x00AA0285`、`OTYPER=0`、`PUPDR=0x0050010A`、`ODR=0`，PB0/PB1均为输出低。
- 已知限制：源码确认自动搜索换向时`auto_aim_start_slow_yaw_move()`会把位置目标重新设为当前反馈角，
  而`yaw_scan_controller_set_target()`在目标变化时清空积分；这会表现为命令目标回撤且扫描中I项
  基本不积累，Kd增加只能抑制机械速度，不能消除上层目标重置。日志还存在`EDGE_QUAD`单帧
  `dx=-228.74/-183.02`离群值，现有±16 px退出阈值仍会响应此类大跳变。
- 待确认事项：先实测Kd=0.006版的机械过冲。若仍反复锁定，应经用户确认后修改目标生成结构：
  锁定时只捕获一次固定Yaw目标、换向时从上一命令目标连续斜坡，并对视觉退出死区增加连续样本确认；
  不应继续只靠增大Kd掩盖目标重置或视觉离群点。

## 2026-07-25 三个任务入口统一高弹道（未烧录）

- 修改原因：用户明确固定靶、自动搜索和距离定位全部采用高弹道；距离定位原先按低根是否超过5°
  自动选根，近距离可能使用低弹道。
- 最终决策：`cannon_aim_pitch_for_distance()`继续使用人工设定的50～100 cm距离和地面0 cm目标，
  但由`CANNON_ARC_AUTO`改为强制`CANNON_ARC_HIGH`。固定靶和自动搜索既有箱体前沿距离、
  `+25 cm`靶心映射、36.5 cm靶高及`d_v=d_t/0.62×1.013`高弹道校正保持不变；距离定位不套用
  这组视觉射程校正。此前距离定位按5°阈值自动选根的方向已被本次决定覆盖。
- 涉及文件：`Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`、
  `docs/architecture.md`、`docs/hardware_mapping.md`和本记录；未修改公共API、协议、距离范围、
  目标高度、PID、任务状态机、充电/击发时序或硬件配置。
- 验证：距离定位50/67.5/75/92/100 cm强制高根后提交Pitch分别为
  `-82.600938/-79.919402/-78.744212/-76.003951/-74.669021°`，均在内部±85°限制内。
  STM32F407VET6 Debug/Release从清理状态无警告构建成功，Flash分别为352,376/347,000 B，
  RAM分别为18,512/18,504 B；`git diff --check`通过。Debug ELF SHA-256=
  `D4807CA788A1C9CB293E3CE63559427E3AE12B0AE77578FEA3FF09B440BA4EE3`。
- 已知限制：只完成公式数值、静态差异和构建验证，尚未验证Pitch机械方向、接近-82.6°的行程、
  上方净空或真实地面落点；本次固件尚未烧录。
- 待确认事项：保持充电和发射功率回路断开并固定机构，优先从100 cm的约-74.67°开始确认方向和
  行程，再逐步测试75、67.5和50 cm；确认上方净空和机械限位后再进行低能落点测试。

## 2026-07-25 热点端点修正、DIST链路恢复与高弹道版本烧录

- 修改原因：当前热点网段由`192.168.84.x`变为`192.168.215.x`，MaixCAM2实际监听
  `192.168.215.66:9000`，STM32固件仍让ESP8266连接旧地址，导致VIS帧中断且TFT `DIST`
  在500 ms超时后保持无效。用户允许继续使用外部2.4 GHz热点并要求修改后立即烧录。
- 最终决策：只把`ESP8266_TCP_SERVER_IPV4`从`192.168.84.66`改为`192.168.215.66`；
  ESP8266继续使用模块已保存的STA热点配置、TCP客户端角色和既有3 s有界重连，MaixCAM2继续作为
  9000端口TCP服务端。本次Debug镜像同时包含上一条“三个任务入口统一高弹道”修改。未增加AP、
  Wi-Fi入网状态机、重发层级或协议字段。
- 涉及文件：`Inc/lcd_demo_config.h`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`、
  `docs/architecture.md`、`docs/hardware_mapping.md`和本记录；未修改JX1协议、ESP状态机、UART、
  任务周期、电机控制、继电器或发射时序。
- 验证：修改前实测`192.168.84.66`不可达，而`192.168.215.66:9000`可连接并输出有效
  `JX1|VIS`帧。STM32F407VET6 Debug/Release从清理状态无警告构建成功，Flash分别为
  352,376/347,004 B，RAM分别为18,512/18,504 B；`git diff --check`通过。Debug ELF SHA-256=
  `A09F7EB488ACC6FE42140A806EDBF107346A221A069713E11F118C999F347251`。
- 烧录：只使用COM3对应CMSIS-DAP `VID:PID=0x0416:0x5051`、FW 1.6.0、序列号
  `2024-09-01`和SWD 100 kHz；写入会话确认CPUID=`0x410FC241`、DBGMCU ID=`0x101F6413`、
  `DEV_ID=0x413`和Flash=512 KiB，OpenOCD报告`Programming Finished`与`Verified OK`并复位。
  独立重连`verify_image`退出码0。复位运行15 s后只读确认ESP状态14、Wi-Fi标志`0x40`、
  `VISION_ONLINE=1`，最新VIS为序号542、`d=864 mm`、宽640、flags=`0x07`；PB0/PB1状态均为0，
  GPIOB ODR=`0`，随后恢复运行。未发送电机、充电、发射或任务命令。
- 已知限制：当前方案依赖外部热点以及MaixCAM2地址保持`192.168.215.66`；热点若重新分配地址，
  ESP8266不会自动发现服务端，DIST会再次超时。ESP的SSID/密码仍依赖模块既有持久配置。
- 待确认事项：断开电脑但保留同一热点，为MaixCAM2和STM32/ESP8266独立供电，确认Maix应用自动
  运行且TFT `DIST`持续变化；在热点端为MaixCAM2配置DHCP地址保留，避免IP再次漂移。

## 2026-07-25 距离定位恢复低弹道并立即烧录

- 修改原因：用户否决距离定位强制高弹道，明确要求距离定位恢复低射；固定靶和自动搜索不得改动。
- 最终决策：只将`cannon_aim_pitch_for_distance()`的选根从`CANNON_ARC_HIGH`改为
  `CANNON_ARC_LOW`，继续使用人工50～100 cm距离和地面0 cm目标。固定靶与自动搜索继续复用
  `solve_calibrated_high_pitch()`及既有`+25 cm`、36.5 cm、`0.62×1.013`高弹道映射。
  此前“三个入口统一高弹道”的距离定位部分被本次决定覆盖。
- 涉及文件：`Application/control/cannon_aim_model.c`、`README.md`、`CURRENT_SYSTEM_LOGIC.md`、
  `docs/architecture.md`、`docs/hardware_mapping.md`和本记录；未修改固定靶/自动搜索调用链、网络端点、
  PID、状态机、充电或击发逻辑。
- 验证：Debug从清理状态无警告构建成功，Flash=352,376 B、RAM=18,512 B，`git diff --check`
  通过；静态回查确认距离定位调用`CANNON_ARC_LOW`，固定靶仍调用`solve_calibrated_high_pitch()`。
  Debug ELF SHA-256=`62690F139732F9AFE821FA7027C0CC76785A9644D6917A8DBA5416EBF373EDD0`。
- 烧录：使用COM3对应CMSIS-DAP FW 1.6.0、序列号`2024-09-01`、SWD 100 kHz；目标识别为
  Cortex-M4、DEV_ID=`0x413`、Flash=512 KiB，OpenOCD报告`Programming Finished`、`Verified OK`
  并复位，退出码0。未发送充电、发射、电机或任务命令。
- 已知限制：完成源码、构建、Flash写入会话校验；尚未实测低弹道机械方向与地面落点。
- 待确认事项：功率回路断开时先测试92 cm，确认Pitch不再跳到约-76°高根，再测试其它距离。
