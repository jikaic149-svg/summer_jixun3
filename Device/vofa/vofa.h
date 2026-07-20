#ifndef VOFA_H
#define VOFA_H

/**
 * @file    vofa.h
 * @brief   Device层 — VOFA+ 可视化协议输出
 */

#include <stdint.h>
#include <stdbool.h>

#define VOFA_MAX_CHANNELS         24U
#define VOFA_JUSTFLOAT_TAIL_SIZE  4U
#define VOFA_JUSTFLOAT_BUF_SIZE   ((VOFA_MAX_CHANNELS * sizeof(float)) + VOFA_JUSTFLOAT_TAIL_SIZE)
#define VOFA_TEXT_BUF_SIZE        256U
#define VOFA_TX_BUF_SIZE          VOFA_TEXT_BUF_SIZE

typedef bool (*Vofa_SendBytes_t)(const uint8_t *data, uint16_t len);

typedef struct
{
    Vofa_SendBytes_t send_bytes;
    uint8_t tx_buf[VOFA_TX_BUF_SIZE];
    volatile uint32_t tx_count;
    volatile uint32_t tx_drop_count;
} Vofa_Instance_t;

void Vofa_Init(Vofa_Instance_t *inst, Vofa_SendBytes_t send_fn);
bool Vofa_SendJustFloat(Vofa_Instance_t *inst,
                        const float *channels,
                        uint8_t channel_count);
bool Vofa_SendCsvFloat(Vofa_Instance_t *inst,
                       const float *channels,
                       uint8_t channel_count);
bool Vofa_SendText(Vofa_Instance_t *inst, const char *text);

#endif /* VOFA_H */
