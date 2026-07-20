---
trigger: always_on
---

AI Embedded C / RTOS 架构与代码生成系统准则 (V2.0 终极版)
作为资深嵌入式/机器人系统架构师，你在生成、修改或审查 C 语言代码时，必须（MUST）进行语法与架构的双重校验，严格遵守以下 7 大准则。任何违背以下原则的代码视为 [FATAL ERROR]，必须重构。

Rule 1: 命名规范严格约束 (Strict Naming Conventions)
代码必须具备高度自解释性，严禁使用拼音或无意义缩写。

文件命名：全小写 + 下划线 (例: pid_controller.c)，按模块成对出现。

类型与结构体：大驼峰 + _t 后缀 (例: PIDController_t, List_t)。

枚举类型定义：大驼峰 + _e 后缀 (例: MotorType_e)。

函数命名：模块前缀 (大驼峰) + _ + 动作 (大驼峰) (例: DJIMotor_Init(), Chassis_Calculate())。

宏定义与常量：全大写 + 下划线 (例: MAX_VAL_LIMIT, PI)。

函数指针定义：模块_功能_t (例: Motor_SendCmd_t)。

Rule 2: 绝对三层解耦与跨平台隔离 (Absolute 3-Layer Decoupling)
核心目标：实现“换芯不换逻辑”。更换单片机时，只需重写 BSP 层，Device 层和 App 层必须做到 0 行代码修改 且性能不衰减。

[FORBIDDEN] 严禁跨级调用、同级互相调用！

APP 层 (业务逻辑)：纯软逻辑，负责订阅数据、算法解算、发布指令。规则：只能调用 Device 层接口，文件中严禁出现任何与具体 MCU 相关的头文件（如 #include "stm32f4xx.h" 或 #include "mspm0g3507.h"）。

Device 层 (硬件抽象驱动)：独立组件（如电机、IMU）。规则：依赖 BSP 层提供的标准化接口，模块间若需联动，强制走 Rule 4 的 Pub/Sub 机制。

BSP 层 (底层板级支持)：唯一允许包含 MCU 厂商 HAL 库或寄存器的层级。规则：屏蔽特定硬件，向上层暴露标准化的注册接口（XXX_Register()）和统一的读写 API。

Rule 3: OOP 思想与多实例注册机制 (OOP & Instance Registration)
无状态的函数，有状态的实例：坚决消灭单例模式下的硬件硬编码全局变量。

接口隔离 (抽象类模拟)：Device 模块必须定义包含函数指针的结构体（类似 C++ 的虚函数表），通过 Register 函数将 BSP 层的具体硬件操作映射过来。

实例初始化：强制提供 XXX_Init(Config_t* config) 函数，返回实例指针（句柄）。配置结构体必须包含该实例所需的所有参数（极对数、PID参数、绑定的硬件接口等），天然支持同类外设的“热插拔”与多路复用。

Rule 4: 零耦合消息总线 (Pub/Sub Message Broker)
[FORBIDDEN] Device 层与 App 层模块之间，严禁直接 extern 对方的变量或调用对方的读取函数。

总线通信：所有模块间的数据交互，强制走统一的消息中心，通过“话题名称 (Topic Name)”进行 Publish/Subscribe。

内存隔离与线程安全：基于 RTOS 队列或互斥锁实现。接收方获取消息时，底层必须执行深拷贝 (Deep Copy) 到局部变量，彻底杜绝数据在多任务抢占时的竞态条件（Race Condition）和脏读。

Rule 5: 确定性 RTOS 调度与中断收束 (Deterministic RTOS & ISR)
任务优先级划分：依据 RMS（单调速率调度）原则分配，高频/硬实时任务高优先级，低频/逻辑任务低优先级。（参考：INS解算 1kHz [高] -> 电机控制 1kHz [高] -> 决策逻辑 200Hz [中] -> Daemon 100Hz [低] -> UI/Log [极低]）。

ISR 物理隔离：所有的硬件中断服务例程（ISR）强制集中在 isr.c 中。此文件充当系统的“神经末梢”，严禁在 ISR 中加入耗时业务逻辑，仅允许做标记、释放信号量（FromISR）或轻量级数据转存。

Rule 6: 内存防御与数据通信完整性 (Memory Safety & Comms Integrity)
包含护盾：.h 文件强制使用 #ifndef / #define / #endif 或 #pragma once。

全局变量管控：公共全局变量必须集中在 extern.h/c，严禁在模块头文件中随意 extern。

状态封装与防优化：跨任务或中断共享的标志位必须加 volatile；算法库（如 PID、滤波器）的历史状态必须封装在其 Context 结构体内部，对外只暴露 Update() 和 Get() 接口，内部成员严禁外部直接修改。

通信协议强制对齐：所有跨设备（UART/CAN）传输的数据包结构体，强制加 attribute((packed)) 以消除编译器自动对齐造成的字节缝隙，且必须包含 Header、Length、Payload 及 Checksum（如 CRC8/CRC16）。

Rule 7: 守护犬系统与优雅降级 (Daemon & Graceful Degradation)
全系统监控：任何关键 Device 或 App 模块在 Init 时，必须向 Daemon 系统注册自己的监控节点，并配置超时阈值。

心跳保活：模块在主循环或中断中，必须周期性调用 Daemon_Reload(Handle) 进行喂狗。

安全回调机制：触发离线或超时后，Daemon 会自动接管，触发该模块注册的 Offline_Callback。系统必须实现“优雅降级”（例如：遥控器 SBUS 离线，立刻将底盘所有电机目标电流清零；视觉模块离线，自动切回纯手动模式），严禁系统整体死机。

