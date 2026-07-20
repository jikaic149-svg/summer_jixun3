#include "vofa.h"
#include <string.h>

static const uint8_t k_justfloat_tail[VOFA_JUSTFLOAT_TAIL_SIZE] = {
    0x00, 0x00, 0x80, 0x7F
};

static bool vofa_append_char(uint8_t *buf, uint16_t buf_size, uint16_t *idx, char ch)
{
    if (*idx >= buf_size) {
        return false;
    }
    buf[*idx] = (uint8_t)ch;
    (*idx)++;
    return true;
}

static bool vofa_append_u32(uint8_t *buf, uint16_t buf_size, uint16_t *idx, uint32_t value)
{
    char tmp[10];
    uint8_t count = 0U;

    if (value == 0U) {
        return vofa_append_char(buf, buf_size, idx, '0');
    }

    while (value > 0U && count < sizeof(tmp)) {
        tmp[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count > 0U) {
        if (!vofa_append_char(buf, buf_size, idx, tmp[--count])) {
            return false;
        }
    }

    return true;
}

static bool vofa_append_fixed3(uint8_t *buf, uint16_t buf_size, uint16_t *idx, float value)
{
    uint32_t scaled;
    uint32_t int_part;
    uint32_t frac_part;

    if (value < 0.0f) {
        if (!vofa_append_char(buf, buf_size, idx, '-')) {
            return false;
        }
        value = -value;
    }

    scaled = (uint32_t)(value * 1000.0f + 0.5f);
    int_part = scaled / 1000U;
    frac_part = scaled % 1000U;

    if (!vofa_append_u32(buf, buf_size, idx, int_part) ||
        !vofa_append_char(buf, buf_size, idx, '.') ||
        !vofa_append_char(buf, buf_size, idx, (char)('0' + (frac_part / 100U))) ||
        !vofa_append_char(buf, buf_size, idx, (char)('0' + ((frac_part / 10U) % 10U))) ||
        !vofa_append_char(buf, buf_size, idx, (char)('0' + (frac_part % 10U)))) {
        return false;
    }

    return true;
}

void Vofa_Init(Vofa_Instance_t *inst, Vofa_SendBytes_t send_fn)
{
    memset(inst, 0, sizeof(*inst));
    inst->send_bytes = send_fn;
}

bool Vofa_SendJustFloat(Vofa_Instance_t *inst,
                        const float *channels,
                        uint8_t channel_count)
{
    uint16_t payload_len;
    uint16_t frame_len;

    if (inst == NULL || inst->send_bytes == NULL ||
        channels == NULL || channel_count == 0U ||
        channel_count > VOFA_MAX_CHANNELS) {
        return false;
    }

    payload_len = (uint16_t)channel_count * (uint16_t)sizeof(float);
    frame_len = payload_len + VOFA_JUSTFLOAT_TAIL_SIZE;
    if (frame_len > sizeof(inst->tx_buf)) {
        inst->tx_drop_count++;
        return false;
    }

    memcpy(inst->tx_buf, channels, payload_len);
    memcpy(inst->tx_buf + payload_len, k_justfloat_tail, VOFA_JUSTFLOAT_TAIL_SIZE);

    if (!inst->send_bytes(inst->tx_buf, frame_len)) {
        inst->tx_drop_count++;
        return false;
    }

    inst->tx_count++;
    return true;
}

bool Vofa_SendCsvFloat(Vofa_Instance_t *inst,
                       const float *channels,
                       uint8_t channel_count)
{
    uint16_t idx = 0U;
    uint8_t i;

    if (inst == NULL || inst->send_bytes == NULL ||
        channels == NULL || channel_count == 0U ||
        channel_count > VOFA_MAX_CHANNELS) {
        return false;
    }

    for (i = 0U; i < channel_count; i++) {
        if (i > 0U && !vofa_append_char(inst->tx_buf, sizeof(inst->tx_buf), &idx, ',')) {
            inst->tx_drop_count++;
            return false;
        }
        if (!vofa_append_fixed3(inst->tx_buf, sizeof(inst->tx_buf), &idx, channels[i])) {
            inst->tx_drop_count++;
            return false;
        }
    }

    if (!vofa_append_char(inst->tx_buf, sizeof(inst->tx_buf), &idx, '\r') ||
        !vofa_append_char(inst->tx_buf, sizeof(inst->tx_buf), &idx, '\n')) {
        inst->tx_drop_count++;
        return false;
    }

    if (!inst->send_bytes(inst->tx_buf, idx)) {
        inst->tx_drop_count++;
        return false;
    }

    inst->tx_count++;
    return true;
}

bool Vofa_SendText(Vofa_Instance_t *inst, const char *text)
{
    uint16_t len;

    if (inst == NULL || inst->send_bytes == NULL || text == NULL) {
        return false;
    }

    len = (uint16_t)strlen(text);
    if (len == 0U) {
        return false;
    }

    if (!inst->send_bytes((const uint8_t *)text, len)) {
        inst->tx_drop_count++;
        return false;
    }

    inst->tx_count++;
    return true;
}
