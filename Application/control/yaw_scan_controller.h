#ifndef APPLICATION_YAW_SCAN_CONTROLLER_H
#define APPLICATION_YAW_SCAN_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;
    bool endpoint_holding;
    int direction;
    uint32_t endpoint_hold_ms;
    float target_degrees;
    float integral_current_a;
} yaw_scan_controller_t;

void yaw_scan_controller_init(yaw_scan_controller_t *controller);
void yaw_scan_controller_start(yaw_scan_controller_t *controller);
void yaw_scan_controller_stop(yaw_scan_controller_t *controller);
bool yaw_scan_controller_set_target(yaw_scan_controller_t *controller,
                                    float target_degrees);
bool yaw_scan_controller_track_target(yaw_scan_controller_t *controller,
                                      float target_degrees);
bool yaw_scan_controller_update(yaw_scan_controller_t *controller,
                                float angle_degrees,
                                float speed_rpm,
                                float *current_a,
                                float *target_degrees);

#endif
