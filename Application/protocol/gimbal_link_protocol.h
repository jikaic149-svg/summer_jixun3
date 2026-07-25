#ifndef APPLICATION_GIMBAL_LINK_PROTOCOL_H
#define APPLICATION_GIMBAL_LINK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GIMBAL_LINK_MAX_FRAME_BYTES  384U
#define GIMBAL_LINK_VISION_X_VALID       0x01U
#define GIMBAL_LINK_VISION_X_FINAL       0x02U
#define GIMBAL_LINK_VISION_DISTANCE_VALID 0x04U
#define GIMBAL_LINK_VISION_AIM_STABLE      0x08U
#define GIMBAL_LINK_VISION_DISTANCE_INVALID 0xFFFFU

typedef enum {
    GIMBAL_LINK_COMMAND_SET_DISTANCE = 0,
    GIMBAL_LINK_COMMAND_FIRE_DISTANCE, /* Uses the configured ground distance. */
    GIMBAL_LINK_COMMAND_FIXED_TARGET,
    GIMBAL_LINK_COMMAND_AUTO_SEARCH,
    GIMBAL_LINK_COMMAND_SWEEP,
    GIMBAL_LINK_COMMAND_START,
    GIMBAL_LINK_COMMAND_STOP,
    GIMBAL_LINK_COMMAND_DISABLE,
    GIMBAL_LINK_COMMAND_SET_YAW_TARGET,
    GIMBAL_LINK_COMMAND_SET_PITCH_TARGET
} gimbal_link_command_type_t;

typedef struct {
    uint16_t sequence;
    gimbal_link_command_type_t type;
    int16_t value;
} gimbal_link_command_t;

typedef struct {
    uint16_t sequence;
    int16_t x;
    uint16_t distance_mm;
    uint16_t frame_width;
    uint8_t flags;
} gimbal_link_vision_t;

typedef enum {
    GIMBAL_LINK_ACK_ACCEPTED = 0,
    GIMBAL_LINK_ACK_FULL,
    GIMBAL_LINK_ACK_INVALID
} gimbal_link_ack_result_t;

typedef struct {
    uint32_t timestamp_ms;
    const char *status;
    const char *feedback;
    uint16_t distance_cm;
    uint16_t yaw_encoder_raw;
    uint16_t pitch_encoder_raw;
    bool yaw_feedback_valid;
    bool pitch_feedback_valid;
    int32_t yaw_mdeg;
    int32_t pitch_mdeg;
    int32_t target_mdeg;
    int32_t pitch_target_mdeg;
    int32_t command_current_ma;
    int32_t feedback_current_ma;
    int32_t pitch_command_current_ma;
    int32_t pitch_feedback_current_ma;
    int32_t speed_mrpm;
    const char *yaw_state;
    const char *pitch_state;
    bool scan_active;
    bool scan_fault;
    uint8_t fault_code;
    uint32_t response_count;
    uint32_t pitch_response_count;
    uint32_t command_count;
    int32_t minimum_mdeg;
    int32_t maximum_mdeg;
    int32_t peak_current_ma;
    uint32_t esp_rx_dropped;
    uint32_t esp_tx_frames;
    uint32_t uart_rx_dropped;
    uint32_t protocol_errors;
} gimbal_link_telemetry_t;

typedef struct {
    uint8_t bytes[GIMBAL_LINK_MAX_FRAME_BYTES];
    uint16_t length;
    uint32_t error_count;
    gimbal_link_vision_t pending_vision;
    bool vision_pending;
} gimbal_link_decoder_t;

uint16_t gimbal_link_crc16(const uint8_t *data, size_t length);
void gimbal_link_decoder_init(gimbal_link_decoder_t *decoder);
bool gimbal_link_decoder_feed(gimbal_link_decoder_t *decoder,
                              uint8_t value,
                              gimbal_link_command_t *command);
bool gimbal_link_decoder_take_vision(gimbal_link_decoder_t *decoder,
                                     gimbal_link_vision_t *vision);
size_t gimbal_link_encode_ack(uint8_t *output,
                              size_t capacity,
                              uint16_t sequence,
                              gimbal_link_command_type_t command,
                              gimbal_link_ack_result_t result);
size_t gimbal_link_encode_telemetry(uint8_t *output,
                                    size_t capacity,
                                    uint16_t sequence,
                                    const gimbal_link_telemetry_t *telemetry);
size_t gimbal_link_encode_debug(uint8_t *output,
                                size_t capacity,
                                uint16_t sequence,
                                const gimbal_link_telemetry_t *telemetry);
const char *gimbal_link_command_name(gimbal_link_command_type_t command);

#endif
