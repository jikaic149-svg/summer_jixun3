#ifndef BSP_CAN1_H
#define BSP_CAN1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t standard_id;
    uint8_t length;
    uint8_t data[8];
} bsp_can1_frame_t;

bool bsp_can1_init(void);
bool bsp_can1_send(const bsp_can1_frame_t *frame);
bool bsp_can1_receive(bsp_can1_frame_t *frame);

#endif
