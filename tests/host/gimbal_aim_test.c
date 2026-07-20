#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "mode_gimbal.h"

static int failures = 0;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static float absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static void check_near(float actual,
                       float expected,
                       float tolerance,
                       const char *message)
{
    CHECK(absf(actual - expected) <= tolerance, message);
}

static GimbalCommand_t get_command(void)
{
    GimbalCommand_t command;

    Mode_GimbalGetCommand(&command);
    return command;
}

static void capture_center(GimbalMode_e mode, float encoder_angle_deg)
{
    Mode_GimbalInit();
    Mode_GimbalSet(mode);
    Mode_GimbalUpdate(encoder_angle_deg, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
}

static void test_relax_and_feedback_failure_are_safe(void)
{
    GimbalCommand_t command;

    Mode_GimbalInit();
    Mode_GimbalUpdate(75.0f, 200.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    CHECK(Mode_GimbalGet() == GIMBAL_MODE_RELAX,
          "initial mode must be RELAX");
    CHECK(command.drive_enabled == 0U && command.yaw_current_a == 0.0f,
          "RELAX must always command zero current");

    Mode_GimbalSet(GIMBAL_MODE_AIM);
    Mode_GimbalSetTargetOffsetDeg(GIMBAL_YAW_SOFT_LIMIT_DEG);
    for (uint8_t i = 0U; i < 20U; i++) {
        Mode_GimbalUpdate(75.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.yaw_current_a != 0.0f,
          "feedback-loss setup must establish a nonzero AIM current");

    Mode_GimbalUpdate(75.0f, -1000.0f, 0U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.yaw_current_a == 0.0f,
          "invalid feedback must suppress drive and current");
}

static void test_hold_captures_current_encoder_angle(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_HOLD, 123.0f);
    command = get_command();
    CHECK(command.drive_enabled != 0U,
          "HOLD with valid feedback must enable the yaw drive");
    check_near(command.target_offset_deg, 0.0f, 0.0001f,
               "HOLD must use zero target offset");
    check_near(command.target_angle_deg, 123.0f, 0.0001f,
               "HOLD must capture the first valid encoder angle");
    check_near(command.error_deg, 0.0f, 0.0001f,
               "capturing HOLD must not create an angle step");
    check_near(command.yaw_current_a, 0.0f, 0.0001f,
               "capturing HOLD at zero speed must output no current");

    Mode_GimbalUpdate(124.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_angle_deg, 123.0f, 0.0001f,
               "HOLD target must remain at the captured position");
    check_near(command.error_deg, -1.0f, 0.0001f,
               "HOLD must correct displacement from the captured position");
}

static void test_signed_corrections_and_angle_wrap(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_AIM, 358.0f);
    Mode_GimbalSetTargetFromEncoderDeg(
        358.0f, VISION_YAW_MAX_CORRECTION_DEG);
    Mode_GimbalUpdate(358.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_offset_deg,
               VISION_YAW_MAX_CORRECTION_DEG, 0.0001f,
               "positive correction must create a positive offset");
    check_near(command.target_angle_deg,
               358.0f + VISION_YAW_MAX_CORRECTION_DEG - 360.0f, 0.0001f,
               "positive correction must wrap through 360 degrees");
    check_near(command.error_deg,
               VISION_YAW_MAX_CORRECTION_DEG, 0.0001f,
               "wrapped positive correction must keep the short-path error");
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN > 0.0f,
          "positive correction must produce current in the configured direction");

    capture_center(GIMBAL_MODE_AIM, 2.0f);
    Mode_GimbalSetTargetFromEncoderDeg(
        2.0f, -VISION_YAW_MAX_CORRECTION_DEG);
    Mode_GimbalUpdate(2.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_offset_deg,
               -VISION_YAW_MAX_CORRECTION_DEG, 0.0001f,
               "negative correction must create a negative offset");
    check_near(command.target_angle_deg,
               2.0f - VISION_YAW_MAX_CORRECTION_DEG + 360.0f, 0.0001f,
               "negative correction must wrap below zero degrees");
    check_near(command.error_deg,
               -VISION_YAW_MAX_CORRECTION_DEG, 0.0001f,
               "wrapped negative correction must keep the short-path error");
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN < 0.0f,
          "negative correction must produce current in the configured direction");
}

static void test_visual_capture_window(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_AIM, 100.0f);
    Mode_GimbalSetTargetFromEncoderDeg(
        170.0f, VISION_YAW_MAX_CORRECTION_DEG);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_offset_deg,
               VISION_YAW_CAPTURE_LIMIT_DEG, 0.0001f,
               "visual target must clamp at the positive capture limit");
    check_near(command.target_angle_deg,
               100.0f + VISION_YAW_CAPTURE_LIMIT_DEG, 0.0001f,
               "positive capture clamp must remain relative to the entry center");

    Mode_GimbalSetTargetFromEncoderDeg(
        30.0f, -VISION_YAW_MAX_CORRECTION_DEG);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_offset_deg,
               -VISION_YAW_CAPTURE_LIMIT_DEG, 0.0001f,
               "visual target must clamp at the negative capture limit");
    check_near(command.target_angle_deg,
               100.0f - VISION_YAW_CAPTURE_LIMIT_DEG, 0.0001f,
               "negative capture clamp must remain relative to the entry center");
}

static void test_visual_error_shaping(void)
{
    float positive_three_px;
    float positive_four_px;

    check_near((float)VISION_YAW_DEADBAND_PX, 2.0f, 0.0001f,
               "visual yaw deadband must remain two pixels");
    check_near(VISION_YAW_FULL_RESPONSE_PX, 5.0f, 0.0001f,
               "visual yaw must reach full response at five pixels");

    for (int16_t error_px = -2; error_px <= 2; error_px++) {
        check_near(Mode_GimbalShapeVisionErrorPx(error_px),
                   0.0f, 0.0001f,
                   "visual errors inside two pixels must be suppressed");
    }

    positive_three_px = Mode_GimbalShapeVisionErrorPx(3);
    positive_four_px = Mode_GimbalShapeVisionErrorPx(4);
    CHECK(positive_three_px > 0.0f && positive_three_px < 3.0f,
          "three-pixel error must enter the smooth transition");
    CHECK(positive_four_px > positive_three_px &&
              positive_four_px < 4.0f,
          "four-pixel response must rise smoothly without a full step");
    check_near(Mode_GimbalShapeVisionErrorPx(5), 5.0f, 0.0001f,
               "five-pixel error must receive full response");
    check_near(Mode_GimbalShapeVisionErrorPx(20), 20.0f, 0.0001f,
               "large visual error must pass through unchanged");

    check_near(Mode_GimbalShapeVisionErrorPx(-3),
               -positive_three_px, 0.0001f,
               "three-pixel transition must be sign symmetric");
    check_near(Mode_GimbalShapeVisionErrorPx(-4),
               -positive_four_px, 0.0001f,
               "four-pixel transition must be sign symmetric");
    check_near(Mode_GimbalShapeVisionErrorPx(-5), -5.0f, 0.0001f,
               "negative full response must be sign symmetric");
}

static void test_zero_visual_correction_keeps_absolute_target(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_AIM, 100.0f);
    Mode_GimbalSetTargetFromEncoderDeg(100.0f, 4.0f);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_angle_deg, 104.0f, 0.0001f,
               "nonzero visual correction must establish an absolute target");

    Mode_GimbalSetTargetFromEncoderDeg(102.0f, 0.0f);
    Mode_GimbalUpdate(102.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.target_angle_deg, 104.0f, 0.0001f,
               "zero visual correction must not re-anchor to the encoder");
    check_near(command.target_offset_deg, 4.0f, 0.0001f,
               "zero visual correction must preserve the previous target offset");
    check_near(command.error_deg, 2.0f, 0.0001f,
               "yaw must continue correcting toward the held absolute target");
}

static void check_current_bounds(float current_a,
                                 float previous_current_a,
                                 const char *limit_message,
                                 const char *slew_message)
{
    const float max_step_a =
        GIMBAL_YAW_CURRENT_SLEW_A_PER_SEC *
        GIMBAL_YAW_CONTROL_DT_SEC;

    CHECK(absf(current_a) <= GIMBAL_YAW_CURRENT_LIMIT_A + 0.00001f,
          limit_message);
    CHECK(absf(current_a - previous_current_a) <= max_step_a + 0.00001f,
          slew_message);
}

static void test_pid_current_limit_and_slew_limit(void)
{
    GimbalCommand_t command;
    float previous_current_a = 0.0f;

    capture_center(GIMBAL_MODE_AIM, 0.0f);
    Mode_GimbalSetTargetOffsetDeg(GIMBAL_YAW_SOFT_LIMIT_DEG);
    for (uint16_t i = 0U; i < 400U; i++) {
        Mode_GimbalUpdate(0.0f, -1000.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
        command = get_command();
        check_current_bounds(
            command.yaw_current_a, previous_current_a,
            "positive saturation must respect the absolute current limit",
            "positive saturation must respect the 1 ms current slew limit");
        previous_current_a = command.yaw_current_a;
    }
    check_near(absf(previous_current_a), GIMBAL_YAW_CURRENT_LIMIT_A,
               0.0001f,
               "sustained large positive demand must reach the current clamp");

    Mode_GimbalSetTargetOffsetDeg(-GIMBAL_YAW_SOFT_LIMIT_DEG);
    for (uint16_t i = 0U; i < 400U; i++) {
        Mode_GimbalUpdate(0.0f, 1000.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
        command = get_command();
        check_current_bounds(
            command.yaw_current_a, previous_current_a,
            "negative saturation must respect the absolute current limit",
            "current reversal must respect the 1 ms current slew limit");
        previous_current_a = command.yaw_current_a;
    }
    check_near(absf(previous_current_a), GIMBAL_YAW_CURRENT_LIMIT_A,
               0.0001f,
               "sustained large negative demand must reach the current clamp");
}

static void test_deadband_does_not_accumulate_current(void)
{
    GimbalCommand_t command;
    const float inside_deadband_deg = GIMBAL_YAW_DEADBAND_DEG * 0.90f;

    capture_center(GIMBAL_MODE_AIM, 200.0f);
    Mode_GimbalSetTargetOffsetDeg(inside_deadband_deg);
    for (uint16_t i = 0U; i < 5000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
        command = get_command();
        CHECK(command.yaw_current_a == 0.0f,
              "positive deadband error must never accumulate current");
    }

    Mode_GimbalSetTargetOffsetDeg(-inside_deadband_deg);
    for (uint16_t i = 0U; i < 5000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
        command = get_command();
        CHECK(command.yaw_current_a == 0.0f,
              "negative deadband error must never accumulate current");
    }
}

static float measure_fresh_current(float control_error_deg,
                                   float encoder_speed_rpm,
                                   float dt_sec)
{
    GimbalCommand_t command;

    Mode_GimbalInit();
    Mode_GimbalSet(GIMBAL_MODE_AIM);
    Mode_GimbalSetTargetOffsetDeg(
        GIMBAL_YAW_DEADBAND_DEG + control_error_deg);
    Mode_GimbalUpdate(200.0f, encoder_speed_rpm, 1U, dt_sec);
    command = get_command();
    return command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN;
}

static void test_yaw_anti_oscillation_tuning(void)
{
    check_near(GIMBAL_YAW_KP_A_PER_DEG, 0.028f, 0.000001f,
               "yaw Kp must use the conservative fixed-target tuning");
    check_near(GIMBAL_YAW_KI_A_PER_DEG_S, 0.0005f, 0.000001f,
               "yaw Ki must use the minimal anti-oscillation tuning");
    check_near(GIMBAL_YAW_KD_A_PER_RPM, 0.0035f, 0.000001f,
               "yaw Kd must provide earlier high-speed braking");
    check_near(GIMBAL_YAW_INTEGRAL_LIMIT_A, 0.002f, 0.000001f,
               "yaw integral current must use the conservative clamp");
    check_near(GIMBAL_YAW_STATIC_FF_A, 0.002f, 0.000001f,
               "yaw static-friction feedforward must use the low level");
    check_near(GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG, 1.0f, 0.000001f,
               "yaw static-friction feedforward must ramp over 1 degree");
}

static float measure_current_after_moving_integral_decay(float direction)
{
    GimbalCommand_t command;
    const float raw_error_deg =
        (GIMBAL_YAW_DEADBAND_DEG + 0.50f) * direction;
    const float moving_speed_rpm = 1.0f * direction;

    capture_center(GIMBAL_MODE_AIM, 200.0f);
    Mode_GimbalSetTargetOffsetDeg(raw_error_deg);
    for (uint16_t i = 0U; i < 12000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN * direction >
              GIMBAL_YAW_INTEGRAL_LIMIT_A,
          "moving-decay setup must first establish signed integral current");

    for (uint16_t i = 0U; i < 2500U; i++) {
        Mode_GimbalUpdate(200.0f, moving_speed_rpm, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    return command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN;
}

static void test_integral_decays_while_yaw_is_moving(void)
{
    const float control_error_deg = 0.50f;
    const float moving_speed_rpm = 1.0f;
    const float error_scale =
        control_error_deg / GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG;
    const float speed_scale =
        (GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM - moving_speed_rpm) /
        (GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM -
         GIMBAL_YAW_STATIC_FF_FULL_SPEED_RPM);
    const float expected_without_integral_a =
        GIMBAL_YAW_KP_A_PER_DEG * control_error_deg -
        GIMBAL_YAW_KD_A_PER_RPM * moving_speed_rpm +
        GIMBAL_YAW_STATIC_FF_A * error_scale * speed_scale;
    float positive_current_a;
    float negative_current_a;

    check_near(GIMBAL_YAW_INTEGRAL_MAX_SPEED_RPM, 0.5f, 0.0001f,
               "yaw integral must stop accumulating from 0.5 rpm");
    CHECK(GIMBAL_YAW_INTEGRAL_MOVING_DECAY > 0.0f &&
              GIMBAL_YAW_INTEGRAL_MOVING_DECAY < 1.0f,
          "moving yaw must use a true integral decay factor");

    positive_current_a = measure_current_after_moving_integral_decay(1.0f);
    negative_current_a = measure_current_after_moving_integral_decay(-1.0f);
    check_near(positive_current_a,
               expected_without_integral_a, 0.0001f,
               "positive moving yaw must decay old integral to the P-D-FF baseline");
    check_near(negative_current_a,
               -expected_without_integral_a, 0.0001f,
               "negative moving yaw must decay old integral symmetrically");
}

static void test_static_friction_feedforward_is_smooth(void)
{
    GimbalCommand_t command;
    const float epsilon_error_deg = 0.01f;
    const float dt_sec = 0.02f;
    const float one_tenth_error_deg =
        GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG * 0.10f;
    const float half_error_deg =
        GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG * 0.50f;
    const float expected_one_tenth_a =
        (GIMBAL_YAW_KP_A_PER_DEG +
         GIMBAL_YAW_KI_A_PER_DEG_S * dt_sec +
         GIMBAL_YAW_STATIC_FF_A /
             GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG) *
        one_tenth_error_deg;
    float positive_epsilon_a;
    float negative_epsilon_a;
    float one_tenth_current_a;
    float half_current_a;
    float zero_speed_current_a;
    float mid_speed_current_a;
    float no_ff_speed_current_a;

    positive_epsilon_a =
        measure_fresh_current(epsilon_error_deg, 0.0f, dt_sec);

    Mode_GimbalInit();
    Mode_GimbalSet(GIMBAL_MODE_AIM);
    Mode_GimbalSetTargetOffsetDeg(
        -(GIMBAL_YAW_DEADBAND_DEG + epsilon_error_deg));
    Mode_GimbalUpdate(200.0f, 0.0f, 1U, dt_sec);
    command = get_command();
    negative_epsilon_a =
        command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN;

    CHECK(positive_epsilon_a > 0.0f &&
              positive_epsilon_a < 0.001f,
          "current just outside the positive deadband must start near zero");
    CHECK(negative_epsilon_a < 0.0f &&
              negative_epsilon_a > -0.001f,
          "current just outside the negative deadband must start near zero");
    check_near(positive_epsilon_a, -negative_epsilon_a, 0.00001f,
               "deadband-edge current must remain sign symmetric");

    one_tenth_current_a =
        measure_fresh_current(one_tenth_error_deg, 0.0f, dt_sec);
    half_current_a =
        measure_fresh_current(half_error_deg, 0.0f, dt_sec);
    check_near(one_tenth_current_a, expected_one_tenth_a, 0.00001f,
               "small-error current must include proportional FF ramping");
    check_near(half_current_a, 5.0f * one_tenth_current_a, 0.00002f,
               "static-friction feedforward must scale linearly with error");

    zero_speed_current_a =
        measure_fresh_current(half_error_deg, 0.0f, dt_sec);
    mid_speed_current_a = measure_fresh_current(
        half_error_deg,
        0.5f * (GIMBAL_YAW_STATIC_FF_FULL_SPEED_RPM +
                GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM),
        dt_sec);
    no_ff_speed_current_a = measure_fresh_current(
        half_error_deg,
        GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM + 1.0f,
        dt_sec);
    CHECK(zero_speed_current_a > mid_speed_current_a &&
              mid_speed_current_a > no_ff_speed_current_a,
          "static-friction feedforward must taper continuously with yaw speed");
    check_near(zero_speed_current_a - mid_speed_current_a,
               GIMBAL_YAW_KI_A_PER_DEG_S * half_error_deg * dt_sec +
                   GIMBAL_YAW_STATIC_FF_A * 0.25f +
                   GIMBAL_YAW_KD_A_PER_RPM *
                       0.5f * (GIMBAL_YAW_STATIC_FF_FULL_SPEED_RPM +
                               GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM),
               0.00002f,
               "mid-speed current must contain half of the error-scaled FF");
}

static void test_integral_output_is_clamped(void)
{
    GimbalCommand_t command;
    const float control_error_deg = 0.50f;
    const float raw_error_deg =
        GIMBAL_YAW_DEADBAND_DEG + control_error_deg;
    const float expected_abs_current_a =
        GIMBAL_YAW_KP_A_PER_DEG * control_error_deg +
        GIMBAL_YAW_INTEGRAL_LIMIT_A +
        GIMBAL_YAW_STATIC_FF_A * control_error_deg /
            GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG;

    capture_center(GIMBAL_MODE_AIM, 200.0f);
    Mode_GimbalSetTargetOffsetDeg(raw_error_deg);
    for (uint16_t i = 0U; i < 12000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    check_near(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN,
               expected_abs_current_a, 0.00002f,
               "positive yaw integral output must stop at its current clamp");

    capture_center(GIMBAL_MODE_AIM, 200.0f);
    Mode_GimbalSetTargetOffsetDeg(-raw_error_deg);
    for (uint16_t i = 0U; i < 12000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    check_near(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN,
               -expected_abs_current_a, 0.00002f,
               "negative yaw integral output must stop at its current clamp");
}

static void test_integral_is_cleared_when_error_reverses(void)
{
    GimbalCommand_t command;
    const float windup_control_error_deg = 0.50f;
    const float reverse_control_error_deg = 0.10f;

    capture_center(GIMBAL_MODE_AIM, 200.0f);
    Mode_GimbalSetTargetOffsetDeg(
        GIMBAL_YAW_DEADBAND_DEG + windup_control_error_deg);
    for (uint16_t i = 0U; i < 12000U; i++) {
        Mode_GimbalUpdate(200.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN > 0.0f,
          "integral reversal setup must establish positive current");

    Mode_GimbalSetTargetOffsetDeg(
        -(GIMBAL_YAW_DEADBAND_DEG + reverse_control_error_deg));
    Mode_GimbalUpdate(200.0f, 0.0f, 1U, 0.02f);
    command = get_command();
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN < 0.0f,
          "crossing the target must discard integral from the old direction");

    Mode_GimbalUpdate(200.0f, 0.0f, 1U, 0.02f);
    command = get_command();
    CHECK(command.yaw_current_a * GIMBAL_YAW_CONTROL_SIGN < 0.0f,
          "new-direction current must remain corrective after crossing target");
}

static void test_relax_exit_clears_current_immediately(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_AIM, 45.0f);
    Mode_GimbalSetTargetOffsetDeg(20.0f);
    for (uint8_t i = 0U; i < 20U; i++) {
        Mode_GimbalUpdate(45.0f, 0.0f, 1U,
                          GIMBAL_YAW_CONTROL_DT_SEC);
    }
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.yaw_current_a != 0.0f,
          "test setup must establish a nonzero AIM current");

    Mode_GimbalSet(GIMBAL_MODE_RELAX);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.yaw_current_a == 0.0f,
          "entering RELAX must synchronously clear the current command");

    Mode_GimbalUpdate(45.0f, 1000.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.yaw_current_a == 0.0f,
          "RELAX updates must remain at zero after a driven AIM state");
}

static void test_chassis_speed_feedforward_only_offsets_yaw_target(void)
{
    GimbalCommand_t command;

    capture_center(GIMBAL_MODE_AIM, 100.0f);
    Mode_GimbalSetTargetFromEncoderDeg(100.0f, 2.0f);
    Mode_GimbalSetChassisSpeedFeedforward(0.08f, 1U);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg, 0.12f, 0.0001f,
               "0.08 m/s measured speed must add 0.12 deg to yaw target");
    check_near(command.target_offset_deg, 2.12f, 0.0001f,
               "yaw feedforward must add to the visual target offset");

    Mode_GimbalSetChassisSpeedFeedforward(-0.08f, 1U);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg, -0.12f, 0.0001f,
               "reverse motion must reverse the yaw feedforward");
    check_near(command.target_offset_deg, 1.88f, 0.0001f,
               "negative yaw feedforward must subtract from visual offset");

    Mode_GimbalSetChassisSpeedFeedforward(0.08f, 0U);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg, 0.0f, 0.0001f,
               "stopping or braking must clear yaw feedforward");
    check_near(command.target_offset_deg, 2.0f, 0.0001f,
               "clearing feedforward must preserve the visual target");

    Mode_GimbalSetChassisSpeedFeedforward(1.0f, 1U);
    Mode_GimbalUpdate(100.0f, 0.0f, 1U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg,
               GIMBAL_YAW_CHASSIS_SPEED_FF_LIMIT_DEG, 0.0001f,
               "yaw speed feedforward must respect its angle limit");

    Mode_GimbalUpdate(100.0f, 0.0f, 0U,
                      GIMBAL_YAW_CONTROL_DT_SEC);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg, 0.0f, 0.0001f,
               "feedback loss must clear yaw feedforward");

    Mode_GimbalSetChassisSpeedFeedforward(0.08f, 1U);
    Mode_GimbalSet(GIMBAL_MODE_RELAX);
    command = get_command();
    check_near(command.chassis_speed_feedforward_deg, 0.0f, 0.0001f,
               "RELAX must synchronously clear yaw feedforward");
}

int main(void)
{
    test_relax_and_feedback_failure_are_safe();
    test_hold_captures_current_encoder_angle();
    test_signed_corrections_and_angle_wrap();
    test_visual_capture_window();
    test_visual_error_shaping();
    test_zero_visual_correction_keeps_absolute_target();
    test_pid_current_limit_and_slew_limit();
    test_deadband_does_not_accumulate_current();
    test_yaw_anti_oscillation_tuning();
    test_integral_decays_while_yaw_is_moving();
    test_static_friction_feedforward_is_smooth();
    test_integral_output_is_clamped();
    test_integral_is_cleared_when_error_reverses();
    test_relax_exit_clears_current_immediately();
    test_chassis_speed_feedforward_only_offsets_yaw_target();

    if (failures != 0) {
        printf("%d gimbal aim assertion(s) failed\n", failures);
        return 1;
    }
    printf("gimbal aim tests passed\n");
    return 0;
}
