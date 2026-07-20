#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chassis_serial_cmd.h"
#include "gimbal_task.h"
#include "vision.h"

volatile uint32_t chassis_serial_cmd_rx_count = 0U;
volatile uint32_t chassis_serial_cmd_ok_count = 0U;
volatile uint8_t chassis_serial_cmd_last_type = 0U;
volatile float chassis_serial_cmd_last_value = 0.0f;

static int failures = 0;
static uint32_t mode_calls = 0U;
static uint32_t wheel_calls = 0U;
static uint32_t target_drive_calls = 0U;
static uint8_t last_target_drive_enabled = 0U;
static uint32_t target_ready_calls = 0U;
static uint8_t last_target_ready = 0U;
static uint32_t vision_laser_calls = 0U;
static uint8_t last_vision_laser_enabled = 0U;
static uint32_t calibration_laser_calls = 0U;
static uint8_t last_calibration_laser_enabled = 0U;
static uint8_t calibration_laser_accepted = 1U;
static ChassisMode_e last_mode = CHASSIS_MODE_STAND;
static float last_wheel_rpm = 0.0f;
static uint32_t task_start_calls = 0U;
static uint32_t task_stop_calls = 0U;
static uint8_t last_task_id = 0U;
static Vision_Instance_t vision;

static void expect_mode(const char *text, ChassisMode_e expected);

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

void Chassis_SetControlMode(ChassisMode_e mode)
{
    mode_calls++;
    last_mode = mode;
}

void WheelSpeed_SetTargetRpm(float rpm)
{
    wheel_calls++;
    last_wheel_rpm = rpm;
}

void Mode_SetTargetDriveEnabled(uint8_t enabled)
{
    target_drive_calls++;
    last_target_drive_enabled = enabled;
}

void Mode_SetTargetVisionReady(uint8_t ready)
{
    target_ready_calls++;
    last_target_ready = ready;
}

void Gimbal_SetVisionLaserRequest(uint8_t enabled)
{
    vision_laser_calls++;
    last_vision_laser_enabled = enabled;
}

uint8_t Gimbal_SetCalibrationLaserRequest(uint8_t enabled)
{
    calibration_laser_calls++;
    last_calibration_laser_enabled = enabled;
    return calibration_laser_accepted;
}

uint8_t Chassis_StartCompetitionTask(uint8_t task_id)
{
    task_start_calls++;
    last_task_id = task_id;
    return 1U;
}

void Chassis_StopCompetitionTask(void)
{
    task_stop_calls++;
}

void Balance_SetManualBiasDeg(float bias_deg)
{
    (void)bias_deg;
}

void Balance_SetPidRuntimeParam(BalancePidRuntimeParam_e param, float value)
{
    (void)param;
    (void)value;
}

static Vision_ParseResult_t route_line(const char *text)
{
    Vision_ParseResult_t result =
        Vision_ParseLine(&vision,
                         (const uint8_t *)text,
                         (uint16_t)strlen(text));

    if (result == VISION_PARSE_NOT_VISION) {
        (void)ChassisSerialCmd_HandleLine((const uint8_t *)text,
                                          (uint8_t)strlen(text));
    }
    return result;
}

static void reset_calls(void)
{
    mode_calls = 0U;
    wheel_calls = 0U;
    target_drive_calls = 0U;
    last_target_drive_enabled = 0U;
    target_ready_calls = 0U;
    last_target_ready = 0U;
    vision_laser_calls = 0U;
    last_vision_laser_enabled = 0U;
    calibration_laser_calls = 0U;
    last_calibration_laser_enabled = 0U;
    calibration_laser_accepted = 1U;
    last_mode = CHASSIS_MODE_STAND;
    last_wheel_rpm = 0.0f;
    task_start_calls = 0U;
    task_stop_calls = 0U;
    last_task_id = 0U;
}

static void expect_target_mode(const char *text, uint8_t drive_enabled)
{
    expect_mode(text, CHASSIS_MODE_TARGET_AIM);
    CHECK(target_drive_calls == 1U &&
              last_target_drive_enabled == drive_enabled,
          "target command must select the expected static/full task stage");
    if (drive_enabled == 0U) {
        CHECK(target_ready_calls == 1U && last_target_ready == 0U,
              "static aim command must clear stale calibration readiness");
    } else {
        CHECK(target_ready_calls == 0U,
              "full task command must not forge vision readiness");
    }
}

static void expect_mode(const char *text, ChassisMode_e expected)
{
    reset_calls();
    CHECK(route_line(text) == VISION_PARSE_NOT_VISION,
          "mode command must not be classified as vision");
    CHECK(mode_calls == 1U && last_mode == expected && wheel_calls == 0U,
          "mode command must select only the expected mode");
}

static void expect_rejected(const char *text)
{
    reset_calls();
    (void)route_line(text);
    CHECK(mode_calls == 0U && wheel_calls == 0U,
          "invalid explicit mode must not alter mode or wheel speed");
}

static void test_mode_compatibility(void)
{
    static const ChassisMode_e legacy_modes[] = {
        CHASSIS_MODE_STAND,
        CHASSIS_MODE_FORWARD,
        CHASSIS_MODE_BACKWARD,
        CHASSIS_MODE_TURN_LEFT,
        CHASSIS_MODE_TURN_RIGHT,
        CHASSIS_MODE_TRACKING
    };

    CHECK(CHASSIS_MODE_STAND == 0 && CHASSIS_MODE_TRACKING == 5 &&
              CHASSIS_MODE_SQUARE_TRACKING == 6 &&
              CHASSIS_MODE_TARGET_AIM == 7,
          "mode numeric ABI must append square 6 and target aim 7");

    for (uint8_t i = 0U; i < 6U; i++) {
        char digit[2] = {(char)('0' + i), '\0'};
        expect_mode(digit, legacy_modes[i]);
    }
    expect_mode("track", CHASSIS_MODE_TRACKING);
    expect_mode("vision", CHASSIS_MODE_STAND);
    CHECK(vision_laser_calls == 1U && last_vision_laser_enabled == 1U,
          "vision command must request laser-only calibration mode");
    expect_mode("stand", CHASSIS_MODE_STAND);
    CHECK(vision_laser_calls == 1U && last_vision_laser_enabled == 0U,
          "stand command must clear the vision laser request");
    expect_mode("mode 5", CHASSIS_MODE_TRACKING);
    expect_mode("square", CHASSIS_MODE_SQUARE_TRACKING);
    expect_mode("box", CHASSIS_MODE_SQUARE_TRACKING);
    expect_mode("mode 6", CHASSIS_MODE_SQUARE_TRACKING);
    expect_mode("mode6", CHASSIS_MODE_SQUARE_TRACKING);
    expect_mode("m6", CHASSIS_MODE_SQUARE_TRACKING);
    expect_target_mode("shoot", 1U);
    expect_target_mode("aim", 0U);
    expect_target_mode("target", 0U);
    expect_target_mode("mode 7", 1U);
    expect_target_mode("mode7", 1U);
    expect_target_mode("m7", 1U);

    reset_calls();
    CHECK(route_line("6") == VISION_PARSE_NOT_VISION,
          "bare 6 must not be classified as vision");
    CHECK(mode_calls == 0U && wheel_calls == 1U && last_wheel_rpm == 6.0f,
          "bare 6 must retain the legacy wheel-speed meaning");

    reset_calls();
    CHECK(route_line("7") == VISION_PARSE_NOT_VISION,
          "bare 7 must not be classified as vision");
    CHECK(mode_calls == 0U && wheel_calls == 1U && last_wheel_rpm == 7.0f,
          "bare 7 must retain the wheel-speed meaning");
}

static void test_invalid_explicit_modes(void)
{
    static const char *const invalid[] = {
        "mode 8", "m8", "m 8", "mode6x", "track6", "square6",
        "square,garbage", "mode 6,0,2", "5x", "6x", "6junk",
        "+350", "-350", "80000", "-80000"
    };

    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(invalid) / sizeof(invalid[0]));
         i++) {
        expect_rejected(invalid[i]);
    }
}

static void test_direct_wheel_speed_limit(void)
{
    reset_calls();
    (void)route_line("300");
    CHECK(wheel_calls == 1U && last_wheel_rpm == 300.0f,
          "+300 rpm must be accepted at the direct speed limit");

    reset_calls();
    (void)route_line("-300");
    CHECK(wheel_calls == 1U && last_wheel_rpm == -300.0f,
          "-300 rpm must be accepted at the direct speed limit");
}

static void test_competition_task_commands(void)
{
    for (uint8_t id = 1U; id <= 5U; id++) {
        char command[] = {'t', 'a', 's', 'k', (char)('0' + id), '\0'};

        reset_calls();
        CHECK(route_line(command) == VISION_PARSE_NOT_VISION,
              "competition task command must not be classified as vision");
        CHECK(task_start_calls == 1U && last_task_id == id &&
                  mode_calls == 0U && wheel_calls == 0U,
              "task1..task5 must route only to task startup");
    }
    reset_calls();
    (void)route_line("task6");
    CHECK(task_start_calls == 0U && mode_calls == 0U && wheel_calls == 0U,
          "invalid task command must not fall through to wheel speed");
}

static void test_calibration_laser_commands(void)
{
    uint32_t ok_before;

    reset_calls();
    CHECK(route_line("cal_laser_on") == VISION_PARSE_NOT_VISION,
          "calibration laser-on command must not be vision data");
    CHECK(calibration_laser_calls == 1U &&
              last_calibration_laser_enabled == 1U &&
              mode_calls == 0U && task_start_calls == 0U,
          "cal_laser_on must only request guarded calibration laser");

    reset_calls();
    CHECK(route_line("cal_laser_off") == VISION_PARSE_NOT_VISION,
          "calibration laser-off command must not be vision data");
    CHECK(calibration_laser_calls == 1U &&
              last_calibration_laser_enabled == 0U,
          "cal_laser_off must route to unconditional laser shutdown");

    reset_calls();
    calibration_laser_accepted = 0U;
    ok_before = chassis_serial_cmd_ok_count;
    (void)route_line("cal_laser_on");
    CHECK(calibration_laser_calls == 1U &&
              chassis_serial_cmd_ok_count == ok_before,
          "unsafe calibration state must reject laser-on command");

    reset_calls();
    (void)route_line("cal_laser_on,5");
    CHECK(calibration_laser_calls == 0U && mode_calls == 0U &&
              wheel_calls == 0U,
          "malformed calibration command must not fall through");
}

static void test_vision_frames_never_become_commands(void)
{
    static const char *const invalid[] = {
        "7,0,2", "7000,0,1", "7,", "6,0,2", "6000,0,1",
        "6,", "5,0,2", "5000,0,1", "5,"
    };
    int16_t dx = 0;
    int16_t dy = 0;
    uint8_t valid = 0U;
    uint32_t frame_count;

    reset_calls();
    CHECK(route_line("6,0,1") == VISION_PARSE_VALID,
          "valid square-leading visual frame must remain vision data");
    frame_count = Vision_GetSnapshot(&vision, &dx, &dy, &valid);
    CHECK(frame_count == 1U && dx == 6 && dy == 0 && valid == 1U,
          "valid visual frame must update only the vision snapshot");
    CHECK(mode_calls == 0U && wheel_calls == 0U,
          "valid visual frame must not issue a control command");

    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(invalid) / sizeof(invalid[0]));
         i++) {
        reset_calls();
        CHECK(route_line(invalid[i]) == VISION_PARSE_INVALID,
              "bad numeric comma candidate must be consumed as invalid vision");
        CHECK(mode_calls == 0U && wheel_calls == 0U,
              "bad visual candidate must not issue a control command");
        CHECK(Vision_GetSnapshot(&vision, &dx, &dy, &valid) == frame_count &&
                  dx == 6 && dy == 0 && valid == 1U,
              "bad visual candidate must preserve the last good snapshot");
    }
}

int main(void)
{
    Vision_Init(&vision);
    test_mode_compatibility();
    test_invalid_explicit_modes();
    test_direct_wheel_speed_limit();
    test_competition_task_commands();
    test_calibration_laser_commands();
    test_vision_frames_never_become_commands();

    if (failures != 0) {
        printf("%d square command assertion(s) failed\n", failures);
        return 1;
    }
    printf("square command tests passed\n");
    return 0;
}
