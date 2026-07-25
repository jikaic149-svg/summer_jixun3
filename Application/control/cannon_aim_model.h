#ifndef APPLICATION_CANNON_AIM_MODEL_H
#define APPLICATION_CANNON_AIM_MODEL_H

#include <stdbool.h>

bool cannon_aim_pitch_for_distance(float distance_cm,
                                   float *pitch_degrees);
bool cannon_aim_pitch_for_fixed_target(float box_front_distance_cm,
                                       float *pitch_degrees);

#endif
