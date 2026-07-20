#include "vision.h"

#include <stddef.h>
#include <string.h>

static void skip_spaces(const uint8_t *line, uint16_t len, uint16_t *index)
{
    while ((*index < len) &&
           ((line[*index] == ' ') || (line[*index] == '\t'))) {
        (*index)++;
    }
}

static uint8_t is_vision_candidate(const uint8_t *line, uint16_t len)
{
    uint16_t index = 0U;
    uint8_t first;

    skip_spaces(line, len, &index);
    if (index >= len) {
        return 0U;
    }

    first = line[index];
    if ((first == 'V') || (first == 'v')) {
        return (((uint16_t)(index + 1U) < len) &&
                (line[index + 1U] == ',')) ? 1U : 0U;
    }
    if ((first != '+') && (first != '-') &&
        ((first < '0') || (first > '9'))) {
        return 0U;
    }

    while (index < len) {
        if (line[index] == ',') {
            return 1U;
        }
        index++;
    }

    return 0U;
}

static uint8_t parse_i32(const uint8_t *line,
                         uint16_t len,
                         uint16_t *index,
                         int32_t *value_out)
{
    int32_t value = 0;
    int32_t sign = 1;
    uint8_t has_digit = 0U;

    skip_spaces(line, len, index);
    if (*index < len) {
        if (line[*index] == '-') {
            sign = -1;
            (*index)++;
        } else if (line[*index] == '+') {
            (*index)++;
        }
    }

    while ((*index < len) &&
           (line[*index] >= '0') && (line[*index] <= '9')) {
        int32_t digit = (int32_t)(line[*index] - '0');
        has_digit = 1U;
        if ((value > 3276) ||
            ((value == 3276) && (digit > 8))) {
            return 0U;
        }
        value = value * 10 + digit;
        (*index)++;
    }

    if (has_digit == 0U) {
        return 0U;
    }

    *value_out = sign * value;
    return 1U;
}

static uint8_t consume_comma(const uint8_t *line,
                             uint16_t len,
                             uint16_t *index)
{
    skip_spaces(line, len, index);
    if ((*index >= len) || (line[*index] != ',')) {
        return 0U;
    }
    (*index)++;
    return 1U;
}

static uint8_t parse_u32(const uint8_t *line,
                         uint16_t len,
                         uint16_t *index,
                         uint32_t *value_out)
{
    uint32_t value = 0U;
    uint8_t has_digit = 0U;

    skip_spaces(line, len, index);
    while ((*index < len) &&
           (line[*index] >= '0') && (line[*index] <= '9')) {
        uint32_t digit = (uint32_t)(line[*index] - '0');

        has_digit = 1U;
        if ((value > 429496729U) ||
            ((value == 429496729U) && (digit > 5U))) {
            return 0U;
        }
        value = value * 10U + digit;
        (*index)++;
    }
    if (has_digit == 0U) {
        return 0U;
    }
    *value_out = value;
    return 1U;
}

void Vision_Init(Vision_Instance_t *inst)
{
    if (inst != NULL) {
        memset(inst, 0, sizeof(*inst));
    }
}

Vision_ParseResult_t Vision_ParseLine(Vision_Instance_t *inst,
                                      const uint8_t *line,
                                      uint16_t len)
{
    uint16_t index = 0U;
    int32_t dx;
    int32_t dy;
    int32_t valid;
    int32_t locked = 0;
    int32_t aim_ready = 0;
    uint32_t move_epoch = 0U;
    uint8_t extended = 0U;

    if ((inst == NULL) || (line == NULL) || (len == 0U)) {
        return VISION_PARSE_NOT_VISION;
    }

    while ((len > 0U) &&
           ((line[len - 1U] == '\r') || (line[len - 1U] == '\n'))) {
        len--;
    }
    if ((len == 0U) || (is_vision_candidate(line, len) == 0U)) {
        return VISION_PARSE_NOT_VISION;
    }

    inst->rx_line_count++;
    skip_spaces(line, len, &index);
    if ((index < len) &&
        ((line[index] == 'V') || (line[index] == 'v'))) {
        extended = 1U;
        index++;
        if (consume_comma(line, len, &index) == 0U) {
            inst->parse_err_count++;
            return VISION_PARSE_INVALID;
        }
    }
    if ((parse_i32(line, len, &index, &dx) == 0U) ||
        (consume_comma(line, len, &index) == 0U) ||
        (parse_i32(line, len, &index, &dy) == 0U) ||
        (consume_comma(line, len, &index) == 0U) ||
        (parse_i32(line, len, &index, &valid) == 0U)) {
        inst->parse_err_count++;
        return VISION_PARSE_INVALID;
    }

    if (extended != 0U) {
        if ((consume_comma(line, len, &index) == 0U) ||
            (parse_i32(line, len, &index, &locked) == 0U) ||
            (consume_comma(line, len, &index) == 0U) ||
            (parse_i32(line, len, &index, &aim_ready) == 0U) ||
            (consume_comma(line, len, &index) == 0U) ||
            (parse_u32(line, len, &index, &move_epoch) == 0U)) {
            inst->parse_err_count++;
            return VISION_PARSE_INVALID;
        }
    } else {
        locked = valid;
    }

    skip_spaces(line, len, &index);
    if ((index != len) ||
        (dx < -VISION_MAX_ABS_ERROR_PX) ||
        (dx > VISION_MAX_ABS_ERROR_PX) ||
        (dy < -VISION_MAX_ABS_ERROR_PX) ||
        (dy > VISION_MAX_ABS_ERROR_PX) ||
        ((valid != 0) && (valid != 1)) ||
        ((locked != 0) && (locked != 1)) ||
        ((aim_ready != 0) && (aim_ready != 1))) {
        inst->parse_err_count++;
        return VISION_PARSE_INVALID;
    }

    inst->publish_version++;
    inst->dx = (int16_t)dx;
    inst->dy = (int16_t)dy;
    inst->valid = (uint8_t)valid;
    inst->locked = (uint8_t)locked;
    inst->aim_ready = (uint8_t)aim_ready;
    inst->move_epoch = move_epoch;
    inst->frame_count++;
    inst->publish_version++;
    return VISION_PARSE_VALID;
}

uint32_t Vision_GetTaskSnapshot(const Vision_Instance_t *inst,
                                Vision_TaskSnapshot_t *snapshot)
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
        snapshot->dx = inst->dx;
        snapshot->dy = inst->dy;
        snapshot->valid = inst->valid;
        snapshot->locked = inst->locked;
        snapshot->aim_ready = inst->aim_ready;
        snapshot->move_epoch = inst->move_epoch;
        snapshot->frame_count = inst->frame_count;
        version_after = inst->publish_version;
        if ((version_before == version_after) &&
            ((version_after & 1U) == 0U)) {
            break;
        }
    }
    return snapshot->frame_count;
}

uint32_t Vision_GetSnapshot(const Vision_Instance_t *inst,
                            int16_t *dx,
                            int16_t *dy,
                            uint8_t *valid)
{
    uint32_t version_before;
    uint32_t version_after;
    uint32_t frame_count;
    int16_t local_dx;
    int16_t local_dy;
    uint8_t local_valid;

    if ((inst == NULL) || (dx == NULL) ||
        (dy == NULL) || (valid == NULL)) {
        return 0U;
    }

    for (;;) {
        version_before = inst->publish_version;
        if ((version_before & 1U) != 0U) {
            continue;
        }
        local_dx = inst->dx;
        local_dy = inst->dy;
        local_valid = inst->valid;
        frame_count = inst->frame_count;
        version_after = inst->publish_version;
        if ((version_before == version_after) &&
            ((version_after & 1U) == 0U)) {
            break;
        }
    }

    *dx = local_dx;
    *dy = local_dy;
    *valid = local_valid;
    return frame_count;
}
