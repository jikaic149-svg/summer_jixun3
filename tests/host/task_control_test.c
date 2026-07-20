#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mode_control.h"
#include "task_control.h"

static int failures = 0;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static TaskInput_t safe_input(uint32_t now_ms, uint8_t route_phase)
{
    TaskInput_t input;

    memset(&input, 0, sizeof(input));
    input.now_ms = now_ms;
    input.route_phase = route_phase;
    input.motion_settled = 1U;
    input.laser_motion_safe = 1U;
    input.vision_fresh = 1U;
    input.vision_valid = 1U;
    input.target_locked = 1U;
    input.aim_ready = 1U;
    input.gimbal_ready = 1U;
    return input;
}

static CompetitionTaskStatus_t get_status(void)
{
    CompetitionTaskStatus_t status;

    Task_GetStatus(&status);
    return status;
}

static TaskOutput_t get_output(void)
{
    TaskOutput_t output;

    Task_GetOutput(&output);
    return output;
}

static uint32_t enter_laser(uint32_t now_ms, uint32_t move_epoch)
{
    TaskInput_t input = safe_input(
        now_ms, MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE);
    uint32_t aim_begin_ms = now_ms;
    TaskPhase_e phase_before = get_status().phase;

    input.move_epoch = move_epoch;
    Task_Update(&input);
    if (phase_before == TASK_PHASE_TRACK_TO_CENTER) {
        CHECK(get_status().phase == TASK_PHASE_LASER_DWELL &&
                  get_output().laser_enabled != 0U,
              "the center marker must start laser dwell immediately");
        return input.now_ms;
    }
    CHECK(get_status().phase == TASK_PHASE_AIM_ACQUIRE,
          "a moved target must enter re-aim");
    for (uint32_t elapsed = 1U;
         elapsed <= TASK_AIM_STABLE_MS + 2U;
         elapsed++) {
        input.now_ms = aim_begin_ms + elapsed;
        Task_Update(&input);
        if (get_status().phase == TASK_PHASE_LASER_DWELL) {
            CHECK(elapsed >= TASK_AIM_STABLE_MS,
                  "re-aim must remain stable for the full interval");
            return input.now_ms;
        }
    }
    CHECK(0, "a full stable aim interval must enter laser dwell");
    return input.now_ms;
}

static uint32_t finish_laser(uint32_t now_ms, uint32_t move_epoch)
{
    TaskInput_t input = safe_input(
        now_ms, MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE);

    input.move_epoch = move_epoch;
    input.now_ms += TASK_LASER_DWELL_MS - 1U;
    Task_Update(&input);
    CHECK(get_status().phase == TASK_PHASE_LASER_DWELL,
          "laser dwell must not finish one millisecond early");
    input.now_ms += 1U;
    Task_Update(&input);
    CHECK(get_output().laser_enabled != 0U,
          "the competition laser must remain on after a shot");
    return input.now_ms;
}

static void test_task1_skips_center_stop(void)
{
    TaskInput_t input;
    TaskOutput_t output;

    Task_Init();
    CHECK(Task_Start(TASK_ID_1) != 0U, "task1 must start");
    output = get_output();
    CHECK(output.route_target_release != 0U &&
              output.route_drive_enabled != 0U &&
              output.laser_enabled != 0U,
          "task1 must release the center with the competition laser on");
    input = safe_input(10U, MODE_TRACKING_PHASE_WAIT_TARGET_EXIT);
    Task_Update(&input);
    CHECK(get_status().phase == TASK_PHASE_TRACK_TO_FINISH,
          "task1 must continue toward B without a shot");
    input.route_phase = MODE_TRACKING_PHASE_FINISHED;
    Task_Update(&input);
    CHECK(get_status().done != 0U,
          "task1 must complete only after B is settled");
}

static void test_task2_marker_starts_uninterrupted_dwell(void)
{
    TaskInput_t input;
    uint32_t now_ms;

    Task_Init();
    CHECK(Task_Start(TASK_ID_2) != 0U, "task2 must start");
    now_ms = enter_laser(100U, 0U);
    input = safe_input(now_ms + 1000U,
                       MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE);
    input.laser_motion_safe = 0U;
    Task_Update(&input);
    CHECK(get_status().phase == TASK_PHASE_LASER_DWELL &&
              get_output().laser_enabled != 0U,
          "marker-triggered dwell must not wait for aim gates");

    now_ms = finish_laser(now_ms, 0U);
    CHECK(get_status().shot_index == 1U &&
              get_status().phase == TASK_PHASE_TRACK_TO_FINISH &&
              get_output().route_target_release != 0U,
          "task2 must depart for B after the marker-triggered shot");
    input = safe_input(now_ms + 1U,
                       MODE_TRACKING_PHASE_FINISH_WAIT_SETTLE);
    input.motion_settled = 0U;
    input.laser_motion_safe = 0U;
    Task_Update(&input);
    CHECK(get_status().done == 0U,
          "task2 must not complete before B settles");
    input.route_phase = MODE_TRACKING_PHASE_FINISHED;
    Task_Update(&input);
    CHECK(get_status().done == 0U,
          "a finished route flag without a settled chassis is insufficient");
    input.motion_settled = 1U;
    Task_Update(&input);
    CHECK(get_status().done != 0U,
          "task2 must complete after B is truly settled");
}

static void test_task3_waits_for_move_and_stays_center(void)
{
    TaskInput_t input;
    uint32_t now_ms;

    Task_Init();
    CHECK(Task_Start(TASK_ID_3) != 0U, "task3 must start");
    now_ms = finish_laser(enter_laser(100U, 7U), 7U);
    CHECK(get_status().phase == TASK_PHASE_WAIT_TARGET_MOVE,
          "task3 must wait after its first shot");
    input = safe_input(now_ms + 100U,
                       MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE);
    input.move_epoch = 7U;
    Task_Update(&input);
    CHECK(get_status().phase == TASK_PHASE_WAIT_TARGET_MOVE &&
              get_output().laser_enabled != 0U,
          "an unchanged epoch must keep task3 stopped with laser on");
    input.move_epoch = 8U;
    Task_Update(&input);
    CHECK(get_status().phase == TASK_PHASE_AIM_ACQUIRE,
          "a new move epoch must start the second aim");
    now_ms = finish_laser(enter_laser(input.now_ms + 1U, 8U), 8U);
    (void)now_ms;
    CHECK(get_status().done != 0U &&
              get_output().route_target_release == 0U,
          "task3 must complete and remain at the center");
}

static void complete_two_shots(TaskId_e task_id)
{
    TaskInput_t input;
    uint32_t now_ms;

    Task_Init();
    CHECK(Task_Start(task_id) != 0U, "two-shot task must start");
    now_ms = finish_laser(enter_laser(100U, 1U), 1U);
    input = safe_input(now_ms + 10U,
                       MODE_TRACKING_PHASE_TARGET_WAIT_SETTLE);
    input.move_epoch = 2U;
    Task_Update(&input);
    (void)finish_laser(enter_laser(input.now_ms + 1U, 2U), 2U);
}

static void test_task4_lock_return(void)
{
    TaskInput_t input;
    TaskOutput_t output;

    complete_two_shots(TASK_ID_4);
    CHECK(get_status().phase == TASK_PHASE_TRACK_TO_FINISH,
          "task4 must return to B after two shots");
    input = safe_input(10000U, MODE_TRACKING_PHASE_FOLLOW_TO_FINISH);
    input.target_locked = 0U;
    Task_Update(&input);
    output = get_output();
    CHECK(output.route_drive_enabled == 0U &&
              output.laser_enabled != 0U,
          "task4 target loss must preserve the always-on task laser");
    input.target_locked = 1U;
    Task_Update(&input);
    output = get_output();
    CHECK(output.route_drive_enabled != 0U &&
              output.laser_enabled != 0U,
          "task4 relock must resume locked return");
}

static void test_task5_milestones_and_finish(void)
{
    TaskInput_t input;
    CompetitionTaskStatus_t status;
    char frame[40];
    size_t len;

    complete_two_shots(TASK_ID_5);
    status = get_status();
    CHECK((status.milestones &
           (TASK_MILESTONE_TRACK_ROUTE |
            TASK_MILESTONE_SHOT1_DONE |
            TASK_MILESTONE_SHOT2_DONE)) ==
          (TASK_MILESTONE_TRACK_ROUTE |
           TASK_MILESTONE_SHOT1_DONE |
           TASK_MILESTONE_SHOT2_DONE),
          "task5 must report route and both shot milestones");
    input = safe_input(12000U, MODE_TRACKING_PHASE_FINISHED);
    Task_Update(&input);
    status = get_status();
    CHECK(status.done != 0U &&
              (status.milestones & TASK_MILESTONE_LOCK_RETURN_DONE) != 0U,
          "task5 must complete its locked-return milestone at settled B");
    len = Task_FormatStatusFrame(frame, sizeof(frame));
    CHECK((len != 0U) &&
               (memcmp(frame, "T,5,6,2,1,1,15,1\n", len) == 0),
          "task status frame must expose task, phase, shots and milestones");
}

int main(void)
{
    test_task1_skips_center_stop();
    test_task2_marker_starts_uninterrupted_dwell();
    test_task3_waits_for_move_and_stays_center();
    test_task4_lock_return();
    test_task5_milestones_and_finish();

    if (failures != 0) {
        printf("%d task-control assertion(s) failed\n", failures);
        return 1;
    }
    printf("task control tests passed\n");
    return 0;
}
