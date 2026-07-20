# STM32F407VET6 LCD 触控工程

当前默认目标为魔女科技 STM32F407VET6 v5.5 开发板和 34 针 2.8 寸
`ILI9341 + XPT2046` 屏幕。旧 `bsp/`、`Device/` 及其余源码均保留；旧
`task/`、`mode/` 已移出工程。CMake 只编译本 README 所列的新 LCD 目标，
不会把旧驱动带入链接。

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

- 50–100 cm 距离输入和“距离发射”；
- 固定靶、自动搜索、往复扫描、停止；
- 状态、水平方位角和垂直仰角；
- 独立数字键盘页，非法距离不会提交。

界面预览：[`主界面`](docs/ui_main_preview.png) / [`距离键盘`](docs/ui_distance_preview.png)。

触摸事件进入 8 槽静态命令 FIFO。`Src/control_port.c` 当前是明确返回
“控制器未接入”的弱端口，只验证屏幕、触摸和 UI，不会驱动任何发射机构。
以后接入控制层时，用强符号实现 `control_port_submit()`，并通过
`lcd_demo_app_set_runtime()`推送状态、方位角和仰角（单位：度）。

## 分层

- `bsp/bsp_board_f407ve.*`：25 MHz HSE、168 MHz 时钟及 1 ms 单调时基；
- `bsp/bsp_lcd_fsmc.*`：FSMC 引脚、地址和总线写入；
- `bsp/bsp_xpt2046_io.*`：触摸 GPIO 软件 SPI；
- `Device/ili9341/`、`Device/xpt2046/`：器件协议和状态；
- `Application/ui/`：无 MCU/RTOS 依赖的界面状态、事件和静态字体；
- `Src/lcd_demo_app.c`：组合端口，不承载控制算法。

详细引脚、时序、NC 引脚和实机检查项见
[`docs/hardware_mapping.md`](docs/hardware_mapping.md)。触摸不准时先修改
`Inc/lcd_demo_config.h` 的 `TOUCH_RAW_X_LEFT/RIGHT` 和
`TOUCH_RAW_Y_TOP/BOTTOM`，不要在上层交换坐标补偿。

运行契约、命令 FIFO 和脏区策略见
[`docs/architecture.md`](docs/architecture.md)。

## 验证边界

Debug/Release 均可在本机交叉编译，但当前没有连接实物。因此编译、链接、
Flash/RAM 尺寸属于已验证项；LCD ID、FSMC 波形、触摸轴向与刷新撕裂仍需上板确认。
