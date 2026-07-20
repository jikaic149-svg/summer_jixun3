#ifndef BSP_SPI_H
#define BSP_SPI_H

/**
 * @file    bsp_spi.h
 * @brief   BSP层 — SPI1 底层操作接口 (轮询 + DMA)
 * @note    唯一包含 HAL 库的层级，向 Device 层暴露标准化 API
 *          参考 QGimbal: SPI DMA 非阻塞读取 BMI088
 */

#include <stdint.h>

/* ======================== 轮询接口 (初始化阶段使用) ======================== */

/**
 * @brief  初始化 SPI 相关 GPIO（已由 CubeMX 完成，此处为空壳）
 */
void BSP_SPI_Init(void);

/**
 * @brief  SPI 全双工单字节读写 (轮询，阻塞)
 * @param  txdata 发送字节
 * @return 接收字节
 */
uint8_t BSP_SPI_ReadWriteByte(uint8_t txdata);

/* ======================== CS 片选控制 ======================== */

void BSP_SPI_AccelCS_Low(void);
void BSP_SPI_AccelCS_High(void);
void BSP_SPI_GyroCS_Low(void);
void BSP_SPI_GyroCS_High(void);

/* ======================== DMA 接口 (运行时使用) ======================== */

/**
 * @brief  初始化 SPI1 DMA 传输 (使能 SPI DMA 请求 + RX DMA TC 中断)
 * @note   参考 QGimbal: SPI1_DMA_init()
 */
void BSP_SPI_DMA_Init(void);

/**
 * @brief  启动一次 SPI DMA 传输
 * @param  tx_buf 发送缓冲区地址
 * @param  rx_buf 接收缓冲区地址
 * @param  ndtr   传输字节数
 */
void BSP_SPI_DMA_Enable(uint32_t tx_buf, uint32_t rx_buf, uint16_t ndtr);

/**
 * @brief  SPI1 RX DMA 传输完成中断处理 (由 stm32f4xx_it.c 调用)
 * @note   内部会调用 BSP_SPI_DMA_RxCpltCallback()
 */
void BSP_SPI_DMA_RX_IRQHandler(void);

/**
 * @brief  DMA RX 完成回调 (弱定义, 由上层覆盖)
 */
extern void BSP_SPI_DMA_RxCpltCallback(void);

#endif /* BSP_SPI_H */
