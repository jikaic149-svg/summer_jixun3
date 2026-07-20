#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "mode_control.h"
#include "track.h"

static int failures = 0;
static uint32_t now_ms = 0U;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static ModeCommand_t get_command(void)
{
    ModeCommand_t command;

    Mode_GetCommand(&command);
    return command;
}

static uint8_t test_mask_for_count(uint8_t black_count)
{
    static const uint8_t masks[9] = {
        0x00U, 0x08U, 0x18U, 0x1CU, 0x3CU,
        0x3EU, 0x77U, 0x7FU, 0xFFU
    };

    return masks[(black_count <= 8U) ? black_count : 8U];
}

static void tick_frame(float error,
                       uint8_t black_count,
                       float actual_velocity_mps,
                       uint8_t laser_safe)
{
    now_ms++;
    Mode_SetActualVelocity(actual_velocity_mps);
    Mode_SetLaserSafe(laser_safe);
    Mode_TrackingSetSample(error, black_count,
                           test_mask_for_count(black_count), now_ms);
    Mode_Update(now_ms);
}

static void run_frames(uint32_t duration_ms,
                       uint32_t frame_period_ms,
                       float error,
                       uint8_t black_count,
                       float actual_velocity_mps,
                       uint8_t laser_safe)
{
    for (uint32_t elapsed = 1U; elapsed <= duration_ms; elapsed++) {
        now_ms++;
        Mode_SetActualVelocity(actual_velocity_mps);
        Mode_SetLaserSafe(laser_safe);
        if ((elapsed == 1U) ||
            ((frame_period_ms != 0U) &&
             ((elapsed % frame_period_ms) == 0U))) {
            Mode_TrackingSetSample(error, black_count,
                                   test_mask_for_count(black_count), now_ms);
        }
        Mode_Update(now_ms);
    }
}

static void enter_course_tracking(void)
{
    ModeCommand_t command;

    now_ms = 0U;
    Mode_Init();
    Mode_Set(CHASSIS_MODE_TRACKING);
    tick_frame(0.0f, 1U, 0.05f, 0U);
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "tracking entry must start straight instead of falling into hold");
    run_frames(TRACKING_REFIND_CONFIRM_MS + 30U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_WAIT_START_MARKER,
          "fresh frames must arm start-marker search");
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.laser_enabled == 0U,
          "start search must move with laser off");
}

static void test_reverse_markers_are_rejected(void)
{
    ModeCommand_t command;

    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 100U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               -0.03f, 0U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_WAIT_START_MARKER,
          "reverse start-marker contact must not advance the course");
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.laser_enabled == 0U,
          "reverse false marker must keep requesting safe forward recovery");

    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 30U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_WAIT_START_EXIT,
          "the start marker may advance only during measured forward motion");
}

static void advance_to_target_runout(void)
{
    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 30U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    run_frames(TRACKING_START_BLOCK_EXIT_MS + 40U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "leaving A must enter target follow");

    run_frames(TRACKING_TARGET_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_TARGET_RUNOUT,
          "forward target marker must enter target runout");
}

static void test_reverse_runout_is_aborted(void)
{
    ModeCommand_t command;

    advance_to_target_runout();
    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, -0.03f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "target runout must abort when measured motion reverses");
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.laser_enabled == 0U,
          "aborted runout must recover forward without laser");
}

static void test_full_target_and_finish_sequence(void)
{
    ModeCommand_t command;

    advance_to_target_runout();
    run_frames(TRACKING_TARGET_BLOCK_RUNOUT_MS + 10U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE,
          "target runout must finish only after measured forward time");
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.laser_enabled == 0U,
          "target wait-settle must stop with laser off");

    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_LASER_DWELL,
          "laser-safe stop must enter target dwell");
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.laser_enabled != 0U,
          "target dwell may request laser only while stopped and safe");

    run_frames(1000U, 20U, 0.0f,
               TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 0U);
    command = get_command();
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE &&
              command.laser_enabled == 0U,
          "one unsafe cycle must extinguish laser and reset dwell");

    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    run_frames(TRACKING_TARGET_BLOCK_LASER_MS - 100U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.0f, 1U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_LASER_DWELL,
          "interrupted dwell must not reuse elapsed laser time");
    command = get_command();
    CHECK(command.laser_enabled != 0U,
          "laser may remain on during the restarted safe dwell");

    run_frames(150U, 20U, 0.0f,
               TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_TARGET_DEPART,
          "full uninterrupted dwell must start target departure");
    command = get_command();
    CHECK(command.laser_enabled == 0U && command.drive_enabled != 0U,
          "departure must turn laser off before moving");

    run_frames(TRACKING_TARGET_BLOCK_DEPART_MS + 20U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_WAIT_TARGET_EXIT,
          "measured forward departure must reach target-exit wait");
    run_frames(TRACKING_TARGET_BLOCK_EXIT_MS + 40U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_FINISH,
          "leaving the target must arm finish follow");

    run_frames(TRACKING_FINISH_BLOCK_CONFIRM_MS + 30U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FINISH_RUNOUT,
          "forward B marker must enter finish runout");
    run_frames(TRACKING_FINISH_BLOCK_RUNOUT_MS + 10U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    command = get_command();
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FINISHED,
          "finish runout must latch course completion");
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "finished course must latch a laser-off hold");
}

static void test_stand_interrupts_immediately(void)
{
    ModeCommand_t command;

    enter_course_tracking();
    Mode_Set(CHASSIS_MODE_STAND);
    now_ms++;
    Mode_Update(now_ms);
    command = get_command();
    CHECK(Mode_Get() == CHASSIS_MODE_STAND &&
              command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "stand must interrupt moving tracking in one mode update");

    advance_to_target_runout();
    run_frames(TRACKING_TARGET_BLOCK_RUNOUT_MS + 10U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    CHECK(get_command().laser_enabled != 0U,
          "test setup must reach active laser dwell");
    Mode_Set(CHASSIS_MODE_STAND);
    now_ms++;
    Mode_Update(now_ms);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.turn_rpm == 0.0f &&
              command.laser_enabled == 0U,
          "stand must extinguish an active laser in one mode update");
}

static void test_five_sensor_crossbar_and_speed_jitter(void)
{
    ModeCommand_t command;

    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
               20U, 3.0f, TRACKING_BLOCK_BLACK_COUNT_MIN - 1U,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_WAIT_START_MARKER,
          "a normal four-sensor line must not become the A crossbar");

    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
               20U, 3.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               -0.010f, 0U);
    command = get_command();
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_WAIT_START_EXIT,
          "the measured five-sensor crossbar must be accepted despite small speed jitter");
    CHECK(command.drive_enabled != 0U && command.turn_rpm == 0.0f,
          "the chassis must cross the A marker straight before following");
}

static void test_target_marker_requires_six_sensors(void)
{
    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    run_frames(TRACKING_START_BLOCK_EXIT_MS + 40U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "test setup must reach center line following");

    run_frames(TRACKING_TARGET_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f,
               TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN - 1U,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "five active sensors must not become the center marker");

    run_frames(TRACKING_TARGET_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_TARGET_RUNOUT,
          "six active sensors must confirm the center marker");
}

static void test_white_gaps_keep_previous_search_direction(void)
{
    ModeCommand_t command;
    float remembered_turn_rpm;

    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    run_frames(TRACKING_START_BLOCK_EXIT_MS + 40U,
               20U, -1.5f, 2U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "test setup must enter normal line following");

    run_frames(160U, 20U, -1.5f, 2U, 0.05f, 0U);
    remembered_turn_rpm = get_command().turn_rpm;
    tick_frame(TRACK_LOST_LINE, 0U, 0.05f, 0U);
    command = get_command();
    CHECK(command.drive_enabled != 0U &&
              command.velocity_mps == TRACKING_LOST_HOLD_VELOCITY_MPS,
          "a short white gap must keep advancing");
    CHECK(command.turn_rpm == remembered_turn_rpm,
          "the first lost frame must preserve the previous steering command");

    run_frames(TRACKING_LOST_HOLD_MS + TRACKING_LOST_REFIND_MS + 200U,
               20U, TRACK_LOST_LINE, 0U, 0.04f, 0U);
    command = get_command();
    CHECK(Mode_Get() == CHASSIS_MODE_TRACKING &&
              Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "a long intentional white gap must not exit route tracking");
    CHECK(command.drive_enabled != 0U &&
              command.velocity_mps == TRACKING_LOST_VELOCITY_MPS &&
              command.turn_rpm < 0.0f,
          "long-gap search must keep moving in the remembered direction");

    run_frames(TRACKING_REFIND_CONFIRM_MS + 40U,
               20U, -0.5f, 2U, 0.05f, 0U);
    command = get_command();
    CHECK(command.drive_enabled != 0U && command.turn_rpm < 0.0f,
          "a continuously reacquired line must resume following without a stop latch");
}

static void test_stale_sensor_never_holds_mid_route(void)
{
    ModeCommand_t command;

    enter_course_tracking();
    run_frames(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    run_frames(TRACKING_START_BLOCK_EXIT_MS + 40U,
               20U, -1.0f, 2U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "test setup must reach target-follow phase");

    for (uint32_t elapsed = 0U;
         elapsed < TRACKING_DATA_TIMEOUT_MS + 300U;
         elapsed++) {
        now_ms++;
        Mode_SetActualVelocity(0.04f);
        Mode_Update(now_ms);
    }
    command = get_command();
    CHECK(Mode_Get() == CHASSIS_MODE_TRACKING &&
              Mode_TrackingGetPhase() ==
                  MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "sensor timeout must not exit course tracking");
    CHECK(command.drive_enabled != 0U,
          "sensor timeout must continue searching instead of entering hold");
}

static void test_external_task_policy_and_finish_settle(void)
{
    ModeCommand_t command;

    advance_to_target_runout();
    Mode_SetTrackingTaskControl(1U, 0U, 0U, 0U);
    run_frames(TRACKING_TARGET_BLOCK_RUNOUT_MS + 10U,
               20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
               0.05f, 1U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE,
          "external task policy must hold at the center");
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.laser_enabled == 0U,
          "external center hold must begin with laser off");

    Mode_SetMotionSettled(1U);
    Mode_SetTrackingTaskControl(1U, 0U, 0U, 1U);
    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.laser_enabled != 0U,
          "external task laser request must reach mode5 center hold");

    Mode_SetTrackingTaskControl(1U, 1U, 1U, 0U);
    tick_frame(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN, 0.0f, 1U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_TARGET_DEPART,
          "target release must resume mode5 without legacy dwell");
    command = get_command();
    CHECK(command.laser_enabled == 0U,
          "departure must turn the task laser off");
    CHECK(command.drive_enabled != 0U &&
              command.velocity_mps ==
                  TRACKING_TARGET_DEPART_VELOCITY_MPS &&
              command.turn_rpm == 0.0f,
          "post-shot departure must use the dedicated straight boost");

    run_frames(TRACKING_TARGET_BLOCK_DEPART_MS + 20U,
               20U, 0.0f, 1U, 0.05f, 0U);
    run_frames(TRACKING_TARGET_BLOCK_EXIT_MS + 40U,
               20U, 0.0f, 1U, 0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_FINISH,
          "leaving the center must enter finish-follow phase");
    Mode_SetTrackingTaskControl(1U, 1U, 0U, 0U);
    tick_frame(-0.5f, 2U, 0.05f, 0U);
    CHECK(get_command().drive_enabled != 0U,
          "task or vision gating must not hold the chassis mid-route");
    Mode_SetTrackingTaskControl(1U, 1U, 1U, 0U);
    run_frames(TRACKING_FINISH_BLOCK_CONFIRM_MS + 30U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    Mode_SetMotionSettled(0U);
    run_frames(TRACKING_FINISH_BLOCK_RUNOUT_MS + 10U,
               20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
               0.05f, 0U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_FINISH_WAIT_SETTLE,
          "task-controlled B must wait for a true settled sample");
    command = get_command();
    CHECK(command.drive_enabled == 0U && command.laser_enabled == 0U,
          "B settle wait must hold with laser off");
    Mode_SetMotionSettled(1U);
    tick_frame(0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN, 0.0f, 0U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FINISHED,
          "B must finish only after the chassis settles");
}

int main(void)
{
    test_reverse_markers_are_rejected();
    test_five_sensor_crossbar_and_speed_jitter();
    test_target_marker_requires_six_sensors();
    test_white_gaps_keep_previous_search_direction();
    test_stale_sensor_never_holds_mid_route();
    test_reverse_runout_is_aborted();
    test_full_target_and_finish_sequence();
    test_stand_interrupts_immediately();
    test_external_task_policy_and_finish_settle();

    if (failures != 0) {
        printf("%d course tracking assertion(s) failed\n", failures);
        return 1;
    }
    printf("course tracking tests passed\n");
    return 0;
}
