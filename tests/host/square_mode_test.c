#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "mode_control.h"
#include "track.h"

static int failures = 0;
static uint32_t now_ms = 0U;

#define LINE_MASK         0x7CU
#define NON_CORNER_MASK   0xFCU
#define CORNER_MASK       0x0FU
#define OPPOSITE_CORNER_MASK 0xF0U

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static uint8_t bit_count(uint8_t value)
{
    uint8_t count = 0U;

    while (value != 0U) {
        count += (uint8_t)(value & 1U);
        value >>= 1U;
    }
    return count;
}

static ModeCommand_t get_command(void)
{
    ModeCommand_t command;

    Mode_GetCommand(&command);
    return command;
}

static void tick_without_frame(void)
{
    now_ms++;
    Mode_Update(now_ms);
}

static void tick_with_frame(float error, uint8_t black_mask)
{
    now_ms++;
    Mode_TrackingSetSample(error, bit_count(black_mask), black_mask, now_ms);
    Mode_Update(now_ms);
}

static void tick_with_feedback(float error,
                               uint8_t black_mask,
                               float actual_velocity_mps,
                               float chassis_yaw_deg)
{
    Mode_SetActualVelocity(actual_velocity_mps);
    Mode_SetChassisYaw(chassis_yaw_deg);
    tick_with_frame(error, black_mask);
}

static void run_with_frames(uint32_t duration_ms,
                            uint32_t frame_period_ms,
                            float error,
                            uint8_t black_mask)
{
    for (uint32_t elapsed = 1U; elapsed <= duration_ms; elapsed++) {
        now_ms++;
        if ((elapsed == 1U) ||
            ((frame_period_ms != 0U) &&
             ((elapsed % frame_period_ms) == 0U))) {
            Mode_TrackingSetSample(error, bit_count(black_mask),
                                   black_mask, now_ms);
        }
        Mode_Update(now_ms);
    }
}

static void run_without_frames(uint32_t duration_ms)
{
    for (uint32_t i = 0U; i < duration_ms; i++) {
        tick_without_frame();
    }
}

static void run_with_feedback_frames(uint32_t duration_ms,
                                     uint32_t frame_period_ms,
                                     float error,
                                     uint8_t black_mask,
                                     float actual_velocity_mps,
                                     float chassis_yaw_deg)
{
    Mode_SetActualVelocity(actual_velocity_mps);
    Mode_SetChassisYaw(chassis_yaw_deg);
    run_with_frames(duration_ms, frame_period_ms, error, black_mask);
}

static void tick_feedback_after(uint32_t delay_ms,
                                float error,
                                uint8_t black_mask,
                                float actual_velocity_mps,
                                float chassis_yaw_deg)
{
    if (delay_ms > 1U) {
        run_without_frames(delay_ms - 1U);
    }
    tick_with_feedback(error, black_mask,
                       actual_velocity_mps, chassis_yaw_deg);
}

static void enter_square_follow(void)
{
    ModeCommand_t command;

    now_ms = 0U;
    Mode_Init();
    Mode_SetMotionSettled(1U);
    Mode_SetActualVelocity(0.05f);
    Mode_SetChassisYaw(0.0f);
    Mode_Set(CHASSIS_MODE_SQUARE_TRACKING);
    Mode_SetLaserSafe(1U);

    tick_with_frame(0.0f, LINE_MASK);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f,
          "first square frame must remain stopped");
    CHECK(command.laser_enabled == 0U,
          "mode 6 must keep the laser off");

    run_with_frames(SQUARE_TRACKING_REFIND_CONFIRM_MS + 30U,
                    20U, 0.0f, LINE_MASK);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW,
          "confirmed normal line must enter follow");
    CHECK(command.drive_enabled != 0U &&
              command.velocity_mps == SQUARE_TRACKING_BASE_VELOCITY_MPS,
          "follow must request base velocity");
}

static void start_corner_runout(void)
{
    ModeCommand_t command;

    Mode_SetMotionSettled(0U);
    run_with_frames(SQUARE_TRACKING_CORNER_CONFIRM_MS + 30U,
                    20U, -4.0f, CORNER_MASK);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_RUNOUT,
          "confirmed turn-side black block must enter runout");
    CHECK(command.drive_enabled != 0U && command.turn_rpm == 0.0f &&
              command.velocity_mps ==
                  SQUARE_TRACKING_CORNER_RUNOUT_VELOCITY_MPS,
          "corner runout must move straight at low speed");
    CHECK(command.laser_enabled == 0U,
          "corner runout must keep laser off");
}

static void enter_corner_turn(float start_yaw_deg)
{
    start_corner_runout();
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_RUNOUT_MS + 2U,
                             20U, TRACK_LOST_LINE, 0U,
                             0.03f, start_yaw_deg);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_BRAKE,
          "runout completion must brake before yaw turn");

    Mode_SetMotionSettled(1U);
    tick_with_feedback(TRACK_LOST_LINE, 0U, 0.0f, start_yaw_deg);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "settled corner brake must capture yaw and enter turn");
    Mode_SetMotionSettled(0U);
}

static void enter_corner_align(float start_yaw_deg, float end_yaw_deg)
{
    enter_corner_turn(start_yaw_deg);
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_TURN_MIN_MS + 20U,
                             20U, TRACK_LOST_LINE, 0U,
                             0.0f, end_yaw_deg);
    tick_with_feedback(2.0f, LINE_MASK, 0.0f, end_yaw_deg);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_ALIGN,
          "new narrow edge after yaw gate must enter alignment");
}

static void test_corner_transition_has_no_stop_pulse(void)
{
    ModeCommand_t command;
    uint8_t entered_runout = 0U;

    enter_square_follow();
    Mode_SetMotionSettled(0U);
    for (uint32_t elapsed = 1U;
         elapsed <= SQUARE_TRACKING_CORNER_CONFIRM_MS + 60U;
         elapsed++) {
        now_ms++;
        if ((elapsed == 1U) || ((elapsed % 20U) == 0U)) {
            Mode_TrackingSetSample(-4.0f, bit_count(CORNER_MASK),
                                   CORNER_MASK, now_ms);
        }
        Mode_Update(now_ms);
        if (Mode_SquareTrackingGetPhase() ==
            MODE_SQUARE_PHASE_CORNER_RUNOUT) {
            command = get_command();
            CHECK(command.drive_enabled != 0U &&
                      command.velocity_mps ==
                          SQUARE_TRACKING_CORNER_RUNOUT_VELOCITY_MPS &&
                      command.turn_rpm == 0.0f,
                  "corner confirmation tick must already command runout");
            entered_runout = 1U;
            break;
        }
    }
    CHECK(entered_runout != 0U,
          "continuous corner frames must reach runout");
}

static void test_follow_turn_is_bounded_and_quiet_near_center(void)
{
    ModeCommand_t command;
    float raw_error;

    CHECK(SQUARE_TRACKING_TURN_LIMIT_RPM == 16.0f,
          "moving square turn limit must remain 16 rpm");

    enter_square_follow();
    run_with_frames(200U, 20U, 9.0f, LINE_MASK);
    command = get_command();
    CHECK(command.turn_rpm == 16.0f,
          "positive moving square correction must clamp at 16 rpm");

    enter_square_follow();
    run_with_frames(200U, 20U, -9.0f, LINE_MASK);
    command = get_command();
    CHECK(command.turn_rpm == -16.0f,
          "negative moving square correction must clamp at -16 rpm");

    enter_square_follow();
    run_with_frames(200U, 20U, 0.10f, LINE_MASK);
    command = get_command();
    CHECK(command.turn_rpm == 0.0f,
          "small fused error must stay inside the continuous turn deadband");

    raw_error = (SQUARE_TRACKING_TURN_DEADBAND_RPM + 0.5f) /
                (SQUARE_TRACKING_TURN_KP_RPM_PER_ERROR *
                 (1.0f - SQUARE_TRACKING_ERROR_LPF_ALPHA));
    enter_square_follow();
    tick_with_frame(raw_error, LINE_MASK);
    command = get_command();
    CHECK((command.turn_rpm > 0.49f) && (command.turn_rpm < 0.51f),
          "positive deadband exit must be continuous");

    enter_square_follow();
    tick_with_frame(-raw_error, LINE_MASK);
    command = get_command();
    CHECK((command.turn_rpm < -0.49f) && (command.turn_rpm > -0.51f),
          "negative deadband exit must be continuous");
}

static void complete_corner(uint8_t expected_corner_count,
                            float start_yaw_deg,
                            float end_yaw_deg)
{
    ModeCommand_t command;

    enter_corner_align(start_yaw_deg, end_yaw_deg);
    tick_with_feedback(2.0f, LINE_MASK, 0.03f, end_yaw_deg);
    command = get_command();
    CHECK(command.drive_enabled != 0U &&
              command.velocity_mps ==
                  SQUARE_TRACKING_CORNER_ALIGN_VELOCITY_MPS &&
              command.turn_rpm > 0.0f,
          "corner alignment must move forward and correct both directions");
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_ALIGN_CONFIRM_MS + 180U,
                             20U, 0.0f, LINE_MASK,
                             0.03f, end_yaw_deg);
    CHECK(Mode_SquareTrackingGetCornerCount() == expected_corner_count,
          "aligned side must increment one corner");
    command = get_command();
    CHECK(command.laser_enabled == 0U,
          "corner completion must keep laser off");

    if (expected_corner_count < SQUARE_TRACKING_TARGET_CORNER_COUNT) {
        CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW,
              "non-final corner must return to follow");
        CHECK(command.drive_enabled != 0U,
              "non-final alignment must not insert a stop pulse");
        run_with_feedback_frames(SQUARE_TRACKING_CORNER_REARM_MS + 30U,
                                 20U, 0.0f, LINE_MASK,
                                 0.03f, end_yaw_deg);
    }
}

static void test_non_corner_masks_do_not_trigger_corner(void)
{
    ModeCommand_t command;

    enter_square_follow();
    run_with_frames(300U, 20U, 0.3f, LINE_MASK);
    run_with_frames(300U, 20U, 0.5f, NON_CORNER_MASK);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW,
          "normal multi-sensor patterns must not be corners");
    CHECK(command.drive_enabled != 0U,
          "normal multi-sensor masks must keep following");
}

static void test_either_single_side_block_triggers_corner(void)
{
    enter_square_follow();
    run_with_frames(SQUARE_TRACKING_CORNER_CONFIRM_MS + 30U,
                    20U, 4.0f, OPPOSITE_CORNER_MASK);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_RUNOUT,
          "either isolated sensor half must identify a square corner");
}

static void test_corner_confirmation_tolerates_speed_jitter(void)
{
    static const float velocity_mps[] = {
        0.006f, -0.006f, 0.0f, -0.010f
    };

    enter_square_follow();
    Mode_SetMotionSettled(0U);
    for (uint32_t i = 0U;
         i < (uint32_t)(sizeof(velocity_mps) / sizeof(velocity_mps[0]));
         i++) {
        tick_feedback_after((i == 0U) ? 1U : 15U,
                            -4.0f, CORNER_MASK,
                            velocity_mps[i], 0.0f);
    }

    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_RUNOUT,
          "small measured-speed sign jitter must not reset corner confirmation");
}

static void test_corner_requires_continuous_frames(void)
{
    enter_square_follow();
    tick_with_frame(-4.0f, CORNER_MASK);
    run_without_frames(SQUARE_TRACKING_MAX_NEW_FRAME_GAP_MS + 2U);
    tick_with_frame(-4.0f, CORNER_MASK);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_WAIT_LINE,
          "isolated corner samples must not be accumulated");
}

static void test_reversing_black_block_does_not_trigger_corner(void)
{
    ModeCommand_t command;

    enter_square_follow();
    Mode_SetActualVelocity(-0.03f);
    run_with_frames(SQUARE_TRACKING_CORNER_CONFIRM_MS + 80U,
                    20U, -4.0f, CORNER_MASK);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW,
          "reverse motion must reject a multi-sensor corner candidate");
    CHECK(command.drive_enabled != 0U,
          "reverse false trigger must keep requesting forward recovery");

    Mode_SetActualVelocity(0.006f);
    run_with_frames(SQUARE_TRACKING_CORNER_CONFIRM_MS + 30U,
                    20U, -4.0f, CORNER_MASK);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_RUNOUT,
          "corner detection may resume once definite reverse motion ends");
}

static void test_runout_stale_fault(void)
{
    ModeCommand_t command;

    enter_square_follow();
    start_corner_runout();
    run_without_frames(TRACKING_DATA_TIMEOUT_MS + 2U);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FAULT_HOLD,
          "stale sensor during corner runout must latch fault");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "runout stale fault must stop safely");
}

static void test_corner_brake_allows_real_settle_delay(void)
{
    enter_square_follow();
    start_corner_runout();
    run_with_frames(SQUARE_TRACKING_CORNER_RUNOUT_MS + 2U,
                    20U, TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_BRAKE,
          "corner runout must enter corner brake");

    run_with_frames(1800U, 20U, TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_BRAKE,
          "corner brake must outlast the measured 1.62 second settle delay");

    Mode_SetMotionSettled(1U);
    tick_with_frame(TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_TURN,
          "settling after the real delay must still start fixed turn");
}

static void test_lost_brake_allows_real_settle_delay(void)
{
    enter_square_follow();
    Mode_SetMotionSettled(0U);
    tick_with_frame(TRACK_LOST_LINE, 0U);
    run_with_frames(SQUARE_TRACKING_LOST_HOLD_MS + 2U,
                    20U, TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_LOST_BRAKE,
          "sustained line loss must enter lost brake");

    run_with_frames(1800U, 20U, TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_LOST_BRAKE,
          "lost brake must outlast the measured 1.62 second settle delay");

    Mode_SetMotionSettled(1U);
    tick_with_frame(TRACK_LOST_LINE, 0U);
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_LOST_SEARCH,
          "settling after the real delay must still start line search");
}

static void test_turn_rejects_line_before_minimum_time(void)
{
    ModeCommand_t command;

    enter_square_follow();
    enter_corner_turn(0.0f);
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_TURN_MIN_MS - 100U,
                             20U, 0.0f, LINE_MASK,
                             0.0f, 90.0f);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() ==
              MODE_SQUARE_PHASE_CORNER_TURN,
          "a visible line and sufficient yaw must not bypass minimum turn time");
    CHECK(command.turn_rpm == -SQUARE_TRACKING_CORNER_SEARCH_RPM,
          "after full yaw, an early line must keep the slow search turn");
}

static void test_turn_rejects_old_line_below_yaw_gate(void)
{
    ModeCommand_t command;

    enter_square_follow();
    enter_corner_turn(0.0f);
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_TURN_MIN_MS + 20U,
                             20U, TRACK_LOST_LINE, 0U,
                             0.0f, 80.0f);
    tick_with_feedback(3.1f, LINE_MASK, 0.0f, 80.0f);
    tick_with_feedback(0.0f, LINE_MASK, 0.0f, 80.0f);
    command = get_command();

    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "old-line 3.1 to 0 sequence must not exit turn below 85 degrees");
    CHECK(command.turn_rpm == -BALANCE_MODE_TURN_SPEED_RPM,
          "below the yaw gate the existing right-turn command must continue");
}

static void test_turn_yaw_gate_handles_wraparound(void)
{
    enter_square_follow();
    enter_corner_turn(350.0f);
    run_with_feedback_frames(SQUARE_TRACKING_CORNER_TURN_MIN_MS + 20U,
                             20U, TRACK_LOST_LINE, 0U,
                             0.0f, 74.0f);
    tick_with_feedback(0.0f, LINE_MASK, 0.0f, 74.0f);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "wrapped 84 degree yaw progress must reject a visible line");

    tick_with_feedback(0.0f, LINE_MASK, 0.0f, 75.0f);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_ALIGN,
          "wrapped 350 to 75 degree progress must pass the 85 degree gate");
}

static void test_full_yaw_without_line_uses_slow_search(void)
{
    ModeCommand_t command;

    enter_square_follow();
    enter_corner_turn(0.0f);
    tick_with_feedback(TRACK_LOST_LINE, 0U, 0.0f, 88.0f);
    command = get_command();

    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "full yaw without a new line must remain in turn search");
    CHECK(command.turn_rpm == -SQUARE_TRACKING_CORNER_SEARCH_RPM,
          "88 degree yaw without a line must reduce to corner search rpm");
}

static void test_turn_frame_timeout_boundary(void)
{
    ModeCommand_t command;

    enter_square_follow();
    enter_corner_turn(0.0f);

    run_without_frames(60U);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "one 60 ms frame gap must not fault a corner turn");
    run_without_frames(TRACKING_DATA_TIMEOUT_MS - 60U);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_TURN,
          "tracking data must remain valid at exactly 120 ms old");

    tick_without_frame();
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FAULT_HOLD,
          "tracking data must fault once it is 121 ms old");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f,
          "turn timeout must latch the shared safe hold command");
}

static void test_alignment_requires_measured_forward_motion(void)
{
    enter_square_follow();
    enter_corner_align(0.0f, -90.0f);

    run_with_feedback_frames(SQUARE_TRACKING_CORNER_ALIGN_CONFIRM_MS + 200U,
                             20U, 0.0f, LINE_MASK,
                             0.019f, -90.0f);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_CORNER_ALIGN &&
              Mode_SquareTrackingGetCornerCount() == 0U,
          "centered alignment below 0.02 m/s must not count a corner");

    run_with_feedback_frames(SQUARE_TRACKING_CORNER_ALIGN_CONFIRM_MS + 40U,
                             20U, 0.0f, LINE_MASK,
                             0.03f, -90.0f);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW &&
              Mode_SquareTrackingGetCornerCount() == 1U,
          "alignment may count only after measured forward motion is present");
}

static void test_four_corner_lap(void)
{
    static const float yaw_deg[] = {
        0.0f, -90.0f, -180.0f, 90.0f, 0.0f
    };
    ModeCommand_t command;

    enter_square_follow();
    for (uint8_t corner = 1U;
         corner <= SQUARE_TRACKING_TARGET_CORNER_COUNT;
         corner++) {
        complete_corner(corner,
                        yaw_deg[corner - 1U],
                        yaw_deg[corner]);
    }

    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FINISHED,
          "four corners must finish one lap");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "finished lap must latch safe hold");
    run_with_frames(500U, 20U, 0.0f, LINE_MASK);
    CHECK(Mode_SquareTrackingGetCornerCount() ==
              SQUARE_TRACKING_TARGET_CORNER_COUNT,
          "finished lap must not restart");
}

static void test_short_line_loss_recovers(void)
{
    ModeCommand_t command;

    enter_square_follow();
    tick_with_frame(TRACK_LOST_LINE, 0U);
    command = get_command();
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_LOST_HOLD &&
              command.drive_enabled != 0U,
          "one lost frame must use bounded moving hold");
    run_with_frames(SQUARE_TRACKING_REFIND_CONFIRM_MS + 30U,
                    20U, 0.0f, LINE_MASK);
    CHECK(Mode_SquareTrackingGetPhase() == MODE_SQUARE_PHASE_FOLLOW,
          "quickly recovered line must resume follow");
}

static void test_course_tracking_api_unchanged(void)
{
    ModeCommand_t command;

    now_ms = 0U;
    Mode_Init();
    Mode_Set(CHASSIS_MODE_TRACKING);
    tick_with_frame(0.0f, 0U);
    command = get_command();
    CHECK(command.drive_enabled == 0U,
          "mode 5 first frame must remain gated");
    run_with_frames(TRACKING_REFIND_CONFIRM_MS + 30U,
                    20U, 0.0f, 0U);
    command = get_command();
    CHECK(Mode_Get() == CHASSIS_MODE_TRACKING &&
              command.drive_enabled != 0U && command.laser_enabled == 0U,
          "mode 5 behavior must remain available");
}

int main(void)
{
    test_corner_transition_has_no_stop_pulse();
    test_follow_turn_is_bounded_and_quiet_near_center();
    test_non_corner_masks_do_not_trigger_corner();
    test_either_single_side_block_triggers_corner();
    test_corner_confirmation_tolerates_speed_jitter();
    test_corner_requires_continuous_frames();
    test_reversing_black_block_does_not_trigger_corner();
    test_runout_stale_fault();
    test_corner_brake_allows_real_settle_delay();
    test_lost_brake_allows_real_settle_delay();
    test_turn_rejects_line_before_minimum_time();
    test_turn_rejects_old_line_below_yaw_gate();
    test_turn_yaw_gate_handles_wraparound();
    test_full_yaw_without_line_uses_slow_search();
    test_turn_frame_timeout_boundary();
    test_alignment_requires_measured_forward_motion();
    test_four_corner_lap();
    test_short_line_loss_recovers();
    test_course_tracking_api_unchanged();

    if (failures != 0) {
        printf("%d square mode assertion(s) failed\n", failures);
        return 1;
    }
    printf("square mode tests passed\n");
    return 0;
}
