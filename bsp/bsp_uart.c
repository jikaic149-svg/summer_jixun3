/**
 * @file    bsp_uart.c
 * @brief   BSP 层 — USART1 (蓝牙)
 * @note    参照 RoboMaster C 板官方例程 8.USART_receive_and_send
 *
 *          引脚: PA9 = TX (AF7), PB7 = RX (AF7), 115200 8N1
 *          (RX 用 PB7, 与官方例程一致; 之前选 PA10 是错的引脚)
 *
 *          RX 走 RXNE 中断, 每个字节进 ISR → 回调
 *          TX 走 DMA2_Stream7_Channel4, 非阻塞
 */

#include "bsp_uart.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ======================== 硬件句柄 ======================== */
static UART_HandleTypeDef huart1;
static DMA_HandleTypeDef  hdma_usart1_tx;

/* ======================== 缓冲 / 状态 ======================== */
#define UART1_TX_BUF_SIZE 256U
static uint8_t           tx_buf[UART1_TX_BUF_SIZE];
static volatile uint8_t  tx_busy = 0;

static BSP_UART1_RxByteCallback_t rx_cb   = NULL;
static BSP_UART1_IdleCallback_t   idle_cb = NULL;

extern void BSP_UART6_NotifyTxDone(void);
extern void BSP_UART6_NotifyTxError(void);

/* ======================== 初始化 ======================== */

void BSP_UART1_Init(void)
{
    /* ----- 1. 时钟 ----- */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* ----- 2. GPIO -----
     *   PA9 = USART1_TX (AF7)
     *   PB7 = USART1_RX (AF7) — 跟官方一致
     */
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin       = GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin       = GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* ----- 3. USART1 (与官方一致) ----- */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    /* ----- 4. DMA TX: DMA2_Stream7_Channel4 ----- */
    hdma_usart1_tx.Instance                 = DMA2_Stream7;
    hdma_usart1_tx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode                = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_usart1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_usart1_tx);
    __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);

    /* ----- 5. NVIC ----- */
    HAL_NVIC_SetPriority(USART1_IRQn,       6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

    /* ----- 6. 使能 RXNE + IDLE 中断 (官方做法) ----- */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

/* ======================== 注册 ======================== */

void BSP_UART1_RegisterRxCallback(BSP_UART1_RxByteCallback_t cb)   { rx_cb   = cb; }
void BSP_UART1_RegisterIdleCallback(BSP_UART1_IdleCallback_t cb)   { idle_cb = cb; }

/* ======================== TX ======================== */

bool BSP_UART1_SendBytes(const uint8_t *data, uint16_t len)
{
    if (tx_busy || len == 0 || len > UART1_TX_BUF_SIZE) return false;

    memcpy(tx_buf, data, len);
    tx_busy = 1;
    if (HAL_UART_Transmit_DMA(&huart1, tx_buf, len) != HAL_OK) {
        tx_busy = 0;
        return false;
    }
    return true;
}

/* ======================== 中断 (与官方风格一致) ======================== */

void BSP_UART1_IRQHandler(void)
{
    uint32_t sr  = huart1.Instance->SR;
    uint32_t cr1 = huart1.Instance->CR1;

    /* ----- RXNE: 收到一字节 ----- */
    if ((sr & USART_SR_RXNE) && (cr1 & USART_CR1_RXNEIE))
    {
        uint8_t b = (uint8_t)(huart1.Instance->DR & 0xFF);
        if (rx_cb) rx_cb(b);
        return;
    }

    /* ----- IDLE: 帧空闲 ----- */
    if ((sr & USART_SR_IDLE) && (cr1 & USART_CR1_IDLEIE))
    {
        (void)huart1.Instance->DR;   /* 读 DR 清 IDLE 标志 (先读 SR 已完成) */
        if (idle_cb) idle_cb();
        return;
    }

    /* ----- TC: DMA TX 完成 ----- */
    if ((sr & USART_SR_TC) && (cr1 & USART_CR1_TCIE))
    {
        __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);
        __HAL_UART_DISABLE_IT(&huart1, UART_IT_TC);
        huart1.gState = HAL_UART_STATE_READY;
        tx_busy = 0;
        return;
    }

    /* ----- ORE: 溢出 ----- */
    if (sr & USART_SR_ORE)
    {
        (void)huart1.Instance->DR;
    }
}

void BSP_UART1_DMA_TX_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != NULL && huart->Instance == USART1) {
        tx_busy = 0U;
    } else if (huart != NULL && huart->Instance == USART6) {
        BSP_UART6_NotifyTxDone();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != NULL && huart->Instance == USART1) {
        tx_busy = 0U;
    } else if (huart != NULL && huart->Instance == USART6) {
        BSP_UART6_NotifyTxError();
    }
}
