/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @note    SVC_Handler, PendSV_Handler, SysTick_Handler 已由 FreeRTOS 接管
  *          (FreeRTOSConfig.h 中 #define 映射)，不在此文件中定义
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_uart.h"
#include "bsp_uart6.h"
#include "bsp_spi.h"
#include "can.h"
/* USER CODE END Includes */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void DebugMon_Handler(void)
{
}

/*
 * 注意: SVC_Handler, PendSV_Handler, SysTick_Handler
 * 已由 FreeRTOS 接管 (FreeRTOSConfig.h 中 #define vPortSVCHandler 等)
 */

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

/* ===== BMI088 DRDY EXTI 中断 ===== */

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
}

/* ===== SPI1 DMA RX 完成中断 (DMA2_Stream2 for SPI1_RX) ===== */

void DMA2_Stream2_IRQHandler(void)
{
    BSP_SPI_DMA_RX_IRQHandler();
}

/* ===== SPI1 DMA TX 完成中断 (DMA2_Stream3 for SPI1_TX) ===== */

void DMA2_Stream3_IRQHandler(void)
{
    extern DMA_HandleTypeDef hdma_spi1_tx;
    HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

/* ===== USART1 RXNE + IDLE 中断 (蓝牙) ===== */

void USART1_IRQHandler(void)
{
    BSP_UART1_IRQHandler();
}

/* ===== DMA2 Stream7 中断 (USART1 TX 完成) ===== */

void DMA2_Stream7_IRQHandler(void)
{
    BSP_UART1_DMA_TX_IRQHandler();
}

/* ===== USART6 RXNE + IDLE 中断 (视觉) ===== */

void USART6_IRQHandler(void)
{
    BSP_UART6_IRQHandler();
}

/* ===== DMA2 Stream6 中断 (USART6 TX 完成) ===== */

void DMA2_Stream6_IRQHandler(void)
{
    BSP_UART6_DMA_TX_IRQHandler();
}

/* ===== CAN1 RX0 中断 ===== */

void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&hcan1);
}
