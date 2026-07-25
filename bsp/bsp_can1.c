#include "bsp_can1.h"

#include <stddef.h>

#include "stm32f4xx.h"

#define CAN_INIT_POLL_LIMIT  1000000UL

static bool wait_for_mask(volatile const uint32_t *reg,
                          uint32_t mask,
                          bool set)
{
    uint32_t remaining = CAN_INIT_POLL_LIMIT;

    while (remaining > 0U) {
        if (((*reg & mask) != 0U) == set) {
            return true;
        }
        --remaining;
    }
    return false;
}

bool bsp_can1_init(void)
{
    uint32_t alternate;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;

    GPIOB->MODER = (GPIOB->MODER &
                    ~((3UL << GPIO_MODER_MODER8_Pos) |
                      (3UL << GPIO_MODER_MODER9_Pos))) |
                   (2UL << GPIO_MODER_MODER8_Pos) |
                   (2UL << GPIO_MODER_MODER9_Pos);
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9);
    GPIOB->OSPEEDR |= (3UL << GPIO_OSPEEDR_OSPEED8_Pos) |
                     (3UL << GPIO_OSPEEDR_OSPEED9_Pos);
    GPIOB->PUPDR &= ~((3UL << GPIO_PUPDR_PUPD8_Pos) |
                      (3UL << GPIO_PUPDR_PUPD9_Pos));
    alternate = GPIOB->AFR[1];
    alternate &= ~((0xFUL << GPIO_AFRH_AFSEL8_Pos) |
                   (0xFUL << GPIO_AFRH_AFSEL9_Pos));
    alternate |= (9UL << GPIO_AFRH_AFSEL8_Pos) |
                 (9UL << GPIO_AFRH_AFSEL9_Pos);
    GPIOB->AFR[1] = alternate;

    RCC->APB1RSTR |= RCC_APB1RSTR_CAN1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_CAN1RST;

    /* One-shot transmission avoids an unbounded retry when the bus is open. */
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_NART;
    if (!wait_for_mask(&CAN1->MSR, CAN_MSR_INAK, true)) {
        return false;
    }

    /* Board example: APB1=42 MHz, prescaler=6, BS1=5, BS2=1: 1 Mbit/s. */
    CAN1->BTR = (4UL << CAN_BTR_TS1_Pos) | 5UL;

    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~CAN_FA1R_FACT0;
    CAN1->FM1R &= ~CAN_FM1R_FBM0;
    CAN1->FS1R |= CAN_FS1R_FSC0;
    CAN1->FFA1R &= ~CAN_FFA1R_FFA0;
    CAN1->sFilterRegister[0].FR1 = 0U;
    CAN1->sFilterRegister[0].FR2 = 0U;
    CAN1->FA1R |= CAN_FA1R_FACT0;
    CAN1->FMR &= ~CAN_FMR_FINIT;

    CAN1->MCR &= ~CAN_MCR_INRQ;
    return wait_for_mask(&CAN1->MSR, CAN_MSR_INAK, false);
}

bool bsp_can1_send(const bsp_can1_frame_t *frame)
{
    uint32_t low = 0U;
    uint32_t high = 0U;
    uint32_t completion_mask;
    uint8_t index;
    uint8_t mailbox;

    if ((frame == NULL) || (frame->standard_id > 0x7FFU) ||
        (frame->length > 8U)) {
        return false;
    }

    if ((CAN1->TSR & CAN_TSR_TME0) != 0U) {
        mailbox = 0U;
    } else if ((CAN1->TSR & CAN_TSR_TME1) != 0U) {
        mailbox = 1U;
    } else if ((CAN1->TSR & CAN_TSR_TME2) != 0U) {
        mailbox = 2U;
    } else {
        return false;
    }

    for (index = 0U; (index < frame->length) && (index < 4U); ++index) {
        low |= (uint32_t)frame->data[index] << (8U * index);
    }
    for (; index < frame->length; ++index) {
        high |= (uint32_t)frame->data[index] << (8U * (index - 4U));
    }

    completion_mask = CAN_TSR_RQCP0 << (8U * mailbox);
    CAN1->TSR = completion_mask;
    CAN1->sTxMailBox[mailbox].TDTR = frame->length;
    CAN1->sTxMailBox[mailbox].TDLR = low;
    CAN1->sTxMailBox[mailbox].TDHR = high;
    CAN1->sTxMailBox[mailbox].TIR =
        ((uint32_t)frame->standard_id << 21U) | CAN_TI0R_TXRQ;
    return true;
}

bool bsp_can1_receive(bsp_can1_frame_t *frame)
{
    uint32_t low;
    uint32_t high;
    uint8_t index;

    if ((frame == NULL) || ((CAN1->RF0R & CAN_RF0R_FMP0) == 0U)) {
        return false;
    }

    frame->standard_id =
        (uint16_t)((CAN1->sFIFOMailBox[0].RIR >> 21U) & 0x7FFU);
    frame->length = (uint8_t)(CAN1->sFIFOMailBox[0].RDTR & 0x0FU);
    if (frame->length > 8U) {
        frame->length = 8U;
    }
    low = CAN1->sFIFOMailBox[0].RDLR;
    high = CAN1->sFIFOMailBox[0].RDHR;
    for (index = 0U; (index < frame->length) && (index < 4U); ++index) {
        frame->data[index] = (uint8_t)(low >> (8U * index));
    }
    for (; index < frame->length; ++index) {
        frame->data[index] = (uint8_t)(high >> (8U * (index - 4U)));
    }
    CAN1->RF0R |= CAN_RF0R_RFOM0;
    return true;
}
