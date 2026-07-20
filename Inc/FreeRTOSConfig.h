#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS 内核配置 (STM32F407 @ 168MHz)
 * @note    遵循 TI.md Rule 5: RMS 优先级调度
 */

/* ======================== 基本配置 ======================== */

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      168000000UL
/* 不要定义 configSYSTICK_CLOCK_HZ!
 * port.c 只要看到它被显式定义(哪怕值等于CPU频率),就会清掉 SysTick 的
 * CLKSOURCE 位,让 SysTick 走外部时钟 HCLK/8=21MHz,而重装载值仍按168MHz算,
 * 结果每个"1ms" tick 实际 8ms —— 全系统 FreeRTOS 计时慢 8 倍
 * (轮速PID"1kHz"实际125Hz, VOFA"20ms"实际160ms)。
 * 不定义时 port.c 默认 configSYSTICK_CLOCK_HZ=configCPU_CLOCK_HZ 且置位
 * CLKSOURCE, SysTick 才真正跑在 168MHz。 */
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                   ((size_t)(20 * 1024))
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* ======================== 内存管理 ======================== */

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* ======================== 钩子函数 ======================== */

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1

/* ======================== 软件定时器 ======================== */

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               2
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

/* ======================== 协程 (不使用) ======================== */

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/* ======================== 可选功能 ======================== */

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_eTaskGetState                   1

/* ======================== Cortex-M4 中断优先级 ======================== */

/*
 * STM32F407 使用 4 位优先级 (NVIC_PRIORITYGROUP_4)
 * FreeRTOS 管理的中断优先级必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 *
 * 优先级值越低 = 越高优先级
 * 0-4: 不受 FreeRTOS 管理 (可用于最高优先级的硬实时中断)
 * 5-15: FreeRTOS 管理的中断
 */

#ifdef __NVIC_PRIO_BITS
 #define configPRIO_BITS         __NVIC_PRIO_BITS
#else
 #define configPRIO_BITS         4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ======================== 断言 ======================== */

#define configASSERT(x) if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

/* ======================== FreeRTOS 接管系统中断 ======================== */

/* 将 SVC、PendSV、SysTick 映射到 FreeRTOS 实现 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
