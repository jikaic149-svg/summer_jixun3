#include "bsp_lcd_fsmc.h"

#include "lcd_demo_config.h"
#include "stm32f4xx.h"

#define LCD_COMMAND_ADDRESS  ((volatile uint16_t *)0x60000000UL)
#define LCD_DATA_ADDRESS     ((volatile uint16_t *)0x60020000UL)

static bool s_initialized;

static void gpio_configure_af(GPIO_TypeDef *gpio, uint32_t pins, uint32_t af)
{
    uint32_t pin;

    for (pin = 0U; pin < 16U; ++pin) {
        const uint32_t mask = 1UL << pin;
        if ((pins & mask) == 0U) {
            continue;
        }

        gpio->MODER = (gpio->MODER & ~(3UL << (pin * 2U))) |
                      (2UL << (pin * 2U));
        gpio->OTYPER &= ~mask;
        gpio->OSPEEDR |= 3UL << (pin * 2U);
        gpio->PUPDR = (gpio->PUPDR & ~(3UL << (pin * 2U))) |
                      (1UL << (pin * 2U));

        if (pin < 8U) {
            gpio->AFR[0] = (gpio->AFR[0] & ~(0xFUL << (pin * 4U))) |
                           (af << (pin * 4U));
        } else {
            const uint32_t index = pin - 8U;
            gpio->AFR[1] = (gpio->AFR[1] & ~(0xFUL << (index * 4U))) |
                           (af << (index * 4U));
        }
    }
}
static void backlight_gpio_init(void)
{
    const uint32_t pin = 15U;

    GPIOA->BSRR = 1UL << (pin + 16U);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << (pin * 2U))) |
                   (1UL << (pin * 2U));
    GPIOA->OTYPER &= ~(1UL << pin);
    GPIOA->OSPEEDR |= 2UL << (pin * 2U);
    GPIOA->PUPDR &= ~(3UL << (pin * 2U));
}

void bsp_lcd_fsmc_init(void)
{
    const uint32_t pins_d = GPIO_ODR_OD0 |
                            GPIO_ODR_OD1 |
                            GPIO_ODR_OD4 |
                            GPIO_ODR_OD5 |
                            GPIO_ODR_OD7 |
                            GPIO_ODR_OD8 |
                            GPIO_ODR_OD9 |
                            GPIO_ODR_OD10 |
                            GPIO_ODR_OD11 |
                            GPIO_ODR_OD14 |
                            GPIO_ODR_OD15;
    const uint32_t pins_e = 0xFF80UL;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIODEN |
                    RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;

    backlight_gpio_init();
    gpio_configure_af(GPIOD, pins_d, 12U);
    gpio_configure_af(GPIOE, pins_e, 12U);

    RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;
    (void)RCC->AHB3ENR;

    FSMC_Bank1->BTCR[0] = FSMC_BCR1_MWID_0 |
                          FSMC_BCR1_WREN |
                          FSMC_BCR1_EXTMOD;
    FSMC_Bank1->BTCR[1] = LCD_FSMC_READ_ADDSET |
                          (LCD_FSMC_READ_DATAST << FSMC_BTR1_DATAST_Pos);
    FSMC_Bank1E->BWTR[0] = LCD_FSMC_WRITE_ADDSET |
                           (LCD_FSMC_WRITE_DATAST << FSMC_BWTR1_DATAST_Pos);
    FSMC_Bank1->BTCR[0] |= FSMC_BCR1_MBKEN;

    __DSB();
    s_initialized = true;
}

bool bsp_lcd_fsmc_is_initialized(void)
{
    return s_initialized;
}

void bsp_lcd_fsmc_backlight(bool enabled)
{
    GPIOA->BSRR = enabled ? GPIO_BSRR_BS15 : GPIO_BSRR_BR15;
}

void bsp_lcd_fsmc_write_command(uint8_t command)
{
    *LCD_COMMAND_ADDRESS = command;
}

void bsp_lcd_fsmc_write_data8(uint8_t data)
{
    *LCD_DATA_ADDRESS = data;
}

void bsp_lcd_fsmc_write_data16(uint16_t data)
{
    *LCD_DATA_ADDRESS = data;
}

uint16_t bsp_lcd_fsmc_read_data16(void)
{
    return *LCD_DATA_ADDRESS;
}

void bsp_lcd_fsmc_write_pixels(const uint16_t *pixels, size_t count)
{
    while (count > 0U) {
        *LCD_DATA_ADDRESS = *pixels;
        ++pixels;
        --count;
    }
}

void bsp_lcd_fsmc_fill_pixels(uint16_t color, size_t count)
{
    while (count > 0U) {
        *LCD_DATA_ADDRESS = color;
        --count;
    }
}
