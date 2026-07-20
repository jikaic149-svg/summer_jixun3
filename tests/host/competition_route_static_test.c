#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "mode_control.h"
#include "task_control.h"
#include "track.h"

static uint32_t now_ms;
static uint32_t move_epoch;
static uint32_t unexpected_midroute_holds;
static uint32_t mode_exit_count;
static int failures;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static uint8_t phase_requires_route_motion(ModeTrackingPhase_e phase)
{
    switch (phase) {
    case MODE_TRACKING_PHASE_WAIT_START_MARKER:
    case MODE_TRACKING_PHASE_WAIT_START_EXIT:
    case MODE_TRACKING_PHASE_FOLLOW_TO_TARGET:
    case MODE_TRACKING_PHASE_TARGET_RUNOUT:
    case MODE_TRACKING_PHASE_TARGET_DEPART:
    case MODE_TRACKING_PHASE_WAIT_TARGET_EXIT:
    case MODE_TRACKING_PHASE_FOLLOW_TO_FINISH:
    case MODE_TRACKING_PHASE_FINISH_RUNOUT:
        return 1U;
    case MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE:
    case MODE_TRACKING_PHASE_TARGET_LASER_DWELL:
    case MODE_TRACKING_PHASE_FINISH_WAIT_SETTLE:
    case MODE_TRACKING_PHASE_FINISHED:
    default:
        return 0U;
    }
}

static uint8_t simulation_mask_for_count(uint8_t black_count)
{
    static const uint8_t masks[9] = {
        0x00U, 0x08U, 0x18U, 0x1CU, 0x3CU,
        0x3EU, 0x77U, 0x7FU, 0xFFU
    };

    return masks[(black_count <= 8U) ? black_count : 8U];
}

static void simulation_step(float error,
                            uint8_t black_count,
                            float actual_velocity_mps,
                            uint8_t motion_settled,
                            uint8_t vision_ok,
                            uint8_t publish_track_frame)
{
    TaskInput_t input;
    TaskOutput_t output;
    ModeCommand_t command;
    ModeTrackingPhase_e phase;

    now_ms++;
    input.now_ms = now_ms;
    input.move_epoch = move_epoch;
    input.route_phase = (uint8_t)Mode_TrackingGetPhase();
    input.motion_settled = motion_settled;
    input.laser_motion_safe = motion_settled;
    input.vision_fresh = vision_ok;
    input.vision_valid = vision_ok;
    input.target_locked = vision_ok;
    input.aim_ready = vision_ok;
    input.gimbal_ready = vision_ok;
    Task_Update(&input);
    Task_GetOutput(&output);

    Mode_SetTrackingTaskControl(output.active,
                                output.route_target_release,
                                output.route_drive_enabled,
                                output.laser_enabled);
    Mode_SetActualVelocity(actual_velocity_mps);
    Mode_SetMotionSettled(motion_settled);
    Mode_SetLaserSafe(motion_settled);
    if (publish_track_frame != 0U) {
        Mode_TrackingSetSample(error, black_count,
                               simulation_mask_for_count(black_count),
                               now_ms);
    }
    Mode_Update(now_ms);

    Mode_GetCommand(&command);
    phase = Mode_TrackingGetPhase();
    if (Mode_Get() != CHASSIS_MODE_TRACKING) {
        mode_exit_count++;
    }
    if ((phase_requires_route_motion(phase) != 0U) &&
        (command.drive_enabled == 0U)) {
        unexpected_midroute_holds++;
    }
}

static void simulation_run(uint32_t duration_ms,
                           uint32_t frame_period_ms,
                           float error,
                           uint8_t black_count,
                           float actual_velocity_mps,
                           uint8_t motion_settled,
                           uint8_t vision_ok)
{
    for (uint32_t elapsed = 1U; elapsed <= duration_ms; elapsed++) {
        uint8_t publish =
            ((elapsed == 1U) ||
             ((frame_period_ms != 0U) &&
              ((elapsed % frame_period_ms) == 0U))) ? 1U : 0U;

        simulation_step(error, black_count, actual_velocity_mps,
                        motion_settled, vision_ok, publish);
    }
}

static CompetitionTaskStatus_t task_status(void)
{
    CompetitionTaskStatus_t status;

    Task_GetStatus(&status);
    return status;
}

static ModeCommand_t mode_command(void)
{
    ModeCommand_t command;

    Mode_GetCommand(&command);
    return command;
}

int main(void)
{
    CompetitionTaskStatus_t status;
    ModeCommand_t command;

    Mode_Init();
    Task_Init();
    CHECK(Task_Start(TASK_ID_5) != 0U, "task5 must start");
    Mode_Set(CHASSIS_MODE_TRACKING);

    /* A start: straight approach, confirm the first wide crossbar, then line. */
    simulation_run(TRACKING_REFIND_CONFIRM_MS + 40U,
                   20U, 0.0f, 1U, 0.05f, 0U, 1U);
    simulation_run(TRACKING_START_BLOCK_CONFIRM_MS + 40U,
                   20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
                   0.05f, 0U, 1U);
    simulation_run(TRACKING_START_BLOCK_EXIT_MS + 40U,
                   20U, -0.8f, 2U, 0.05f, 0U, 1U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_TARGET,
          "A crossbar must lead to center line following");

    /* Intentional white gaps and a full sensor timeout must not stop route. */
    simulation_run(220U, 20U, -1.2f, 2U, 0.05f, 0U, 1U);
    simulation_run(TRACKING_LOST_HOLD_MS + 80U,
                   20U, TRACK_LOST_LINE, 0U, 0.04f, 0U, 1U);
    simulation_run(TRACKING_DATA_TIMEOUT_MS + 250U,
                   0U, TRACK_LOST_LINE, 0U, 0.04f, 0U, 1U);
    CHECK(mode_command().drive_enabled != 0U,
          "track timeout before center must keep searching");
    simulation_run(TRACKING_REFIND_CONFIRM_MS + 40U,
                   20U, -0.4f, 2U, 0.05f, 0U, 1U);

    /* Center marker: preserve the original runout and then shoot for 3 s. */
    simulation_run(TRACKING_TARGET_BLOCK_CONFIRM_MS + 40U,
                   20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.05f, 0U, 1U);
    simulation_run(TRACKING_TARGET_BLOCK_RUNOUT_MS + 30U,
                   20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.05f, 0U, 1U);
    simulation_run(3U, 1U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.0f, 1U, 1U);
    status = task_status();
    command = mode_command();
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE,
          "center marker must enter the allowed parking phase");
    CHECK(status.phase == TASK_PHASE_LASER_DWELL &&
              command.drive_enabled == 0U && command.laser_enabled != 0U,
          "center parking must immediately start the first laser dwell");
    simulation_run(TASK_LASER_DWELL_MS + 2U,
                   20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.0f, 1U, 1U);
    status = task_status();
    CHECK(status.shot_index == 1U &&
              status.phase == TASK_PHASE_WAIT_TARGET_MOVE,
          "first 3 s shot must finish and wait for target movement");

    /* Target movement, stable reacquire and the second complete 3 s shot. */
    move_epoch++;
    simulation_step(0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                    0.0f, 1U, 1U, 1U);
    CHECK(task_status().phase == TASK_PHASE_AIM_ACQUIRE,
          "move epoch must start second-shot reacquisition");
    simulation_run(TASK_AIM_STABLE_MS + 2U,
                   20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.0f, 1U, 1U);
    CHECK(task_status().phase == TASK_PHASE_LASER_DWELL,
          "stable reacquisition must start the second dwell");
    simulation_run(TASK_LASER_DWELL_MS + 2U,
                   20U, 0.0f, TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN,
                   0.0f, 1U, 1U);
    status = task_status();
    CHECK(status.shot_index == 2U &&
              status.phase == TASK_PHASE_TRACK_TO_FINISH,
          "second shot must release the route toward B");

    /* Depart center. Vision loss must not stop route or the task laser. */
    simulation_run(TRACKING_TARGET_BLOCK_DEPART_MS + 20U,
                   20U, 0.0f, 1U, 0.05f, 0U, 1U);
    simulation_run(TRACKING_TARGET_BLOCK_EXIT_MS + 40U,
                   20U, 0.5f, 2U, 0.05f, 0U, 1U);
    CHECK(Mode_TrackingGetPhase() == MODE_TRACKING_PHASE_FOLLOW_TO_FINISH,
          "center departure must resume B line following");
    simulation_run(250U, 20U, 0.8f, 2U, 0.05f, 0U, 0U);
    command = mode_command();
    CHECK(command.drive_enabled != 0U && command.laser_enabled != 0U,
          "return vision loss must keep tracking with the task laser on");
    simulation_run(TRACKING_DATA_TIMEOUT_MS + 250U,
                   0U, TRACK_LOST_LINE, 0U, 0.04f, 0U, 1U);
    CHECK(mode_command().drive_enabled != 0U,
          "return track timeout must keep searching for the line");
    simulation_run(TRACKING_REFIND_CONFIRM_MS + 40U,
                   20U, 0.4f, 2U, 0.05f, 0U, 1U);

    /* Only the confirmed B wide crossbar may end route motion. */
    simulation_run(TRACKING_FINISH_BLOCK_CONFIRM_MS + 40U,
                   20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
                   0.05f, 0U, 1U);
    simulation_run(TRACKING_FINISH_BLOCK_RUNOUT_MS + 30U,
                   20U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
                   0.05f, 0U, 1U);
    CHECK(Mode_TrackingGetPhase() ==
              MODE_TRACKING_PHASE_FINISH_WAIT_SETTLE,
          "B wide crossbar must enter the final settle hold");
    CHECK(mode_command().drive_enabled == 0U,
          "B settle is the allowed final route hold");
    simulation_run(3U, 1U, 0.0f, TRACKING_BLOCK_BLACK_COUNT_MIN,
                   0.0f, 1U, 1U);
    status = task_status();

    CHECK(mode_exit_count == 0U,
          "the chassis mode must remain TRACKING for the whole route");
    CHECK(unexpected_midroute_holds == 0U,
          "no route phase may fall into HOLD before a marker");
    CHECK(status.done != 0U && status.shot_index == 2U,
          "task5 must finish at B after both shots");
    CHECK(status.milestones ==
              (TASK_MILESTONE_TRACK_ROUTE |
               TASK_MILESTONE_SHOT1_DONE |
               TASK_MILESTONE_SHOT2_DONE |
               TASK_MILESTONE_LOCK_RETURN_DONE),
          "task5 must publish all four milestones");

    if (failures != 0) {
        printf("STATIC SIM FAIL: %d assertion(s), midroute_hold=%lu, "
               "mode_exit=%lu\n",
               failures,
               (unsigned long)unexpected_midroute_holds,
               (unsigned long)mode_exit_count);
        return 1;
    }

    printf("STATIC SIM PASS: task5 A->center->2x3s->B, "
           "midroute_hold=0, mode_exit=0, shots=%u, milestones=0x%02X\n",
           status.shot_index, status.milestones);
    return 0;
}
