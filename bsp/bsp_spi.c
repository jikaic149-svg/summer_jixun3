/**
 * @file    bsp_spi.c
 * @brief   BSP层 — SPI1 底层操作 (STM32F407 HAL)
 * @note    轮询模式: 初始化阶段使用
 *          DMA模式: 运行时高速 IMU 数据读取 (参考 QGimbal)
 *          CS引脚: ACCEL=PA4, GYRO=PB0
 *          SPI1: SCK=PB3, MISO=PB4, MOSI=PA7
 *          DMA: RX=DMA2_Stream2, TX=DMA2_Stream3
 */

#include "bsp_spi.h"
#include "main.h"
#include "spi.h"
#include "dma.h"

/* ======================== 外部 DMA 句柄 ======================== */
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_rx;
extern DMA_HandleTypeDef hdma_spi1_tx;

/* ======================== 初始化 ======================== */

void BSP_SPI_Init(void)
{
    /* GPIO 和 SPI 已由 CubeMX 生成的 MX_GPIO_Init / MX_SPI1_Init 完成 */
}

/* ======================== 全双工轮询读写 ======================== */

uint8_t BSP_SPI_ReadWriteByte(uint8_t txdata)
{
    uint8_t rx_data;
    HAL_SPI_TransmitReceive(&hspi1, &txdata, &rx_data, 1, 1000);
    return rx_data;
}

/* ======================== CS 片选控制 ======================== */

void BSP_SPI_AccelCS_Low(void)
{
    HAL_GPIO_WritePin(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin, GPIO_PIN_RESET);
}

void BSP_SPI_AccelCS_High(void)
{
    HAL_GPIO_WritePin(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin, GPIO_PIN_SET);
}

void BSP_SPI_GyroCS_Low(void)
{
    HAL_GPIO_WritePin(CS1_GYRO_GPIO_Port, CS1_GYRO_Pin, GPIO_PIN_RESET);
}

void BSP_SPI_GyroCS_High(void)
{
    HAL_GPIO_WritePin(CS1_GYRO_GPIO_Port, CS1_GYRO_Pin, GPIO_PIN_SET);
}

/* ======================== SPI DMA 传输 ======================== */

/**
 * @brief  初始化 SPI1 的 DMA 传输能力
 *         使能 SPI TX/RX DMA 请求 + RX DMA 传输完成中断
 * @note   参考 QGimbal: SPI1_DMA_init()
 */
void BSP_SPI_DMA_Init(void)
{
    /* 使能 SPI1 的 DMA 收发请求 */
    SET_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    SET_BIT(hspi1.Instance->CR2, SPI_CR2_RXDMAEN);

    /* 使能 SPI1 */
    __HAL_SPI_ENABLE(&hspi1);

    /* 使能 RX DMA 传输完成中断 */
    __HAL_DMA_ENABLE_IT(&hdma_spi1_rx, DMA_IT_TC);
}

/**
 * @brief  启动一次 SPI DMA 传输
 * @note   调用前需先拉低对应 CS，传输完成后在中断中拉高 CS
 *         参考 QGimbal: SPI1_DMA_enable()
 */
void BSP_SPI_DMA_Enable(uint32_t tx_buf, uint32_t rx_buf, uint16_t ndtr)
{
    /* 禁用 DMA 通道 */
    __HAL_DMA_DISABLE(&hdma_spi1_rx);
    __HAL_DMA_DISABLE(&hdma_spi1_tx);

    while (hdma_spi1_rx.Instance->CR & DMA_SxCR_EN)
        __HAL_DMA_DISABLE(&hdma_spi1_rx);
    while (hdma_spi1_tx.Instance->CR & DMA_SxCR_EN)
        __HAL_DMA_DISABLE(&hdma_spi1_tx);

    /* 清除所有 DMA 标志 */
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmarx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_HT_FLAG_INDEX(hspi1.hdmarx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_TE_FLAG_INDEX(hspi1.hdmarx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_DME_FLAG_INDEX(hspi1.hdmarx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_FE_FLAG_INDEX(hspi1.hdmarx));

    __HAL_DMA_CLEAR_FLAG(hspi1.hdmatx, __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmatx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmatx, __HAL_DMA_GET_HT_FLAG_INDEX(hspi1.hdmatx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmatx, __HAL_DMA_GET_TE_FLAG_INDEX(hspi1.hdmatx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmatx, __HAL_DMA_GET_DME_FLAG_INDEX(hspi1.hdmatx));
    __HAL_DMA_CLEAR_FLAG(hspi1.hdmatx, __HAL_DMA_GET_FE_FLAG_INDEX(hspi1.hdmatx));

    /* 设置内存地址和外设地址 */
    hdma_spi1_rx.Instance->M0AR = rx_buf;
    hdma_spi1_rx.Instance->PAR  = (uint32_t)&(SPI1->DR);
    hdma_spi1_tx.Instance->M0AR = tx_buf;
    hdma_spi1_tx.Instance->PAR  = (uint32_t)&(SPI1->DR);

    /* 设置传输长度 */
    __HAL_DMA_SET_COUNTER(&hdma_spi1_rx, ndtr);
    __HAL_DMA_SET_COUNTER(&hdma_spi1_tx, ndtr);

    /* 启动 DMA */
    __HAL_DMA_ENABLE(&hdma_spi1_rx);
    __HAL_DMA_ENABLE(&hdma_spi1_tx);
}

/* ======================== DMA 中断处理 ======================== */

/**
 * @brief  DMA2_Stream2 (SPI1 RX) 传输完成中断
 *         参考 QGimbal: DMA2_Stream0_IRQHandler
 */
void BSP_SPI_DMA_RX_IRQHandler(void)
{
    if (__HAL_DMA_GET_FLAG(hspi1.hdmarx, __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmarx)) != RESET)
    {
        __HAL_DMA_CLEAR_FLAG(hspi1.hdmarx, __HAL_DMA_GET_TC_FLAG_INDEX(hspi1.hdmarx));

        /* 调用上层回调 */
        BSP_SPI_DMA_RxCpltCallback();
    }
}

/* 弱定义回调 */
__attribute__((weak)) void BSP_SPI_DMA_RxCpltCallback(void)
{
    /* 由 ins_task.c 覆盖 */
}
