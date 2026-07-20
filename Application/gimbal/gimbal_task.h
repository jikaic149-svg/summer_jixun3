#ifndef GIMBAL_TASK_H
#define GIMBAL_TASK_H

#include "stdint.h"
#include "mode_control.h"

typedef enum {
    CHASSIS_MOTOR_ESTOP = 0,
    CHASSIS_MOTOR_WAIT_FEEDBACK,
    CHASSIS_MOTOR_WAIT_ENABLE,
    CHASSIS_MOTOR_READY,
    CHASSIS_MOTOR_SPEED_TEST,
    CHASSIS_MOTOR_BALANCE_PID
} ChassisMotorState_e;

/* High-level modes describe the requested task. This state describes how the
 * balance controller is executing the longitudinal start/stop transition. */
typedef enum {
    BALANCE_MOTION_HOLD = 0,
    BALANCE_MOTION_STARTING,
    BALANCE_MOTION_RUNNING,
    BALANCE_MOTION_BRAKING
} BalanceMotionState_e;

typedef enum {
    BALANCE_PID_RUNTIME_KP = 0,
    BALANCE_PID_RUNTIME_KI,
    BALANCE_PID_RUNTIME_KD,
    BALANCE_PID_RUNTIME_SPEED_LIMIT_RPM,
    BALANCE_PID_RUNTIME_CURRENT_LIMIT_A,
    BALANCE_PID_RUNTIME_CURRENT_SLEW_A_PER_SEC
} BalancePidRuntimeParam_e;

extern volatile ChassisMotorState_e chassis_motor_state;
extern volatile ChassisMode_e chassis_mode;
extern volatile uint8_t chassis_motor_feedback_ok;
extern volatile uint8_t chassis_motor_enabled_ok;
extern volatile uint8_t chassis_speed_test_armed;
extern volatile uint8_t chassis_fault_code;
extern volatile uint8_t emergency_stop;
extern volatile BalanceMotionState_e balance_motion_state;
extern volatile uint8_t chassis_laser_safe;
extern volatile uint8_t gimbal_laser_on;
extern volatile uint8_t gimbal_vision_laser_request;
extern volatile uint8_t balance_stop_brake_state;

extern volatile float wheel_speed_ref_left_rpm;
extern volatile float wheel_speed_actual_left_rpm;
extern volatile float wheel_speed_output_left_a;
extern volatile float wheel_speed_ref_right_rpm;
extern volatile float wheel_speed_actual_right_rpm;
extern volatile float wheel_speed_output_right_a;
extern volatile float wheel_speed_feedback_left_a;
extern volatile float wheel_speed_feedback_right_a;
extern volatile float wheel_speed_eso_z1_rpm;
extern volatile float wheel_speed_eso_z2_rpm_s;
extern volatile float wheel_speed_pi_integral;
extern volatile float wheel_speed_pi_integral_right;
extern volatile float wheel_speed_target_rpm;
extern volatile float balance_pid_theta_deg;
extern volatile float balance_pid_theta_ref_deg;
extern volatile float balance_pid_manual_bias_deg;
extern volatile float balance_pid_theta_rate_dps;
extern volatile float balance_pid_accel_angle_deg;
extern volatile float balance_pid_position_m;
extern volatile float balance_pid_position_ref_m;
extern volatile float balance_pid_velocity_mps;
extern volatile float balance_pid_velocity_ref_mps;
extern volatile float balance_pid_accel_mps2;
extern volatile float balance_pid_accel_theta_ref_deg;
extern volatile float balance_pid_wheel_diff_mps;
extern volatile float balance_pid_speed_theta_ref_deg;
extern volatile float balance_pid_integral_theta_ref_deg;
extern volatile float balance_pid_zero_trim_deg;
extern volatile float balance_pid_angle_speed_ref_rpm;
extern volatile float balance_pid_rate_speed_ref_rpm;
extern volatile float balance_pid_balance_speed_ref_rpm;
extern volatile float balance_pid_current_target_a;
extern volatile float balance_pid_current_cmd_a;
extern volatile float balance_pid_current_feedback_a;
extern volatile float balance_pid_turn_speed_ref_rpm;
extern volatile float balance_pid_output_left_a;
extern volatile float balance_pid_output_right_a;
extern volatile float balance_pid_runtime_kp;
extern volatile float balance_pid_runtime_ki;
extern volatile float balance_pid_runtime_kd;
extern volatile float balance_pid_runtime_speed_limit_rpm;
extern volatile float balance_pid_runtime_current_limit_a;
extern volatile float balance_pid_runtime_current_slew_a_per_sec;
extern volatile uint32_t chassis_serial_cmd_rx_count;
extern volatile uint32_t chassis_serial_cmd_ok_count;
extern volatile uint8_t chassis_serial_cmd_last_type;
extern volatile float chassis_serial_cmd_last_value;
extern volatile uint8_t balance_zero_bias_frozen;
extern volatile uint32_t gimbal_yaw_enable_queue_ok_count;
extern volatile uint32_t gimbal_yaw_enable_queue_fail_count;
extern volatile uint32_t gimbal_yaw_current_tx_ok_count;
extern volatile uint32_t gimbal_yaw_current_tx_fail_count;

extern void Gimbal_Task(void *argument);
void WheelSpeed_SetTargetRpm(float rpm);
void Balance_SetManualBiasDeg(float bias_deg);
void Balance_SetPidRuntimeParam(BalancePidRuntimeParam_e param, float value);
void Chassis_SetControlMode(ChassisMode_e mode);
uint8_t Chassis_StartCompetitionTask(uint8_t task_id);
void Chassis_StopCompetitionTask(void);
void Gimbal_SetVisionLaserRequest(uint8_t enabled);
uint8_t Gimbal_SetCalibrationLaserRequest(uint8_t enabled);

#endif /* GIMBAL_TASK_H */
