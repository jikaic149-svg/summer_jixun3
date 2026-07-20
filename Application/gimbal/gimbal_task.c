#include "gimbal_task.h"

#include <math.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ins_task.h"
#include "bsp_can.h"
#include "bsp_laser.h"
#include "bsp_uart6.h"
#include "config.h"
#include "main.h"
#include "mode_control.h"
#include "mode_gimbal.h"
#include "balance_control_helpers.h"
#include "task_control.h"

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral;
    float integral_limit;
    float output_limit;
} ChassisPidController_t;

volatile ChassisMotorState_e chassis_motor_state = CHASSIS_MOTOR_ESTOP;
volatile ChassisMode_e chassis_mode = CHASSIS_MODE_STAND;
volatile uint8_t chassis_motor_feedback_ok = 0U;
volatile uint8_t chassis_motor_enabled_ok = 0U;
volatile uint8_t chassis_speed_test_armed = 0U;
volatile uint8_t chassis_fault_code = CHASSIS_FAULT_NONE;
volatile float wheel_speed_ref_left_rpm = 0.0f;
volatile float wheel_speed_actual_left_rpm = 0.0f;
volatile float wheel_speed_output_left_a = 0.0f;
volatile float wheel_speed_ref_right_rpm = 0.0f;
volatile float wheel_speed_actual_right_rpm = 0.0f;
volatile float wheel_speed_output_right_a = 0.0f;
volatile float wheel_speed_feedback_left_a = 0.0f;
volatile float wheel_speed_feedback_right_a = 0.0f;
volatile float wheel_speed_eso_z1_rpm = 0.0f;
volatile float wheel_speed_eso_z2_rpm_s = 0.0f;
volatile float wheel_speed_pi_integral = 0.0f;
volatile float wheel_speed_pi_integral_right = 0.0f;
volatile float wheel_speed_target_rpm = WHEEL_SPEED_TEST_REF_RPM;
volatile float balance_pid_theta_deg = 0.0f;
volatile float balance_pid_theta_ref_deg = 0.0f;
volatile float balance_pid_manual_bias_deg = 0.0f;
volatile float balance_pid_theta_rate_dps = 0.0f;
volatile float balance_pid_accel_angle_deg = 0.0f;
volatile float balance_pid_position_m = 0.0f;
volatile float balance_pid_position_ref_m = 0.0f;
volatile float balance_pid_velocity_mps = 0.0f;
volatile float balance_pid_velocity_ref_mps = 0.0f;
volatile float balance_pid_accel_mps2 = 0.0f;
volatile float balance_pid_accel_theta_ref_deg = 0.0f;
volatile float balance_pid_wheel_diff_mps = 0.0f;
volatile float balance_pid_speed_theta_ref_deg = 0.0f;
volatile float balance_pid_integral_theta_ref_deg = 0.0f;
volatile float balance_pid_zero_trim_deg = 0.0f;
volatile float balance_pid_angle_speed_ref_rpm = 0.0f;
volatile float balance_pid_rate_speed_ref_rpm = 0.0f;
volatile float balance_pid_balance_speed_ref_rpm = 0.0f;
volatile float balance_pid_current_target_a = 0.0f;
volatile float balance_pid_current_cmd_a = 0.0f;
volatile float balance_pid_current_feedback_a = 0.0f;
volatile float balance_pid_turn_speed_ref_rpm = 0.0f;
volatile float balance_pid_output_left_a = 0.0f;
volatile float balance_pid_output_right_a = 0.0f;
volatile float balance_pid_runtime_kp = BALANCE_PID_ANGLE_KP_RPM_PER_DEG;
volatile float balance_pid_runtime_ki = BALANCE_PID_ANGLE_KI_RPM_PER_DEG_S;
volatile float balance_pid_runtime_kd = BALANCE_PID_RATE_KD_RPM_PER_DPS;
volatile float balance_pid_runtime_speed_limit_rpm =
    BALANCE_PID_BALANCE_SPEED_LIMIT_RPM;
volatile float balance_pid_runtime_current_limit_a =
    BALANCE_PID_CURRENT_LIMIT_A;
volatile float balance_pid_runtime_current_slew_a_per_sec =
    BALANCE_PID_CURRENT_SLEW_A_PER_SEC;
volatile uint32_t chassis_serial_cmd_rx_count = 0U;
volatile uint32_t chassis_serial_cmd_ok_count = 0U;
volatile uint8_t chassis_serial_cmd_last_type = 0U;
volatile float chassis_serial_cmd_last_value = 0.0f;
volatile uint8_t balance_zero_bias_frozen = 0U;
volatile uint8_t emergency_stop = 1U;
volatile BalanceMotionState_e balance_motion_state = BALANCE_MOTION_HOLD;
volatile uint8_t chassis_laser_safe = 0U;
volatile uint8_t gimbal_laser_on = 0U;
volatile uint8_t gimbal_vision_laser_request = 0U;
volatile uint8_t balance_stop_brake_state = 0U;
volatile uint32_t gimbal_yaw_enable_queue_ok_count = 0U;
volatile uint32_t gimbal_yaw_enable_queue_fail_count = 0U;
volatile uint32_t gimbal_yaw_current_tx_ok_count = 0U;
volatile uint32_t gimbal_yaw_current_tx_fail_count = 0U;

static float gimbal_yaw_pending_current_a = 0.0f;
static uint32_t gimbal_yaw_last_current_tx_tick = 0U;
static uint8_t gimbal_yaw_current_active = 0U;
static uint8_t gimbal_yaw_zero_retry_count = 0U;
static volatile uint8_t gimbal_task_feedback_ready = 0U;

static ChassisMotorState_e last_chassis_motor_state = CHASSIS_MOTOR_ESTOP;
static volatile uint8_t wheel_speed_target_changed = 0U;
static volatile ChassisMode_e chassis_mode_request = CHASSIS_MODE_STAND;
static volatile uint8_t chassis_mode_request_pending = 0U;
static volatile uint8_t chassis_mode_restart_tracking = 0U;
static float left_speed_pid_integral = 0.0f;
static float right_speed_pid_integral = 0.0f;
static float left_speed_feedback_lpf = 0.0f;
static float right_speed_feedback_lpf = 0.0f;
static float left_speed_last_output = 0.0f;
static float right_speed_last_output = 0.0f;
static float wheel_speed_target_ramp_rpm = 0.0f;
static uint8_t wheel_speed_feedback_lpf_ready = 0U;
#if WHEEL_SPEED_TEST_ENABLE && WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE
static const float wheel_speed_test_auto_sequence_rpm[] = {
    50.0f, 0.0f,
    100.0f, 0.0f,
    150.0f, 0.0f,
    200.0f, 0.0f,
    -50.0f, 0.0f,
    -100.0f, 0.0f,
    -150.0f, 0.0f
};
static uint8_t wheel_speed_test_auto_index = 0U;
static uint32_t wheel_speed_test_auto_step_tick = 0U;
static uint8_t wheel_speed_test_auto_running = 0U;
#endif
static ChassisPidController_t balance_angle_pid;
static uint8_t balance_pid_initialized = 0U;
static float balance_manual_bias_deg = BALANCE_PID_THETA_REF_DEG;
static float balance_position_m = 0.0f;
static float balance_position_ref_m = 0.0f;
static float balance_velocity_lpf_mps = 0.0f;
static uint8_t balance_velocity_lpf_ready = 0U;
static float balance_velocity_ref_ramp_mps = 0.0f;
static float balance_last_measured_velocity_mps = 0.0f;
static float balance_accel_lpf_mps2 = 0.0f;
static uint8_t balance_accel_lpf_ready = 0U;
static float balance_speed_integral_m = 0.0f;
static float balance_speed_theta_ref_slew_deg = 0.0f;
static float balance_theta_ref_slew_deg = 0.0f;
static uint8_t balance_speed_guard_active = 0U;
static float balance_mode_bias_slew_deg = BALANCE_MODE_STAND_BIAS_DEG;
static float balance_start_assist_deg = 0.0f;
static float balance_drive_actual_base_blend = 0.0f;
static float balance_drive_actual_base_lpf_rpm = 0.0f;
static float balance_start_assist_timer_sec = 0.0f;
static float balance_start_exit_timer_sec = 0.0f;
static float balance_start_retry_quiet_timer_sec = 0.0f;
static float balance_motion_settle_timer_sec = 0.0f;
static float balance_turn_ref_slew_rpm = 0.0f;
static float balance_wheel_diff_lpf_rpm = 0.0f;
static uint8_t balance_start_retry_blocked = 0U;
static float balance_active_drive_sign = 0.0f;
static float balance_drive_launch_timer_sec = 0.0f;
static float balance_launch_rearm_timer_sec = 0.0f;
static float balance_stop_brake_timer_sec = 0.0f;
static float balance_stop_brake_dir = 0.0f;
static float balance_stop_brake_entry_dir = 0.0f;
static float balance_stop_release_timer_sec = 0.0f;
static float balance_stop_rebound_timer_sec = 0.0f;
static float balance_stop_rebound_candidate_dir = 0.0f;
static uint8_t balance_stop_position_hold_armed = 0U;
static float balance_drive_pulse_timer_sec = 0.0f;
static float balance_drive_pulse_brake_timer_sec = 0.0f;
static float balance_drive_pulse_hold_timer_sec = 0.0f;
/* 零点自学习 trim:整个会话保留,不随 fault/重新解锁清零 */
static float balance_zero_trim_deg = 0.0f;

static float Chassis_AbsF(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float Chassis_LimitAbs(float value, float abs_limit)
{
    float limit = Chassis_AbsF(abs_limit);

    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float Chassis_ClampF(float value, float min_value, float max_value)
{
    if (value > max_value) {
        return max_value;
    }
    if (value < min_value) {
        return min_value;
    }
    return value;
}

static float Chassis_StepTowards(float current, float target, float max_step)
{
    float delta = target - current;

    if (max_step <= 0.0f) {
        return target;
    }

    delta = Chassis_LimitAbs(delta, max_step);
    return current + delta;
}

static float Chassis_LimitSpeedTarget(float rpm)
{
    float abs_rpm = Chassis_AbsF(rpm);

    if (abs_rpm <= 0.001f) {
        return 0.0f;
    }

    if (abs_rpm < WHEEL_SPEED_TEST_REF_MIN_ABS_RPM) {
        abs_rpm = WHEEL_SPEED_TEST_REF_MIN_ABS_RPM;
    }
    if (abs_rpm > WHEEL_SPEED_TEST_REF_MAX_ABS_RPM) {
        abs_rpm = WHEEL_SPEED_TEST_REF_MAX_ABS_RPM;
    }

    return (rpm >= 0.0f) ? abs_rpm : -abs_rpm;
}

static float Chassis_SpeedFeedforward(float ref_rpm)
{
    float abs_ref = Chassis_AbsF(ref_rpm);
    float static_ff;
    float static_ramp_rpm;
    float ff;

    if (abs_ref <= 0.001f) {
        return 0.0f;
    }

    static_ff = WHEEL_SPEED_FF_STATIC_A;
    static_ramp_rpm = Chassis_AbsF(WHEEL_SPEED_FF_STATIC_RAMP_RPM);
    if ((static_ramp_rpm > 0.001f) && (abs_ref < static_ramp_rpm)) {
        static_ff *= abs_ref / static_ramp_rpm;
    }

    ff = static_ff + WHEEL_SPEED_FF_KV_A_PER_RPM * abs_ref;
    return (ref_rpm >= 0.0f) ? ff : -ff;
}

static void ChassisPid_Init(ChassisPidController_t *pid,
                            float kp,
                            float ki,
                            float kd,
                            float integral_limit,
                            float output_limit)
{
    if (pid == NULL) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral = 0.0f;
    pid->integral_limit = Chassis_AbsF(integral_limit);
    pid->output_limit = Chassis_AbsF(output_limit);
}

static void ChassisPid_Reset(ChassisPidController_t *pid)
{
    if (pid == NULL) {
        return;
    }

    pid->integral = 0.0f;
}

static void ChassisPid_ApplyRuntime(ChassisPidController_t *pid)
{
    if (pid == NULL) {
        return;
    }

    pid->kp = balance_pid_runtime_kp;
    pid->ki = balance_pid_runtime_ki;
    pid->kd = balance_pid_runtime_kd;
    pid->integral_limit =
        Chassis_AbsF(BALANCE_PID_ANGLE_INTEGRAL_LIMIT_DEG_S);
    pid->output_limit = Chassis_AbsF(balance_pid_runtime_speed_limit_rpm);
}

#if BALANCE_PID_ENABLE
static float ChassisPid_UpdateAngle(ChassisPidController_t *pid,
                                    float theta_error_deg,
                                    float theta_rate_dps,
                                    float dt_sec)
{
    float old_integral;
    float output_unsat;
    float output;

    if (pid == NULL) {
        return 0.0f;
    }

    old_integral = pid->integral;
    if ((pid->ki != 0.0f) && (dt_sec > 0.0f)) {
        pid->integral += theta_error_deg * dt_sec;
        pid->integral = Chassis_LimitAbs(pid->integral,
                                         pid->integral_limit);
    } else {
        pid->integral = 0.0f;
    }

    output_unsat = (pid->kp * theta_error_deg +
                    pid->ki * pid->integral +
                    pid->kd * theta_rate_dps) *
                   BALANCE_PID_POLARITY;
    output = Chassis_LimitAbs(output_unsat, pid->output_limit);

    if ((pid->ki != 0.0f) && (output != output_unsat)) {
        pid->integral = old_integral;
    }

    return output;
}
#endif

static void Chassis_BalancePidInitOnce(void)
{
    if (balance_pid_initialized == 0U) {
        ChassisPid_Init(&balance_angle_pid,
                        balance_pid_runtime_kp,
                        balance_pid_runtime_ki,
                        balance_pid_runtime_kd,
                        BALANCE_PID_ANGLE_INTEGRAL_LIMIT_DEG_S,
                        balance_pid_runtime_speed_limit_rpm);
        balance_pid_initialized = 1U;
    } else {
        ChassisPid_ApplyRuntime(&balance_angle_pid);
    }
}

void WheelSpeed_SetTargetRpm(float rpm)
{
    wheel_speed_target_rpm = Chassis_LimitSpeedTarget(rpm);
    wheel_speed_target_changed = 1U;
}

void Balance_SetManualBiasDeg(float bias_deg)
{
    balance_manual_bias_deg = Chassis_LimitAbs(bias_deg,
                                               BALANCE_MANUAL_BIAS_LIMIT_DEG);
    balance_pid_manual_bias_deg = balance_manual_bias_deg;
}

void Balance_SetPidRuntimeParam(BalancePidRuntimeParam_e param, float value)
{
    switch (param) {
    case BALANCE_PID_RUNTIME_KP:
        balance_pid_runtime_kp =
            Chassis_ClampF(value,
                           0.0f,
                           BALANCE_PID_RUNTIME_KP_MAX_RPM_PER_DEG);
        break;
    case BALANCE_PID_RUNTIME_KI:
        balance_pid_runtime_ki = Chassis_ClampF(value, 0.0f, 20.0f);
        break;
    case BALANCE_PID_RUNTIME_KD:
        balance_pid_runtime_kd = Chassis_ClampF(value, 0.0f, 5.0f);
        break;
    case BALANCE_PID_RUNTIME_SPEED_LIMIT_RPM:
        balance_pid_runtime_speed_limit_rpm =
            Chassis_ClampF(Chassis_AbsF(value),
                           BALANCE_PID_RUNTIME_SPEED_LIMIT_MIN_RPM,
                           500.0f);
        break;
    case BALANCE_PID_RUNTIME_CURRENT_LIMIT_A:
        balance_pid_runtime_current_limit_a =
            Chassis_ClampF(Chassis_AbsF(value), 0.05f, 1.00f);
        break;
    case BALANCE_PID_RUNTIME_CURRENT_SLEW_A_PER_SEC:
        balance_pid_runtime_current_slew_a_per_sec =
            Chassis_ClampF(Chassis_AbsF(value), 1.0f, 200.0f);
        break;
    default:
        return;
    }

    ChassisPid_ApplyRuntime(&balance_angle_pid);
    ChassisPid_Reset(&balance_angle_pid);
}

void Chassis_SetControlMode(ChassisMode_e mode)
{
    if (mode >= CHASSIS_MODE_COUNT) {
        mode = CHASSIS_MODE_STAND;
    }
    if ((mode != CHASSIS_MODE_STAND) &&
        ((emergency_stop != 0U) ||
         (chassis_speed_test_armed == 0U) ||
         (chassis_motor_feedback_ok == 0U) ||
         (chassis_motor_enabled_ok == 0U) ||
         (chassis_fault_code != CHASSIS_FAULT_NONE))) {
        mode = CHASSIS_MODE_STAND;
    }

    taskENTER_CRITICAL();
    chassis_mode_request = mode;
    chassis_mode_request_pending = 1U;
    taskEXIT_CRITICAL();
}

static void Chassis_ApplyControlModeRequest(void)
{
    ChassisMode_e requested_mode = CHASSIS_MODE_STAND;
    uint8_t request_pending;
    uint8_t restart_tracking;

    taskENTER_CRITICAL();
    request_pending = chassis_mode_request_pending;
    restart_tracking = chassis_mode_restart_tracking;
    if (request_pending != 0U) {
        requested_mode = chassis_mode_request;
        chassis_mode_request_pending = 0U;
        chassis_mode_restart_tracking = 0U;
    }
    taskEXIT_CRITICAL();

    if ((emergency_stop != 0U) ||
        (chassis_speed_test_armed == 0U) ||
        (chassis_motor_feedback_ok == 0U) ||
        (chassis_motor_enabled_ok == 0U) ||
        (chassis_fault_code != CHASSIS_FAULT_NONE) ||
        (chassis_motor_state != CHASSIS_MOTOR_BALANCE_PID)) {
        requested_mode = CHASSIS_MODE_STAND;
        request_pending = 1U;
    }

    if (request_pending == 0U) {
        return;
    }

    if ((requested_mode != CHASSIS_MODE_TRACKING) &&
        (Task_IsActive() != 0U)) {
        Task_Stop();
        Mode_SetTrackingTaskControl(0U, 0U, 0U, 0U);
        Gimbal_SetVisionLaserRequest(0U);
        BSP_LaserOff();
        gimbal_laser_on = 0U;
    }

    Balance_SetManualBiasDeg(0.0f);
    if ((requested_mode == CHASSIS_MODE_TRACKING) &&
        (restart_tracking != 0U)) {
        Mode_Set(CHASSIS_MODE_STAND);
    }
    Mode_Set(requested_mode);
    if (chassis_mode != requested_mode) {
        chassis_mode = requested_mode;
    }
}

static void Chassis_UpdateKey(void)
{
    static uint8_t key_stable_state = 1U;
    static uint8_t key_last_raw = 1U;
    static uint32_t key_raw_change_tick = 0U;
    uint32_t now_tick = (uint32_t)xTaskGetTickCount();
    uint8_t key_raw_state;

    key_raw_state = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET) ? 0U : 1U;
    if (key_raw_state != key_last_raw) {
        key_last_raw = key_raw_state;
        key_raw_change_tick = now_tick;
    }

    if (((uint32_t)(now_tick - key_raw_change_tick) >= pdMS_TO_TICKS(KEY_DEBOUNCE_MS)) &&
        key_stable_state != key_last_raw) {
        key_stable_state = key_last_raw;

        if (key_stable_state == 0U) {
            if (emergency_stop == 0U) {
                Chassis_SetControlMode(CHASSIS_MODE_STAND);
                Gimbal_SetVisionLaserRequest(0U);
                chassis_speed_test_armed = 0U;
                emergency_stop = 1U;
            } else if ((chassis_motor_feedback_ok != 0U) &&
                       (chassis_motor_enabled_ok != 0U)) {
#if (BALANCE_PID_ENABLE || WHEEL_SPEED_TEST_ENABLE)
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
                if (INS_GetYawCalibrationState() != INS_YAW_CAL_DONE) {
                    if (INS_GetYawCalibrationState() != INS_YAW_CAL_RUNNING) {
                        INS_RequestYawCalibration();
                    }
                    chassis_fault_code = CHASSIS_FAULT_NONE;
                    chassis_speed_test_armed = 0U;
                    emergency_stop = 1U;
                } else {
                    chassis_fault_code = CHASSIS_FAULT_NONE;
                    chassis_speed_test_armed = 1U;
                    INS_RequestBalanceAngleReseed();
                    Chassis_SetControlMode(CHASSIS_MODE_STAND);
                    emergency_stop = 0U;
                }
#else
                chassis_fault_code = CHASSIS_FAULT_NONE;
                chassis_speed_test_armed = 1U;
                INS_RequestBalanceAngleReseed();
                Chassis_SetControlMode(CHASSIS_MODE_STAND);
                emergency_stop = 0U;
#endif
#endif
            }
        }
    }
}

static void Chassis_UpdateLed(void)
{
    GPIO_PinState blue = GPIO_PIN_RESET;
    GPIO_PinState green = GPIO_PIN_RESET;
    GPIO_PinState red = GPIO_PIN_RESET;
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
    uint8_t yaw_cal_state = INS_GetYawCalibrationState();
    uint8_t blink_on = ((((uint32_t)xTaskGetTickCount()) /
                         pdMS_TO_TICKS(250U)) & 1U) ? 1U : 0U;
#endif

    if ((chassis_motor_feedback_ok == 0U) ||
        (chassis_motor_enabled_ok == 0U)) {
        red = GPIO_PIN_SET;
    } else if ((emergency_stop == 0U) && (chassis_speed_test_armed != 0U)) {
        green = GPIO_PIN_SET;
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
    } else if (yaw_cal_state == INS_YAW_CAL_RUNNING) {
        blue = (blink_on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    } else if (yaw_cal_state == INS_YAW_CAL_DONE) {
        blue = GPIO_PIN_SET;
        green = GPIO_PIN_SET;
#endif
    } else {
        blue = GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_10, blue);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, green);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_12, red);
}

static void Chassis_UpdateState(void)
{
    if (chassis_motor_feedback_ok == 0U) {
        chassis_motor_state = CHASSIS_MOTOR_WAIT_FEEDBACK;
        return;
    }

    if (chassis_motor_enabled_ok == 0U) {
        chassis_motor_state = CHASSIS_MOTOR_WAIT_ENABLE;
        return;
    }

    if ((emergency_stop != 0U) || (chassis_speed_test_armed == 0U)) {
        chassis_motor_state = CHASSIS_MOTOR_ESTOP;
        return;
    }

#if BALANCE_PID_ENABLE
    chassis_motor_state = CHASSIS_MOTOR_BALANCE_PID;
#elif WHEEL_SPEED_TEST_ENABLE
    chassis_motor_state = CHASSIS_MOTOR_SPEED_TEST;
#else
    chassis_motor_state = CHASSIS_MOTOR_READY;
#endif
}

static void Chassis_ResetSpeedCtrl(void)
{
    left_speed_pid_integral = 0.0f;
    right_speed_pid_integral = 0.0f;
    left_speed_feedback_lpf = left_wheel_motor.speed_can_rpm;
    right_speed_feedback_lpf = right_wheel_motor.speed_can_rpm;
    left_speed_last_output = 0.0f;
    right_speed_last_output = 0.0f;
    wheel_speed_target_ramp_rpm = 0.0f;
    wheel_speed_feedback_lpf_ready = 0U;
    wheel_speed_feedback_left_a = left_wheel_motor.current;
    wheel_speed_feedback_right_a = right_wheel_motor.current;
    wheel_speed_eso_z1_rpm = left_wheel_motor.speed_can_rpm;
    wheel_speed_eso_z2_rpm_s = 0.0f;
    wheel_speed_pi_integral = 0.0f;
    wheel_speed_pi_integral_right = 0.0f;
}

#if WHEEL_SPEED_TEST_ENABLE && WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE
static void Chassis_ResetSpeedTestAuto(void)
{
    wheel_speed_test_auto_index = 0U;
    wheel_speed_test_auto_step_tick = 0U;
    wheel_speed_test_auto_running = 0U;
    WheelSpeed_SetTargetRpm(0.0f);
}

static void Chassis_UpdateSpeedTestAuto(void)
{
    uint32_t now_tick = (uint32_t)xTaskGetTickCount();
    uint32_t step_ticks = pdMS_TO_TICKS(WHEEL_SPEED_TEST_AUTO_STEP_MS);
    uint8_t sequence_count =
        (uint8_t)(sizeof(wheel_speed_test_auto_sequence_rpm) /
                  sizeof(wheel_speed_test_auto_sequence_rpm[0]));

    if (step_ticks == 0U) {
        step_ticks = 1U;
    }

    if (wheel_speed_test_auto_running == 0U) {
        wheel_speed_test_auto_index = 0U;
        wheel_speed_test_auto_step_tick = now_tick;
        wheel_speed_test_auto_running = 1U;
        WheelSpeed_SetTargetRpm(
            wheel_speed_test_auto_sequence_rpm[wheel_speed_test_auto_index]);
        return;
    }

    if (((uint32_t)(now_tick - wheel_speed_test_auto_step_tick) >= step_ticks) &&
        (wheel_speed_test_auto_index < (uint8_t)(sequence_count - 1U))) {
        wheel_speed_test_auto_index++;
        wheel_speed_test_auto_step_tick = now_tick;
        WheelSpeed_SetTargetRpm(
            wheel_speed_test_auto_sequence_rpm[wheel_speed_test_auto_index]);
    }
}
#endif

static void Chassis_ResetBalancePid(void)
{
    Chassis_BalancePidInitOnce();
    ChassisPid_Reset(&balance_angle_pid);
    balance_position_m = 0.0f;
    balance_position_ref_m = 0.0f;
    balance_speed_integral_m = 0.0f;
    balance_speed_theta_ref_slew_deg = 0.0f;
    balance_theta_ref_slew_deg = 0.0f;
    balance_speed_guard_active = 0U;
    balance_mode_bias_slew_deg = BALANCE_MODE_STAND_BIAS_DEG;
    balance_start_assist_deg = 0.0f;
    balance_drive_actual_base_blend = 0.0f;
    balance_drive_actual_base_lpf_rpm = 0.0f;
    balance_start_assist_timer_sec = 0.0f;
    balance_start_exit_timer_sec = 0.0f;
    balance_start_retry_quiet_timer_sec = 0.0f;
    balance_motion_settle_timer_sec = 0.0f;
    balance_turn_ref_slew_rpm = 0.0f;
    balance_wheel_diff_lpf_rpm = 0.0f;
    balance_start_retry_blocked = 0U;
    balance_active_drive_sign = 0.0f;
    balance_motion_state = BALANCE_MOTION_HOLD;
    balance_drive_launch_timer_sec = 0.0f;
    balance_launch_rearm_timer_sec = 0.0f;
    balance_stop_brake_timer_sec = 0.0f;
    balance_stop_brake_dir = 0.0f;
    balance_stop_brake_entry_dir = 0.0f;
    balance_stop_release_timer_sec = 0.0f;
    balance_stop_rebound_timer_sec = 0.0f;
    balance_stop_rebound_candidate_dir = 0.0f;
    balance_stop_position_hold_armed = 0U;
    balance_stop_brake_state = 0U;
    balance_drive_pulse_timer_sec = 0.0f;
    balance_drive_pulse_brake_timer_sec = 0.0f;
    balance_drive_pulse_hold_timer_sec = 0.0f;
    balance_velocity_lpf_mps = 0.0f;
    balance_velocity_lpf_ready = 0U;
    balance_velocity_ref_ramp_mps = 0.0f;
    balance_last_measured_velocity_mps = 0.0f;
    balance_accel_lpf_mps2 = 0.0f;
    balance_accel_lpf_ready = 0U;
    balance_zero_bias_frozen = 0U;
    balance_pid_theta_deg = global_balance_angle;
    balance_pid_theta_ref_deg = 0.0f;
    balance_pid_manual_bias_deg = balance_manual_bias_deg;
    balance_pid_theta_rate_dps = global_balance_rate;
    balance_pid_accel_angle_deg = global_balance_accel_angle;
    balance_pid_position_m = 0.0f;
    balance_pid_position_ref_m = 0.0f;
    balance_pid_velocity_mps = 0.0f;
    balance_pid_velocity_ref_mps = 0.0f;
    balance_pid_accel_mps2 = 0.0f;
    balance_pid_accel_theta_ref_deg = 0.0f;
    balance_pid_wheel_diff_mps = 0.0f;
    balance_pid_speed_theta_ref_deg = 0.0f;
    balance_pid_integral_theta_ref_deg = 0.0f;
    balance_pid_angle_speed_ref_rpm = 0.0f;
    balance_pid_rate_speed_ref_rpm = 0.0f;
    balance_pid_balance_speed_ref_rpm = 0.0f;
    balance_pid_current_target_a = 0.0f;
    balance_pid_current_cmd_a = 0.0f;
    balance_pid_current_feedback_a = 0.0f;
    balance_pid_turn_speed_ref_rpm = 0.0f;
    balance_pid_output_left_a = 0.0f;
    balance_pid_output_right_a = 0.0f;
    Chassis_ResetSpeedCtrl();
}

static void Chassis_EnterMotionStarting(float drive_sign)
{
    BSP_UART6_MuteTxForMs(VISION_UART_POWER_GUARD_MS);
    balance_motion_state = BALANCE_MOTION_STARTING;
    balance_active_drive_sign = (drive_sign >= 0.0f) ? 1.0f : -1.0f;
    balance_start_assist_deg = 0.0f;
    balance_start_assist_timer_sec = 0.0f;
    balance_start_exit_timer_sec = 0.0f;
    balance_start_retry_quiet_timer_sec = 0.0f;
    balance_motion_settle_timer_sec = 0.0f;
    balance_speed_integral_m = 0.0f;
    balance_velocity_ref_ramp_mps = 0.0f;
    balance_drive_actual_base_blend = 0.0f;
    balance_drive_actual_base_lpf_rpm = 0.0f;
    balance_position_m = 0.0f;
    balance_position_ref_m = 0.0f;
    balance_stop_brake_timer_sec = 0.0f;
    balance_stop_brake_dir = 0.0f;
    balance_stop_brake_entry_dir = 0.0f;
    balance_stop_release_timer_sec = 0.0f;
    balance_stop_rebound_timer_sec = 0.0f;
    balance_stop_rebound_candidate_dir = 0.0f;
    balance_stop_position_hold_armed = 0U;
}

static void Chassis_EnterMotionBraking(uint8_t *stop_edge)
{
    if (balance_motion_state == BALANCE_MOTION_BRAKING) {
        return;
    }

    balance_motion_state = BALANCE_MOTION_BRAKING;
    balance_motion_settle_timer_sec = 0.0f;
    balance_start_assist_timer_sec = 0.0f;
    balance_start_exit_timer_sec = 0.0f;
    balance_drive_actual_base_blend = 0.0f;
    balance_drive_actual_base_lpf_rpm = 0.0f;
    balance_stop_brake_timer_sec = 0.0f;
    balance_stop_brake_dir = 0.0f;
    balance_stop_brake_entry_dir = 0.0f;
    balance_stop_release_timer_sec = 0.0f;
    balance_stop_rebound_timer_sec = 0.0f;
    balance_stop_rebound_candidate_dir = 0.0f;
    balance_stop_position_hold_armed = 0U;
    if (stop_edge != NULL) {
        *stop_edge = 1U;
    }
}

static void Chassis_EnterMotionHold(float measured_position_m)
{
    balance_motion_state = BALANCE_MOTION_HOLD;
    balance_active_drive_sign = 0.0f;
    balance_motion_settle_timer_sec = 0.0f;
    balance_position_ref_m = measured_position_m;
    balance_speed_integral_m = 0.0f;
    balance_drive_actual_base_blend = 0.0f;
    balance_drive_actual_base_lpf_rpm = 0.0f;
    balance_stop_brake_timer_sec = 0.0f;
    balance_stop_brake_dir = 0.0f;
    balance_stop_brake_entry_dir = 0.0f;
    balance_stop_release_timer_sec = 0.0f;
    balance_stop_rebound_timer_sec = 0.0f;
    balance_stop_rebound_candidate_dir = 0.0f;
    balance_stop_position_hold_armed = 1U;
}

uint8_t Chassis_StartCompetitionTask(uint8_t task_id)
{
    uint8_t started;

    BSP_UART6_MuteTxForMs(VISION_UART_POWER_GUARD_MS);
    taskENTER_CRITICAL();
    Task_Stop();
    Mode_SetTrackingTaskControl(0U, 0U, 0U, 0U);
    Gimbal_SetVisionLaserRequest(0U);
    started = Task_Start((TaskId_e)task_id);
    if (started != 0U) {
        chassis_mode_request = CHASSIS_MODE_TRACKING;
        chassis_mode_request_pending = 1U;
        chassis_mode_restart_tracking = 1U;
    }
    taskEXIT_CRITICAL();
    if (started == 0U) {
        BSP_LaserOff();
        gimbal_laser_on = 0U;
        return 0U;
    }
    if ((emergency_stop == 0U) &&
        (chassis_fault_code == CHASSIS_FAULT_NONE)) {
        BSP_LaserOn();
        gimbal_laser_on = BSP_LaserIsOn();
    }
    return 1U;
}

void Chassis_StopCompetitionTask(void)
{
    taskENTER_CRITICAL();
    Task_Stop();
    Mode_SetTrackingTaskControl(0U, 0U, 0U, 0U);
    Gimbal_SetVisionLaserRequest(0U);
    taskEXIT_CRITICAL();
    BSP_LaserOff();
    gimbal_laser_on = 0U;
    Chassis_SetControlMode(CHASSIS_MODE_STAND);
}

static float Chassis_GetWheelSpeedRpm(const volatile Motor_Measure_t *motor)
{
    if (motor == NULL) {
        return 0.0f;
    }

    return motor->speed_can_rpm;
}

static float Chassis_GetBalanceAngleDeg(void)
{
    float kalman_weight = Chassis_ClampF(BALANCE_PID_KALMAN_ANGLE_WEIGHT,
                                         0.0f,
                                         1.0f);
    float accel_weight = Chassis_ClampF(BALANCE_PID_ACCEL_ANGLE_WEIGHT,
                                        0.0f,
                                        1.0f);
    float weight_sum = kalman_weight + accel_weight;

    if (weight_sum <= 0.0001f) {
        return global_balance_angle;
    }

    return (kalman_weight * global_balance_angle +
            accel_weight * global_balance_accel_angle) /
           weight_sum;
}

static uint8_t Chassis_IsBalanceQuiet(float theta_deg,
                                      float theta_ref_deg,
                                      float theta_rate_dps,
                                      float left_rpm,
                                      float right_rpm)
{
    if ((Chassis_AbsF(theta_deg - theta_ref_deg) >
         BALANCE_MODE_SETTLE_MAX_THETA_ERROR_DEG) ||
        (Chassis_AbsF(theta_rate_dps) >
         BALANCE_MODE_SETTLE_MAX_RATE_DPS) ||
        (Chassis_AbsF(left_rpm) > BALANCE_MODE_SETTLE_MAX_WHEEL_RPM) ||
        (Chassis_AbsF(right_rpm) > BALANCE_MODE_SETTLE_MAX_WHEEL_RPM)) {
        return 0U;
    }
    return 1U;
}

static void Chassis_UpdateBalanceViewZeroOutput(void)
{
    float left_rpm = Chassis_GetWheelSpeedRpm(&left_wheel_motor);
    float right_rpm = Chassis_GetWheelSpeedRpm(&right_wheel_motor);
    float velocity_mps = 0.5f * (left_rpm + right_rpm) *
                         BALANCE_RPM_TO_MPS;
    float wheel_diff_mps = (right_rpm - left_rpm) * BALANCE_RPM_TO_MPS;

    balance_pid_theta_deg = Chassis_GetBalanceAngleDeg();
    balance_pid_theta_ref_deg = 0.0f;
    balance_pid_manual_bias_deg = balance_manual_bias_deg;
    balance_pid_theta_rate_dps = global_balance_rate;
    balance_pid_accel_angle_deg = global_balance_accel_angle;
    balance_pid_position_m = BALANCE_PID_POSITION_SIGN * balance_position_m;
    balance_pid_position_ref_m = balance_position_ref_m;
    balance_pid_velocity_mps = BALANCE_PID_VELOCITY_SIGN * velocity_mps;
    balance_pid_velocity_ref_mps = 0.0f;
    balance_pid_accel_mps2 = 0.0f;
    balance_pid_accel_theta_ref_deg = 0.0f;
    balance_pid_wheel_diff_mps = wheel_diff_mps;
    balance_pid_speed_theta_ref_deg = 0.0f;
    balance_pid_integral_theta_ref_deg = 0.0f;
    balance_pid_angle_speed_ref_rpm = 0.0f;
    balance_pid_rate_speed_ref_rpm = 0.0f;
    balance_pid_balance_speed_ref_rpm = 0.0f;
    balance_pid_current_target_a = 0.0f;
    balance_pid_current_cmd_a = 0.0f;
    balance_pid_current_feedback_a = 0.5f * (left_wheel_motor.current +
                                             right_wheel_motor.current);
    balance_pid_turn_speed_ref_rpm = 0.0f;
    balance_pid_output_left_a = 0.0f;
    balance_pid_output_right_a = 0.0f;
    wheel_speed_ref_left_rpm = 0.0f;
    wheel_speed_actual_left_rpm = left_rpm;
    wheel_speed_output_left_a = 0.0f;
    wheel_speed_ref_right_rpm = 0.0f;
    wheel_speed_actual_right_rpm = right_rpm;
    wheel_speed_output_right_a = 0.0f;
}

static float Chassis_SpeedPidUpdate(float ref_rpm,
                                    float feedback_rpm,
                                    float polarity,
                                    float *integral,
                                    float *last_output,
                                    float dt_sec,
                                    float current_limit_a,
                                    float current_slew_a_per_sec)
{
    float speed_error;
    float proportional_error;
    float error_deadband;
    float speed_ff;
    float old_integral;
    float integral_delta_output;
    float u_unsat;
    float output;
    float max_current_step;

    if ((integral == NULL) || (last_output == NULL)) {
        return 0.0f;
    }

    speed_error = ref_rpm - feedback_rpm;
    proportional_error = speed_error;
    error_deadband = WHEEL_SPEED_PID_ERROR_DEADBAND_RPM;
    if (error_deadband > 0.0f) {
        if (proportional_error > error_deadband) {
            proportional_error -= error_deadband;
        } else if (proportional_error < -error_deadband) {
            proportional_error += error_deadband;
        } else {
            proportional_error = 0.0f;
        }
    }

    old_integral = *integral;
    if (WHEEL_SPEED_PID_KI_A_PER_RPM_S != 0.0f) {
        *integral += speed_error * dt_sec;
        *integral = Chassis_LimitAbs(*integral, WHEEL_SPEED_PID_INTEGRAL_LIMIT);
    } else {
        *integral = 0.0f;
    }

    speed_ff = Chassis_SpeedFeedforward(ref_rpm);
#if WHEEL_SPEED_FF_DISABLE_WHEN_BRAKING
    if ((ref_rpm * feedback_rpm > 0.0f) &&
        (Chassis_AbsF(ref_rpm) < Chassis_AbsF(feedback_rpm))) {
        speed_ff = 0.0f;
    }
#endif

    u_unsat = (speed_ff +
               WHEEL_SPEED_PID_KP_A_PER_RPM * proportional_error +
               WHEEL_SPEED_PID_KI_A_PER_RPM_S * (*integral) -
               WHEEL_SPEED_PID_KD_A_PER_RPM * feedback_rpm) *
              polarity;
    output = Chassis_LimitAbs(u_unsat, current_limit_a);
    max_current_step = current_slew_a_per_sec * dt_sec;
    output = Chassis_StepTowards(*last_output, output, max_current_step);

    integral_delta_output = WHEEL_SPEED_PID_KI_A_PER_RPM_S *
                            (*integral - old_integral) * polarity;
    if ((WHEEL_SPEED_PID_KI_A_PER_RPM_S != 0.0f) &&
        (output != u_unsat) &&
        ((u_unsat * integral_delta_output) > 0.0f)) {
        *integral = old_integral;
    }

    *last_output = output;
    return output;
}

#if WHEEL_SPEED_TEST_ENABLE
static void Chassis_CalcSpeedTestOutput(float *left_current, float *right_current)
{
    float left_ref = 0.0f;
    float right_ref = 0.0f;
    float left_out = 0.0f;
    float right_out = 0.0f;
    float left_feedback_rpm = 0.0f;
    float right_feedback_rpm = 0.0f;

    if (chassis_motor_state == CHASSIS_MOTOR_SPEED_TEST) {
        float target_step = WHEEL_SPEED_TARGET_RAMP_RPM_PER_SEC *
                            WHEEL_SPEED_TEST_DT_SEC;
        float left_feedback_alpha = LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA;
        float right_feedback_alpha = RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA;

#if WHEEL_SPEED_TEST_ENABLE && WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE
        Chassis_UpdateSpeedTestAuto();
#endif

        left_feedback_alpha = Chassis_ClampF(left_feedback_alpha, 0.0f, 1.0f);
        right_feedback_alpha = Chassis_ClampF(right_feedback_alpha, 0.0f, 1.0f);

        wheel_speed_target_ramp_rpm = Chassis_StepTowards(wheel_speed_target_ramp_rpm,
                                                          wheel_speed_target_rpm,
                                                          target_step);
        left_ref = wheel_speed_target_ramp_rpm;
        right_ref = wheel_speed_target_ramp_rpm;

        if (wheel_speed_feedback_lpf_ready == 0U) {
            left_speed_feedback_lpf = left_wheel_motor.speed_can_rpm;
            right_speed_feedback_lpf = right_wheel_motor.speed_can_rpm;
            wheel_speed_feedback_lpf_ready = 1U;
        } else {
            left_speed_feedback_lpf += left_feedback_alpha *
                                       (left_wheel_motor.speed_can_rpm -
                                        left_speed_feedback_lpf);
            right_speed_feedback_lpf += right_feedback_alpha *
                                        (right_wheel_motor.speed_can_rpm -
                                         right_speed_feedback_lpf);
        }
        left_feedback_rpm = left_speed_feedback_lpf;
        right_feedback_rpm = right_speed_feedback_lpf;

        left_out = Chassis_SpeedPidUpdate(left_ref,
                                          left_feedback_rpm,
                                          LEFT_WHEEL_SPEED_PID_POLARITY,
                                          &left_speed_pid_integral,
                                          &left_speed_last_output,
                                          WHEEL_SPEED_TEST_DT_SEC,
                                          WHEEL_SPEED_TEST_CURRENT_LIMIT_A,
                                          WHEEL_SPEED_CURRENT_SLEW_A_PER_SEC);
        right_out = Chassis_SpeedPidUpdate(right_ref,
                                           right_feedback_rpm,
                                           RIGHT_WHEEL_SPEED_PID_POLARITY,
                                           &right_speed_pid_integral,
                                           &right_speed_last_output,
                                           WHEEL_SPEED_TEST_DT_SEC,
                                           WHEEL_SPEED_TEST_CURRENT_LIMIT_A,
                                           WHEEL_SPEED_CURRENT_SLEW_A_PER_SEC);

#if WHEEL_SPEED_TEST_UNIDIRECTIONAL
        if (((left_ref > 0.0f) && (left_out < 0.0f)) ||
            ((left_ref < 0.0f) && (left_out > 0.0f))) {
            left_out = 0.0f;
        }
        if (((right_ref > 0.0f) && (right_out < 0.0f)) ||
            ((right_ref < 0.0f) && (right_out > 0.0f))) {
            right_out = 0.0f;
        }
#endif
    } else {
        Chassis_ResetSpeedCtrl();
    }

    wheel_speed_ref_left_rpm = left_ref;
    wheel_speed_actual_left_rpm = left_feedback_rpm;
    wheel_speed_output_left_a = left_out;
    wheel_speed_ref_right_rpm = right_ref;
    wheel_speed_actual_right_rpm = right_feedback_rpm;
    wheel_speed_output_right_a = right_out;
    wheel_speed_feedback_left_a = left_wheel_motor.current;
    wheel_speed_feedback_right_a = right_wheel_motor.current;
    wheel_speed_eso_z1_rpm = left_feedback_rpm;
    wheel_speed_eso_z2_rpm_s = right_feedback_rpm;
    wheel_speed_pi_integral = left_speed_pid_integral;
    wheel_speed_pi_integral_right = right_speed_pid_integral;

    *left_current = left_out;
    *right_current = right_out;
}
#endif

#if BALANCE_PID_ENABLE
static void Chassis_CalcBalancePidOutput(float *left_current, float *right_current)
{
    float left_rpm;
    float right_rpm;
    float velocity_raw_mps;
    float measured_velocity_mps;
    float measured_position_m;
    float velocity_alpha;
    float wheel_diff_mps;
    float theta_deg;
    float accel_angle_deg;
    float theta_ref_deg;
    float theta_ref_target_deg;
    float theta_rate_dps;
    float theta_error_deg;
    float target_velocity_mps;
    float mode_target_velocity_mps;
    float speed_error_mps;
    float accel_raw_mps2;
    float accel_error_mps2;
    float accel_theta_ref_deg;
    float position_error_m;
    float speed_theta_ref_deg;
    float mode_bias_deg;
    float mode_bias_target_deg;
    float drive_base_rpm;
    float drive_reference_base_rpm;
    float drive_actual_base_rpm;
    float stop_brake_rpm;
    float launch_bias_deg;
    float balance_speed_ref_rpm;
    float balance_speed_limit_rpm;
    float stand_theta_ref_limit_deg;
    float theta_ref_slew_deg_per_sec;
    float final_theta_ref_slew_deg_per_sec;
    float speed_current_limit_a;
    float speed_current_slew_a_per_sec;
    float turn_target_rpm;
    float turn_speed_ref_rpm;
    float turn_diff_correction_rpm;
    float yaw_rate_correction_rpm;
    float desired_wheel_diff_rpm;
    float actual_wheel_diff_rpm;
    float left_ref_rpm;
    float right_ref_rpm;
    float left_out;
    float right_out;
    ModeCommand_t mode_command;
    uint8_t drive_bias_mode;
    uint8_t drive_requested;
    uint8_t drive_launch_active;
    uint8_t drive_pulse_brake_active;
    uint8_t drive_pulse_hold_active;
    uint8_t stop_brake_active;
    uint8_t stop_brake_boost_active;
    uint8_t drive_stop_edge;
    uint8_t speed_integral_safety_decay;
    uint8_t speed_integration_eligible;
    BalanceControlIntegralAction_e speed_integral_action;

    if (chassis_motor_state != CHASSIS_MOTOR_BALANCE_PID) {
        Chassis_ResetBalancePid();
        Chassis_UpdateBalanceViewZeroOutput();
        *left_current = 0.0f;
        *right_current = 0.0f;
        return;
    }

    Chassis_BalancePidInitOnce();

    theta_deg = Chassis_GetBalanceAngleDeg();
#if BALANCE_ZERO_TRIM_ENABLE
    theta_deg -= balance_zero_trim_deg;
#endif
    accel_angle_deg = global_balance_accel_angle;
    theta_rate_dps = global_balance_rate;

    if (Chassis_AbsF(theta_deg) > BALANCE_PID_SAFE_ANGLE_DEG) {
        chassis_fault_code = CHASSIS_FAULT_PID_ANGLE;
        Chassis_SetControlMode(CHASSIS_MODE_STAND);
        chassis_speed_test_armed = 0U;
        emergency_stop = 1U;
        Chassis_ResetBalancePid();
        Chassis_UpdateBalanceViewZeroOutput();
        *left_current = 0.0f;
        *right_current = 0.0f;
        return;
    }

    if (Chassis_AbsF(theta_rate_dps) > BALANCE_PID_SAFE_RATE_DPS) {
        chassis_fault_code = CHASSIS_FAULT_PID_RATE;
        Chassis_SetControlMode(CHASSIS_MODE_STAND);
        chassis_speed_test_armed = 0U;
        emergency_stop = 1U;
        Chassis_ResetBalancePid();
        Chassis_UpdateBalanceViewZeroOutput();
        *left_current = 0.0f;
        *right_current = 0.0f;
        return;
    }

    left_rpm = Chassis_GetWheelSpeedRpm(&left_wheel_motor);
    right_rpm = Chassis_GetWheelSpeedRpm(&right_wheel_motor);
    velocity_raw_mps = 0.5f * (left_rpm + right_rpm) *
                       BALANCE_RPM_TO_MPS;
    velocity_raw_mps = Chassis_LimitAbs(velocity_raw_mps,
                                        BALANCE_PID_VELOCITY_LIMIT_MPS);

    velocity_alpha = Chassis_ClampF(BALANCE_PID_VELOCITY_LPF_ALPHA, 0.0f, 1.0f);
    if (balance_velocity_lpf_ready == 0U) {
        balance_velocity_lpf_mps = velocity_raw_mps;
        balance_velocity_lpf_ready = 1U;
    } else {
        balance_velocity_lpf_mps += velocity_alpha *
                                    (velocity_raw_mps -
                                     balance_velocity_lpf_mps);
    }

    balance_position_m += balance_velocity_lpf_mps * BALANCE_PID_DT_SEC;
    measured_velocity_mps = BALANCE_PID_VELOCITY_SIGN *
                            balance_velocity_lpf_mps;
    measured_position_m = BALANCE_PID_POSITION_SIGN * balance_position_m;
    wheel_diff_mps = (right_rpm - left_rpm) * BALANCE_RPM_TO_MPS;
    actual_wheel_diff_rpm = right_rpm - left_rpm;
    balance_wheel_diff_lpf_rpm +=
        Chassis_ClampF(BALANCE_MODE_WHEEL_DIFF_LPF_ALPHA, 0.0f, 1.0f) *
        (actual_wheel_diff_rpm - balance_wheel_diff_lpf_rpm);

    accel_raw_mps2 = (measured_velocity_mps -
                      balance_last_measured_velocity_mps) /
                     BALANCE_PID_DT_SEC;
    if (balance_accel_lpf_ready == 0U) {
        balance_accel_lpf_mps2 = 0.0f;
        balance_accel_lpf_ready = 1U;
    } else {
        balance_accel_lpf_mps2 += velocity_alpha *
                                  (accel_raw_mps2 -
                                   balance_accel_lpf_mps2);
    }
    balance_last_measured_velocity_mps = measured_velocity_mps;

#if BALANCE_ZERO_TRIM_ENABLE
    /*
     * 零点自学习:静止直立时 theta 均值就是真实零点。
     * 门控确保只在"stand + 车不动 + 轮不转 + 姿态安静"时学习,
     * 时间常数 1/0.02 = 50s,与直立环(~1s)动力学解耦。
     */
    balance_zero_bias_frozen =
        ((balance_motion_state == BALANCE_MOTION_HOLD) &&
         (chassis_motor_state == CHASSIS_MOTOR_BALANCE_PID)) ? 0U : 1U;
    if ((balance_zero_bias_frozen == 0U) &&
        (chassis_mode == CHASSIS_MODE_STAND) &&
        (Chassis_AbsF(theta_deg) < BALANCE_ZERO_TRIM_GATE_THETA_DEG) &&
        (Chassis_AbsF(theta_rate_dps) < BALANCE_ZERO_TRIM_GATE_RATE_DPS) &&
        (Chassis_AbsF(measured_velocity_mps) < BALANCE_ZERO_TRIM_GATE_V_MPS) &&
        (Chassis_AbsF(left_rpm) < BALANCE_ZERO_TRIM_GATE_WHEEL_RPM) &&
        (Chassis_AbsF(right_rpm) < BALANCE_ZERO_TRIM_GATE_WHEEL_RPM)) {
        balance_zero_trim_deg += BALANCE_ZERO_TRIM_RATE_PER_SEC *
                                 theta_deg * BALANCE_PID_DT_SEC;
        balance_zero_trim_deg =
            Chassis_LimitAbs(balance_zero_trim_deg,
                             BALANCE_ZERO_TRIM_LIMIT_DEG);
    }
#endif

    Mode_GetCommand(&mode_command);
    mode_target_velocity_mps = mode_command.velocity_mps;
    drive_requested =
        ((mode_command.drive_enabled != 0U) &&
         (Chassis_AbsF(mode_target_velocity_mps) > 0.001f)) ? 1U : 0U;
    drive_stop_edge = 0U;
    if (drive_requested == 0U) {
        balance_start_retry_blocked = 0U;
        balance_start_retry_quiet_timer_sec = 0.0f;
    } else if ((balance_motion_state == BALANCE_MOTION_HOLD) &&
               (balance_start_retry_blocked != 0U)) {
        uint8_t retry_quiet = Chassis_IsBalanceQuiet(
            theta_deg,
            balance_theta_ref_slew_deg,
            theta_rate_dps,
            left_rpm,
            right_rpm);

        if (BalanceControl_QuietConfirmed(
                retry_quiet,
                BALANCE_PID_DT_SEC,
                BALANCE_MODE_START_RETRY_QUIET_SEC,
                &balance_start_retry_quiet_timer_sec) != 0U) {
            balance_start_retry_blocked = 0U;
        }
    } else {
        balance_start_retry_quiet_timer_sec = 0.0f;
    }

    if (drive_requested != 0U) {
        float requested_sign =
            (mode_target_velocity_mps >= 0.0f) ? 1.0f : -1.0f;

        if ((balance_motion_state == BALANCE_MOTION_HOLD) &&
            (balance_start_retry_blocked == 0U)) {
            if (Chassis_AbsF(measured_velocity_mps) >
                BALANCE_MODE_BRAKE_V_DONE_MPS) {
                Chassis_EnterMotionBraking(&drive_stop_edge);
            } else if (Chassis_AbsF(balance_turn_ref_slew_rpm) <=
                       BALANCE_MODE_TURN_SETTLE_RPM &&
                       Chassis_AbsF(balance_wheel_diff_lpf_rpm) <=
                       BALANCE_MODE_TURN_SETTLE_DIFF_RPM) {
                Chassis_EnterMotionStarting(requested_sign);
                measured_position_m = 0.0f;
            }
        } else if (((balance_motion_state == BALANCE_MOTION_STARTING) ||
                    (balance_motion_state == BALANCE_MOTION_RUNNING)) &&
                   ((balance_active_drive_sign * requested_sign) < 0.0f)) {
            Chassis_EnterMotionBraking(&drive_stop_edge);
        }
    } else if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
               (balance_motion_state == BALANCE_MOTION_RUNNING)) {
        Chassis_EnterMotionBraking(&drive_stop_edge);
    }

    if (balance_motion_state == BALANCE_MOTION_BRAKING) {
        target_velocity_mps = 0.0f;
        mode_bias_target_deg = BALANCE_MODE_STAND_BIAS_DEG;
    } else if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
               (balance_motion_state == BALANCE_MOTION_RUNNING)) {
        target_velocity_mps = mode_target_velocity_mps;
        mode_bias_target_deg = mode_command.bias_deg;
    } else {
        target_velocity_mps = 0.0f;
        mode_bias_target_deg = BALANCE_MODE_STAND_BIAS_DEG;
    }

    drive_bias_mode =
        ((balance_motion_state == BALANCE_MOTION_STARTING) ||
         (balance_motion_state == BALANCE_MOTION_RUNNING)) ? 1U : 0U;

    if ((balance_motion_state == BALANCE_MOTION_STARTING) &&
        (drive_requested != 0U)) {
        balance_start_assist_timer_sec += BALANCE_PID_DT_SEC;
        if ((measured_velocity_mps * balance_active_drive_sign) >=
            BALANCE_MODE_START_ASSIST_EXIT_MAX_MPS) {
            balance_start_exit_timer_sec += BALANCE_PID_DT_SEC;
        } else {
            balance_start_exit_timer_sec = 0.0f;
        }
        if (balance_start_exit_timer_sec >=
            BALANCE_MODE_START_ASSIST_EXIT_SEC) {
            balance_motion_state = BALANCE_MOTION_RUNNING;
            balance_start_exit_timer_sec = 0.0f;
        }
        if ((balance_motion_state == BALANCE_MOTION_STARTING) &&
            (balance_start_assist_timer_sec >=
             BALANCE_MODE_START_ASSIST_TIMEOUT_SEC)) {
            if (chassis_mode == CHASSIS_MODE_TRACKING) {
                /* A low-speed or discontinuous course must not turn the
                 * generic launch timeout into an uncommanded route HOLD. */
                balance_motion_state = BALANCE_MOTION_RUNNING;
                balance_start_assist_timer_sec = 0.0f;
                balance_start_exit_timer_sec = 0.0f;
                balance_start_retry_blocked = 0U;
            } else {
                balance_start_retry_blocked = 1U;
                Chassis_EnterMotionBraking(&drive_stop_edge);
                target_velocity_mps = 0.0f;
                mode_bias_target_deg = BALANCE_MODE_STAND_BIAS_DEG;
            }
        }
    }

#if BALANCE_MODE_START_ASSIST_ENABLE
    if ((balance_motion_state == BALANCE_MOTION_STARTING) &&
        (drive_requested != 0U)) {
        float assist_limit_deg = BALANCE_MODE_START_ASSIST_MAX_DEG;
        float assist_slew_deg_per_sec =
            BALANCE_MODE_START_ASSIST_SLEW_DEG_PER_SEC;

        if ((chassis_mode == CHASSIS_MODE_TRACKING) &&
            (Mode_TrackingGetPhase() ==
             MODE_TRACKING_PHASE_TARGET_DEPART)) {
            assist_limit_deg = TRACKING_TARGET_DEPART_ASSIST_DEG;
            assist_slew_deg_per_sec =
                TRACKING_TARGET_DEPART_ASSIST_SLEW_DEG_PER_SEC;
        }
        balance_start_assist_deg =
            Chassis_StepTowards(
                balance_start_assist_deg,
                balance_active_drive_sign *
                assist_limit_deg,
                assist_slew_deg_per_sec * BALANCE_PID_DT_SEC);
    } else {
        float assist_release_deg_per_sec =
            (balance_motion_state == BALANCE_MOTION_BRAKING) ?
            BALANCE_MODE_BIAS_STOP_SLEW_DEG_PER_SEC :
            BALANCE_MODE_START_ASSIST_RELEASE_DEG_PER_SEC;
        balance_start_assist_deg =
            Chassis_StepTowards(
                balance_start_assist_deg,
                0.0f,
                assist_release_deg_per_sec * BALANCE_PID_DT_SEC);
    }
#else
    balance_start_assist_deg = 0.0f;
#endif

    if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
        (balance_motion_state == BALANCE_MOTION_RUNNING)) {
        balance_position_m = 0.0f;
        balance_position_ref_m = 0.0f;
        measured_position_m = 0.0f;
    }

    if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
        (balance_motion_state == BALANCE_MOTION_RUNNING)) {
        balance_mode_bias_slew_deg =
            Chassis_StepTowards(
                balance_mode_bias_slew_deg,
                mode_bias_target_deg,
                BALANCE_MODE_BIAS_SLEW_DEG_PER_SEC *
                BALANCE_PID_DT_SEC);
    } else {
        /* 停止拍 bias 不许阶跃:1.7deg 一拍归零 = +170rpm 轮速冲击,
         * 是停止震动的激励源之一;20deg/s 与 stop-brake 同速。 */
        balance_mode_bias_slew_deg =
            Chassis_StepTowards(
                balance_mode_bias_slew_deg,
                mode_bias_target_deg,
                BALANCE_MODE_BIAS_STOP_SLEW_DEG_PER_SEC *
                BALANCE_PID_DT_SEC);
    }
    mode_bias_deg = balance_mode_bias_slew_deg;
    /*
     * 闭环限速守卫(带迟滞):超速时撤销行走速度目标,让外环全部权限
     * 用于刹车。只改 target_velocity_mps,不触碰直立角度环。
     */
    if (Chassis_AbsF(measured_velocity_mps) > BALANCE_SPEED_GUARD_ON_MPS) {
        balance_speed_guard_active = 1U;
    } else if (Chassis_AbsF(measured_velocity_mps) <
               BALANCE_SPEED_GUARD_OFF_MPS) {
        balance_speed_guard_active = 0U;
    }
    if (balance_speed_guard_active != 0U) {
        target_velocity_mps = 0.0f;
        /* 不等斜坡,立刻撤销速度目标,让 Kv 项全额刹车 */
        balance_velocity_ref_ramp_mps = 0.0f;
    }
    drive_launch_active =
        ((drive_bias_mode != 0U) &&
         (balance_drive_launch_timer_sec > 0.0f)) ? 1U : 0U;
    if (drive_launch_active != 0U) {
        target_velocity_mps = 0.0f;
        balance_velocity_ref_ramp_mps = 0.0f;
        balance_drive_launch_timer_sec =
            Chassis_StepTowards(balance_drive_launch_timer_sec,
                                0.0f,
                                BALANCE_PID_DT_SEC);
        /*
         * 破摩擦成功(速度已进平滑滚动窗口)立即结束 launch,
         * 防止 tref=+1.4deg 继续把车加速到守卫线。
         */
        if (((mode_target_velocity_mps >= 0.0f) &&
             (measured_velocity_mps >= BALANCE_MODE_LAUNCH_EXIT_MPS)) ||
            ((mode_target_velocity_mps < 0.0f) &&
             (measured_velocity_mps <= -BALANCE_MODE_LAUNCH_EXIT_MPS))) {
            balance_drive_launch_timer_sec = 0.0f;
        }
        /*
         * launch 结束的这一拍,车已被甩到 ~0.15m/s 滚动;斜坡若从 0
         * 重爬(0.08/s 需 2s),Kv 项会先把车刹回静摩擦死区。
         * 用当前实测速度做斜坡起点(限幅在 0~巡航目标之间),接力续滚。
         */
        if (balance_drive_launch_timer_sec <= 0.0f) {
            float ramp_seed_mps = measured_velocity_mps;
            if (mode_target_velocity_mps >= 0.0f) {
                if (ramp_seed_mps < 0.0f) {
                    ramp_seed_mps = 0.0f;
                }
                if (ramp_seed_mps > mode_target_velocity_mps) {
                    ramp_seed_mps = mode_target_velocity_mps;
                }
            } else {
                if (ramp_seed_mps > 0.0f) {
                    ramp_seed_mps = 0.0f;
                }
                if (ramp_seed_mps < mode_target_velocity_mps) {
                    ramp_seed_mps = mode_target_velocity_mps;
                }
            }
            balance_velocity_ref_ramp_mps = ramp_seed_mps;
        }
    }
#if BALANCE_MODE_LAUNCH_ENABLE && BALANCE_MODE_LAUNCH_RETRIGGER_ENABLE
    /*
     * 步态化走路:launch 顶出楼板 -> 滚动段 Kv 维持 -> 方向速度衰减回
     * 楼板以下且持续 DWELL 秒,自动补一发 launch。与旧 pulse 蠕动方向
     * 相反:不是刹进楼板,而是周期性顶出楼板。launch 空转(21:12 fwd#2:
     * 车身甩过分离角但轮没破摩擦)也由此自动重试。守卫激活期间不重装。
     */
    if ((drive_bias_mode != 0U) && (drive_launch_active == 0U) &&
        (balance_speed_guard_active == 0U)) {
        float gait_dir_v_mps = (mode_target_velocity_mps >= 0.0f) ?
                               measured_velocity_mps :
                               -measured_velocity_mps;
        if (gait_dir_v_mps < BALANCE_MODE_LAUNCH_RETRIGGER_BELOW_MPS) {
            balance_launch_rearm_timer_sec += BALANCE_PID_DT_SEC;
            if (balance_launch_rearm_timer_sec >=
                BALANCE_MODE_LAUNCH_RETRIGGER_DWELL_SEC) {
                balance_drive_launch_timer_sec =
                    BALANCE_MODE_LAUNCH_TIME_SEC;
                balance_launch_rearm_timer_sec = 0.0f;
            }
        } else {
            balance_launch_rearm_timer_sec = 0.0f;
        }
    } else {
        balance_launch_rearm_timer_sec = 0.0f;
    }
#endif
    drive_pulse_brake_active = 0U;
    drive_pulse_hold_active = 0U;
#if BALANCE_MODE_PULSE_DRIVE_ENABLE
    if (drive_bias_mode != 0U) {
        if (balance_drive_pulse_brake_timer_sec > 0.0f) {
            drive_pulse_brake_active = 1U;
        } else if (balance_drive_pulse_hold_timer_sec > 0.0f) {
            drive_pulse_hold_active = 1U;
        } else if ((drive_launch_active == 0U) &&
                   ((balance_drive_pulse_timer_sec <= 0.0f) ||
                    (Chassis_AbsF(measured_velocity_mps) >
                     BALANCE_MODE_PULSE_BRAKE_ON_MPS))) {
            balance_drive_pulse_brake_timer_sec =
                BALANCE_MODE_PULSE_BRAKE_TIME_SEC;
            drive_pulse_brake_active = 1U;
        } else if (drive_launch_active == 0U) {
            balance_drive_pulse_timer_sec =
                Chassis_StepTowards(balance_drive_pulse_timer_sec,
                                    0.0f,
                                    BALANCE_PID_DT_SEC);
        }

        if (drive_pulse_brake_active != 0U) {
            float resume_timer_sec = BALANCE_MODE_PULSE_BRAKE_TIME_SEC -
                                     BALANCE_MODE_PULSE_BRAKE_MIN_TIME_SEC;
            if (resume_timer_sec < 0.0f) {
                resume_timer_sec = 0.0f;
            }
            target_velocity_mps = 0.0f;
            balance_velocity_ref_ramp_mps = 0.0f;
            balance_drive_pulse_brake_timer_sec =
                Chassis_StepTowards(balance_drive_pulse_brake_timer_sec,
                                    0.0f,
                                    BALANCE_PID_DT_SEC);
            if ((balance_drive_pulse_brake_timer_sec <= 0.0f) ||
                ((balance_drive_pulse_brake_timer_sec <= resume_timer_sec) &&
                 (Chassis_AbsF(measured_velocity_mps) <
                 BALANCE_MODE_PULSE_RESUME_MPS))) {
                balance_drive_pulse_brake_timer_sec = 0.0f;
                if (BALANCE_MODE_PULSE_HOLD_TIME_SEC > 0.0f) {
                    balance_drive_pulse_hold_timer_sec =
                        BALANCE_MODE_PULSE_HOLD_TIME_SEC;
                    drive_pulse_hold_active = 1U;
                } else {
                    balance_drive_pulse_timer_sec =
                        BALANCE_MODE_PULSE_DRIVE_TIME_SEC;
                }
                drive_pulse_brake_active = 0U;
            }
        } else if (drive_pulse_hold_active != 0U) {
            target_velocity_mps = 0.0f;
            balance_velocity_ref_ramp_mps = 0.0f;
            balance_drive_pulse_hold_timer_sec =
                Chassis_StepTowards(balance_drive_pulse_hold_timer_sec,
                                    0.0f,
                                    BALANCE_PID_DT_SEC);
            if (balance_drive_pulse_hold_timer_sec <= 0.0f) {
                balance_drive_pulse_hold_timer_sec = 0.0f;
                balance_drive_pulse_timer_sec =
                    BALANCE_MODE_PULSE_DRIVE_TIME_SEC;
                drive_pulse_hold_active = 0U;
            }
        }
    }
#else
    (void)drive_pulse_brake_active;
    (void)drive_pulse_hold_active;
#endif
    /* All controlled stops use sign-adaptive viscous damping. The 0.8 s
     * timer grants higher wheel-loop authority; damping itself remains active
     * until velocity and posture are continuously quiet. */
    stop_brake_active = 0U;
    stop_brake_boost_active = 0U;
#if BALANCE_MODE_STOP_BRAKE_ENABLE
    if ((balance_motion_state == BALANCE_MOTION_HOLD) &&
        (drive_requested == 0U) &&
        (balance_stop_position_hold_armed != 0U)) {
        if (BalanceControl_StopReboundConfirmed(
                measured_velocity_mps,
                BALANCE_PID_DT_SEC,
                BALANCE_MODE_STOP_REBOUND_V_MPS,
                BALANCE_MODE_STOP_REBOUND_CONFIRM_SEC,
                &balance_stop_rebound_timer_sec,
                &balance_stop_rebound_candidate_dir) != 0U) {
            Chassis_EnterMotionBraking(&drive_stop_edge);
        }
    } else {
        balance_stop_rebound_timer_sec = 0.0f;
        balance_stop_rebound_candidate_dir = 0.0f;
    }

    if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
        (balance_motion_state == BALANCE_MOTION_RUNNING)) {
        balance_stop_brake_timer_sec = 0.0f;
        balance_stop_brake_dir = 0.0f;
        balance_stop_brake_entry_dir = 0.0f;
        balance_stop_release_timer_sec = 0.0f;
        balance_stop_rebound_timer_sec = 0.0f;
        balance_stop_rebound_candidate_dir = 0.0f;
        balance_stop_position_hold_armed = 0U;
    } else if (balance_motion_state == BALANCE_MOTION_BRAKING) {
        float stop_brake_abs_v_mps = Chassis_AbsF(measured_velocity_mps);
        uint8_t stop_release_posture_ready =
            ((Chassis_AbsF(theta_deg - balance_theta_ref_slew_deg) <=
              BALANCE_MODE_SETTLE_MAX_THETA_ERROR_DEG) &&
             (Chassis_AbsF(theta_rate_dps) <=
              BALANCE_MODE_SETTLE_MAX_RATE_DPS)) ? 1U : 0U;

        if (drive_stop_edge != 0U) {
            balance_stop_brake_timer_sec = 0.0f;
            balance_stop_brake_dir = 0.0f;
            balance_stop_brake_entry_dir = 0.0f;
            balance_stop_release_timer_sec = 0.0f;
            balance_stop_rebound_timer_sec = 0.0f;
            balance_stop_rebound_candidate_dir = 0.0f;
            balance_stop_position_hold_armed = 0U;
            balance_stop_brake_entry_dir =
                BalanceControl_StopCaptureDirection(
                    measured_velocity_mps,
                    BALANCE_MODE_STOP_BRAKE_EDGE_MIN_MPS);
            if (balance_stop_brake_entry_dir != 0.0f) {
                balance_stop_brake_dir = balance_stop_brake_entry_dir;
                balance_stop_brake_timer_sec =
                    BALANCE_MODE_STOP_BRAKE_TIME_SEC;
            }
        }

        /* A near-zero stop edge may not have a reliable direction yet. If
         * the body disturbance subsequently creates real motion, arm from
         * that direction so its next zero crossing can enter HOLD. */
        if (balance_stop_brake_entry_dir == 0.0f) {
            balance_stop_brake_entry_dir =
                BalanceControl_StopCaptureDirection(
                    measured_velocity_mps,
                    BALANCE_MODE_STOP_BRAKE_EDGE_MIN_MPS);
            if (balance_stop_brake_entry_dir != 0.0f) {
                balance_stop_brake_dir = balance_stop_brake_entry_dir;
                balance_stop_brake_timer_sec =
                    BALANCE_MODE_STOP_BRAKE_TIME_SEC;
            }
        }

        /* Attitude braking is intentionally one-sided. Reversing it after
         * every zero crossing creates a delayed 8-10 Hz limit cycle. At the
         * first genuine crossing, capture position and let HOLD settle the
         * body; sustained rebound is handled by the hysteretic re-arm path. */
        if (BalanceControl_StopZeroCrossed(
                measured_velocity_mps,
                balance_stop_brake_entry_dir,
                BALANCE_MODE_STOP_ZERO_CROSS_MAX_MPS) != 0U) {
            Chassis_EnterMotionHold(measured_position_m);
        } else {
            if (stop_brake_abs_v_mps > BALANCE_MODE_STOP_RELEASE_V_MPS) {
                balance_stop_brake_dir =
                    (measured_velocity_mps >= 0.0f) ? 1.0f : -1.0f;
            }

            if (stop_release_posture_ready == 0U) {
                balance_stop_release_timer_sec = 0.0f;
            } else if (BalanceControl_StopReleaseConfirmed(
                           measured_velocity_mps,
                           BALANCE_PID_DT_SEC,
                           BALANCE_MODE_STOP_RELEASE_V_MPS,
                           BALANCE_MODE_STOP_RELEASE_CONFIRM_SEC,
                           &balance_stop_release_timer_sec) != 0U) {
                Chassis_EnterMotionHold(measured_position_m);
            } else {
                stop_brake_active = 1U;
            }
        }
    }
#endif
    if ((balance_motion_state == BALANCE_MOTION_BRAKING) &&
        (stop_brake_active == 0U)) {
        stop_brake_active = 1U;
    }
    if ((stop_brake_active != 0U) &&
        (balance_stop_brake_timer_sec > 0.0f)) {
        balance_stop_brake_timer_sec =
            Chassis_StepTowards(balance_stop_brake_timer_sec,
                                0.0f,
                                BALANCE_PID_DT_SEC);
        if (balance_stop_brake_timer_sec > 0.0f) {
            stop_brake_boost_active = 1U;
        }
    }
    if (drive_pulse_brake_active != 0U) {
        stop_brake_active = 1U;
    }
    if ((stop_brake_active != 0U) &&
        (Chassis_AbsF(theta_deg) > BALANCE_MODE_BRAKE_RECOVER_ANGLE_DEG)) {
        stop_brake_active = 0U;
        balance_stop_brake_timer_sec = 0.0f;
        balance_stop_brake_dir = 0.0f;
        balance_stop_brake_entry_dir = 0.0f;
        balance_stop_release_timer_sec = 0.0f;
    }
    balance_stop_brake_state =
        ((stop_brake_active != 0U) ? 0x01U : 0U) |
        ((balance_stop_brake_dir > 0.0f) ? 0x02U : 0U) |
        ((balance_stop_brake_dir < 0.0f) ? 0x04U : 0U) |
        ((balance_stop_position_hold_armed != 0U) ? 0x08U : 0U) |
        ((balance_stop_release_timer_sec > 0.0f) ? 0x10U : 0U) |
        ((balance_stop_rebound_timer_sec > 0.0f) ? 0x20U : 0U) |
        ((stop_brake_boost_active != 0U) ? 0x40U : 0U);
    balance_velocity_ref_ramp_mps =
        Chassis_StepTowards(balance_velocity_ref_ramp_mps,
                            target_velocity_mps,
                            BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC *
                            BALANCE_PID_DT_SEC);

    turn_target_rpm = Chassis_LimitAbs(
        mode_command.turn_rpm, balance_pid_runtime_speed_limit_rpm);
    if ((balance_motion_state == BALANCE_MOTION_BRAKING) ||
        ((balance_motion_state == BALANCE_MOTION_HOLD) &&
         (drive_requested != 0U))) {
        turn_target_rpm = 0.0f;
    }
    desired_wheel_diff_rpm = -2.0f * turn_target_rpm;
    turn_diff_correction_rpm = 0.0f;
    yaw_rate_correction_rpm = 0.0f;
    if ((balance_motion_state == BALANCE_MOTION_STARTING) ||
        (balance_motion_state == BALANCE_MOTION_RUNNING) ||
        (Chassis_AbsF(mode_command.turn_rpm) > 0.001f)) {
        float wheel_diff_error_rpm = BalanceControl_ApplyContinuousDeadband(
            balance_wheel_diff_lpf_rpm - desired_wheel_diff_rpm,
            BALANCE_MODE_WHEEL_DIFF_DEADBAND_RPM);

        turn_diff_correction_rpm = BALANCE_MODE_WHEEL_DIFF_KP *
                                   wheel_diff_error_rpm;
        turn_diff_correction_rpm = Chassis_LimitAbs(
            turn_diff_correction_rpm, BALANCE_MODE_WHEEL_DIFF_LIMIT_RPM);
        yaw_rate_correction_rpm = BALANCE_MODE_YAW_RATE_DAMP_SIGN *
                                  BALANCE_MODE_YAW_RATE_DAMP_KP_RPM_PER_DPS *
                                  global_chassis_yaw_rate;
        yaw_rate_correction_rpm = Chassis_LimitAbs(
            yaw_rate_correction_rpm, BALANCE_MODE_YAW_RATE_DAMP_LIMIT_RPM);
    }
    if ((balance_motion_state != BALANCE_MOTION_BRAKING) &&
        !((balance_motion_state == BALANCE_MOTION_HOLD) &&
          (drive_requested != 0U))) {
        turn_target_rpm += turn_diff_correction_rpm +
                           yaw_rate_correction_rpm;
    }
    turn_target_rpm = Chassis_LimitAbs(
        turn_target_rpm, balance_pid_runtime_speed_limit_rpm);
    balance_turn_ref_slew_rpm = Chassis_StepTowards(
        balance_turn_ref_slew_rpm,
        turn_target_rpm,
        ((chassis_mode == CHASSIS_MODE_TRACKING) ?
         TRACKING_TURN_SLEW_RPM_PER_SEC :
         BALANCE_MODE_TURN_SLEW_RPM_PER_SEC) * BALANCE_PID_DT_SEC);
    turn_speed_ref_rpm = balance_turn_ref_slew_rpm;

    drive_bias_mode =
        ((balance_motion_state == BALANCE_MOTION_STARTING) ||
         (balance_motion_state == BALANCE_MOTION_RUNNING)) ? 1U : 0U;

    if (drive_bias_mode != 0U) {
        balance_position_ref_m = measured_position_m;
    }

    speed_error_mps = balance_velocity_ref_ramp_mps - measured_velocity_mps;
    speed_error_mps = BalanceControl_ApplyContinuousDeadband(
        speed_error_mps, BALANCE_PID_WHEEL_SPEED_DEADBAND_MPS);
    position_error_m = balance_position_ref_m - measured_position_m;
    if (drive_bias_mode != 0U) {
        position_error_m = 0.0f;
    }
    accel_error_mps2 = 0.0f - balance_accel_lpf_mps2;
    accel_theta_ref_deg = BALANCE_POSITION_KA_DEG_PER_MPS2 *
                          accel_error_mps2;

    speed_integral_safety_decay =
        ((drive_requested == 0U) ||
         ((balance_motion_state != BALANCE_MOTION_STARTING) &&
          (balance_motion_state != BALANCE_MOTION_RUNNING)) ||
         (balance_speed_guard_active != 0U) ||
         (stop_brake_active != 0U) ||
         (drive_pulse_brake_active != 0U) ||
         (drive_pulse_hold_active != 0U) ||
         (drive_launch_active != 0U) ||
         (BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S == 0.0f)) ? 1U : 0U;
    speed_integration_eligible = 0U;
    if ((speed_integral_safety_decay == 0U) &&
        (balance_motion_state == BALANCE_MOTION_RUNNING) &&
        (Chassis_AbsF(balance_start_assist_deg) <=
         BALANCE_MODE_START_ASSIST_INTEGRATOR_GATE_DEG) &&
        (Chassis_AbsF(balance_velocity_ref_ramp_mps -
                      target_velocity_mps) <=
         BALANCE_MODE_DRIVE_INTEGRATOR_RAMP_GATE_MPS)) {
        float theta_current =
            BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS * speed_error_mps +
            BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S *
            balance_speed_integral_m;
        float total_theta_current;

        theta_current = Chassis_LimitAbs(
            theta_current, BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG);
        total_theta_current = balance_manual_bias_deg +
                              mode_bias_deg +
                              balance_start_assist_deg +
                              theta_current;
        total_theta_current = Chassis_LimitAbs(
            total_theta_current, BALANCE_PID_THETA_REF_LIMIT_DEG);
        if (Chassis_AbsF(balance_speed_theta_ref_slew_deg - theta_current) <=
            BALANCE_MODE_DRIVE_INTEGRATOR_LEAD_LIMIT_DEG &&
            Chassis_AbsF(balance_theta_ref_slew_deg - total_theta_current) <=
            BALANCE_MODE_DRIVE_INTEGRATOR_LEAD_LIMIT_DEG) {
            speed_integration_eligible = 1U;
        }
    }
    speed_integral_action = BalanceControl_SelectIntegralAction(
        speed_integral_safety_decay, speed_integration_eligible);
    if (speed_integral_action == BALANCE_CONTROL_INTEGRAL_INTEGRATE) {
        float integral_candidate = BalanceControl_ApplyIntegralAction(
            balance_speed_integral_m,
            speed_error_mps,
            BALANCE_PID_DT_SEC,
            BALANCE_MODE_DRIVE_SPEED_INTEGRAL_LIMIT_M_S,
            BALANCE_SPEED_GUARD_INTEGRAL_DECAY_M_PER_S,
            speed_integral_action);
        float theta_candidate =
            BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS * speed_error_mps +
            BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S * integral_candidate;
        float theta_limited = Chassis_LimitAbs(
            theta_candidate, BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG);
        float total_theta_candidate = balance_manual_bias_deg +
                                      mode_bias_deg +
                                      balance_start_assist_deg +
                                      theta_limited;
        uint8_t integration_allowed = 1U;

        if ((Chassis_AbsF(theta_candidate) >
             BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG) &&
            ((theta_candidate * speed_error_mps) > 0.0f)) {
            integration_allowed = 0U;
        }
        if ((Chassis_AbsF(total_theta_candidate) >
             BALANCE_PID_THETA_REF_LIMIT_DEG) &&
            ((total_theta_candidate * speed_error_mps) > 0.0f)) {
            integration_allowed = 0U;
        }
        if (integration_allowed != 0U) {
            balance_speed_integral_m = integral_candidate;
        }
    } else {
        balance_speed_integral_m = BalanceControl_ApplyIntegralAction(
            balance_speed_integral_m,
            speed_error_mps,
            BALANCE_PID_DT_SEC,
            BALANCE_MODE_DRIVE_SPEED_INTEGRAL_LIMIT_M_S,
            BALANCE_SPEED_GUARD_INTEGRAL_DECAY_M_PER_S,
            speed_integral_action);
    }
#if !BALANCE_MODE_DRIVE_THETA_REF_ENABLE
    if (drive_bias_mode != 0U) {
        balance_speed_integral_m = 0.0f;
    }
#endif
    if (drive_bias_mode != 0U) {
        speed_theta_ref_deg =
            BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS * speed_error_mps +
            BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S * balance_speed_integral_m;
        speed_theta_ref_deg =
            Chassis_LimitAbs(speed_theta_ref_deg,
                             BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG);
    } else {
        speed_theta_ref_deg =
            BALANCE_POSITION_KP_DEG_PER_M * position_error_m +
            BALANCE_POSITION_KV_DEG_PER_MPS * speed_error_mps +
            accel_theta_ref_deg +
            BALANCE_POSITION_KI_DEG_PER_M_S * balance_speed_integral_m;
        speed_theta_ref_deg = Chassis_LimitAbs(
            speed_theta_ref_deg,
            BALANCE_POSITION_THETA_REF_LIMIT_DEG);
    }
#if !BALANCE_MODE_DRIVE_THETA_REF_ENABLE
    if (drive_bias_mode != 0U) {
        speed_theta_ref_deg = 0.0f;
    }
#endif
    if (drive_launch_active != 0U) {
        launch_bias_deg = (mode_target_velocity_mps >= 0.0f) ?
                          BALANCE_MODE_LAUNCH_BIAS_DEG :
                          -BALANCE_MODE_LAUNCH_BIAS_DEG;
        speed_theta_ref_deg = launch_bias_deg;
    }
#if BALANCE_MODE_STOP_BRAKE_ENABLE
    /*
     * 姿态域刹车:倒立摆减速的唯一物理路径是先把车身转到平衡点后方,
     * 轮级反转指令只会把车身推得更前倾(16:36 "越刹越快")。刹车窗口内
     * 在 Kv 之外叠加与速度成正比的后仰目标,让车身 0.3~0.4s 转过去,
     * 而不是靠巡航 Kv 权限慢慢蹭 2s(20:02 实测冲出 0.14~0.16m)。
     */
    if (stop_brake_active != 0U) {
        float brake_theta_deg = 0.0f;

        if (balance_stop_brake_dir != 0.0f) {
            float base_theta_ref_deg = balance_manual_bias_deg +
                                       mode_bias_deg +
                                       balance_start_assist_deg +
                                       speed_theta_ref_deg;
            float guarded_theta_ref_deg;

            brake_theta_deg = BalanceControl_CalcViscousBrakeTheta(
                measured_velocity_mps,
                BALANCE_MODE_STOP_BRAKE_KV_DEG_PER_MPS,
                BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG);
            guarded_theta_ref_deg =
                Chassis_ClampF(base_theta_ref_deg + brake_theta_deg,
                               theta_deg -
                               BALANCE_MODE_STOP_BRAKE_TRACK_ERR_DEG,
                               theta_deg +
                               BALANCE_MODE_STOP_BRAKE_TRACK_ERR_DEG);
            brake_theta_deg = guarded_theta_ref_deg - base_theta_ref_deg;
            if ((brake_theta_deg * measured_velocity_mps) > 0.0f) {
                brake_theta_deg = 0.0f;
            }
        }

        speed_theta_ref_deg += brake_theta_deg;
    }
#endif
    stand_theta_ref_limit_deg = BALANCE_POSITION_THETA_REF_LIMIT_DEG;
#if BALANCE_MODE_STOP_BRAKE_ENABLE
    if ((stop_brake_active != 0U) &&
        (stand_theta_ref_limit_deg <
         BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG)) {
        stand_theta_ref_limit_deg =
            BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG;
    }
#endif
    if ((drive_requested == 0U) ||
        (balance_motion_state == BALANCE_MOTION_BRAKING)) {
        speed_theta_ref_deg =
            Chassis_LimitAbs(speed_theta_ref_deg,
                             stand_theta_ref_limit_deg);
    }
    if (drive_launch_active != 0U) {
        balance_speed_theta_ref_slew_deg = speed_theta_ref_deg;
    } else {
        theta_ref_slew_deg_per_sec =
            BALANCE_PID_SPEED_THETA_REF_SLEW_DEG_PER_SEC;
        if ((drive_requested != 0U) &&
            (balance_motion_state != BALANCE_MOTION_BRAKING)) {
            theta_ref_slew_deg_per_sec =
                BALANCE_MODE_DRIVE_THETA_REF_SLEW_DEG_PER_SEC;
        }
#if BALANCE_MODE_STOP_BRAKE_ENABLE
        if (stop_brake_active != 0U) {
            theta_ref_slew_deg_per_sec =
                BALANCE_MODE_STOP_BRAKE_THETA_SLEW_DEG_PER_SEC;
        }
#endif
        balance_speed_theta_ref_slew_deg =
            Chassis_StepTowards(
                balance_speed_theta_ref_slew_deg,
                speed_theta_ref_deg,
                theta_ref_slew_deg_per_sec * BALANCE_PID_DT_SEC);
        speed_theta_ref_deg = balance_speed_theta_ref_slew_deg;
    }

    theta_ref_target_deg = balance_manual_bias_deg + mode_bias_deg +
                           balance_start_assist_deg + speed_theta_ref_deg;
    theta_ref_target_deg = Chassis_LimitAbs(
        theta_ref_target_deg, BALANCE_PID_THETA_REF_LIMIT_DEG);
    final_theta_ref_slew_deg_per_sec =
        BALANCE_PID_SPEED_THETA_REF_SLEW_DEG_PER_SEC;
    if ((drive_requested != 0U) &&
        (balance_motion_state != BALANCE_MOTION_BRAKING)) {
        final_theta_ref_slew_deg_per_sec =
            BALANCE_MODE_DRIVE_THETA_REF_SLEW_DEG_PER_SEC;
    }
#if BALANCE_MODE_STOP_BRAKE_ENABLE
    if (stop_brake_active != 0U) {
        final_theta_ref_slew_deg_per_sec =
            BALANCE_MODE_STOP_BRAKE_THETA_SLEW_DEG_PER_SEC;
    }
#endif
    /* Bias, startup assist and speed/brake terms can move together. Limit the
     * final sum so their individual slopes never add into a body-pitch step. */
    balance_theta_ref_slew_deg = Chassis_StepTowards(
        balance_theta_ref_slew_deg,
        theta_ref_target_deg,
        final_theta_ref_slew_deg_per_sec * BALANCE_PID_DT_SEC);
    theta_ref_deg = balance_theta_ref_slew_deg;
    theta_error_deg = theta_deg - theta_ref_deg;

    balance_speed_ref_rpm = ChassisPid_UpdateAngle(&balance_angle_pid,
                                                   theta_error_deg,
                                                   theta_rate_dps,
                                                   BALANCE_PID_DT_SEC);
    balance_speed_limit_rpm = balance_pid_runtime_speed_limit_rpm;
    if ((drive_requested == 0U) ||
        (balance_motion_state == BALANCE_MOTION_BRAKING)) {
        float stand_speed_limit_rpm = BALANCE_PID_STAND_SPEED_LIMIT_RPM;
#if BALANCE_MODE_STOP_BRAKE_ENABLE
        if ((stop_brake_boost_active != 0U) &&
            (stand_speed_limit_rpm <
             BALANCE_MODE_STOP_BRAKE_STAND_SPEED_LIMIT_RPM)) {
            stand_speed_limit_rpm =
                BALANCE_MODE_STOP_BRAKE_STAND_SPEED_LIMIT_RPM;
        }
#endif
        balance_speed_limit_rpm =
            Chassis_ClampF(stand_speed_limit_rpm,
                           BALANCE_PID_RUNTIME_SPEED_LIMIT_MIN_RPM,
                           balance_pid_runtime_speed_limit_rpm);
    }
    balance_speed_ref_rpm =
        Chassis_LimitAbs(balance_speed_ref_rpm,
                         balance_speed_limit_rpm);
    if (stop_brake_active != 0U) {
        balance_speed_ref_rpm *= BALANCE_MODE_STOP_BRAKE_BALANCE_SCALE;
    }

    /* STARTING keeps the full reference-speed base to break static friction.
     * RUNNING transfers only bounded ownership to measured wheel speed, so a
     * small reference base remains available instead of falling back into the
     * measured 8 Hz stick-slip cycle. */
    drive_base_rpm = 0.0f;
    drive_reference_base_rpm = BALANCE_PID_VELOCITY_SIGN *
                               balance_velocity_ref_ramp_mps /
                               BALANCE_RPM_TO_MPS;
    drive_actual_base_rpm = 0.5f * (left_rpm + right_rpm);
    if ((drive_actual_base_rpm * balance_active_drive_sign) < 0.0f) {
        /* A measured rollback must not turn the feed-forward base around.
         * The balance correction remains free to command either direction. */
        drive_actual_base_rpm = 0.0f;
    }
    if (balance_motion_state == BALANCE_MOTION_RUNNING) {
        balance_drive_actual_base_lpf_rpm =
            BalanceControl_UpdateLowPass(
                balance_drive_actual_base_lpf_rpm,
                drive_actual_base_rpm,
                BALANCE_PID_DT_SEC,
                BALANCE_MODE_DRIVE_ACTUAL_BASE_LPF_TAU_SEC);
    } else {
        /* Seed STARTING with the reference base so the first RUNNING sample
         * is continuous even though ownership begins in the same cycle. */
        balance_drive_actual_base_lpf_rpm = drive_reference_base_rpm;
    }
    if ((drive_bias_mode != 0U) &&
        (drive_requested != 0U) &&
        (balance_speed_guard_active == 0U) &&
        (drive_launch_active == 0U) &&
        (drive_pulse_brake_active == 0U) &&
        (drive_pulse_hold_active == 0U) &&
        (stop_brake_active == 0U)) {
#if BALANCE_MODE_DRIVE_USE_ACTUAL_BASE
        balance_drive_actual_base_blend =
            BalanceControl_UpdateActualBaseBlend(
                balance_drive_actual_base_blend,
                BALANCE_PID_DT_SEC,
                BALANCE_MODE_DRIVE_BASE_BLEND_PER_SEC,
                BALANCE_MODE_DRIVE_ACTUAL_BASE_BLEND_MAX,
                (balance_motion_state == BALANCE_MOTION_RUNNING) ? 1U : 0U);
        drive_base_rpm = drive_reference_base_rpm +
                         balance_drive_actual_base_blend *
                         (balance_drive_actual_base_lpf_rpm -
                          drive_reference_base_rpm);
#else
        balance_drive_actual_base_blend = 0.0f;
        drive_base_rpm = drive_reference_base_rpm;
#endif
    } else {
        balance_drive_actual_base_blend = 0.0f;
    }
    stop_brake_rpm = 0.0f;
    if (stop_brake_active != 0U) {
        stop_brake_rpm = -BALANCE_MODE_STOP_BRAKE_GAIN *
                         measured_velocity_mps /
                         BALANCE_RPM_TO_MPS;
        stop_brake_rpm =
            Chassis_LimitAbs(stop_brake_rpm,
                             BALANCE_MODE_STOP_BRAKE_LIMIT_RPM);
    }
    left_ref_rpm = balance_speed_ref_rpm + drive_base_rpm + stop_brake_rpm +
                   BALANCE_PID_LEFT_YAW_SIGN * turn_speed_ref_rpm;
    right_ref_rpm = balance_speed_ref_rpm + drive_base_rpm + stop_brake_rpm +
                    BALANCE_PID_RIGHT_YAW_SIGN * turn_speed_ref_rpm;
    left_ref_rpm = Chassis_LimitAbs(left_ref_rpm,
                                    balance_pid_runtime_speed_limit_rpm);
    right_ref_rpm = Chassis_LimitAbs(right_ref_rpm,
                                     balance_pid_runtime_speed_limit_rpm);

    speed_current_limit_a = balance_pid_runtime_current_limit_a;
    speed_current_slew_a_per_sec =
        balance_pid_runtime_current_slew_a_per_sec;
#if BALANCE_MODE_STOP_BRAKE_ENABLE
    if (stop_brake_boost_active != 0U) {
        if (speed_current_limit_a <
            BALANCE_MODE_STOP_BRAKE_CURRENT_LIMIT_A) {
            speed_current_limit_a =
                BALANCE_MODE_STOP_BRAKE_CURRENT_LIMIT_A;
        }
        if (speed_current_slew_a_per_sec <
            BALANCE_MODE_STOP_BRAKE_CURRENT_SLEW_A_PER_SEC) {
            speed_current_slew_a_per_sec =
                BALANCE_MODE_STOP_BRAKE_CURRENT_SLEW_A_PER_SEC;
        }
    }
#endif

    left_out = Chassis_SpeedPidUpdate(left_ref_rpm,
                                      left_rpm,
                                      LEFT_WHEEL_SPEED_PID_POLARITY,
                                      &left_speed_pid_integral,
                                      &left_speed_last_output,
                                      BALANCE_PID_DT_SEC,
                                      speed_current_limit_a,
                                      speed_current_slew_a_per_sec);
    right_out = Chassis_SpeedPidUpdate(right_ref_rpm,
                                       right_rpm,
                                       RIGHT_WHEEL_SPEED_PID_POLARITY,
                                       &right_speed_pid_integral,
                                       &right_speed_last_output,
                                       BALANCE_PID_DT_SEC,
                                       speed_current_limit_a,
                                       speed_current_slew_a_per_sec);

    balance_pid_theta_deg = theta_deg;
    balance_pid_theta_ref_deg = theta_ref_deg;
    balance_pid_manual_bias_deg = balance_manual_bias_deg;
    balance_pid_theta_rate_dps = theta_rate_dps;
    balance_pid_accel_angle_deg = accel_angle_deg;
    balance_pid_position_m = measured_position_m;
    balance_pid_position_ref_m = balance_position_ref_m;
    balance_pid_velocity_mps = measured_velocity_mps;
    balance_pid_velocity_ref_mps = balance_velocity_ref_ramp_mps;
    balance_pid_accel_mps2 = balance_accel_lpf_mps2;
    balance_pid_accel_theta_ref_deg = accel_theta_ref_deg;
    balance_pid_wheel_diff_mps = wheel_diff_mps;
    balance_pid_speed_theta_ref_deg = speed_theta_ref_deg;
    balance_pid_integral_theta_ref_deg =
        ((drive_bias_mode != 0U) ?
         BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S :
         BALANCE_POSITION_KI_DEG_PER_M_S) * balance_speed_integral_m;
    balance_pid_zero_trim_deg = balance_zero_trim_deg;
    balance_pid_angle_speed_ref_rpm =
        BALANCE_PID_POLARITY *
        balance_pid_runtime_kp * theta_error_deg;
    balance_pid_rate_speed_ref_rpm =
        BALANCE_PID_POLARITY *
        balance_pid_runtime_kd * theta_rate_dps;
    balance_pid_balance_speed_ref_rpm = balance_speed_ref_rpm;
    balance_pid_turn_speed_ref_rpm = turn_speed_ref_rpm;
    balance_pid_current_target_a = 0.5f * (left_out + right_out);
    balance_pid_current_cmd_a = 0.5f * (left_out + right_out);
    balance_pid_current_feedback_a = 0.5f * (left_wheel_motor.current +
                                             right_wheel_motor.current);
    balance_pid_output_left_a = left_out;
    balance_pid_output_right_a = right_out;

    wheel_speed_ref_left_rpm = left_ref_rpm;
    wheel_speed_actual_left_rpm = left_rpm;
    wheel_speed_output_left_a = left_out;
    wheel_speed_ref_right_rpm = right_ref_rpm;
    wheel_speed_actual_right_rpm = right_rpm;
    wheel_speed_output_right_a = right_out;
    wheel_speed_feedback_left_a = left_wheel_motor.current;
    wheel_speed_feedback_right_a = right_wheel_motor.current;
    wheel_speed_eso_z1_rpm = global_balance_accel_forward_ms2;
    wheel_speed_eso_z2_rpm_s = wheel_diff_mps;
    wheel_speed_pi_integral = balance_angle_pid.integral;
    wheel_speed_pi_integral_right = balance_speed_integral_m;

    *left_current = left_out;
    *right_current = right_out;
}
#endif

static void Chassis_CalcMotorOutput(float *left_current, float *right_current)
{
#if BALANCE_PID_ENABLE
    if (chassis_motor_state == CHASSIS_MOTOR_BALANCE_PID) {
        Chassis_CalcBalancePidOutput(left_current, right_current);
        return;
    }
#endif

#if WHEEL_SPEED_TEST_ENABLE
    if (chassis_motor_state == CHASSIS_MOTOR_SPEED_TEST) {
        Chassis_CalcSpeedTestOutput(left_current, right_current);
        return;
    }
#endif

    Chassis_ResetSpeedCtrl();
    Chassis_UpdateBalanceViewZeroOutput();
    *left_current = 0.0f;
    *right_current = 0.0f;
}

static uint8_t Gimbal_IsVisualAimActive(void)
{
    TaskOutput_t task_output;

    if (chassis_mode == CHASSIS_MODE_TARGET_AIM) {
        return 1U;
    }
    Task_GetOutput(&task_output);
    return ((task_output.active != 0U) &&
            (task_output.visual_aim_enabled != 0U)) ? 1U : 0U;
}

static void Gimbal_UpdateCompetitionTask(uint32_t now_tick,
                                         uint8_t motion_settled)
{
    static uint32_t last_frame_count = 0U;
    static uint32_t last_frame_tick = 0U;
    Vision_TaskSnapshot_t vision;
    TaskInput_t input;
    TaskOutput_t output;

    if (Task_IsActive() == 0U) {
        Mode_SetTrackingTaskControl(0U, 0U, 0U, 0U);
        return;
    }

    (void)INS_GetVisionTaskTarget(&vision);
    if (vision.frame_count != last_frame_count) {
        last_frame_count = vision.frame_count;
        last_frame_tick = now_tick;
    }

    input.now_ms = now_tick;
    input.move_epoch = vision.move_epoch;
    input.route_phase = (uint8_t)Mode_TrackingGetPhase();
    input.motion_settled = motion_settled;
    input.laser_motion_safe = chassis_laser_safe;
    input.vision_fresh =
        ((vision.frame_count != 0U) &&
         ((uint32_t)(now_tick - last_frame_tick) <=
          pdMS_TO_TICKS(VISION_YAW_FRAME_TIMEOUT_MS))) ? 1U : 0U;
    input.vision_valid = vision.valid;
    input.target_locked = vision.locked;
    input.aim_ready =
        ((vision.aim_ready != 0U) &&
         (vision.dx >= -(int16_t)VISION_YAW_DEADBAND_PX) &&
         (vision.dx <= (int16_t)VISION_YAW_DEADBAND_PX)) ? 1U : 0U;
    input.gimbal_ready = gimbal_task_feedback_ready;

    Task_Update(&input);
    Task_GetOutput(&output);
    Mode_SetTrackingTaskControl(output.active,
                                output.route_target_release,
                                output.route_drive_enabled,
                                output.laser_enabled);
}

static void Gimbal_UpdateCalibrationLaserGuard(uint32_t now_tick)
{
    static uint32_t last_frame_count = 0U;
    static uint32_t last_valid_tick = 0U;
    static uint8_t request_was_active = 0U;
    Vision_TaskSnapshot_t vision;

    if (gimbal_vision_laser_request == 0U) {
        request_was_active = 0U;
        return;
    }

    (void)INS_GetVisionTaskTarget(&vision);
    if (request_was_active == 0U) {
        request_was_active = 1U;
        last_frame_count = vision.frame_count;
        last_valid_tick = now_tick;
    }
    if (vision.frame_count != last_frame_count) {
        last_frame_count = vision.frame_count;
        /* SET sends V heartbeats before a red target is locked. Any new
         * parsed frame renews the short lease; camera/link loss does not. */
        last_valid_tick = now_tick;
    }
    if ((uint32_t)(now_tick - last_valid_tick) >
        pdMS_TO_TICKS(VISION_YAW_FRAME_TIMEOUT_MS)) {
        (void)Gimbal_SetCalibrationLaserRequest(0U);
        request_was_active = 0U;
    }
}

static void Gimbal_UpdateYawMotor(uint32_t now_tick)
{
#if GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED
    static uint32_t enable_last_tick = 0U;
    static uint8_t enable_request_sent = 0U;
    static uint32_t vision_last_frame_count = 0U;
    static uint32_t vision_last_valid_tick = 0U;
    static ChassisMode_e vision_last_mode = CHASSIS_MODE_COUNT;
    static TaskId_e vision_last_task_id = TASK_ID_NONE;
    static uint8_t vision_had_valid = 0U;
    static uint8_t vision_tracking_valid = 0U;
    int16_t vision_dx = 0;
    int16_t vision_dy = 0;
    uint8_t vision_valid = 0U;
    uint32_t vision_frame_count =
        INS_GetVisionTarget(&vision_dx, &vision_dy, &vision_valid);
    CompetitionTaskStatus_t task_status;
    uint8_t visual_aim_active = Gimbal_IsVisualAimActive();
    uint8_t feedback_ready =
        Motor_FeedbackReady(&gimbal_yaw_motor,
                            now_tick,
                            pdMS_TO_TICKS(GIMBAL_YAW_FEEDBACK_TIMEOUT_MS));

    Task_GetStatus(&task_status);
    if ((vision_last_mode != chassis_mode) ||
        (vision_last_task_id != task_status.task_id)) {
        vision_last_mode = chassis_mode;
        vision_last_task_id = task_status.task_id;
        vision_last_frame_count = vision_frame_count;
        vision_last_valid_tick = now_tick;
        vision_had_valid = 0U;
        vision_tracking_valid = 0U;
        Mode_GimbalSetChassisSpeedFeedforward(0.0f, 0U);
        Mode_GimbalResetIntegral();
        /* A Mode 7 command alone must never energize yaw. The first fresh,
         * valid visual frame below is the only transition into AIM. */
        Mode_GimbalSet(GIMBAL_MODE_RELAX);
    }

    if (visual_aim_active != 0U) {
        if (vision_frame_count != vision_last_frame_count) {
            float correction_deg = 0.0f;

            vision_last_frame_count = vision_frame_count;
            if ((vision_valid != 0U) &&
                (feedback_ready != 0U) &&
                (gimbal_yaw_motor.enabled != 0U) &&
                (emergency_stop == 0U)) {
                float corrected_dx;

                vision_last_valid_tick = now_tick;
                vision_had_valid = 1U;
                vision_tracking_valid = 1U;
                Mode_SetTargetVisionReady(1U);
                corrected_dx = Mode_GimbalShapeVisionErrorPx(vision_dx);
                correction_deg =
                    atanf((VISION_YAW_SIGN * corrected_dx) /
                          VISION_YAW_FOCAL_LENGTH_PX) *
                    57.29577951308232f;
                Mode_GimbalSet(GIMBAL_MODE_AIM);
                Mode_GimbalSetTargetFromEncoderDeg(
                    gimbal_yaw_motor.angle, correction_deg);
            } else {
                vision_tracking_valid = 0U;
                Mode_SetTargetVisionReady(0U);
                Mode_GimbalResetIntegral();
            }
        }
        if ((uint32_t)(now_tick - vision_last_valid_tick) >
            pdMS_TO_TICKS(VISION_YAW_FRAME_TIMEOUT_MS)) {
            vision_tracking_valid = 0U;
            Mode_SetTargetVisionReady(0U);
            Mode_GimbalResetIntegral();
            if (vision_had_valid != 0U) {
                Mode_GimbalSet(GIMBAL_MODE_RELAX);
            }
        }
        if ((vision_had_valid != 0U) &&
            ((uint32_t)(now_tick - vision_last_valid_tick) >
             pdMS_TO_TICKS(VISION_YAW_CURRENT_STOP_TIMEOUT_MS))) {
            Mode_GimbalSet(GIMBAL_MODE_RELAX);
        }
    }

    Mode_GimbalSetChassisSpeedFeedforward(
        balance_pid_velocity_mps,
        ((visual_aim_active != 0U) &&
         (vision_tracking_valid != 0U) &&
         (feedback_ready != 0U) &&
         (gimbal_yaw_motor.enabled != 0U) &&
         (emergency_stop == 0U) &&
         ((balance_motion_state == BALANCE_MOTION_STARTING) ||
          (balance_motion_state == BALANCE_MOTION_RUNNING))) ? 1U : 0U);

    (void)vision_dy;

    if (visual_aim_active != 0U) {
        if ((feedback_ready != 0U) && (gimbal_yaw_motor.enabled != 0U)) {
            enable_request_sent = 0U;
        } else if ((enable_request_sent == 0U) ||
                   ((uint32_t)(now_tick - enable_last_tick) >=
                    pdMS_TO_TICKS(GIMBAL_YAW_ENABLE_RETRY_MS))) {
            if (Motor_Enable(GIMBAL_YAW_MOTOR_ID) == HAL_OK) {
                gimbal_yaw_enable_queue_ok_count++;
                enable_last_tick = now_tick;
                enable_request_sent = 1U;
            } else {
                gimbal_yaw_enable_queue_fail_count++;
            }
        }
    } else {
        enable_request_sent = 0U;
    }

    uint8_t control_ready =
        ((feedback_ready != 0U) &&
         (gimbal_yaw_motor.enabled != 0U) &&
         (emergency_stop == 0U)) ? 1U : 0U;
    gimbal_task_feedback_ready = control_ready;

    Mode_GimbalUpdate(gimbal_yaw_motor.angle,
                      gimbal_yaw_motor.speed_can_rpm,
                      control_ready,
                      GIMBAL_YAW_CONTROL_DT_SEC);
#if GIMBAL_YAW_CURRENT_COMMAND_ENABLE
    GimbalCommand_t command;
    uint8_t current_active;

    Mode_GimbalGetCommand(&command);
    current_active =
        ((visual_aim_active != 0U) &&
         (control_ready != 0U) &&
         (command.drive_enabled != 0U)) ? 1U : 0U;
    gimbal_yaw_pending_current_a =
        (current_active != 0U) ? command.yaw_current_a : 0.0f;
    if ((gimbal_yaw_current_active != 0U) &&
        (current_active == 0U)) {
        gimbal_yaw_zero_retry_count = GIMBAL_YAW_ZERO_RETRY_COUNT;
        if (Motor_TrySetCurrent(GIMBAL_YAW_MOTOR_ID, 0.0f) == HAL_OK) {
            gimbal_yaw_current_tx_ok_count++;
            gimbal_yaw_last_current_tx_tick = now_tick;
            if (gimbal_yaw_zero_retry_count > 0U) {
                gimbal_yaw_zero_retry_count--;
            }
        } else {
            gimbal_yaw_current_tx_fail_count++;
        }
    } else if (current_active != 0U) {
        gimbal_yaw_zero_retry_count = 0U;
    }
    gimbal_yaw_current_active = current_active;
#endif
#else
    (void)now_tick;
    Mode_GimbalUpdate(0.0f, 0.0f, 0U, GIMBAL_YAW_CONTROL_DT_SEC);
#endif
}

void Gimbal_SetVisionLaserRequest(uint8_t enabled)
{
    gimbal_vision_laser_request = (enabled != 0U) ? 1U : 0U;
}

uint8_t Gimbal_SetCalibrationLaserRequest(uint8_t enabled)
{
    if (enabled == 0U) {
        Gimbal_SetVisionLaserRequest(0U);
        BSP_LaserOff();
        gimbal_laser_on = 0U;
        return 1U;
    }

    if ((Task_IsActive() != 0U) ||
        (chassis_mode != CHASSIS_MODE_STAND) ||
        ((chassis_mode_request_pending != 0U) &&
         (chassis_mode_request != CHASSIS_MODE_STAND))) {
        return 0U;
    }
    Gimbal_SetVisionLaserRequest(1U);
    return 1U;
}

static void Gimbal_FlushYawCurrent(uint32_t now_tick)
{
#if GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED && GIMBAL_YAW_CURRENT_COMMAND_ENABLE
    uint32_t period_tick;
    float current_a;

    if (gimbal_yaw_zero_retry_count != 0U) {
        period_tick = pdMS_TO_TICKS(1U);
        current_a = 0.0f;
    } else if (gimbal_yaw_current_active != 0U) {
        period_tick = pdMS_TO_TICKS(GIMBAL_YAW_COMMAND_PERIOD_MS);
        current_a = gimbal_yaw_pending_current_a;
    } else {
        return;
    }
    if (period_tick == 0U) {
        period_tick = 1U;
    }
    if ((uint32_t)(now_tick - gimbal_yaw_last_current_tx_tick) <
        period_tick) {
        return;
    }

    if (Motor_TrySetCurrent(GIMBAL_YAW_MOTOR_ID, current_a) == HAL_OK) {
        gimbal_yaw_current_tx_ok_count++;
        gimbal_yaw_last_current_tx_tick = now_tick;
        if (gimbal_yaw_zero_retry_count != 0U) {
            gimbal_yaw_zero_retry_count--;
        }
    } else {
        gimbal_yaw_current_tx_fail_count++;
    }
#else
    (void)now_tick;
    gimbal_task_feedback_ready = 0U;
#endif
}

static uint8_t Chassis_IsMotionSettled(void)
{
    if ((balance_motion_state != BALANCE_MOTION_HOLD) ||
        (chassis_motor_state != CHASSIS_MOTOR_BALANCE_PID) ||
        (chassis_motor_feedback_ok == 0U) ||
        (chassis_motor_enabled_ok == 0U) ||
        (emergency_stop != 0U) ||
        (chassis_fault_code != CHASSIS_FAULT_NONE) ||
        (balance_stop_brake_timer_sec > 0.0f) ||
        (balance_stop_brake_dir != 0.0f) ||
        (Chassis_AbsF(balance_pid_velocity_mps) >
         BALANCE_MODE_BRAKE_V_DONE_MPS) ||
        (Chassis_AbsF(balance_turn_ref_slew_rpm) >
         BALANCE_MODE_TURN_SETTLE_RPM) ||
        (Chassis_AbsF(balance_wheel_diff_lpf_rpm) >
         BALANCE_MODE_TURN_SETTLE_DIFF_RPM) ||
        (Chassis_IsBalanceQuiet(balance_pid_theta_deg,
                                balance_pid_theta_ref_deg,
                                balance_pid_theta_rate_dps,
                                left_wheel_motor.speed_can_rpm,
                                right_wheel_motor.speed_can_rpm) == 0U)) {
        return 0U;
    }

    return 1U;
}

static uint8_t Chassis_IsLaserMotionSafe(void)
{
    float theta_error_deg = balance_pid_theta_deg -
                            balance_pid_theta_ref_deg;

    if ((chassis_mode != CHASSIS_MODE_TRACKING) ||
        (Chassis_IsMotionSettled() == 0U) ||
        (Chassis_AbsF(balance_pid_velocity_mps) >
         TRACKING_LASER_STOP_MAX_V_MPS) ||
        (Chassis_AbsF(left_wheel_motor.speed_can_rpm) >
         TRACKING_LASER_STOP_MAX_WHEEL_RPM) ||
        (Chassis_AbsF(right_wheel_motor.speed_can_rpm) >
         TRACKING_LASER_STOP_MAX_WHEEL_RPM) ||
        (Chassis_AbsF(theta_error_deg) >
         TRACKING_LASER_MAX_THETA_ERROR_DEG) ||
        (Chassis_AbsF(balance_pid_theta_rate_dps) >
         TRACKING_LASER_MAX_RATE_DPS) ||
        (Chassis_AbsF(balance_turn_ref_slew_rpm) >
         TRACKING_LASER_MAX_TURN_RPM)) {
        return 0U;
    }
    return 1U;
}

static void Gimbal_UpdateLaser(uint8_t motion_safe)
{
    ModeCommand_t command;
    TaskOutput_t task_output;
    uint8_t target_task_safe;
    uint8_t competition_task_safe;
    uint8_t vision_laser_safe;

    Mode_GetCommand(&command);
    Task_GetOutput(&task_output);
    target_task_safe =
        ((chassis_mode == CHASSIS_MODE_TARGET_AIM) &&
         (command.laser_enabled != 0U) &&
         (chassis_motor_state == CHASSIS_MOTOR_BALANCE_PID) &&
         (chassis_motor_feedback_ok != 0U) &&
         (chassis_motor_enabled_ok != 0U) &&
         (emergency_stop == 0U) &&
         (chassis_fault_code == CHASSIS_FAULT_NONE)) ? 1U : 0U;
    vision_laser_safe =
        ((gimbal_vision_laser_request != 0U) &&
         (task_output.active == 0U) &&
         (chassis_mode == CHASSIS_MODE_STAND) &&
         (chassis_fault_code == CHASSIS_FAULT_NONE) &&
         (Chassis_IsMotionSettled() != 0U)) ? 1U : 0U;
    competition_task_safe =
        ((task_output.active != 0U) &&
         (task_output.laser_enabled != 0U) &&
         (emergency_stop == 0U) &&
         (chassis_fault_code == CHASSIS_FAULT_NONE)) ? 1U : 0U;

    if ((vision_laser_safe != 0U) ||
        (target_task_safe != 0U) ||
        (competition_task_safe != 0U) ||
        ((motion_safe != 0U) &&
        (command.laser_enabled != 0U) &&
        (chassis_mode == CHASSIS_MODE_TRACKING) &&
        (Mode_TrackingGetPhase() ==
         MODE_TRACKING_PHASE_TARGET_LASER_DWELL))) {
        BSP_LaserOn();
        gimbal_laser_on = BSP_LaserIsOn();
    } else {
        BSP_LaserOff();
        gimbal_laser_on = 0U;
    }
}

void Gimbal_Task(void *argument)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(WHEEL_SAFE_TASK_PERIOD_MS);

    (void)argument;

    Mode_Init();
    Mode_GimbalInit();
    Task_Init();
    BSP_LaserOff();
#if GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED && GIMBAL_YAW_CURRENT_COMMAND_ENABLE
    /* A warm MCU reset does not reset the motor driver. Explicitly overwrite
     * any current command left latched in the driver before normal control. */
    gimbal_yaw_pending_current_a = 0.0f;
    gimbal_yaw_current_active = 0U;
    gimbal_yaw_zero_retry_count = GIMBAL_YAW_ZERO_RETRY_COUNT;
#endif
#if GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED && GIMBAL_YAW_AUTO_HOLD_ENABLE
    Mode_GimbalSet(GIMBAL_MODE_HOLD);
#endif

    uint8_t chassis_motion_settled = 0U;

    for (;;) {
        uint32_t now_tick = (uint32_t)xTaskGetTickCount();
        float left_current = 0.0f;
        float right_current = 0.0f;
        uint8_t left_feedback_ok =
            Motor_FeedbackReady(&left_wheel_motor,
                                now_tick,
                                pdMS_TO_TICKS(WHEEL_FEEDBACK_TIMEOUT_MS));
        uint8_t right_feedback_ok =
            Motor_FeedbackReady(&right_wheel_motor,
                                now_tick,
                                pdMS_TO_TICKS(WHEEL_FEEDBACK_TIMEOUT_MS));

        chassis_motor_feedback_ok = (left_feedback_ok && right_feedback_ok) ? 1U : 0U;
        chassis_motor_enabled_ok =
            (left_wheel_motor.enabled && right_wheel_motor.enabled) ? 1U : 0U;

        if ((chassis_motor_feedback_ok == 0U) ||
            (chassis_motor_enabled_ok == 0U)) {
            chassis_fault_code = (chassis_motor_feedback_ok == 0U) ?
                                 CHASSIS_FAULT_FEEDBACK :
                                 CHASSIS_FAULT_ENABLE;
            Chassis_SetControlMode(CHASSIS_MODE_STAND);
            chassis_speed_test_armed = 0U;
            emergency_stop = 1U;
        }

        Chassis_UpdateKey();
#if IMU_YAW_BIAS_KEY_CAL_ENABLE
        if (INS_GetYawCalibrationState() == INS_YAW_CAL_RUNNING) {
            chassis_speed_test_armed = 0U;
            emergency_stop = 1U;
        }
#endif
        Chassis_UpdateState();
        Chassis_ApplyControlModeRequest();
        /* Consume the previous post-control sample. A one-cycle unsafe sample
         * must reach the mode state machine instead of being overwritten by
         * a fresh pre-control reading. */
        Mode_SetMotionSettled(chassis_motion_settled);
        Mode_SetActualVelocity(balance_pid_velocity_mps);
        Mode_SetChassisYaw(INS_GetChassisYaw());
        Mode_SetLaserSafe(chassis_laser_safe);
        Gimbal_UpdateCalibrationLaserGuard(now_tick);
        Gimbal_UpdateCompetitionTask(now_tick, chassis_motion_settled);
        Mode_Update(now_tick);

        if (last_chassis_motor_state != chassis_motor_state) {
            if (last_chassis_motor_state == CHASSIS_MOTOR_BALANCE_PID) {
                Chassis_ResetBalancePid();
            }
#if WHEEL_SPEED_TEST_ENABLE && WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE
            if (last_chassis_motor_state == CHASSIS_MOTOR_SPEED_TEST) {
                Chassis_ResetSpeedTestAuto();
            }
#endif

            if (chassis_motor_state == CHASSIS_MOTOR_SPEED_TEST) {
                Chassis_ResetSpeedCtrl();
#if WHEEL_SPEED_TEST_ENABLE && WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE
                Chassis_ResetSpeedTestAuto();
#endif
            } else if (chassis_motor_state == CHASSIS_MOTOR_BALANCE_PID) {
                BSP_UART6_MuteTxForMs(VISION_UART_POWER_GUARD_MS);
                Chassis_ResetBalancePid();
            }
        }
        last_chassis_motor_state = chassis_motor_state;

        if (wheel_speed_target_changed != 0U) {
            wheel_speed_target_changed = 0U;
            left_speed_pid_integral = 0.0f;
            right_speed_pid_integral = 0.0f;
        }

        Chassis_CalcMotorOutput(&left_current, &right_current);
        chassis_motion_settled = Chassis_IsMotionSettled();
        chassis_laser_safe = Chassis_IsLaserMotionSafe();
        Mode_SetMotionSettled(chassis_motion_settled);
        Mode_SetActualVelocity(balance_pid_velocity_mps);
        Mode_SetLaserSafe(chassis_laser_safe);
        Gimbal_UpdateLaser(chassis_laser_safe);

        Gimbal_UpdateYawMotor(now_tick);
        Motor_SetCurrent(LEFT_WHEEL_MOTOR_ID, left_current);
        Motor_SetCurrent(RIGHT_WHEEL_MOTOR_ID, right_current);
        Gimbal_FlushYawCurrent(now_tick);

        Chassis_UpdateLed();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
