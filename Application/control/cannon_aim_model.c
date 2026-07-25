#include "cannon_aim_model.h"

#include <math.h>
#include <stddef.h>

#include "lcd_demo_config.h"

typedef enum {
    CANNON_ARC_AUTO = 0,
    CANNON_ARC_LOW,
    CANNON_ARC_HIGH
} cannon_arc_selection_t;

static bool solve_pitch(float distance_cm,
                        float target_height_cm,
                        cannon_arc_selection_t arc_selection,
                        float *pitch_degrees)
{
    const float radians_to_degrees = 57.29577951308232f;
    float gravity_term_cm;
    float discriminant;
    float root;
    float low_tangent;
    float high_tangent;
    float low_degrees;
    float target;

    float relative_height_cm;

    if ((pitch_degrees == NULL) || !isfinite(distance_cm) ||
        !isfinite(target_height_cm) || (distance_cm <= 0.0f) ||
        (CANNON_MUZZLE_HEIGHT_CM <= 0.0f) ||
        (CANNON_LEVEL_RANGE_CM <= 0.0f)) {
        return false;
    }

    gravity_term_cm = CANNON_MUZZLE_HEIGHT_CM *
                      (distance_cm / CANNON_LEVEL_RANGE_CM) *
                      (distance_cm / CANNON_LEVEL_RANGE_CM);
    relative_height_cm = target_height_cm - CANNON_MUZZLE_HEIGHT_CM;
    discriminant = distance_cm * distance_cm -
                   4.0f * gravity_term_cm *
                   (gravity_term_cm + relative_height_cm);
    if (!isfinite(discriminant) || (discriminant < 0.0f) ||
        (gravity_term_cm < 0.001f)) {
        return false;
    }

    root = sqrtf(discriminant);
    low_tangent = (distance_cm - root) /
                  (2.0f * gravity_term_cm);
    high_tangent = (distance_cm + root) /
                   (2.0f * gravity_term_cm);
    low_degrees = atanf(low_tangent) * radians_to_degrees;
    if (arc_selection == CANNON_ARC_HIGH) {
        target = atanf(high_tangent) * radians_to_degrees;
    } else if (arc_selection == CANNON_ARC_LOW) {
        target = low_degrees;
    } else {
        target = (low_degrees > CANNON_LOW_ARC_MAX_DEG) ?
                 atanf(high_tangent) * radians_to_degrees :
                 low_degrees;
    }
    if (!isfinite(target) ||
        (fabsf(target) > PITCH_TARGET_LIMIT_DEG)) {
        return false;
    }

    *pitch_degrees = target;
    return true;
}

static bool solve_calibrated_high_pitch(float target_center_distance_cm,
                                        float target_height_cm,
                                        float *pitch_degrees)
{
    const float radians_to_degrees = 57.29577951308232f;
    const float measured_center_distance_cm =
        CANNON_HIGH_ARC_MEASURED_FRONT_CM +
        (float)CANNON_BOX_FRONT_TO_TARGET_CENTER_MM / 10.0f;
    const float range_ratio =
        measured_center_distance_cm /
        CANNON_HIGH_ARC_REFERENCE_CENTER_CM;
    const float speed_squared_over_gravity_cm =
        CANNON_LEVEL_RANGE_CM * CANNON_LEVEL_RANGE_CM /
        (2.0f * CANNON_MUZZLE_HEIGHT_CM);
    const float relative_height_cm =
        target_height_cm - CANNON_MUZZLE_HEIGHT_CM;
    const float max_range_term =
        speed_squared_over_gravity_cm - 2.0f * relative_height_cm;
    float calibrated_distance_cm;
    float max_range_cm;
    float max_range_tangent;
    float target;

    if ((pitch_degrees == NULL) ||
        !isfinite(target_center_distance_cm) ||
        (target_center_distance_cm <= 0.0f) ||
        !isfinite(range_ratio) || (range_ratio <= 0.0f) ||
        !isfinite(max_range_term) || (max_range_term <= 0.0f)) {
        return false;
    }

    /*
     * The field result says the real high-arc horizontal progress is 62% of
     * the nominal model.  Solve the same physical high root at d/0.62, with
     * a small multiplicative distance margin to trade a little height for
     * horizontal reach.  If that virtual distance exceeds the model's
     * reachable envelope, use the maximum-range tangent instead of returning
     * an invalid target.
     */
    calibrated_distance_cm = target_center_distance_cm / range_ratio *
                             CANNON_HIGH_ARC_RANGE_MARGIN;
    max_range_cm = sqrtf(speed_squared_over_gravity_cm * max_range_term);
    if (calibrated_distance_cm < max_range_cm) {
        return solve_pitch(calibrated_distance_cm, target_height_cm,
                           CANNON_ARC_HIGH, pitch_degrees);
    }

    max_range_tangent =
        sqrtf(speed_squared_over_gravity_cm / max_range_term);
    target = atanf(max_range_tangent) * radians_to_degrees;
    if (!isfinite(target) || (fabsf(target) > PITCH_TARGET_LIMIT_DEG)) {
        return false;
    }

    *pitch_degrees = target;
    return true;
}

bool cannon_aim_pitch_for_distance(float distance_cm,
                                   float *pitch_degrees)
{
    float ballistic_degrees;

    if ((distance_cm < (float)DISTANCE_INPUT_MIN_CM) ||
        (distance_cm > (float)DISTANCE_INPUT_MAX_CM) ||
        !solve_pitch(distance_cm, 0.0f, CANNON_ARC_LOW,
                     &ballistic_degrees) ||
        (pitch_degrees == NULL)) {
        return false;
    }

    *pitch_degrees = -ballistic_degrees;
    return true;
}

bool cannon_aim_pitch_for_fixed_target(float box_front_distance_cm,
                                       float *pitch_degrees)
{
    const float target_center_distance_cm =
        box_front_distance_cm +
        (float)CANNON_BOX_FRONT_TO_TARGET_CENTER_MM / 10.0f;
    float target_center_degrees;

    if ((box_front_distance_cm < (float)DISTANCE_INPUT_MIN_CM) ||
        (box_front_distance_cm > (float)DISTANCE_INPUT_MAX_CM) ||
        !solve_calibrated_high_pitch(target_center_distance_cm,
                                     CANNON_FIXED_TARGET_HEIGHT_CM,
                                     &target_center_degrees) ||
        (pitch_degrees == NULL)) {
        return false;
    }

    *pitch_degrees = -target_center_degrees;
    return true;
}
