#include "bsp_xpt2046_io.h"

#include "stm32f4xx.h"

#define TOUCH_CS_PIN     13U
#define TOUCH_SCK_PIN     0U
#define TOUCH_MOSI_PIN    2U
#define TOUCH_MISO_PIN    3U
#define TOUCH_PEN_PIN     4U

#define TOUCH_SPI_HALF_PERIOD_LOOPS    60U
#define TOUCH_CONVERSION_DELAY_LOOPS  300U

static void gpio_output(GPIO_TypeDef *gpio, uint32_t pin)
{
    gpio->MODER = (gpio->MODER & ~(3UL << (pin * 2U))) |
                  (1UL << (pin * 2U));
    gpio->OTYPER &= ~(1UL << pin);
    gpio->OSPEEDR = (gpio->OSPEEDR & ~(3UL << (pin * 2U))) |
                    (1UL << (pin * 2U));
    gpio->PUPDR &= ~(3UL << (pin * 2U));
}
static void gpio_input_pullup(GPIO_TypeDef *gpio, uint32_t pin)
{
    gpio->MODER &= ~(3UL << (pin * 2U));
    gpio->PUPDR = (gpio->PUPDR & ~(3UL << (pin * 2U))) |
                  (1UL << (pin * 2U));
}

static void spi_half_period(void)
{
    uint32_t count;
    for (count = 0U; count < TOUCH_SPI_HALF_PERIOD_LOOPS; ++count) {
        __NOP();
    }
}

static void conversion_delay(void)
{
    uint32_t count;

    /*
     * The touch interface is started only after SYSCLK reaches 168 MHz.
     * In the Release image this loop takes at least 5 us.  It deliberately
     * follows the vendor's known-good XPT2046 BUSY/dummy-clock timing.
     */
    for (count = 0U; count < TOUCH_CONVERSION_DELAY_LOOPS; ++count) {
        __NOP();
    }
}

void bsp_xpt2046_io_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;

    GPIOD->BSRR = 1UL << TOUCH_CS_PIN;
    GPIOE->BSRR = (1UL << (TOUCH_SCK_PIN + 16U)) |
                  (1UL << (TOUCH_MOSI_PIN + 16U));

    gpio_output(GPIOD, TOUCH_CS_PIN);
    gpio_output(GPIOE, TOUCH_SCK_PIN);
    gpio_output(GPIOE, TOUCH_MOSI_PIN);
    gpio_input_pullup(GPIOE, TOUCH_MISO_PIN);
    gpio_input_pullup(GPIOE, TOUCH_PEN_PIN);
}

bool bsp_xpt2046_pen_is_active(void)
{
    return (GPIOE->IDR & (1UL << TOUCH_PEN_PIN)) == 0U;
}

void bsp_xpt2046_select(bool selected)
{
    GPIOD->BSRR = selected ?
                   (1UL << (TOUCH_CS_PIN + 16U)) :
                   (1UL << TOUCH_CS_PIN);
}

uint8_t bsp_xpt2046_transfer(uint8_t output)
{
    uint8_t input = 0U;
    uint8_t bit;

    for (bit = 0U; bit < 8U; ++bit) {
        if ((output & 0x80U) != 0U) {
            GPIOE->BSRR = 1UL << TOUCH_MOSI_PIN;
        } else {
            GPIOE->BSRR = 1UL << (TOUCH_MOSI_PIN + 16U);
        }

        spi_half_period();
        GPIOE->BSRR = 1UL << TOUCH_SCK_PIN;
        spi_half_period();

        input = (uint8_t)(input << 1U);
        if ((GPIOE->IDR & (1UL << TOUCH_MISO_PIN)) != 0U) {
            input |= 1U;
        }

        GPIOE->BSRR = 1UL << (TOUCH_SCK_PIN + 16U);
        output = (uint8_t)(output << 1U);
    }

    return input;
}

uint16_t bsp_xpt2046_receive_12bit(void)
{
    uint16_t input = 0U;
    uint8_t bit;

    /* Complete the command's final low half-cycle before the BUSY clock. */
    spi_half_period();

    /* Discard the BUSY/null bit and allow the conversion to settle. */
    GPIOE->BSRR = 1UL << TOUCH_SCK_PIN;
    conversion_delay();
    GPIOE->BSRR = 1UL << (TOUCH_SCK_PIN + 16U);
    conversion_delay();

    for (bit = 0U; bit < 12U; ++bit) {
        input = (uint16_t)(input << 1U);
        GPIOE->BSRR = 1UL << TOUCH_SCK_PIN;
        spi_half_period();
        if ((GPIOE->IDR & (1UL << TOUCH_MISO_PIN)) != 0U) {
            input |= 1U;
        }
        GPIOE->BSRR = 1UL << (TOUCH_SCK_PIN + 16U);
        spi_half_period();
    }

    return (uint16_t)(input & 0x0FFFU);
}
