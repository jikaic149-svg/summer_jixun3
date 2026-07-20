#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "mode_private.h"
#include "track.h"

static int failures = 0;
static uint32_t line_follow_calls = 0U;
static uint32_t line_follow_reset_calls = 0U;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

void Mode_StopBuild(ModeCommand_t *command)
{
    command->velocity_mps = 0.0f;
    command->bias_deg = BALANCE_MODE_STAND_BIAS_DEG;
    command->turn_rpm = 0.0f;
    command->drive_enabled = 0U;
    command->tracking_active = 0U;
    command->laser_enabled = 0U;
}

void Mode_TrackingBuildLineFollow(ModeCommand_t *command,
                                  float error,
                                  uint8_t sensor_flags,
                                  float dt_sec)
{
    (void)sensor_flags;
    (void)dt_sec;
    line_follow_calls++;
    command->velocity_mps = 0.08f;
    command->bias_deg = 0.0f;
    command->turn_rpm = 4.0f * error;
    command->drive_enabled = 1U;
    command->tracking_active = 1U;
    command->laser_enabled = 0U;
}

void Mode_TrackingReset(void)
{
    line_follow_reset_calls++;
}

static ModeCommand_t build_with_drive(float error,
                                      uint8_t black_count,
                                      uint8_t black_mask,
                                      uint8_t sensor_flags,
                                      uint8_t vision_ready,
                                      uint8_t drive_enabled)
{
    ModeCommand_t command;

    Mode_TargetAimBuild(&command, error, black_count, black_mask,
                        sensor_flags, vision_ready, drive_enabled, 0.001f);
    CHECK(command.tracking_active != 0U,
          "target task must remain marked as tracking");
    CHECK(command.laser_enabled != 0U,
          "laser must remain on throughout the target task");
    return command;
}

static ModeCommand_t build(float error,
                           uint8_t black_count,
                           uint8_t black_mask,
                           uint8_t sensor_flags,
                           uint8_t vision_ready)
{
    return build_with_drive(error, black_count, black_mask, sensor_flags,
                            vision_ready, 1U);
}

static void run_frames(uint32_t duration_ms,
                       uint32_t frame_period_ms,
                       float error,
                       uint8_t black_count,
                       uint8_t black_mask,
                       uint8_t vision_ready)
{
    for (uint32_t elapsed = 1U; elapsed <= duration_ms; elapsed++) {
        uint8_t flags = MODE_TRACKING_SENSOR_FRESH;

        if ((elapsed == 1U) ||
            ((frame_period_ms != 0U) &&
             ((elapsed % frame_period_ms) == 0U))) {
            flags |= MODE_TRACKING_SENSOR_NEW_FRAME;
        }
        (void)build(error, black_count, black_mask, flags, vision_ready);
    }
}

static void enter_follow_line(void)
{
    ModeCommand_t command;

    Mode_TargetAimReset();
    line_follow_calls = 0U;
    line_follow_reset_calls = 0U;
    command = build(0.0f, 1U, 0x08U, 0U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_WAIT_LINE,
          "vision calibration must open the line-acquisition gate");
    CHECK(command.drive_enabled == 0U,
          "calibration completion must not start on an old track frame");

    run_frames(TARGET_AIM_LINE_CONFIRM_MS + 30U,
               10U, 0.5f, 1U, 0x08U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "continuous fresh line frames must enter line following");
    CHECK(line_follow_calls != 0U,
          "line-follow phase must call the shared tracking core");
}

static void test_calibration_and_line_gates(void)
{
    ModeCommand_t command;

    Mode_TargetAimReset();
    command = build(0.5f, 1U, 0x08U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    0U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_WAIT_CALIBRATION,
          "track data must not bypass vision calibration");
    CHECK(command.drive_enabled == 0U,
          "calibration gate must hold the chassis");

    (void)build(0.5f, 1U, 0x08U, 0U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_WAIT_LINE,
          "vision-ready must advance only to line acquisition");
    run_frames((TARGET_AIM_LINE_CONFIRM_MS > 10U) ?
                   TARGET_AIM_LINE_CONFIRM_MS - 10U : 1U,
               10U, 0.5f, 1U, 0x08U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_WAIT_LINE,
          "short line contact must not start the task");

    (void)build(TRACK_LOST_LINE, 0U, 0U,
                MODE_TRACKING_SENSOR_FRESH |
                MODE_TRACKING_SENSOR_NEW_FRAME,
                1U);
    run_frames(TARGET_AIM_LINE_CONFIRM_MS + 30U,
               10U, 0.5f, 1U, 0x08U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "a broken line confirmation must restart from zero");
}

static void test_static_aim_requires_explicit_drive_release(void)
{
    ModeCommand_t command;

    Mode_TargetAimReset();
    command = build_with_drive(0.5f, 1U, 0x08U,
                               MODE_TRACKING_SENSOR_FRESH |
                               MODE_TRACKING_SENSOR_NEW_FRAME,
                               1U, 0U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_STATIC_AIM,
          "aim command must remain in the static yaw test phase");
    CHECK(command.drive_enabled == 0U,
          "static yaw test must hold the chassis stopped");

    (void)build_with_drive(0.5f, 1U, 0x08U,
                           MODE_TRACKING_SENSOR_FRESH |
                           MODE_TRACKING_SENSOR_NEW_FRAME,
                           0U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_STATIC_AIM,
          "RUN must remain static until a fresh calibrated target is ready");

    (void)build_with_drive(0.5f, 1U, 0x08U,
                           MODE_TRACKING_SENSOR_FRESH |
                           MODE_TRACKING_SENSOR_NEW_FRAME,
                           1U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_WAIT_LINE,
          "shoot command must explicitly release line acquisition");
    CHECK(line_follow_reset_calls != 0U,
          "releasing static aim must clear old line-follow controller state");
}

static void test_white_confirmation_and_latched_stop(void)
{
    ModeCommand_t command;
    uint32_t calls_before_white;

    enter_follow_line();
    command = build(0.75f, 1U, 0x10U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    calls_before_white = line_follow_calls;
    CHECK(command.drive_enabled != 0U,
          "line following must command forward motion");

    command = build(0.75f, 0U, 0U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "first centered white frame must remain in finish confirmation");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              line_follow_calls == calls_before_white,
          "finish confirmation must stop without reusing an old turn command");

    run_frames(TARGET_AIM_WHITE_CONFIRM_MS + 30U,
               10U, TRACK_LOST_LINE, 0U, 0U, 1U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_STOP_CONFIRM,
          "only continuous centered all-white frames may enter stop confirmation");

    command = build(0.25f, 1U, 0x08U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_STOP_CONFIRM,
          "one line frame must not abort a candidate stop");
    CHECK(command.drive_enabled == 0U,
          "line noise during stop confirmation must remain stopped");

    command = build(0.0f, 8U, 0xFFU,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_STOP_CONFIRM &&
              command.drive_enabled == 0U,
          "wide black after white must never restart forward motion");

    run_frames(TARGET_AIM_STOP_ABORT_CONFIRM_MS + 30U,
               10U, 0.25f, 1U, 0x08U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "only a continuous normal line may abort stop confirmation");

    run_frames(2U * TARGET_AIM_WHITE_CONFIRM_MS + 30U,
               10U, TRACK_LOST_LINE, 0U, 0U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_AIM_HOLD,
          "continuous all-white frames must enter aim hold");
    command = build(0.5f, 1U, 0x08U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f,
          "aim hold must latch a stopped chassis");
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_AIM_HOLD,
          "later line frames must not restart a completed approach");
}

static void test_target_turn_is_gently_limited(void)
{
    ModeCommand_t command;

    enter_follow_line();
    command = build(5.0f, 1U, 0x08U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "a narrow line must remain under forward control");
    CHECK(command.turn_rpm == TARGET_AIM_TURN_LIMIT_RPM,
          "TARGET must clamp shared line-follow steering to a gentle limit");
}

static void test_wide_black_start_probe_is_bounded(void)
{
    ModeCommand_t command;

    Mode_TargetAimReset();
    (void)build(0.0f, 1U, 0x08U, 0U, 1U);
    command = build(0.0f, 8U, 0xFFU,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U && command.turn_rpm == 0.0f,
          "wide black at startup must use a straight low-speed probe");
    CHECK(command.velocity_mps ==
              TARGET_AIM_WIDE_START_PROBE_VELOCITY_MPS,
          "startup probe must use its bounded velocity");

    run_frames(TARGET_AIM_WIDE_START_PROBE_MAX_MS + 20U,
               10U, 0.0f, 8U, 0xFFU, 1U);
    command = build(0.0f, 8U, 0xFFU,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled == 0U,
          "wide-black startup probe must stop after its time limit");
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_WAIT_LINE,
          "probe timeout must not pretend that a line was acquired");

    run_frames(TARGET_AIM_LINE_CONFIRM_MS + 30U,
               10U, 0.25f, 2U, 0x18U, 1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "a normal line must remain acquirable after probe timeout");
}

static void test_stale_data_stops_without_finishing(void)
{
    ModeCommand_t command;

    enter_follow_line();
    command = build(TRACK_LOST_LINE, 0U, 0U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled == 0U,
          "first white frame must stop immediately");
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "unconfirmed white must remain in line-follow phase");

    (void)build(TRACK_LOST_LINE, 0U, 0U, 0U, 1U);
    (void)build(TRACK_LOST_LINE, 0U, 0U,
                MODE_TRACKING_SENSOR_FRESH |
                MODE_TRACKING_SENSOR_NEW_FRAME,
                1U);
    CHECK(Mode_TargetAimGetPhase() ==
              MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "a stale gap must restart white timing without finishing");
}

static void test_edge_loss_uses_bounded_line_recovery(void)
{
    ModeCommand_t command;
    uint32_t calls_before_loss;

    enter_follow_line();
    command = build(5.0f, 1U, 0x80U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "an edge line must remain under line-follow control");
    calls_before_loss = line_follow_calls;

    command = build(TRACK_LOST_LINE, 0U, 0U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "edge loss must not enter stop confirmation");
    CHECK(command.drive_enabled != 0U && command.turn_rpm > 0.0f &&
              line_follow_calls > calls_before_loss,
          "edge loss must continue shared line-follow in the last direction");

    command = build(0.0f, 0U, 0U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "an ambiguous mask=0 frame must not latch a permanent hold");

    run_frames(TARGET_AIM_WIDE_FOLLOW_PROBE_MAX_MS + 30U,
               10U, TRACK_LOST_LINE, 0U, 0U, 1U);
    command = build(TRACK_LOST_LINE, 0U, 0U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f,
          "persistent edge loss must stop after the bounded recovery window");
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "lost-recovery timeout must not masquerade as a detected finish");

    command = build(-0.5f, 2U, 0x18U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "a real line must remain recoverable after loss timeout");
}

static void test_wide_black_stops_without_finishing(void)
{
    ModeCommand_t command;

    enter_follow_line();
    command = build(4.3f, 6U, 0xFCU,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "one wide-black frame must not chatter the drive command off");

    run_frames(TARGET_AIM_WIDE_FOLLOW_PROBE_MAX_MS + 30U,
               10U, 4.3f, 6U, 0xFCU, 1U);
    command = build(4.3f, 6U, 0xFCU,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(Mode_TargetAimGetPhase() == MODE_TARGET_AIM_PHASE_FOLLOW_LINE,
          "a wide black pattern must not be treated as the white finish");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f,
          "persistent 0xfc/0xff probe must stop at its distance bound");

    command = build(0.25f, 2U, 0x18U,
                    MODE_TRACKING_SENSOR_FRESH |
                    MODE_TRACKING_SENSOR_NEW_FRAME,
                    1U);
    CHECK(command.drive_enabled != 0U,
          "a normal line must reset the bounded wide-pattern probe");
}

int main(void)
{
    test_calibration_and_line_gates();
    test_static_aim_requires_explicit_drive_release();
    test_white_confirmation_and_latched_stop();
    test_target_turn_is_gently_limited();
    test_wide_black_start_probe_is_bounded();
    test_stale_data_stops_without_finishing();
    test_wide_black_stops_without_finishing();
    test_edge_loss_uses_bounded_line_recovery();

    if (failures != 0) {
        printf("%d target aim assertion(s) failed\n", failures);
        return 1;
    }
    printf("target aim mode tests passed\n");
    return 0;
}
