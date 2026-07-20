#ifndef BSP_UART_H
#define BSP_UART_H

/**
 * @file    bsp_uart.h
 * @brief   BSP 层 — USART1 (PA9=TX, PB7=RX, 115200 8N1)
 *
 *  设计原则 (ti.md Rule 2): BSP 只暴露 MCU 无关接口
 *  - RX: RXNE 中断逐字节回调 (与官方 8.USART_receive_and_send 一致)
 *  - TX: DMA2_Stream7_Channel4 非阻塞发送
 *
 *  本层不做协议解析、不做行装配, 上层 (Device) 自己负责。
 */

#include <stdint.h>
#include <stdbool.h>

/* ======================== 类型 ======================== */

/* RX 单字节回调: 在 ISR 上下文调用 */
typedef void (*BSP_UART1_RxByteCallback_t)(uint8_t byte);

/* IDLE 帧边界回调: 在 ISR 上下文调用 (可选, 不注册即不触发) */
typedef void (*BSP_UART1_IdleCallback_t)(void);

/* ======================== 接口 ======================== */

/**
 * @brief 初始化 USART1 (GPIO + UART + DMA TX + NVIC + RXNE + IDLE)
 */
void BSP_UART1_Init(void);

/**
 * @brief 注册逐字节接收回调 (BSP 收到一个字节即调用一次)
 */
void BSP_UART1_RegisterRxCallback(BSP_UART1_RxByteCallback_t cb);

/**
 * @brief 注册 IDLE 帧边界回调 (UART 总线空闲时触发)
 */
void BSP_UART1_RegisterIdleCallback(BSP_UART1_IdleCallback_t cb);

/**
 * @brief DMA 非阻塞发送; 上一次未完成返回 false
 */
bool BSP_UART1_SendBytes(const uint8_t *data, uint16_t len);

/* ======================== 中断入口 (供 stm32f4xx_it.c 调用) ======================== */
void BSP_UART1_IRQHandler(void);
void BSP_UART1_DMA_TX_IRQHandler(void);

#endif /* BSP_UART_H */
