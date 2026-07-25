#include "gimbal_link_protocol.h"

#include <limits.h>
#include <string.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool valid;
} text_builder_t;

static void append_byte(text_builder_t *builder, char value)
{
    if (!builder->valid || (builder->length >= builder->capacity)) {
        builder->valid = false;
        return;
    }
    builder->data[builder->length++] = value;
}

static void append_text(text_builder_t *builder, const char *text)
{
    if (text == NULL) {
        builder->valid = false;
        return;
    }
    while (*text != '\0') {
        append_byte(builder, *text++);
    }
}

static void append_uint(text_builder_t *builder, uint32_t value)
{
    char reverse[10];
    size_t count = 0U;

    do {
        reverse[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value > 0U) && (count < sizeof(reverse)));
    while (count > 0U) {
        append_byte(builder, reverse[--count]);
    }
}

static void append_int(text_builder_t *builder, int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        append_byte(builder, '-');
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }
    append_uint(builder, magnitude);
}

static void append_field_prefix(text_builder_t *builder,
                                bool *first,
                                const char *key)
{
    if (!*first) {
        append_byte(builder, '|');
    }
    *first = false;
    append_text(builder, key);
    append_byte(builder, '=');
}

static void append_field_text(text_builder_t *builder,
                              bool *first,
                              const char *key,
                              const char *value)
{
    append_field_prefix(builder, first, key);
    append_text(builder, value);
}

static void append_field_uint(text_builder_t *builder,
                              bool *first,
                              const char *key,
                              uint32_t value)
{
    append_field_prefix(builder, first, key);
    append_uint(builder, value);
}

static void append_field_int(text_builder_t *builder,
                             bool *first,
                             const char *key,
                             int32_t value)
{
    append_field_prefix(builder, first, key);
    append_int(builder, value);
}

uint16_t gimbal_link_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t index;
    uint8_t bit;

    if (data == NULL) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U) ?
                  (uint16_t)((crc << 1U) ^ 0x1021U) :
                  (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static size_t encode_frame(uint8_t *output,
                           size_t capacity,
                           const char *kind,
                           uint16_t sequence,
                           const char *payload,
                           size_t payload_length)
{
    static const char hex[] = "0123456789ABCDEF";
    text_builder_t frame = {(char *)output, capacity, 0U, true};
    uint16_t crc;

    append_text(&frame, "JX1|");
    append_text(&frame, kind);
    append_byte(&frame, '|');
    append_uint(&frame, sequence);
    append_byte(&frame, '|');
    append_uint(&frame, (uint32_t)payload_length);
    if (payload_length > 0U) {
        append_byte(&frame, '|');
        if ((payload == NULL) ||
            (frame.length + payload_length > frame.capacity)) {
            frame.valid = false;
        } else {
            memcpy(&frame.data[frame.length], payload, payload_length);
            frame.length += payload_length;
        }
    }
    if (!frame.valid || (frame.length + 6U > capacity)) {
        return 0U;
    }
    crc = gimbal_link_crc16(output, frame.length);
    append_byte(&frame, '|');
    append_byte(&frame, hex[(crc >> 12U) & 0x0FU]);
    append_byte(&frame, hex[(crc >> 8U) & 0x0FU]);
    append_byte(&frame, hex[(crc >> 4U) & 0x0FU]);
    append_byte(&frame, hex[crc & 0x0FU]);
    append_byte(&frame, '\n');
    return frame.valid ? frame.length : 0U;
}

const char *gimbal_link_command_name(gimbal_link_command_type_t command)
{
    static const char *const names[] = {
        "SET_DISTANCE", "FIRE_DISTANCE", "FIXED_TARGET", "AUTO_SEARCH",
        "SWEEP", "START", "STOP", "DISABLE", "SET_YAW_TARGET",
        "SET_PITCH_TARGET"
    };
    return ((unsigned int)command < (sizeof(names) / sizeof(names[0]))) ?
           names[command] : "INVALID";
}

static const char *ack_name(gimbal_link_ack_result_t result)
{
    switch (result) {
    case GIMBAL_LINK_ACK_ACCEPTED:
        return "ACCEPTED";
    case GIMBAL_LINK_ACK_FULL:
        return "FULL";
    case GIMBAL_LINK_ACK_INVALID:
    default:
        return "INVALID";
    }
}

size_t gimbal_link_encode_ack(uint8_t *output,
                              size_t capacity,
                              uint16_t sequence,
                              gimbal_link_command_type_t command,
                              gimbal_link_ack_result_t result)
{
    char payload[64];
    text_builder_t builder = {payload, sizeof(payload), 0U, true};
    bool first = true;

    append_field_text(&builder, &first, "command",
                      gimbal_link_command_name(command));
    append_field_text(&builder, &first, "result", ack_name(result));
    return builder.valid ? encode_frame(output, capacity, "ACK", sequence,
                                        payload, builder.length) : 0U;
}

size_t gimbal_link_encode_telemetry(uint8_t *output,
                                    size_t capacity,
                                    uint16_t sequence,
                                    const gimbal_link_telemetry_t *telemetry)
{
    char payload[320];
    text_builder_t builder = {payload, sizeof(payload), 0U, true};
    bool first = true;

    if ((output == NULL) || (telemetry == NULL)) {
        return 0U;
    }
    append_field_uint(&builder, &first, "t", telemetry->timestamp_ms);
    append_field_text(&builder, &first, "s", telemetry->status);
    append_field_text(&builder, &first, "f", telemetry->feedback);
    append_field_uint(&builder, &first, "d", telemetry->distance_cm);
    append_field_uint(&builder, &first, "yr", telemetry->yaw_encoder_raw);
    append_field_uint(&builder, &first, "pr", telemetry->pitch_encoder_raw);
    append_field_uint(&builder, &first, "yv",
                      telemetry->yaw_feedback_valid ? 1U : 0U);
    append_field_uint(&builder, &first, "pv",
                      telemetry->pitch_feedback_valid ? 1U : 0U);
    append_field_int(&builder, &first, "y", telemetry->yaw_mdeg);
    append_field_int(&builder, &first, "p", telemetry->pitch_mdeg);
    append_field_int(&builder, &first, "g", telemetry->target_mdeg);
    append_field_int(&builder, &first, "h", telemetry->pitch_target_mdeg);
    append_field_int(&builder, &first, "c", telemetry->command_current_ma);
    append_field_int(&builder, &first, "i", telemetry->feedback_current_ma);
    append_field_int(&builder, &first, "pc",
                     telemetry->pitch_command_current_ma);
    append_field_int(&builder, &first, "pi",
                     telemetry->pitch_feedback_current_ma);
    append_field_int(&builder, &first, "v", telemetry->speed_mrpm);
    append_field_text(&builder, &first, "ys", telemetry->yaw_state);
    append_field_text(&builder, &first, "ps", telemetry->pitch_state);
    append_field_uint(&builder, &first, "a", telemetry->scan_active ? 1U : 0U);
    append_field_uint(&builder, &first, "e", telemetry->scan_fault ? 1U : 0U);
    append_field_uint(&builder, &first, "x", telemetry->fault_code);
    return builder.valid ? encode_frame(output, capacity, "TEL", sequence,
                                        payload, builder.length) : 0U;
}

size_t gimbal_link_encode_debug(uint8_t *output,
                                size_t capacity,
                                uint16_t sequence,
                                const gimbal_link_telemetry_t *telemetry)
{
    char payload[192];
    text_builder_t builder = {payload, sizeof(payload), 0U, true};
    bool first = true;

    if ((output == NULL) || (telemetry == NULL)) {
        return 0U;
    }
    append_field_uint(&builder, &first, "t", telemetry->timestamp_ms);
    append_field_uint(&builder, &first, "r", telemetry->response_count);
    append_field_uint(&builder, &first, "rp",
                      telemetry->pitch_response_count);
    append_field_uint(&builder, &first, "n", telemetry->command_count);
    append_field_int(&builder, &first, "lo", telemetry->minimum_mdeg);
    append_field_int(&builder, &first, "hi", telemetry->maximum_mdeg);
    append_field_int(&builder, &first, "pk", telemetry->peak_current_ma);
    append_field_uint(&builder, &first, "er", telemetry->esp_rx_dropped);
    append_field_uint(&builder, &first, "et", telemetry->esp_tx_frames);
    append_field_uint(&builder, &first, "ur", telemetry->uart_rx_dropped);
    append_field_uint(&builder, &first, "pe", telemetry->protocol_errors);
    return builder.valid ? encode_frame(output, capacity, "DBG", sequence,
                                        payload, builder.length) : 0U;
}

static int hex_value(uint8_t value)
{
    if ((value >= (uint8_t)'0') && (value <= (uint8_t)'9')) {
        return value - (uint8_t)'0';
    }
    if ((value >= (uint8_t)'A') && (value <= (uint8_t)'F')) {
        return value - (uint8_t)'A' + 10;
    }
    if ((value >= (uint8_t)'a') && (value <= (uint8_t)'f')) {
        return value - (uint8_t)'a' + 10;
    }
    return -1;
}

static bool parse_uint(const char *text, uint32_t maximum, uint32_t *value)
{
    uint32_t result = 0U;

    if ((text == NULL) || (*text == '\0')) {
        return false;
    }
    while (*text != '\0') {
        if ((*text < '0') || (*text > '9')) {
            return false;
        }
        if (result > (maximum - (uint32_t)(*text - '0')) / 10U) {
            return false;
        }
        result = result * 10U + (uint32_t)(*text - '0');
        ++text;
    }
    *value = result;
    return true;
}

static bool parse_int16(const char *text, int16_t *value)
{
    bool negative = false;
    uint32_t magnitude;
    uint32_t maximum = INT16_MAX;

    if ((text == NULL) || (value == NULL) || (*text == '\0')) {
        return false;
    }
    if (*text == '-') {
        negative = true;
        maximum = (uint32_t)INT16_MAX + 1U;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    if (!parse_uint(text, maximum, &magnitude)) {
        return false;
    }
    *value = negative ? (int16_t)(-(int32_t)magnitude) :
                        (int16_t)magnitude;
    return true;
}

static bool command_from_name(const char *name,
                              gimbal_link_command_type_t *command)
{
    unsigned int index;

    for (index = 0U; index <= GIMBAL_LINK_COMMAND_SET_PITCH_TARGET; ++index) {
        if (strcmp(name, gimbal_link_command_name(
                   (gimbal_link_command_type_t)index)) == 0) {
            *command = (gimbal_link_command_type_t)index;
            return true;
        }
    }
    return false;
}

static bool decode_command(uint8_t *line,
                           size_t length,
                           gimbal_link_command_t *command)
{
    char *tokens[16];
    size_t token_count = 0U;
    size_t body_length;
    size_t payload_length;
    char *cursor;
    char *separator;
    char *checksum;
    char *name = NULL;
    char *value_text = NULL;
    uint16_t expected_crc = 0U;
    uint32_t number;
    size_t index;

    if ((line == NULL) || (command == NULL) || (length < 12U) ||
        (length >= GIMBAL_LINK_MAX_FRAME_BYTES)) {
        return false;
    }
    line[length] = '\0';
    separator = strrchr((char *)line, '|');
    if ((separator == NULL) || (strlen(separator + 1) != 4U)) {
        return false;
    }
    checksum = separator + 1;
    for (index = 0U; index < 4U; ++index) {
        const int nibble = hex_value((uint8_t)checksum[index]);
        if (nibble < 0) {
            return false;
        }
        expected_crc = (uint16_t)((expected_crc << 4U) | (uint16_t)nibble);
    }
    body_length = (size_t)(separator - (char *)line);
    if (gimbal_link_crc16(line, body_length) != expected_crc) {
        return false;
    }
    *separator = '\0';

    cursor = (char *)line;
    while ((token_count < (sizeof(tokens) / sizeof(tokens[0]))) &&
           (cursor != NULL)) {
        tokens[token_count++] = cursor;
        separator = strchr(cursor, '|');
        if (separator != NULL) {
            *separator = '\0';
            cursor = separator + 1;
        } else {
            cursor = NULL;
        }
    }
    if ((token_count < 6U) || (strcmp(tokens[0], "JX1") != 0) ||
        (strcmp(tokens[1], "CMD") != 0) ||
        !parse_uint(tokens[2], UINT16_MAX, &number)) {
        return false;
    }
    command->sequence = (uint16_t)number;
    if (!parse_uint(tokens[3], GIMBAL_LINK_MAX_FRAME_BYTES, &number)) {
        return false;
    }
    payload_length = body_length -
        (size_t)(tokens[4] - (char *)line);
    if (number != payload_length) {
        return false;
    }
    for (index = 4U; index < token_count; ++index) {
        if (strncmp(tokens[index], "name=", 5U) == 0) {
            name = tokens[index] + 5U;
        } else if (strncmp(tokens[index], "value=", 6U) == 0) {
            value_text = tokens[index] + 6U;
        }
    }
    if ((name == NULL) || (value_text == NULL) ||
        !command_from_name(name, &command->type) ||
        !parse_int16(value_text, &command->value)) {
        return false;
    }
    return true;
}

static bool decode_vision(uint8_t *line,
                          size_t length,
                          gimbal_link_vision_t *vision)
{
    char *tokens[16];
    size_t token_count = 0U;
    size_t body_length;
    size_t payload_length;
    char *cursor;
    char *separator;
    char *checksum;
    char *x_text = NULL;
    char *distance_text = NULL;
    char *flags_text = NULL;
    char *width_text = NULL;
    uint16_t expected_crc = 0U;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t distance;
    uint32_t flags;
    uint32_t width;
    int16_t x;
    size_t index;

    if ((line == NULL) || (vision == NULL) || (length < 12U) ||
        (length >= GIMBAL_LINK_MAX_FRAME_BYTES)) {
        return false;
    }
    line[length] = '\0';
    separator = strrchr((char *)line, '|');
    if ((separator == NULL) || (strlen(separator + 1) != 4U)) {
        return false;
    }
    checksum = separator + 1;
    for (index = 0U; index < 4U; ++index) {
        const int nibble = hex_value((uint8_t)checksum[index]);
        if (nibble < 0) {
            return false;
        }
        expected_crc = (uint16_t)((expected_crc << 4U) | (uint16_t)nibble);
    }
    body_length = (size_t)(separator - (char *)line);
    if (gimbal_link_crc16(line, body_length) != expected_crc) {
        return false;
    }
    *separator = '\0';

    cursor = (char *)line;
    while ((token_count < (sizeof(tokens) / sizeof(tokens[0]))) &&
           (cursor != NULL)) {
        tokens[token_count++] = cursor;
        separator = strchr(cursor, '|');
        if (separator != NULL) {
            *separator = '\0';
            cursor = separator + 1;
        } else {
            cursor = NULL;
        }
    }
    if ((token_count < 8U) || (strcmp(tokens[0], "JX1") != 0) ||
        (strcmp(tokens[1], "VIS") != 0) ||
        !parse_uint(tokens[2], UINT16_MAX, &sequence) ||
        !parse_uint(tokens[3], GIMBAL_LINK_MAX_FRAME_BYTES, &payload_size)) {
        return false;
    }
    payload_length = body_length -
        (size_t)(tokens[4] - (char *)line);
    if (payload_size != payload_length) {
        return false;
    }
    for (index = 4U; index < token_count; ++index) {
        if (strncmp(tokens[index], "x=", 2U) == 0) {
            x_text = tokens[index] + 2U;
        } else if (strncmp(tokens[index], "d=", 2U) == 0) {
            distance_text = tokens[index] + 2U;
        } else if (strncmp(tokens[index], "f=", 2U) == 0) {
            flags_text = tokens[index] + 2U;
        } else if (strncmp(tokens[index], "w=", 2U) == 0) {
            width_text = tokens[index] + 2U;
        }
    }
    if ((x_text == NULL) || (distance_text == NULL) ||
        (flags_text == NULL) || (width_text == NULL) ||
        !parse_int16(x_text, &x) ||
        !parse_uint(distance_text, UINT16_MAX, &distance) ||
        !parse_uint(flags_text, UINT8_MAX, &flags) ||
        !parse_uint(width_text, UINT16_MAX, &width) ||
        (width == 0U) ||
        ((flags & ~(GIMBAL_LINK_VISION_X_VALID |
                    GIMBAL_LINK_VISION_X_FINAL |
                    GIMBAL_LINK_VISION_DISTANCE_VALID |
                    GIMBAL_LINK_VISION_AIM_STABLE)) != 0U) ||
        (((flags & GIMBAL_LINK_VISION_X_FINAL) != 0U) &&
         ((flags & GIMBAL_LINK_VISION_X_VALID) == 0U)) ||
        (((flags & GIMBAL_LINK_VISION_AIM_STABLE) != 0U) &&
         ((flags & GIMBAL_LINK_VISION_X_FINAL) == 0U)) ||
        (((flags & GIMBAL_LINK_VISION_X_VALID) != 0U) &&
         ((x < 0) || ((uint16_t)x >= (uint16_t)width))) ||
        (((flags & GIMBAL_LINK_VISION_X_VALID) == 0U) && (x != -1)) ||
        (((flags & GIMBAL_LINK_VISION_DISTANCE_VALID) != 0U) &&
         (distance == GIMBAL_LINK_VISION_DISTANCE_INVALID)) ||
        (((flags & GIMBAL_LINK_VISION_DISTANCE_VALID) == 0U) &&
         (distance != GIMBAL_LINK_VISION_DISTANCE_INVALID))) {
        return false;
    }

    vision->sequence = (uint16_t)sequence;
    vision->x = x;
    vision->distance_mm = (uint16_t)distance;
    vision->frame_width = (uint16_t)width;
    vision->flags = (uint8_t)flags;
    return true;
}

void gimbal_link_decoder_init(gimbal_link_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

bool gimbal_link_decoder_feed(gimbal_link_decoder_t *decoder,
                              uint8_t value,
                              gimbal_link_command_t *command)
{
    bool decoded = false;
    bool vision_line;

    if ((decoder == NULL) || (command == NULL)) {
        return false;
    }
    if (value == (uint8_t)'\n') {
        if (decoder->length > 0U) {
            vision_line = (decoder->length >= 8U) &&
                          (memcmp(decoder->bytes, "JX1|VIS|", 8U) == 0);
            if (vision_line) {
                if (decode_vision(decoder->bytes, decoder->length,
                                  &decoder->pending_vision)) {
                    decoder->vision_pending = true;
                } else {
                    ++decoder->error_count;
                }
            } else {
                decoded = decode_command(decoder->bytes, decoder->length,
                                         command);
                if (!decoded) {
                    ++decoder->error_count;
                }
            }
        }
        decoder->length = 0U;
    } else if (value != (uint8_t)'\r') {
        if (decoder->length < GIMBAL_LINK_MAX_FRAME_BYTES - 1U) {
            decoder->bytes[decoder->length++] = value;
        } else {
            decoder->length = 0U;
            ++decoder->error_count;
        }
    }
    return decoded;
}

bool gimbal_link_decoder_take_vision(gimbal_link_decoder_t *decoder,
                                     gimbal_link_vision_t *vision)
{
    if ((decoder == NULL) || (vision == NULL) ||
        !decoder->vision_pending) {
        return false;
    }
    *vision = decoder->pending_vision;
    decoder->vision_pending = false;
    return true;
}
