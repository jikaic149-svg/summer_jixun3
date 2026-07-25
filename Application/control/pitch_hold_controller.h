#ifndef APPLICATION_PITCH_HOLD_CONTROLLER_H
#define APPLICATION_PITCH_HOLD_CONTROLLER_H

#include <stdbool.h>

typedef struct {
    bool active;
    float target_degrees;
    float integral_current_a;
} pitch_hold_controller_t;

void pitch_hold_controller_init(pitch_hold_controller_t *controller);
void pitch_hold_controller_start(pitch_hold_controller_t *controller);
void pitch_hold_controller_stop(pitch_hold_controller_t *controller);
bool pitch_hold_controller_set_target(pitch_hold_controller_t *controller,
                                      float target_degrees);
bool pitch_hold_controller_update(pitch_hold_controller_t *controller,
                                  float angle_degrees,
                                  float speed_rpm,
                                  float *current_a);

#endif
