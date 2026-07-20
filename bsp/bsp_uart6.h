#ifndef BSP_UART6_H
#define BSP_UART6_H

/**
 * @file    bsp_uart6.h
 * @brief   BSP 层 — USART6 (PG14=TX, PG9=RX, 115200 8N1, 视觉)
 *
 *  严格按官方 RoboMaster C 板例程 8.USART_receive_and_send 的引脚配置
 *  (DJI C 板 silkscreen "UART1" 实际接到 PG14/PG9)
 *  RX: RXNE 中断逐字节回调
 *  TX: DMA2_Stream6_Channel5 非阻塞
 */

#include <stdint.h>
#include <stdbool.h>

typedef void (*BSP_UART6_RxByteCallback_t)(uint8_t byte);
typedef void (*BSP_UART6_IdleCallback_t)(void);

void BSP_UART6_Init(void);
void BSP_UART6_RegisterRxCallback(BSP_UART6_RxByteCallback_t cb);
void BSP_UART6_RegisterIdleCallback(BSP_UART6_IdleCallback_t cb);
bool BSP_UART6_SendBytes(const uint8_t *data, uint16_t len);
/* Abort reverse traffic, make PG14 high impedance, and reject TX for the
 * requested interval. RX on PG9 remains active. */
void BSP_UART6_MuteTxForMs(uint32_t duration_ms);

/* 中断入口 */
void BSP_UART6_IRQHandler(void);
void BSP_UART6_DMA_TX_IRQHandler(void);

#endif /* BSP_UART6_H */
