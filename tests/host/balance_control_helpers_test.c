#include <stdint.h>
#include <stdio.h>

#include "balance_control_helpers.h"

static int failures = 0;

static float absf(float value)
{
    return value >= 0.0f ? value : -value;
}

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static void check_near(float actual, float expected, const char *message)
{
    CHECK(absf(actual - expected) < 0.00001f, message);
}

static void test_assist_transfer_is_continuous(void)
{
    float integral = 0.20f;
    float before_total = 1.10f + 2.0f * integral;

    integral = BalanceControl_TransferAssistToIntegral(
        integral, 1.10f, 1.096f, 2.0f, 0.50f);
    check_near(1.096f + 2.0f * integral, before_total,
               "forward assist release must preserve total attitude");

    integral = -0.20f;
    before_total = -1.10f + 2.0f * integral;
    integral = BalanceControl_TransferAssistToIntegral(
        integral, -1.10f, -1.096f, 2.0f, 0.50f);
    check_near(-1.096f + 2.0f * integral, before_total,
               "backward assist release must preserve total attitude");

    integral = BalanceControl_TransferAssistToIntegral(
        0.49f, 1.0f, 0.0f, 2.0f, 0.50f);
    check_near(integral, 0.50f,
               "assist transfer must respect integral limit");
}

static void test_bumpless_assist_handoff(void)
{
    float integral_after = 0.0f;
    float assist_after;
    float total_before;

    total_before = 1.10f + 1.20f * 0.20f;
    assist_after = BalanceControl_TransferAssistBumpless(
        0.20f, 1.10f, 1.098f, 1.20f, 0.50f, 1U,
        &integral_after);
    check_near(assist_after + 1.20f * integral_after, total_before,
               "under-speed handoff must preserve forward attitude");

    total_before = -1.10f + 1.20f * -0.20f;
    assist_after = BalanceControl_TransferAssistBumpless(
        -0.20f, -1.10f, -1.098f, 1.20f, 0.50f, 1U,
        &integral_after);
    check_near(assist_after + 1.20f * integral_after, total_before,
               "under-speed handoff must preserve reverse attitude");

    assist_after = BalanceControl_TransferAssistBumpless(
        0.50f, 0.50f, 0.498f, 1.20f, 0.50f, 1U,
        &integral_after);
    check_near(integral_after, 0.50f,
               "saturated integral must stay bounded during handoff");
    check_near(assist_after, 0.50f,
               "unabsorbed assist must remain while under target speed");

    assist_after = BalanceControl_TransferAssistBumpless(
        0.50f, 0.50f, 0.498f, 1.20f, 0.50f, 0U,
        &integral_after);
    check_near(integral_after, 0.50f,
               "at-speed assist release must not add more integral");
    check_near(assist_after, 0.498f,
               "at-speed assist must be allowed to unwind");

    check_near(BalanceControl_SeedAssistFromIntegral(
                   0.20f, 0.50f, 1.20f, 1.0f, 1.10f),
               0.80f,
               "forward restart must preserve integral attitude as assist");
    check_near(BalanceControl_SeedAssistFromIntegral(
                   -0.20f, -0.50f, 1.20f, -1.0f, 1.10f),
               -0.80f,
               "reverse restart must preserve integral attitude as assist");
    check_near(BalanceControl_SeedAssistFromIntegral(
                   -0.20f, 0.0f, 1.20f, 1.0f, 1.10f),
               0.0f,
               "restart must not carry assist against the command");
    check_near(BalanceControl_SeedAssistFromIntegral(
                   1.00f, 0.50f, 1.20f, 1.0f, 1.10f),
               1.10f,
               "restart assist must remain bounded");
}

static void test_continuous_speed_deadband(void)
{
    check_near(BalanceControl_ApplyContinuousDeadband(0.004f, 0.005f),
               0.0f, "positive values inside deadband must be zero");
    check_near(BalanceControl_ApplyContinuousDeadband(-0.005f, 0.005f),
               0.0f, "deadband edge must be continuous");
    check_near(BalanceControl_ApplyContinuousDeadband(0.006f, 0.005f),
               0.001f, "positive deadband exit must not jump");
    check_near(BalanceControl_ApplyContinuousDeadband(-0.006f, 0.005f),
               -0.001f, "negative deadband exit must not jump");
}

static void test_quiet_retry_confirmation(void)
{
    float timer_sec = 0.0f;

    CHECK(BalanceControl_QuietConfirmed(
              1U, 0.10f, 0.30f, &timer_sec) == 0U,
          "one quiet sample must not release a blocked start");
    CHECK(BalanceControl_QuietConfirmed(
              0U, 0.10f, 0.30f, &timer_sec) == 0U,
          "motion must reset start retry confirmation");
    check_near(timer_sec, 0.0f,
               "retry quiet timer must reset on motion");
    CHECK(BalanceControl_QuietConfirmed(
              1U, 0.10f, 0.30f, &timer_sec) == 0U,
          "retry must wait for the full quiet interval");
    CHECK(BalanceControl_QuietConfirmed(
              1U, 0.10f, 0.30f, &timer_sec) == 0U,
          "retry must remain blocked before confirmation completes");
    CHECK(BalanceControl_QuietConfirmed(
              1U, 0.10f, 0.30f, &timer_sec) != 0U,
          "continuous quiet HOLD must release a blocked start");
}

static void test_actual_base_blend_is_deterministic(void)
{
    float blend = 0.0f;

    for (uint16_t i = 0U; i < 250U; i++) {
        blend = BalanceControl_UpdateActualBaseBlend(
            blend, 0.001f, 2.0f, 0.65f, 1U);
    }
    check_near(blend, 0.50f,
               "actual-base blend must reach half in 0.25 seconds");

    for (uint16_t i = 0U; i < 250U; i++) {
        blend = BalanceControl_UpdateActualBaseBlend(
            blend, 0.001f, 2.0f, 0.65f, 1U);
    }
    check_near(blend, 0.65f,
               "actual-base ownership must stop at the configured maximum");
    check_near(BalanceControl_UpdateActualBaseBlend(
                   blend, 0.001f, 2.0f, 0.65f, 0U),
               0.0f,
               "leaving RUNNING must reset actual-base ownership");
    check_near(BalanceControl_UpdateActualBaseBlend(
                   0.4f, 0.0f, 2.0f, 0.65f, 1U),
               0.4f,
               "invalid dt must not change actual-base ownership");
    check_near(BalanceControl_UpdateActualBaseBlend(
                   1.2f, 0.001f, 2.0f, 0.65f, 1U),
               0.65f,
               "actual-base ownership must clamp to its configured maximum");
    check_near(BalanceControl_UpdateActualBaseBlend(
                   0.0f, 1.0f, 2.0f, 2.0f, 1U),
               1.0f,
               "actual-base ownership maximum must remain bounded to one");
}

static void test_actual_base_low_pass(void)
{
    float filtered = 0.0f;
    float peak = 0.0f;

    for (uint16_t i = 0U; i < 100U; i++) {
        filtered = BalanceControl_UpdateLowPass(
            filtered, 1.0f, 0.001f, 0.10f);
    }
    CHECK((filtered > 0.625f) && (filtered < 0.635f),
          "actual-base LPF must reach about 63 percent in one tau");

    for (uint16_t i = 100U; i < 1000U; i++) {
        filtered = BalanceControl_UpdateLowPass(
            filtered, 1.0f, 0.001f, 0.10f);
    }
    CHECK((filtered > 0.9999f) && (filtered <= 1.0f),
          "actual-base LPF must retain unity DC gain without overshoot");

    filtered = 0.0f;
    for (uint16_t i = 0U; i < 100U; i++) {
        filtered = BalanceControl_UpdateLowPass(
            filtered, -1.0f, 0.001f, 0.10f);
    }
    CHECK((filtered < -0.625f) && (filtered > -0.635f),
          "actual-base LPF must be symmetric for reverse motion");

    filtered = 0.0f;
    for (uint16_t i = 0U; i < 4000U; i++) {
        float input = ((i % 125U) < 63U) ? 1.0f : -1.0f;

        filtered = BalanceControl_UpdateLowPass(
            filtered, input, 0.001f, 0.10f);
        if ((i >= 2000U) && (absf(filtered) > peak)) {
            peak = absf(filtered);
        }
    }
    CHECK(peak < 0.35f,
          "actual-base LPF must strongly attenuate an 8 Hz reversal");

    check_near(BalanceControl_UpdateLowPass(
                   0.25f, 1.0f, 0.0f, 0.10f),
               0.25f,
               "invalid LPF dt must preserve the previous value");
    check_near(BalanceControl_UpdateLowPass(
                   0.25f, 1.0f, 0.001f, 0.0f),
               1.0f,
               "zero LPF tau must pass the input through");
}

static void test_start_exit_confirmation(void)
{
    float timer_sec = 0.0f;

    CHECK(BalanceControl_StartExitConfirmed(
              0.029f, 0.040f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "low-speed mode must use the target-ratio threshold");
    check_near(timer_sec, 0.0f,
               "sub-threshold speed must reset startup confirmation");
    CHECK(BalanceControl_StartExitConfirmed(
              0.030f, 0.040f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "first startup confirmation sample must not exit");
    CHECK(BalanceControl_StartExitConfirmed(
              0.031f, 0.040f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "second startup confirmation sample must not exit");
    CHECK(BalanceControl_StartExitConfirmed(
              0.032f, 0.040f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) != 0U,
          "continuous startup speed must exit after confirmation");

    timer_sec = 0.0f;
    CHECK(BalanceControl_StartExitConfirmed(
              0.040f, -0.080f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "motion opposite the command must not exit startup");
    CHECK(BalanceControl_StartExitConfirmed(
              -0.035f, -0.080f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "reverse startup must confirm in the commanded direction");
    CHECK(BalanceControl_StartExitConfirmed(
              -0.036f, -0.080f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) == 0U,
          "reverse startup must still satisfy the dwell time");
    CHECK(BalanceControl_StartExitConfirmed(
              -0.037f, -0.080f, 0.75f, 0.035f,
              0.020f, 0.050f, &timer_sec) != 0U,
          "reverse startup must use the same absolute threshold");
}

static void test_integral_actions(void)
{
    float integral;

    CHECK(BalanceControl_SelectIntegralAction(1U, 1U) ==
              BALANCE_CONTROL_INTEGRAL_DECAY,
          "safety decay must override integration eligibility");
    CHECK(BalanceControl_SelectIntegralAction(0U, 1U) ==
              BALANCE_CONTROL_INTEGRAL_INTEGRATE,
          "eligible running control must integrate");
    CHECK(BalanceControl_SelectIntegralAction(0U, 0U) ==
              BALANCE_CONTROL_INTEGRAL_HOLD,
          "temporary gates must hold the integral");

    integral = BalanceControl_ApplyIntegralAction(
        0.20f, 0.10f, 0.10f, 0.50f, 0.60f,
        BALANCE_CONTROL_INTEGRAL_INTEGRATE);
    check_near(integral, 0.21f,
               "integrate action must accumulate the speed error");
    integral = BalanceControl_ApplyIntegralAction(
        integral, 10.0f, 1.0f, 0.50f, 0.60f,
        BALANCE_CONTROL_INTEGRAL_HOLD);
    check_near(integral, 0.21f,
               "hold action must not leak during a temporary gate");
    integral = BalanceControl_ApplyIntegralAction(
        integral, 0.0f, 0.10f, 0.50f, 0.60f,
        BALANCE_CONTROL_INTEGRAL_DECAY);
    check_near(integral, 0.15f,
               "safety decay must move a positive integral toward zero");
    integral = BalanceControl_ApplyIntegralAction(
        -0.02f, 0.0f, 0.10f, 0.50f, 0.60f,
        BALANCE_CONTROL_INTEGRAL_DECAY);
    check_near(integral, 0.0f,
               "safety decay must not cross zero");

    integral = BalanceControl_TransferAssistToIntegral(
        0.0f, 1.10f, 0.60f, 1.20f, 0.50f);
    for (uint16_t i = 0U; i < 400U; i++) {
        integral = BalanceControl_ApplyIntegralAction(
            integral, 0.08f, 0.001f, 0.50f, 0.60f,
            BALANCE_CONTROL_INTEGRAL_HOLD);
    }
    check_near(integral, 0.4166667f,
               "ramp and slew gates must retain the assist handoff");
}

static void test_stop_release_rejects_sign_noise(void)
{
    static const float signed_velocity[] = {
        0.040f, 0.015f, -0.004f, 0.012f, -0.003f
    };
    float timer_sec = 0.0f;

    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(signed_velocity) /
                       sizeof(signed_velocity[0]));
         i++) {
        CHECK(BalanceControl_StopReleaseConfirmed(
                  signed_velocity[i], 0.010f, 0.005f, 0.030f,
                  &timer_sec) == 0U,
              "isolated sign crossings must not release stop brake");
    }

    for (uint8_t i = 0U; i < 4U; i++) {
        CHECK(BalanceControl_StopReleaseConfirmed(
                  -0.040f, 0.010f, 0.005f, 0.030f,
                  &timer_sec) == 0U,
              "sustained reverse motion must not release stop brake");
    }
    check_near(timer_sec, 0.0f,
               "reverse motion must keep the release timer reset");

    CHECK(BalanceControl_StopReleaseConfirmed(
              0.010f, 0.010f, 0.005f, 0.030f, &timer_sec) == 0U,
          "above-threshold motion must reset the release window");
    CHECK(BalanceControl_StopReleaseConfirmed(
              0.004f, 0.010f, 0.005f, 0.030f, &timer_sec) == 0U,
          "first near-zero sample must not release");
    CHECK(BalanceControl_StopReleaseConfirmed(
              0.003f, 0.010f, 0.005f, 0.030f, &timer_sec) == 0U,
          "second near-zero sample must not release");
    CHECK(BalanceControl_StopReleaseConfirmed(
              -0.002f, 0.010f, 0.005f, 0.030f, &timer_sec) != 0U,
          "continuous near-zero window must release once confirmed");
}

static void test_stop_rebound_and_brake_direction(void)
{
    float timer_sec = 0.0f;
    float candidate_direction = 0.0f;

    CHECK(BalanceControl_StopReboundConfirmed(
              -0.026f, 0.010f, 0.025f, 0.050f,
              &timer_sec, &candidate_direction) == 0U,
          "one reverse sample must not retrigger braking");
    CHECK(BalanceControl_StopReboundConfirmed(
              -0.010f, 0.010f, 0.025f, 0.050f,
              &timer_sec, &candidate_direction) == 0U,
          "sub-threshold sample must reset rebound confirmation");
    check_near(timer_sec, 0.0f,
               "rebound timer must reset below threshold");

    for (uint8_t i = 0U; i < 8U; i++) {
        float alternating_velocity = ((i & 1U) == 0U) ? 0.026f : -0.026f;
        CHECK(BalanceControl_StopReboundConfirmed(
                  alternating_velocity, 0.010f, 0.025f, 0.050f,
                  &timer_sec, &candidate_direction) == 0U,
              "alternating sign noise must not confirm a rebound");
    }
    check_near(timer_sec, 0.01f,
               "a sign change must restart rebound confirmation");
    CHECK(BalanceControl_StopReboundConfirmed(
              0.0f, 0.010f, 0.025f, 0.050f,
              &timer_sec, &candidate_direction) == 0U,
          "quiet sample must reset the alternating-noise candidate");

    for (uint8_t i = 0U; i < 4U; i++) {
        CHECK(BalanceControl_StopReboundConfirmed(
                  -0.040f, 0.010f, 0.025f, 0.050f,
                  &timer_sec, &candidate_direction) == 0U,
              "rebound must satisfy the full confirmation window");
    }
    CHECK(BalanceControl_StopReboundConfirmed(
              -0.040f, 0.010f, 0.025f, 0.050f,
              &timer_sec, &candidate_direction) != 0U,
          "sustained reverse rebound must retrigger braking");

    check_near(BalanceControl_CalcViscousBrakeTheta(
                   0.040f, 6.0f, 0.9f),
               -0.240f,
               "positive motion must request a backward brake attitude");
    check_near(BalanceControl_CalcViscousBrakeTheta(
                   -0.040f, 6.0f, 0.9f),
               0.240f,
               "reverse rebound must request a forward brake attitude");
    check_near(BalanceControl_CalcViscousBrakeTheta(
                   -1.0f, 6.0f, 0.9f),
               0.9f,
               "viscous brake attitude must respect its limit");
}

static void test_stop_zero_cross_capture(void)
{
    CHECK(BalanceControl_StopZeroCrossed(0.010f, 1.0f, 0.025f) == 0U,
          "forward braking must remain active before zero crossing");
    CHECK(BalanceControl_StopZeroCrossed(-0.001f, 1.0f, 0.025f) != 0U,
          "forward stop must capture the first near-zero reverse sample");
    CHECK(BalanceControl_StopZeroCrossed(0.001f, -1.0f, 0.025f) != 0U,
          "reverse stop must capture the first near-zero forward sample");
    CHECK(BalanceControl_StopZeroCrossed(-0.040f, 1.0f, 0.025f) == 0U,
          "an implausibly large sign jump must not release braking");
    CHECK(BalanceControl_StopZeroCrossed(0.0f, 0.0f, 0.025f) == 0U,
          "a stop without an entry direction must use quiet confirmation");

    check_near(BalanceControl_StopCaptureDirection(0.019f, 0.020f),
               0.0f,
               "sub-threshold stop motion must not capture noisy direction");
    check_near(BalanceControl_StopCaptureDirection(0.026f, 0.020f),
               1.0f,
               "late forward motion must arm a near-zero stop");
    check_near(BalanceControl_StopCaptureDirection(-0.026f, 0.020f),
               -1.0f,
               "late reverse motion must arm a near-zero stop");
}

int main(void)
{
    test_assist_transfer_is_continuous();
    test_bumpless_assist_handoff();
    test_continuous_speed_deadband();
    test_quiet_retry_confirmation();
    test_actual_base_blend_is_deterministic();
    test_actual_base_low_pass();
    test_start_exit_confirmation();
    test_integral_actions();
    test_stop_release_rejects_sign_noise();
    test_stop_rebound_and_brake_direction();
    test_stop_zero_cross_capture();

    if (failures != 0) {
        printf("%d balance helper assertion(s) failed\n", failures);
        return 1;
    }
    printf("balance control helper tests passed\n");
    return 0;
}
