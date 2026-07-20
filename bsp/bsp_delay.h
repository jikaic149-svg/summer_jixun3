#ifndef BSP_DELAY_H
#define BSP_DELAY_H

/**
 * @file    bsp_delay.h
 * @brief   BSP层 — 精确延时接口
 */

#include <stdint.h>

/**
 * @brief  DWT 周期计数器初始化 (用于微秒级时间戳)
 * @note   使能 Cortex-M4 的 DWT->CYCCNT，供 BSP_GetMicros 使用。
 *         必须在调度器启动前调用一次。
 */
void BSP_DWT_Init(void);

/**
 * @brief  获取自 DWT 使能以来的微秒时间戳 (约 25.5s 回绕)
 * @return 微秒计数，调用方用无符号差值即可安全处理回绕
 * @note   基于 DWT->CYCCNT @168MHz，分辨率约 6ns，远高于 1ms SysTick。
 */
uint32_t BSP_GetMicros(void);

/**
 * @brief  微秒级延时 (SysTick 精确计数)
 * @param  us 微秒数
 */
void BSP_Delay_Us(uint16_t us);

/**
 * @brief  毫秒级延时
 * @param  ms 毫秒数
 */
void BSP_Delay_Ms(uint16_t ms);

#endif /* BSP_DELAY_H */
