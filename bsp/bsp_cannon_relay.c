#include "bsp_cannon_relay.h"

#include <stdint.h>

#include "lcd_demo_config.h"
#include "stm32f4xx.h"

#define FIRE_RELAY_PIN     0U
#define CHARGE_RELAY_PIN   1U
#define FIRE_RELAY_MASK    (1UL << FIRE_RELAY_PIN)
#define CHARGE_RELAY_MASK  (1UL << CHARGE_RELAY_PIN)

static void relay_write(uint32_t mask, bool enabled, uint32_t active_low)
{
    const bool drive_high = (active_low != 0U) ? !enabled : enabled;

    GPIOB->BSRR = drive_high ? mask : (mask << 16U);
}

void bsp_cannon_relay_init(void)
{
    const uint32_t mode_mask =
        (3UL << (FIRE_RELAY_PIN * 2U)) |
        (3UL << (CHARGE_RELAY_PIN * 2U));
    const uint32_t pull =
        ((CANNON_FIRE_RELAY_ACTIVE_LOW != 0U ? 1UL : 2UL) <<
         (FIRE_RELAY_PIN * 2U)) |
        ((CANNON_CHARGE_RELAY_ACTIVE_LOW != 0U ? 1UL : 2UL) <<
         (CHARGE_RELAY_PIN * 2U));

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    /* Establish both inactive levels before changing PB0/PB1 to outputs. */
    relay_write(FIRE_RELAY_MASK, false, CANNON_FIRE_RELAY_ACTIVE_LOW);
    relay_write(CHARGE_RELAY_MASK, false, CANNON_CHARGE_RELAY_ACTIVE_LOW);

    GPIOB->OTYPER &= ~(FIRE_RELAY_MASK | CHARGE_RELAY_MASK);
    GPIOB->OSPEEDR &= ~mode_mask;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~mode_mask) | pull;
    GPIOB->MODER = (GPIOB->MODER & ~mode_mask) |
                   (1UL << (FIRE_RELAY_PIN * 2U)) |
                   (1UL << (CHARGE_RELAY_PIN * 2U));
}

void bsp_cannon_relay_set_fire(bool enabled)
{
    relay_write(FIRE_RELAY_MASK, enabled, CANNON_FIRE_RELAY_ACTIVE_LOW);
}

void bsp_cannon_relay_set_charge(bool enabled)
{
    relay_write(CHARGE_RELAY_MASK, enabled,
                CANNON_CHARGE_RELAY_ACTIVE_LOW);
}

void bsp_cannon_relay_all_off(void)
{
    bsp_cannon_relay_set_fire(false);
    bsp_cannon_relay_set_charge(false);
}
