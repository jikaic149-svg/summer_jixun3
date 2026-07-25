#ifndef DEVICE_QD4310_H
#define DEVICE_QD4310_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    QD4310_ENABLE_IDLE = 0,
    QD4310_ENABLE_WAITING,
    QD4310_ENABLE_CONFIRMED,
    QD4310_ENABLE_REJECTED,
    QD4310_ENABLE_TIMEOUT,
    QD4310_ENABLE_CAN_ERROR
} qd4310_enable_state_t;

typedef struct {
    uint8_t motor_id;
    qd4310_enable_state_t state;
    uint32_t deadline_ms;
    uint32_t response_count;
    uint16_t last_response_id;
    uint8_t last_response_length;
    uint8_t last_status;
    bool expected_enabled;
    int16_t last_current_raw;
    int16_t last_speed_raw;
    uint16_t last_angle_raw;
    bool feedback_valid;
} qd4310_enable_test_t;

void qd4310_enable_test_init(qd4310_enable_test_t *test, uint8_t motor_id);
void qd4310_enable_test_begin(qd4310_enable_test_t *test,
                              uint32_t now_ms,
                              uint32_t timeout_ms);
void qd4310_disable_test_begin(qd4310_enable_test_t *test,
                               uint32_t now_ms,
                               uint32_t timeout_ms);
void qd4310_enable_test_accept(qd4310_enable_test_t *test,
                               uint16_t standard_id,
                               uint8_t length,
                               const uint8_t *data);
void qd4310_enable_test_step(qd4310_enable_test_t *test, uint32_t now_ms);
bool qd4310_set_test_current(uint8_t motor_id, float current_a);

#endif
