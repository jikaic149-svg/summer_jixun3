# LCD 固件架构与运行契约

## 执行模型

当前目标为裸机协作式主循环，不包含 FreeRTOS Task：

1. `bsp_board_poll()`推进 HSE、PLL 和时钟切换状态机；
2. `ili9341_poll()`推进上电等待、软件复位和 Sleep Out；
3. XPT2046 每 5 ms 读取 5 组样本并做中值滤波；
4. UI 只在触摸事件或模型变化时重画对应控件；
5. 循环末尾执行 `WFI`，由 1 kHz SysTick 唤醒。

HSE、PLL 各有 100 ms 截止时间；LCD 初始化等待分别为 10 ms、10 ms、
120 ms，均通过回绕安全的时间比较推进。没有等待硬件标志的空转循环。
FSMC 连续像素写和软件 SPI 单字节传输是确定长度的同步操作，不等待外部事件。

## 静态内存

- 不调用 `malloc/calloc/realloc/free`；
- RGB565 字形缓冲为 16×16×2 = 512 B；
- 命令 FIFO 深度 8，所有槽静态分配；
- 不配置 320×240×2 = 153,600 B 的全帧缓冲；
- CCM 当前不放置变量，避免未来 DMA 可见性误用。

## 命令 Topic

| 字段 | 契约 |
|---|---|
| Topic | `app_command_t` 离散事件 FIFO |
| 生产者 | `target_ui_step()`，主循环上下文 |
| 消费者 | `control_port_submit()`，主循环上下文 |
| Payload | 命令枚举、`distance_cm`（厘米）、`timestamp_ms`（uint32 单调毫秒） |
| 深度 | 8 |
| 满载策略 | 立即返回 `APP_PUBLISH_FULL`，UI 显示队列已满 |
| 重试 | 无自动重试 |
| ISR | 不发布、不消费 |

当前弱 `control_port_submit()`始终返回 `CONTROL_SUBMIT_UNAVAILABLE`，因此工程不会
把 UI 点击伪装成执行机构已经响应。接入控制层时应提供强实现，并保持提交函数
非阻塞；需要异步完成结果时应新增有界状态 Topic，而不是让 UI 轮询控制器私有变量。

## UI 刷新

UI 使用固定控件表和 32 位 dirty mask。按下、释放、反馈、状态、方位角和仰角分别
使对应控件失效；页面切换才整屏重绘。矩形采用半开区间 `(x,y,width,height)`，
ILI9341 窗口末端只在 Device 层转换为 `x+width-1/y+height-1`。

## 触摸事件

- PENIRQ 低有效，当前轮询，不在 ISR 中读软件 SPI；
- 连续 3 次有效样本才产生 Press，连续 3 次无触摸才产生 Release；
- Click 要求按下和释放命中同一控件；
- 距离输入只在 50–100 cm 时发布；
- “停止”是普通软件命令按钮，不声明为硬件急停或安全互锁。
