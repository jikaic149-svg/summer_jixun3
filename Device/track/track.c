/**
 * @file track.c
 * @brief 8路红外模拟循迹模块驱动实现
 */
#include "track.h"
#include <string.h>

static Track_Instance *track_bound_inst = NULL;

/* 2026-07-10 daylight VOFA calibration at the normal running height. */
static const uint16_t TRACK_WHITE_REFERENCE[8] = {
    1286, 1463, 1166, 1228, 993, 1051, 1326, 1079
};
static const uint16_t TRACK_BLACK_REFERENCE[8] = {
    2783, 2857, 2718, 2773, 2681, 2738, 2836, 2657
};
static const float TRACK_LINE_CONTRAST_DETECT_RATIO = 0.08f;
static const float TRACK_BLOCK_BLACK_DETECT_RATIO = 0.55f;
static const float TRACK_WEIGHTS[8] = {
    -9.0f, -5.0f, -2.0f, -1.0f, 1.0f, 2.0f, 5.0f, 9.0f
};

static float track_black_strength(uint8_t sensor, uint16_t adc)
{
    float white = (float)TRACK_WHITE_REFERENCE[sensor];
    float span = (float)(TRACK_BLACK_REFERENCE[sensor] -
                         TRACK_WHITE_REFERENCE[sensor]);
    float strength = ((float)adc - white) / span;

    if (strength < 0.0f) {
        return 0.0f;
    }
    if (strength > 1.0f) {
        return 1.0f;
    }
    return strength;
}

static float track_common_strength(const float strength[TRACK_SENSOR_COUNT])
{
    float min_1 = strength[0];
    float min_2 = strength[1];

    if (min_2 < min_1) {
        float temp = min_1;
        min_1 = min_2;
        min_2 = temp;
    }

    for (uint8_t i = 2U; i < TRACK_SENSOR_COUNT; i++) {
        if (strength[i] < min_1) {
            min_2 = min_1;
            min_1 = strength[i];
        } else if (strength[i] < min_2) {
            min_2 = strength[i];
        }
    }

    return 0.5f * (min_1 + min_2);
}

static uint8_t track_black_mask_from_adc(
    const uint16_t adc_value[TRACK_SENSOR_COUNT])
{
    uint8_t black_mask = 0U;

    for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
        if (track_black_strength(i, adc_value[i]) >
            TRACK_BLOCK_BLACK_DETECT_RATIO) {
            black_mask |= (uint8_t)(1U << i);
        }
    }

    return black_mask;
}

static uint8_t track_black_count_from_mask(uint8_t black_mask)
{
    uint8_t black_count = 0U;

    while (black_mask != 0U) {
        black_count += (uint8_t)(black_mask & 1U);
        black_mask >>= 1U;
    }
    return black_count;
}

static float track_error_from_adc(
    const uint16_t adc_value[TRACK_SENSOR_COUNT])
{
    float strength[TRACK_SENSOR_COUNT];
    float common_strength;
    float difference = 0.0f;
    float sum = 0.0f;

    for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
        strength[i] = track_black_strength(i, adc_value[i]);
    }

    if (track_black_count_from_mask(
            track_black_mask_from_adc(adc_value)) >= 7U) {
        return 0.0f;
    }

    common_strength = track_common_strength(strength);
    for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
        float contrast = strength[i] - common_strength;

        if (contrast > TRACK_LINE_CONTRAST_DETECT_RATIO) {
            float signal = contrast - TRACK_LINE_CONTRAST_DETECT_RATIO;
            difference += signal * TRACK_WEIGHTS[i];
            sum += signal;
        }
    }

    if (sum <= 0.001f) {
        return TRACK_LOST_LINE;
    }

    return difference / sum;
}

static void track_publish_adc(Track_Instance *inst,
                              const uint16_t adc_value[TRACK_SENSOR_COUNT])
{
    inst->publish_version++;
    for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
        inst->adc_value[i] = adc_value[i];
    }
    inst->frame_count++;
    inst->data_ready = 1U;
    inst->publish_version++;
}

void Track_Init(Track_Instance *inst, Track_UartSend_t send_fn)
{
    if (inst == NULL) {
        return;
    }

    inst->uart_send = send_fn;
    inst->rx_len = 0U;
    inst->publish_version = 0U;
    inst->data_ready = 0U;
    inst->frame_count = 0U;
    inst->rx_byte_count = 0U;
    inst->parse_error_count = 0U;
    for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
        inst->adc_value[i] = 0U;
    }
    memset(inst->rx_buf, 0, sizeof(inst->rx_buf));
    track_bound_inst = inst;

    /* 发送启动命令 "$0,1,0#" 让循迹模块开始输出数据 */
    /* 必须用 static，否则 DMA 还没传完数据函数就返回了，栈变量会被覆盖！ */
    if (inst->uart_send) {
        static const uint8_t cmd[] = "$0,1,0#";
        inst->uart_send((uint8_t *)cmd, sizeof(cmd) - 1);
    }
}

void Track_ParsePacket(Track_Instance *inst, const uint8_t *data, uint16_t len)
{
    const uint8_t *p;
    const uint8_t *end;
    uint16_t parsed_adc[TRACK_SENSOR_COUNT];

    if (inst == NULL) {
        return;
    }
    if ((data == NULL) || (len < 8U)) {
        inst->parse_error_count++;
        return;
    }

    p = data;
    end = data + len;
    if ((p[0] != '$') || (p[1] != 'A') || (p[2] != ',')) {
        inst->parse_error_count++;
        return;
    }
    p += 3;

    for (uint8_t channel = 0U; channel < TRACK_SENSOR_COUNT; channel++) {
        uint32_t value = 0U;
        uint8_t digit_count = 0U;

        if (((uint32_t)(end - p) < 4U) ||
            (p[0] != 'x') ||
            (p[1] != (uint8_t)('1' + channel)) ||
            (p[2] != ':')) {
            inst->parse_error_count++;
            return;
        }
        p += 3;

        while ((p < end) && (*p >= '0') && (*p <= '9')) {
            value = value * 10U + (uint32_t)(*p - '0');
            digit_count++;
            if (value > TRACK_ADC_MAX_VALUE) {
                inst->parse_error_count++;
                return;
            }
            p++;
        }
        if (digit_count == 0U) {
            inst->parse_error_count++;
            return;
        }
        parsed_adc[channel] = (uint16_t)value;

        if (channel < (TRACK_SENSOR_COUNT - 1U)) {
            if ((p >= end) || (*p != ',')) {
                inst->parse_error_count++;
                return;
            }
        } else if ((p >= end) || (*p != '#')) {
            inst->parse_error_count++;
            return;
        }
        p++;
    }

    if (p != end) {
        inst->parse_error_count++;
        return;
    }

    track_publish_adc(inst, parsed_adc);
}

void Track_OnRxByte(uint8_t byte)
{
    Track_Instance *inst = track_bound_inst;

    if (inst == NULL) {
        return;
    }

    inst->rx_byte_count++;

    if (byte == '$') {
        if (inst->rx_len != 0U) {
            inst->parse_error_count++;
        }
        inst->rx_len = 0U;
    } else if (inst->rx_len == 0U) {
        return;
    }

    if (inst->rx_len < sizeof(inst->rx_buf)) {
        inst->rx_buf[inst->rx_len++] = byte;
    } else {
        inst->rx_len = 0U;
        inst->parse_error_count++;
        return;
    }

    if (byte == '#') {
        Track_ParsePacket(inst, inst->rx_buf, inst->rx_len);
        inst->rx_len = 0U;
    }
}

uint8_t Track_GetSnapshot(const Track_Instance *inst, Track_Snapshot_t *snapshot)
{
    uint32_t version_before;
    uint32_t version_after;

    if ((inst == NULL) || (snapshot == NULL)) {
        return 0U;
    }

    for (;;) {
        version_before = inst->publish_version;
        if ((version_before & 1U) != 0U) {
            continue;
        }
        for (uint8_t i = 0U; i < TRACK_SENSOR_COUNT; i++) {
            snapshot->adc_value[i] = inst->adc_value[i];
        }
        snapshot->frame_count = inst->frame_count;
        version_after = inst->publish_version;
        if ((version_before == version_after) &&
            ((version_after & 1U) == 0U)) {
            break;
        }
    }

    snapshot->black_mask = track_black_mask_from_adc(snapshot->adc_value);
    snapshot->black_count = track_black_count_from_mask(snapshot->black_mask);
    snapshot->error = track_error_from_adc(snapshot->adc_value);
    snapshot->version = version_after;
    return 1U;
}

float Track_GetError(Track_Instance *inst)
{
    Track_Snapshot_t snapshot;

    if (Track_GetSnapshot(inst, &snapshot) == 0U) {
        return TRACK_LOST_LINE;
    }
    return snapshot.error;
}

uint8_t Track_GetBlackCount(Track_Instance *inst)
{
    Track_Snapshot_t snapshot;

    if (Track_GetSnapshot(inst, &snapshot) == 0U) {
        return 0U;
    }
    return snapshot.black_count;
}

uint32_t Track_GetFrameCount(const Track_Instance *inst)
{
    return (inst != NULL) ? inst->frame_count : 0U;
}

uint32_t Track_GetRxByteCount(const Track_Instance *inst)
{
    return (inst != NULL) ? inst->rx_byte_count : 0U;
}
