#include "bsp_board_f407ve.h"

#include "lcd_demo_config.h"
#include "stm32f4xx.h"

typedef enum {
    CLOCK_WAIT_HSE = 0,
    CLOCK_WAIT_PLL,
    CLOCK_WAIT_SWITCH,
    CLOCK_DONE,
    CLOCK_FAILED
} clock_state_t;

static volatile uint32_t s_millis;
static clock_state_t s_clock_state;
static uint32_t s_clock_deadline;

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void systick_configure(uint32_t core_clock_hz)
{
    (void)SysTick_Config(core_clock_hz / 1000U);
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
}

void bsp_board_init(void)
{
    SystemCoreClock = BOARD_HSI_HZ;
    systick_configure(SystemCoreClock);

    RCC->CR |= RCC_CR_HSEON;
    s_clock_state = CLOCK_WAIT_HSE;
    s_clock_deadline = s_millis + BOARD_CLOCK_TIMEOUT_MS;
}

void bsp_board_poll(void)
{
    const uint32_t now = s_millis;

    switch (s_clock_state) {
    case CLOCK_WAIT_HSE:
        if ((RCC->CR & RCC_CR_HSERDY) != 0U) {
            RCC->APB1ENR |= RCC_APB1ENR_PWREN;
            PWR->CR |= PWR_CR_VOS;

            FLASH->ACR = FLASH_ACR_LATENCY_5WS |
                         FLASH_ACR_PRFTEN |
                         FLASH_ACR_ICEN |
                         FLASH_ACR_DCEN;

            RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE |
                                        RCC_CFGR_PPRE1 |
                                        RCC_CFGR_PPRE2)) |
                        RCC_CFGR_HPRE_DIV1 |
                        RCC_CFGR_PPRE1_DIV4 |
                        RCC_CFGR_PPRE2_DIV2;

            RCC->PLLCFGR = 25U |
                           (336U << RCC_PLLCFGR_PLLN_Pos) |
                           RCC_PLLCFGR_PLLSRC_HSE |
                           (7U << RCC_PLLCFGR_PLLQ_Pos);
            RCC->CR |= RCC_CR_PLLON;
            s_clock_state = CLOCK_WAIT_PLL;
            s_clock_deadline = now + BOARD_CLOCK_TIMEOUT_MS;
        } else if (deadline_reached(now, s_clock_deadline)) {
            s_clock_state = CLOCK_FAILED;
        }
        break;

    case CLOCK_WAIT_PLL:
        if ((RCC->CR & RCC_CR_PLLRDY) != 0U) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
            s_clock_state = CLOCK_WAIT_SWITCH;
            s_clock_deadline = now + BOARD_CLOCK_TIMEOUT_MS;
        } else if (deadline_reached(now, s_clock_deadline)) {
            s_clock_state = CLOCK_FAILED;
        }
        break;

    case CLOCK_WAIT_SWITCH:
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
            SystemCoreClock = BOARD_CORE_CLOCK_HZ;
            systick_configure(SystemCoreClock);
            s_clock_state = CLOCK_DONE;
        } else if (deadline_reached(now, s_clock_deadline)) {
            s_clock_state = CLOCK_FAILED;
        }
        break;

    case CLOCK_DONE:
    case CLOCK_FAILED:
    default:
        break;
    }
}

uint32_t bsp_board_millis(void)
{
    return s_millis;
}

bsp_board_clock_status_t bsp_board_clock_status(void)
{
    if (s_clock_state == CLOCK_DONE) {
        return BSP_BOARD_CLOCK_READY;
    }
    if (s_clock_state == CLOCK_FAILED) {
        return BSP_BOARD_CLOCK_ERROR;
    }
    return BSP_BOARD_CLOCK_STARTING;
}

void bsp_board_idle(void)
{
    __WFI();
}

void SysTick_Handler(void)
{
    ++s_millis;
}
