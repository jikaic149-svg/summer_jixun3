#include "bsp_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>

#include "lcd_demo_config.h"
#include "stm32f4xx.h"

#define UART_BRIDGE_BUFFER_SIZE  256U
#define UART_BRIDGE_BUFFER_MASK  (UART_BRIDGE_BUFFER_SIZE - 1U)
#define UART_ERROR_MASK          (USART_SR_PE | USART_SR_FE | USART_SR_NE | \
                                  USART_SR_ORE)

typedef struct {
    uint8_t data[UART_BRIDGE_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_bridge_ring_t;

static uart_bridge_ring_t s_pc_to_esp;
static uart_bridge_ring_t s_esp_to_pc;
static uart_bridge_ring_t s_esp_to_app;
static volatile bsp_uart_bridge_stats_t s_stats;
static volatile bool s_esp_claimed;

_Static_assert((UART_BRIDGE_BUFFER_SIZE & UART_BRIDGE_BUFFER_MASK) == 0U,
               "UART bridge buffer size must be a power of two");

static bool ring_push(uart_bridge_ring_t *ring, uint8_t value)
{
    const uint16_t next =
        (uint16_t)((ring->head + 1U) & UART_BRIDGE_BUFFER_MASK);

    if (next == ring->tail) {
        return false;
    }
    ring->data[ring->head] = value;
    ring->head = next;
    return true;
}

static bool ring_pop(uart_bridge_ring_t *ring, uint8_t *value)
{
    if (ring->tail == ring->head) {
        return false;
    }
    *value = ring->data[ring->tail];
    ring->tail =
        (uint16_t)((ring->tail + 1U) & UART_BRIDGE_BUFFER_MASK);
    return true;
}

static uint16_t ring_free(const uart_bridge_ring_t *ring)
{
    const uint16_t used =
        (uint16_t)((ring->head - ring->tail) & UART_BRIDGE_BUFFER_MASK);
    return (uint16_t)(UART_BRIDGE_BUFFER_MASK - used);
}

static uint32_t uart_brr(uint32_t peripheral_clock_hz,
                         uint32_t baud_rate)
{
    return (peripheral_clock_hz + (baud_rate / 2U)) / baud_rate;
}

static void configure_gpio_af7(GPIO_TypeDef *gpio,
                               uint8_t first_pin,
                               uint8_t second_pin)
{
    const uint32_t mode_mask =
        (3UL << (first_pin * 2U)) | (3UL << (second_pin * 2U));
    const uint32_t afr_first_position =
        (uint32_t)(first_pin - 8U) * 4U;
    const uint32_t afr_second_position =
        (uint32_t)(second_pin - 8U) * 4U;
    const uint32_t afr_mask =
        (0xFUL << afr_first_position) | (0xFUL << afr_second_position);

    gpio->MODER = (gpio->MODER & ~mode_mask) |
                  (2UL << (first_pin * 2U)) |
                  (2UL << (second_pin * 2U));
    gpio->OTYPER &= ~((1UL << first_pin) | (1UL << second_pin));
    gpio->OSPEEDR = (gpio->OSPEEDR & ~mode_mask) |
                    (3UL << (first_pin * 2U)) |
                    (3UL << (second_pin * 2U));
    gpio->PUPDR = (gpio->PUPDR & ~mode_mask) |
                  (1UL << (first_pin * 2U)) |
                  (1UL << (second_pin * 2U));
    gpio->AFR[1] = (gpio->AFR[1] & ~afr_mask) |
                   (7UL << afr_first_position) |
                   (7UL << afr_second_position);
}

void bsp_uart_bridge_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR;

    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->APB1RSTR |= RCC_APB1RSTR_USART3RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART3RST;

    configure_gpio_af7(GPIOA, 9U, 10U);
    configure_gpio_af7(GPIOB, 10U, 11U);

    USART1->BRR = uart_brr(BOARD_APB2_CLOCK_HZ,
                           DAP_VCOM_UART_BAUD_RATE);
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE;

    USART3->BRR = uart_brr(BOARD_APB1_CLOCK_HZ,
                           ESP8266_UART_BAUD_RATE);
    USART3->CR2 = 0U;
    USART3->CR3 = 0U;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE;

    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_SetPriority(USART3_IRQn, 6U);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(USART3_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART3_IRQn);
}

void bsp_uart_bridge_claim_esp(void)
{
    s_esp_claimed = true;
}

bool bsp_uart_bridge_esp_write(const uint8_t *data, size_t length)
{
    uint32_t primask;
    size_t index;

    if ((data == NULL) || (length == 0U) ||
        (length > UART_BRIDGE_BUFFER_MASK)) {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (ring_free(&s_pc_to_esp) < length) {
        s_stats.app_to_esp_dropped += (uint32_t)length;
        if (primask == 0U) {
            __enable_irq();
        }
        return false;
    }
    for (index = 0U; index < length; ++index) {
        (void)ring_push(&s_pc_to_esp, data[index]);
    }
    s_stats.app_to_esp_bytes += (uint32_t)length;
    USART3->CR1 |= USART_CR1_TXEIE;
    if (primask == 0U) {
        __enable_irq();
    }
    return true;
}

bool bsp_uart_bridge_esp_read(uint8_t *value)
{
    return (value != NULL) && ring_pop(&s_esp_to_app, value);
}

void bsp_uart_bridge_get_stats(bsp_uart_bridge_stats_t *stats)
{
    uint32_t primask;

    if (stats == NULL) {
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    *stats = s_stats;
    if (primask == 0U) {
        __enable_irq();
    }
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;
    uint8_t value;

    if ((status & (USART_SR_RXNE | UART_ERROR_MASK)) != 0U) {
        value = (uint8_t)USART1->DR;
        if ((status & UART_ERROR_MASK) != 0U) {
            ++s_stats.usart1_errors;
        }
        if (((status & USART_SR_RXNE) != 0U) && !s_esp_claimed) {
            if (ring_push(&s_pc_to_esp, value)) {
                ++s_stats.pc_to_esp_bytes;
                USART3->CR1 |= USART_CR1_TXEIE;
            } else {
                ++s_stats.pc_to_esp_dropped;
            }
        } else if ((status & USART_SR_RXNE) != 0U) {
            ++s_stats.pc_to_esp_dropped;
        }
    }

    if (((status & USART_SR_TXE) != 0U) &&
        ((USART1->CR1 & USART_CR1_TXEIE) != 0U)) {
        if (ring_pop(&s_esp_to_pc, &value)) {
            USART1->DR = value;
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

void USART3_IRQHandler(void)
{
    const uint32_t status = USART3->SR;
    uint8_t value;

    if ((status & (USART_SR_RXNE | UART_ERROR_MASK)) != 0U) {
        value = (uint8_t)USART3->DR;
        if ((status & UART_ERROR_MASK) != 0U) {
            ++s_stats.usart3_errors;
        }
        if ((status & USART_SR_RXNE) != 0U) {
            if (ring_push(&s_esp_to_pc, value)) {
                ++s_stats.esp_to_pc_bytes;
                USART1->CR1 |= USART_CR1_TXEIE;
            } else {
                ++s_stats.esp_to_pc_dropped;
            }
            if (ring_push(&s_esp_to_app, value)) {
                ++s_stats.esp_to_app_bytes;
            } else {
                ++s_stats.esp_to_app_dropped;
            }
        }
    }

    if (((status & USART_SR_TXE) != 0U) &&
        ((USART3->CR1 & USART_CR1_TXEIE) != 0U)) {
        if (ring_pop(&s_pc_to_esp, &value)) {
            USART3->DR = value;
        } else {
            USART3->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
