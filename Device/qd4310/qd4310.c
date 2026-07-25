#include "qd4310.h"

#include <math.h>
#include <stddef.h>

#include "bsp_can1.h"
#include "lcd_demo_config.h"

#define QD4310_COMMAND_BASE_ID   0x400U
#define QD4310_RESPONSE_BASE_ID  0x500U
#define QD4310_COMMAND_ENABLE    0x01U
#define QD4310_COMMAND_DISABLE   0x02U
#define QD4310_COMMAND_CURRENT   0x03U
#define QD4310_RESPONSE_LENGTH   8U
#define QD4310_STATUS_ENABLED    0x01U
#define QD4310_CURRENT_FULL_SCALE_A 10.0f
#define QD4310_RAW_POSITIVE_MAX     32767.0f

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool send_command(uint8_t motor_id, uint8_t command, int16_t value)
{
    bsp_can1_frame_t frame = {
        .standard_id = 0U,
        .length = 3U,
        .data = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };

    if (motor_id > 0x0FU) {
        return false;
    }
    frame.standard_id = (uint16_t)(QD4310_COMMAND_BASE_ID + motor_id);
    frame.data[0] = command;
    frame.data[1] = (uint8_t)((uint16_t)value & 0xFFU);
    frame.data[2] = (uint8_t)((uint16_t)value >> 8U);
    return bsp_can1_send(&frame);
}

void qd4310_enable_test_init(qd4310_enable_test_t *test, uint8_t motor_id)
{
    if (test == NULL) {
        return;
    }

    test->motor_id = motor_id;
    test->state = QD4310_ENABLE_IDLE;
    test->deadline_ms = 0U;
    test->response_count = 0U;
    test->last_response_id = 0U;
    test->last_response_length = 0U;
    test->last_status = 0U;
    test->expected_enabled = false;
    test->last_current_raw = 0;
    test->last_speed_raw = 0;
    test->last_angle_raw = 0U;
    test->feedback_valid = false;
}

static void qd4310_request_begin(qd4310_enable_test_t *test,
                                 uint32_t now_ms,
                                 uint32_t timeout_ms,
                                 uint8_t command,
                                 bool expected_enabled)
{
    if ((test == NULL) || (test->motor_id > 0x0FU)) {
        return;
    }

    test->expected_enabled = expected_enabled;
    if (!send_command(test->motor_id, command, 0)) {
        test->state = QD4310_ENABLE_CAN_ERROR;
        return;
    }

    test->deadline_ms = now_ms + timeout_ms;
    test->state = QD4310_ENABLE_WAITING;
}

void qd4310_enable_test_begin(qd4310_enable_test_t *test,
                              uint32_t now_ms,
                              uint32_t timeout_ms)
{
    qd4310_request_begin(test, now_ms, timeout_ms,
                         QD4310_COMMAND_ENABLE, true);
}

void qd4310_disable_test_begin(qd4310_enable_test_t *test,
                               uint32_t now_ms,
                               uint32_t timeout_ms)
{
    qd4310_request_begin(test, now_ms, timeout_ms,
                         QD4310_COMMAND_DISABLE, false);
}

void qd4310_enable_test_step(qd4310_enable_test_t *test, uint32_t now_ms)
{
    if (test == NULL) {
        return;
    }

    if ((test->state == QD4310_ENABLE_WAITING) &&
        deadline_reached(now_ms, test->deadline_ms)) {
        test->state = QD4310_ENABLE_TIMEOUT;
    }
}

void qd4310_enable_test_accept(qd4310_enable_test_t *test,
                               uint16_t standard_id,
                               uint8_t length,
                               const uint8_t *data)
{
    if ((test == NULL) || (data == NULL)) {
        return;
    }

    test->last_response_id = standard_id;
    test->last_response_length = length;
    if ((standard_id ==
         (uint16_t)(QD4310_RESPONSE_BASE_ID + test->motor_id)) &&
        (length >= QD4310_RESPONSE_LENGTH)) {
        ++test->response_count;
        test->last_status = data[0];
        test->last_current_raw = (int16_t)((uint16_t)data[2] |
                                  ((uint16_t)data[3] << 8U));
        test->last_speed_raw = (int16_t)((uint16_t)data[4] |
                                ((uint16_t)data[5] << 8U));
        test->last_angle_raw = (uint16_t)data[6] |
                               ((uint16_t)data[7] << 8U);
        test->feedback_valid = true;
        test->state = (((data[0] & QD4310_STATUS_ENABLED) != 0U) ==
                       test->expected_enabled) ?
                      QD4310_ENABLE_CONFIRMED : QD4310_ENABLE_REJECTED;
    }
}

bool qd4310_set_test_current(uint8_t motor_id, float current_a)
{
    float scaled;
    int16_t raw;

    if (!isfinite(current_a) ||
        (fabsf(current_a) > YAW_SCAN_CURRENT_LIMIT_A)) {
        return false;
    }
    scaled = current_a / QD4310_CURRENT_FULL_SCALE_A *
             QD4310_RAW_POSITIVE_MAX;
    raw = (int16_t)((scaled >= 0.0f) ? scaled + 0.5f : scaled - 0.5f);
    return send_command(motor_id, QD4310_COMMAND_CURRENT, raw);
}
