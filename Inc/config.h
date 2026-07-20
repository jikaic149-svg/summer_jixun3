#ifndef __CONFIG_H__
#define __CONFIG_H__

/*
 * ========================================================
 * 平衡车底盘 QDrive 左右轮参数
 * ========================================================
 *
 * 左轮：can.id = 000
 * 右轮：can.id = 001
 */

#define LEFT_WHEEL_MOTOR_ID     0x00U
#define RIGHT_WHEEL_MOTOR_ID    0x01U

/* QD4310 yaw motor: command 0x402, feedback 0x502, CAN 1 Mbps. */
#define GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED  1U
#define GIMBAL_YAW_MOTOR_ID                 0x02U

#if GIMBAL_YAW_MOTOR_CAN_ID_CONFIGURED
#if (GIMBAL_YAW_MOTOR_ID > 0x0FU)
#error "GIMBAL_YAW_MOTOR_ID must be in the QD4310 range 0x00..0x0F"
#endif
#if ((GIMBAL_YAW_MOTOR_ID == LEFT_WHEEL_MOTOR_ID) || \
     (GIMBAL_YAW_MOTOR_ID == RIGHT_WHEEL_MOTOR_ID))
#error "GIMBAL_YAW_MOTOR_ID must not conflict with a wheel motor ID"
#endif
#endif

#define DRIVE_CAN_BAUD_RATE     1000000U
#define DRIVE_UART_BAUD_RATE    115200U

/*
 * QD4310 无刷电机规格
 */
#define QD4310_RATED_VOLTAGE_MIN_V       7.0f
#define QD4310_RATED_VOLTAGE_MAX_V       26.0f
#define QD4310_RATED_CURRENT_A           1.0f
#define QD4310_PEAK_CURRENT_A            2.0f
#define QD4310_RATED_TORQUE_NM           0.2f
#define QD4310_PEAK_TORQUE_NM            0.3f
#define QD4310_RATED_SPEED_RPM           500.0f
#define QD4310_PEAK_SPEED_RPM            800.0f
#define QD4310_POLE_PAIRS                14U
#define QD4310_ENCODER_BITS              15U
#define QD4310_CONTROL_ACCURACY_DEG      0.0109f
#define QD4310_WEIGHT_KG                 0.127f

#define QD4310_MODE_CURRENT              1U
#define QD4310_MODE_SPEED                1U
#define QD4310_MODE_ANGLE                1U
#define QD4310_MODE_LOW_SPEED_TORQUE     1U
#define QD4310_MODE_ANGLE_STEP           1U

#define QD4310_COMM_CAN                  1U
#define QD4310_COMM_USB                  1U
#define QD4310_COMM_UART                 1U
#define QD4310_CAN_BAUD_RATE             1000000U
#define QD4310_UART_BAUD_RATE_DEFAULT    115200U

/* 旧通用电机规格宏，保留给已有代码兼容。 */
#define MOTOR_RATED_VOLTAGE_MIN          QD4310_RATED_VOLTAGE_MIN_V
#define MOTOR_RATED_VOLTAGE_MAX          QD4310_RATED_VOLTAGE_MAX_V
#define MOTOR_RATED_CURRENT              QD4310_RATED_CURRENT_A
#define MOTOR_PEAK_CURRENT               QD4310_PEAK_CURRENT_A
#define MOTOR_RATED_TORQUE               QD4310_RATED_TORQUE_NM
#define MOTOR_PEAK_TORQUE                QD4310_PEAK_TORQUE_NM
#define MOTOR_RATED_SPEED                QD4310_RATED_SPEED_RPM
#define MOTOR_PEAK_SPEED                 QD4310_PEAK_SPEED_RPM
#define MOTOR_POLE_PAIRS                 QD4310_POLE_PAIRS
#define MOTOR_ENCODER_BITS               QD4310_ENCODER_BITS
#define MOTOR_CONTROL_ACCURACY           QD4310_CONTROL_ACCURACY_DEG
#define MOTOR_WEIGHT_KG                  QD4310_WEIGHT_KG

/* 左轮 QDrive 内部参数 */
#define LEFT_WHEEL_PID_SPEED_KP      0.00300f
#define LEFT_WHEEL_PID_SPEED_KI      0.000078f
#define LEFT_WHEEL_PID_SPEED_KD      0.000f
#define LEFT_WHEEL_PID_ANGLE_KP      1200.0f
#define LEFT_WHEEL_PID_ANGLE_KI      0.000f
#define LEFT_WHEEL_PID_ANGLE_KD      0.000f
#define LEFT_WHEEL_LIMIT_SPEED_RPM   500.0f
#define LEFT_WHEEL_LIMIT_CURRENT_A   1.65f

/* 右轮 QDrive 内部参数 */
#define RIGHT_WHEEL_PID_SPEED_KP     0.00300f
#define RIGHT_WHEEL_PID_SPEED_KI     0.000390f
#define RIGHT_WHEEL_PID_SPEED_KD     0.000f
#define RIGHT_WHEEL_PID_ANGLE_KP     1200.0f
#define RIGHT_WHEEL_PID_ANGLE_KI     0.000f
#define RIGHT_WHEEL_PID_ANGLE_KD     0.000f
#define RIGHT_WHEEL_LIMIT_SPEED_RPM  500.0f
#define RIGHT_WHEEL_LIMIT_CURRENT_A  1.65f

/* 轮端方向归一化：正方向定义为车体前进。
 * 2026-07-09 整车正方向翻转(原正->负):左右轮反馈/电流符号同时取反,
 * 反馈与电流必须成对翻,否则轮速内环变正反馈。 */
#define LEFT_WHEEL_FEEDBACK_SIGN      1.0f
#define RIGHT_WHEEL_FEEDBACK_SIGN     (-1.0f)
#define LEFT_WHEEL_CURRENT_SIGN       1.0f
#define RIGHT_WHEEL_CURRENT_SIGN      (-1.0f)

/* 无刷启动流程：CAN启动后先enable左右轮，任务内发送0A保活并检测反馈。 */
#define WHEEL_FEEDBACK_TIMEOUT_MS     100U
#define WHEEL_ZERO_CURRENT_KEEPALIVE  1
#define WHEEL_SAFE_TASK_PERIOD_MS     1U

/* One-axis QD4310 yaw gimbal. It is energized only by target-aim mode. */
#define GIMBAL_YAW_AUTO_HOLD_ENABLE       0U
#define GIMBAL_YAW_CURRENT_COMMAND_ENABLE 1U
#define GIMBAL_YAW_ENABLE_RETRY_MS        100U
#define GIMBAL_YAW_FEEDBACK_TIMEOUT_MS    100U
#define GIMBAL_YAW_MOTOR_SPEED_KP         0.00300f
#define GIMBAL_YAW_MOTOR_SPEED_KI         0.000390f
#define GIMBAL_YAW_MOTOR_SPEED_KD         0.000f
#define GIMBAL_YAW_MOTOR_ANGLE_KP         1200.0f
#define GIMBAL_YAW_MOTOR_ANGLE_KI         0.000f
#define GIMBAL_YAW_MOTOR_ANGLE_KD         0.000f
#define GIMBAL_YAW_MOTOR_SPEED_LIMIT_RPM  250.0f
#define GIMBAL_YAW_MOTOR_CURRENT_LIMIT_A  1.65f
#define GIMBAL_YAW_FEEDBACK_SIGN          1.0f
#define GIMBAL_YAW_CURRENT_SIGN           1.0f
#define GIMBAL_YAW_CONTROL_SIGN           1.0f
#define GIMBAL_YAW_CONTROL_DT_SEC          0.001f
#define GIMBAL_YAW_SOFT_LIMIT_DEG         120.0f
#define GIMBAL_YAW_MAX_ERROR_DEG          4.0f
#define GIMBAL_YAW_DEADBAND_DEG           0.15f
#define GIMBAL_YAW_INTEGRAL_ZONE_DEG      2.0f
#define GIMBAL_YAW_KP_A_PER_DEG           0.028f
#define GIMBAL_YAW_KI_A_PER_DEG_S         0.0005f
#define GIMBAL_YAW_KD_A_PER_RPM           0.0035f
#define GIMBAL_YAW_INTEGRAL_LIMIT_A       0.002f
#define GIMBAL_YAW_INTEGRAL_MAX_SPEED_RPM 0.5f
#define GIMBAL_YAW_INTEGRAL_MOVING_DECAY  0.995f
#define GIMBAL_YAW_STATIC_FF_A            0.002f
#define GIMBAL_YAW_STATIC_FF_FULL_ERROR_DEG 1.0f
#define GIMBAL_YAW_STATIC_FF_FULL_SPEED_RPM 0.5f
#define GIMBAL_YAW_STATIC_FF_MAX_SPEED_RPM  2.0f
/* Bring-up clamp; raise only after encoder/current polarity is verified. */
#define GIMBAL_YAW_CURRENT_LIMIT_A        0.12f
#define GIMBAL_YAW_CURRENT_SLEW_A_PER_SEC 20.0f
/* Old-sample weight: larger values reject more quantized CAN speed noise. */
#define GIMBAL_YAW_SPEED_LPF_ALPHA        0.80f
#define GIMBAL_YAW_CHASSIS_SPEED_FF_DEG_PER_MPS 1.50f
#define GIMBAL_YAW_CHASSIS_SPEED_FF_LIMIT_DEG   0.20f
#define GIMBAL_YAW_COMMAND_PERIOD_MS      5U
#define GIMBAL_YAW_ZERO_RETRY_COUNT       5U

/* 320x240 MaixCAM2 visual-yaw conversion and freshness gates. */
#define VISION_YAW_SIGN                   (-1.0f)
#define VISION_YAW_FOCAL_LENGTH_PX         270.0f
#define VISION_YAW_DEADBAND_PX             2
#define VISION_YAW_FULL_RESPONSE_PX        5.0f
#define VISION_YAW_FRAME_TIMEOUT_MS        150U
#define VISION_YAW_CURRENT_STOP_TIMEOUT_MS 180U
#define VISION_YAW_MAX_CORRECTION_DEG      4.0f
#define VISION_YAW_CAPTURE_LIMIT_DEG       60.0f
/* Keep STM32->Maix TX high impedance across wheel-current startup transients. */
#define VISION_UART_POWER_GUARD_MS         800U

/* 离地左轮 ESO+PI 轮速测试：上电默认急停，按键授权后才输出非零电流。 */
#define WHEEL_SPEED_TEST_ENABLE             0U
#define WHEEL_SPEED_TEST_REF_RPM            0.0f
#define WHEEL_SPEED_TEST_REF_MIN_ABS_RPM    20.0f
#define WHEEL_SPEED_TEST_REF_MAX_ABS_RPM    200.0f
#define WHEEL_SPEED_TEST_CURRENT_LIMIT_A    0.35f
#define WHEEL_SPEED_TEST_BRAKE_CURRENT_LIMIT_A 0.010f
#define WHEEL_SPEED_TEST_DT_SEC             0.001f
#define WHEEL_SPEED_TEST_UNIDIRECTIONAL     0U
#define WHEEL_SPEED_TEST_AUTO_SEQUENCE_ENABLE 0U
#define WHEEL_SPEED_TEST_AUTO_STEP_MS       1500U

/* jixun2风格轮速PID：u = (Kp*err + Ki*integral - Kd*speed) * polarity。 */
#define WHEEL_SPEED_PID_KP_A_PER_RPM        0.00250f
#define WHEEL_SPEED_PID_KI_A_PER_RPM_S      0.001000f
#define WHEEL_SPEED_PID_KD_A_PER_RPM        0.000f
#define WHEEL_SPEED_PID_ERROR_DEADBAND_RPM  0.0f
#define WHEEL_SPEED_PID_POLARITY            1.0f
#define LEFT_WHEEL_SPEED_PID_POLARITY       WHEEL_SPEED_PID_POLARITY
#define RIGHT_WHEEL_SPEED_PID_POLARITY      WHEEL_SPEED_PID_POLARITY
#define WHEEL_SPEED_PID_INTEGRAL_LIMIT      120.0f
/* 整车变重后正压力增大,破静摩擦电流同步上调约 20% */
#define WHEEL_SPEED_FF_STATIC_A             0.022f
#define WHEEL_SPEED_FF_STATIC_RAMP_RPM      30.0f
#define WHEEL_SPEED_FF_KV_A_PER_RPM         0.00015f
#define WHEEL_SPEED_FF_DISABLE_WHEN_BRAKING 1U
#define LEFT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA 0.20f
#define RIGHT_WHEEL_SPEED_FEEDBACK_LPF_ALPHA 0.20f
#define WHEEL_SPEED_TARGET_RAMP_RPM_PER_SEC 3000.0f
#define WHEEL_SPEED_CURRENT_SLEW_A_PER_SEC  20.0f

/* 三个主调参数：Kp/Ki 产生期望轮速加速度，wo 是 ESO 带宽。 */
#define WHEEL_SPEED_ESO_PI_KP               0.5f
#define WHEEL_SPEED_ESO_PI_KI               0.0f
#define WHEEL_SPEED_ESO_WO                  20.0f

/* 固定估计参数。 */
#define WHEEL_SPEED_ESO_B0_RPM_S_PER_A      800.0f
#define WHEEL_SPEED_ESO_Z2_LIMIT_RPM_S      60.0f
#define WHEEL_SPEED_ESO_KAW                 0.0f

#define KEY_DEBOUNCE_MS                     30U

/* Balance PID standing loop. */
#define BALANCE_PID_ENABLE                  1U
#define CHASSIS_FAULT_NONE                  0U
#define CHASSIS_FAULT_FEEDBACK              1U
#define CHASSIS_FAULT_ENABLE                2U
#define CHASSIS_FAULT_PID_ANGLE             3U
#define CHASSIS_FAULT_PID_RATE              4U
#define BALANCE_PID_DT_SEC                  (0.001f * (float)WHEEL_SAFE_TASK_PERIOD_MS)
/* 加装相机/激光笔后 m·g·h 上升,同角度误差需要更大力矩;电机限 1.65A,余量充足 */
#define BALANCE_PID_CURRENT_LIMIT_A         0.50f
#define BALANCE_PID_CURRENT_SLEW_A_PER_SEC  60.0f
#define BALANCE_PID_SAFE_ANGLE_DEG          30.0f
#define BALANCE_PID_SAFE_RATE_DPS           600.0f
#define BALANCE_PID_POSITION_LIMIT_M        1.00f
#define BALANCE_PID_VELOCITY_LPF_ALPHA      0.15f
#define BALANCE_PID_VELOCITY_LIMIT_MPS      1.5f
#define BALANCE_PID_THETA_REF_DEG           0.0f
#define BALANCE_PID_THETA_REF_LIMIT_DEG     3.2f
#define BALANCE_PID_SPEED_THETA_REF_SLEW_DEG_PER_SEC 20.0f
#define BALANCE_PID_ANGLE_INTEGRAL_LIMIT_DEG_S 4.0f
#define BALANCE_PID_BALANCE_SPEED_LIMIT_RPM 320.0f
#define BALANCE_PID_STAND_SPEED_LIMIT_RPM   220.0f
#define BALANCE_PID_RUNTIME_SPEED_LIMIT_MIN_RPM 120.0f
#define BALANCE_PID_RUNTIME_KP_MAX_RPM_PER_DEG 100.0f
/* Restore the 2026-07-11 hardware-proven upright-loop baseline. */
#define BALANCE_PID_ANGLE_KP_RPM_PER_DEG    100.00f
#define BALANCE_PID_ANGLE_KI_RPM_PER_DEG_S  0.0f
/* 顶部质量升高使摆更慢(自然频率下降),相位裕度变宽,D 小步上调压顶部动量;出现高频细抖先回 0.65 */
#define BALANCE_PID_RATE_KD_RPM_PER_DPS     0.400f
#define BALANCE_POSITION_KP_DEG_PER_M       0.30f
/*
 * 21:12 数据:Kv=5.0 越过稳定边界——stop 后 stand 慢速极限环(±0.1m/s
 * 爬 9s),fwd#3 大激励后速度振荡发散(+0.39<->-0.38)最终 -21deg 向后摔。
 * 且 5.0 的 DC 权限(1.0deg)仍顶不进 1.4deg 摩擦带:巡航靠 Kv 顶静摩擦
 * 这条路"要么不稳要么不动"。回撤到 3.0,破摩擦职责移交步态 launch,
 * Kv 只管滚动段调速与阻尼。
 * 2026-07-09 18:41:翻转后重调曾走 3→12→7→5;停止震动复现 21:12 极限环
 * 特征(BWD 无 bias 冲击也震),按本注释回 3.0。静立稳(16:37)只证明
 * 振幅在静摩擦带内,不证明带外稳定,别再以静立数据为由抬 Kv。
 */
#define BALANCE_POSITION_KV_DEG_PER_MPS     3.00f
#define BALANCE_POSITION_THETA_REF_LIMIT_DEG 1.60f
#define BALANCE_POSITION_KA_DEG_PER_MPS2    0.0f
/*
 * 行走速度积分:已停用(=0)。14:49 数据证明它与轮速前馈死锁:
 * 车不动->积分抬 theta_ref 到 +1.29deg->角度环输出 -17rpm
 * 恰好抵消前馈 +17.5rpm->轮子净指令为 0,静摩擦锁死 13.8s。
 * 前馈架构下摩擦由轮速内环破,不需要倾角积分;平地爬行不要再打开。
 */
#define BALANCE_POSITION_KI_DEG_PER_M_S     0.0f
#define BALANCE_POSITION_INTEGRAL_LIMIT_M_S 0.30f
#define BALANCE_PID_KALMAN_ANGLE_WEIGHT     1.0f
#define BALANCE_PID_ACCEL_ANGLE_WEIGHT      0.0f
#define BALANCE_PID_WHEEL_SPEED_DEADBAND_MPS 0.010f
#define BALANCE_MODE_VELOCITY_RAMP_MPS_PER_SEC 0.15f
#define BALANCE_MODE_DRIVE_SPEED_KP_DEG_PER_MPS 10.50f
#define BALANCE_MODE_DRIVE_SPEED_KI_DEG_PER_M_S 1.20f
#define BALANCE_MODE_DRIVE_SPEED_INTEGRAL_LIMIT_M_S 0.50f
#define BALANCE_MODE_DRIVE_THETA_REF_LIMIT_DEG 1.60f
#define BALANCE_MODE_DRIVE_THETA_REF_SLEW_DEG_PER_SEC 8.0f
#define BALANCE_MODE_DRIVE_INTEGRATOR_RAMP_GATE_MPS 0.002f
#define BALANCE_MODE_DRIVE_INTEGRATOR_LEAD_LIMIT_DEG 0.20f
#define BALANCE_MODE_START_ASSIST_ENABLE     1U
#define BALANCE_MODE_START_ASSIST_MAX_DEG    1.10f
#define BALANCE_MODE_START_ASSIST_SLEW_DEG_PER_SEC 2.0f
#define BALANCE_MODE_START_ASSIST_RELEASE_DEG_PER_SEC 4.0f
#define BALANCE_MODE_START_ASSIST_EXIT_MAX_MPS 0.035f
#define BALANCE_MODE_START_ASSIST_EXIT_SEC   0.05f
#define BALANCE_MODE_START_ASSIST_INTEGRATOR_GATE_DEG 0.02f
#define BALANCE_MODE_START_ASSIST_TIMEOUT_SEC 2.0f
#define BALANCE_MODE_START_RETRY_QUIET_SEC  0.30f
#define BALANCE_MODE_BRAKE_REF_DONE_MPS      0.005f
#define BALANCE_MODE_BRAKE_V_DONE_MPS        0.020f
#define BALANCE_MODE_BRAKE_SETTLE_SEC        0.15f
#define BALANCE_MODE_SETTLE_MAX_THETA_ERROR_DEG 0.40f
#define BALANCE_MODE_SETTLE_MAX_RATE_DPS     12.0f
#define BALANCE_MODE_SETTLE_MAX_WHEEL_RPM     6.0f
#define BALANCE_MODE_TURN_SLEW_RPM_PER_SEC   160.0f
#define BALANCE_MODE_TURN_SETTLE_RPM          0.50f
#define BALANCE_MODE_TURN_SETTLE_DIFF_RPM     6.0f
#define BALANCE_MODE_WHEEL_DIFF_KP           0.10f
#define BALANCE_MODE_WHEEL_DIFF_LIMIT_RPM    2.0f
#define BALANCE_MODE_WHEEL_DIFF_LPF_ALPHA    0.10f
#define BALANCE_MODE_WHEEL_DIFF_DEADBAND_RPM 1.0f
/* Keep disabled until yaw-rate/turn sign is verified on lifted wheels. */
#define BALANCE_MODE_YAW_RATE_DAMP_KP_RPM_PER_DPS 0.0f
#define BALANCE_MODE_YAW_RATE_DAMP_LIMIT_RPM 6.0f
#define BALANCE_MODE_YAW_RATE_DAMP_SIGN      (-1.0f)
/*
 * 前进/后退不再使用固定开环 bias(巡航速度 = (bias-摩擦角-零点误差)/Kv,
 * 摩擦(±1.4deg)和零点漂移都比 bias 本身大,只能"不走或冲")。
 * 改为速度闭环目标 ±BALANCE_MODE_DRIVE_VELOCITY_MPS,倾角由外环自己算。
 */
/*
 * 19:24 数据:平衡点不是一个点而是一条静摩擦带——车身停在 -0.4deg
 * (tref 追上)时 bref 仅 +0.7rpm,重力被静摩擦整个吃掉,任何小于
 * 分离角(~1.4deg)的固定前倾都只会"靠在摩擦上站着"。
 * 走路方案回到 16:14 验证过的组合:LAUNCH 甩出摩擦坑(theta 到
 * +2.37deg、v 到 0.19m/s 有实测),DRIVE 用速度外环 Kv->theta_ref
 * 维持巡航;斜坡在 launch 结束时用实测速度接力(gimbal_task.c),
 * 不再从 0 重爬把车刹回坑里。bias 全部回真实平衡点 -0.8。
 */
#define BALANCE_MODE_STAND_BIAS_DEG        -0.1f
/* Velocity PI owns longitudinal motion; a persistent forward bias defeats
 * overspeed braking. Static-friction breakout is handled only in STARTING. */
#define BALANCE_MODE_FORWARD_BIAS_DEG       BALANCE_MODE_STAND_BIAS_DEG
#define BALANCE_MODE_BACKWARD_BIAS_DEG      BALANCE_MODE_STAND_BIAS_DEG
#define BALANCE_MODE_BIAS_SLEW_DEG_PER_SEC  2.0f
/*
 * 2026-07-09 18:41:停止(drive_enabled=0)拍 bias 原来一拍直接归零,
 * FWD 停止 = theta_ref 1ms 内掉 1.7deg,直立环 Kp=100 即 +170rpm 轮速
 * 冲击。改走 20deg/s 斜坡(与 stop-brake THETA_SLEW 同速,1.7deg 85ms
 * 放完),消停止冲击且不拖慢刹车。
 */
#define BALANCE_MODE_BIAS_STOP_SLEW_DEG_PER_SEC 20.0f
#define BALANCE_MODE_LAUNCH_ENABLE          0U
#define BALANCE_MODE_LAUNCH_TIME_SEC        0.40f
/*
 * 19:44 数据:bias=1.0 时车身干净追到 +0.28(相对平衡点仅 ~1.1deg),
 * 小于分离角 ~1.4deg,轮子全程未破摩擦(v=0)。16:14 能成功是因为
 * 当时角度环过冲到 +2.37deg。现在角度环不过冲,bias 必须自己给够:
 * 2.2deg > 分离角。破摩擦成功(|v| 进平滑窗口)立即提前退出 launch,
 * 防止继续追 +1.4deg 一路加速到守卫线。
 */
#define BALANCE_MODE_LAUNCH_BIAS_DEG        2.2f
#define BALANCE_MODE_LAUNCH_EXIT_MPS        0.15f
/*
 * 步态化走路(21:12 决策):launch 顶出楼板 -> 滚动段 Kv 维持 ->
 * 方向速度低于 RETRIGGER_BELOW 持续 DWELL 秒自动补一发 launch。
 * launch 空转(车身甩过分离角但轮没破摩擦)也由此自动重试。
 */
#define BALANCE_MODE_LAUNCH_RETRIGGER_ENABLE    0U
#define BALANCE_MODE_LAUNCH_RETRIGGER_BELOW_MPS 0.08f
#define BALANCE_MODE_LAUNCH_RETRIGGER_DWELL_SEC 0.50f
/* STARTING keeps the reference-speed base that breaks static friction.
 * RUNNING transfers bounded ownership to measured wheel speed while retaining
 * enough reference base to avoid falling back into low-speed stick-slip. */
#define BALANCE_MODE_DRIVE_USE_ACTUAL_BASE  1U
#define BALANCE_MODE_DRIVE_BASE_BLEND_PER_SEC 2.0f
#define BALANCE_MODE_DRIVE_ACTUAL_BASE_BLEND_MAX 0.65f
#define BALANCE_MODE_DRIVE_ACTUAL_BASE_LPF_TAU_SEC 0.10f
#define BALANCE_MODE_DRIVE_THETA_REF_ENABLE 1U
#define BALANCE_MODE_STOP_BRAKE_ENABLE      1U
#define BALANCE_MODE_STOP_BRAKE_TIME_SEC    0.80f
/*
 * 20:02 数据:轮级反向刹车(GAIN 2.0 + 直立环 0.25 折)一次都没生效——
 * 旧激活逻辑被速度噪声(±0.03)一拍抖穿即永久清 timer。修复窗口逻辑
 * 后改走姿态域刹车:GAIN 归零(轮级反转会把车身推得更前倾)、直立环
 * 权限恢复满额,刹车改为窗口内叠加 -KV_BRAKE*v 的后仰目标(限幅 1deg)。
 */
#define BALANCE_MODE_STOP_BRAKE_GAIN        0.0f
#define BALANCE_MODE_STOP_BRAKE_LIMIT_RPM   160.0f
#define BALANCE_MODE_STOP_BRAKE_BALANCE_SCALE 1.0f
#define BALANCE_MODE_STOP_BRAKE_KV_DEG_PER_MPS 6.0f
#define BALANCE_MODE_STOP_BRAKE_TREF_LIMIT_DEG 0.9f
#define BALANCE_MODE_STOP_BRAKE_STAND_TREF_LIMIT_DEG 1.9f
#define BALANCE_MODE_STOP_BRAKE_THETA_SLEW_DEG_PER_SEC 20.0f
#define BALANCE_MODE_STOP_BRAKE_STAND_SPEED_LIMIT_RPM 280.0f
#define BALANCE_MODE_STOP_BRAKE_CURRENT_LIMIT_A 0.50f
#define BALANCE_MODE_STOP_BRAKE_CURRENT_SLEW_A_PER_SEC 60.0f
#define BALANCE_MODE_STOP_BRAKE_EDGE_MIN_MPS 0.020f
#define BALANCE_MODE_STOP_ZERO_CROSS_MAX_MPS 0.025f
#define BALANCE_MODE_STOP_RELEASE_V_MPS      0.010f
#define BALANCE_MODE_STOP_RELEASE_CONFIRM_SEC 0.005f
#define BALANCE_MODE_STOP_REBOUND_V_MPS      0.025f
#define BALANCE_MODE_STOP_REBOUND_CONFIRM_SEC 0.050f
#define BALANCE_MODE_STOP_BRAKE_TRACK_ERR_DEG 1.2f
#define BALANCE_MODE_BRAKE_RECOVER_ANGLE_DEG 6.0f
/*
 * pulse 蠕动关闭:brake_on=0.10m/s 会把车永远压在粘滑楼板(平滑
 * 滚动窗口 0.15~0.35m/s)以下,和 15:23 的摩擦结论自相矛盾。
 */
#define BALANCE_MODE_PULSE_DRIVE_ENABLE     0U
#define BALANCE_MODE_PULSE_DRIVE_TIME_SEC   1.20f
#define BALANCE_MODE_PULSE_BRAKE_TIME_SEC   0.70f
#define BALANCE_MODE_PULSE_BRAKE_MIN_TIME_SEC 0.20f
#define BALANCE_MODE_PULSE_HOLD_TIME_SEC    0.15f
#define BALANCE_MODE_PULSE_BRAKE_ON_MPS     0.10f
#define BALANCE_MODE_PULSE_RESUME_MPS       0.03f
#define BALANCE_MODE_PULSE_DRIVE_BASE_RPM   0.0f
/*
 * 行走速度必须在"平滑滚动窗口"内,不能贴着摩擦楼板爬:
 * 数据实测粘滑破摩擦自然窜速 0.25m/s,不可回收区 >0.4~0.5m/s(守卫封顶)。
 * 0.06m/s(18rpm) 在楼板以下,表现只能是"不动或窜";
 * 0.20m/s(62rpm) 在窗口中间,且是轮速内环验证过的舒适区。
 */
#define BALANCE_MODE_DRIVE_VELOCITY_MPS     0.12f
/*
 * 闭环限速守卫:|v| 超过 ON 阈值时把速度目标强制为 0(撤销前进指令,
 * 全部外环权限用于刹车),并冻结速度积分;低于 OFF 阈值恢复。带迟滞防抖。
 * 依据:v>0.5m/s 后 theta_ref ±3deg 限幅使最大减速度仅 0.51m/s^2,
 * 且轮速上限 320rpm≈1.04m/s 吃不起非最小相位刹车瞬态,属不可回收区。
 * 只修改外环速度目标,不触碰直立角度环任何量。
 */
/* 变重变高后同速度动能/回收难度更大,守卫提前;07-07 曾两次 0.383 擦线未触发 */
#define BALANCE_SPEED_GUARD_ON_MPS          0.35f
#define BALANCE_SPEED_GUARD_OFF_MPS         0.15f
/*
 * 零点自学习:静止直立时车实际站住的角度就是物理零点(定义如此),
 * 用 50s 时间常数的慢积分在线吸收加速度计温漂/机械变动,
 * 取消"每个会话手动重调 offset/B" 的环节。
 * 门控:仅 stand 模式、已在闭环、小角度、小角速度、车与轮都静止时学习;
 * 被推/行走/摔倒全部冻结。与直立环时间尺度差 ~50 倍,动力学解耦。
 * 学到的 trim 只在 RAM,掉电回到 BALANCE_ANGLE_OFFSET_DEG 基准。
 */
/*
 * 2026-07-09 硬件变更:车后方加循迹模块(低位悬臂)、云台上方加
 * MaixCAM 2 相机+激光笔。重心前后偏移量未知,可能超出旧 ±2deg
 * 权限;放宽限幅到 ±4deg 并把学习速率提到 0.06(时间常数 ~17s),
 * 首次上电静立 ~1min 内收敛到新零点。收敛后读 I13 zero_trim,
 * 把稳定值并入 BALANCE_ANGLE_OFFSET_DEG 再考虑收回限幅/速率。
 */
/*
 * 2026-07-09 16:05 数据:旧门控(v<0.03/轮<5rpm/rate<10dps)开门占空比仅
 * 18.8%——爬行(v≈0.07,轮≈21rpm)时 v/轮门关死,形成"不停住就不学零点,
 * 零点不学就停不住"死锁;静立 12Hz 微抖(rate p80=15.4dps)又被 rate 门
 * 再砍 47%。仿真按旧门积分 -0.226 与实测 trim -0.197 吻合。放宽三个门
 * (占空比 63.8%)让 trim 在慢速爬行中也能学;θ 门与速率 0.06 不动,
 * τ≈17s(门内时间)仍与直立环(~1s)解耦。
 */
#define BALANCE_ZERO_TRIM_ENABLE            1U
#define BALANCE_ZERO_TRIM_RATE_PER_SEC      0.06f
#define BALANCE_ZERO_TRIM_LIMIT_DEG         4.0f
#define BALANCE_ZERO_TRIM_GATE_V_MPS        0.02f
#define BALANCE_ZERO_TRIM_GATE_RATE_DPS     20.0f
#define BALANCE_ZERO_TRIM_GATE_THETA_DEG    1.0f
#define BALANCE_ZERO_TRIM_GATE_WHEEL_RPM    6.0f
/*
 * 守卫期间积分向 0 衰减(不是冻结):冻结会让起步攒满的 +1.5deg 积分
 * 在超速时继续同向助推(v=0.4 时速度项 -1.0deg 压不过它,净 +0.5deg)。
 * 0.60 m/s 速率 = 0.5s 内清完 0.30m 积分限幅,之后刹车全额生效;
 * 衰减到 0 即停,不会积成反向,windup 保护仍然成立。
 */
#define BALANCE_SPEED_GUARD_INTEGRAL_DECAY_M_PER_S 0.60f
#define BALANCE_MODE_TURN_SPEED_RPM         16.0f
#define TRACKING_UART_ENABLE                1U
#define TRACKING_DATA_TIMEOUT_MS            120U
#define TRACKING_START_SEARCH_VELOCITY_MPS  0.10f
#define TRACKING_BASE_VELOCITY_MPS          0.11f
#define TRACKING_LOST_HOLD_MS               1000U
#define TRACKING_LOST_HOLD_VELOCITY_MPS     0.10f
#define TRACKING_LOST_REFIND_MS             500U
#define TRACKING_LOST_REFIND_VELOCITY_MPS   0.09f
/* The course contains intentional white gaps. Keep advancing while searching
 * in the last known turn direction instead of stopping inside a gap. */
#define TRACKING_LOST_VELOCITY_MPS          0.08f
#define TRACKING_FORWARD_BIAS_DEG           0.0f
#define TRACKING_TURN_KP_RPM_PER_ERROR      14.0f
#define TRACKING_TURN_KI_RPM_PER_ERROR_S    0.0f
#define TRACKING_TURN_KD_RPM_PER_ERROR_RATE 0.0f
#define TRACKING_TURN_LIMIT_RPM             40.0f
#define TRACKING_TURN_SLEW_RPM_PER_SEC      260.0f
#define TRACKING_LOST_HOLD_TURN_RPM         6.0f
#define TRACKING_LOST_REFIND_TURN_RPM       8.0f
#define TRACKING_LOST_SEARCH_TURN_RPM       4.0f
#define TRACKING_LOST_DEFAULT_TURN_DIR      1.0f
#define TRACKING_TURN_MEMORY_MIN_RPM        2.0f
#define TRACKING_ERROR_LPF_ALPHA            0.40f
#define TRACKING_INTEGRAL_ZONE_ERROR        1.5f
#define TRACKING_INTEGRAL_LIMIT_ERROR_S     2.0f
#define TRACKING_INTEGRAL_DECAY             0.80f
#define TRACKING_CURVE_SLOWDOWN_ERROR       2.0f
#define TRACKING_SHARP_TURN_ENTER_ERROR     3.0f
#define TRACKING_SHARP_TURN_EXIT_ERROR      1.2f
#define TRACKING_SHARP_TURN_GATE_RPM        20.0f
#define TRACKING_SHARP_TURN_VELOCITY_MPS    0.10f
/* Course order: A start crossbar -> center target marker -> B finish crossbar. */
#define TRACKING_BLOCK_BLACK_COUNT_MIN      5U
#define TRACKING_TARGET_BLOCK_BLACK_COUNT_MIN 6U
#define TRACKING_MARKER_MIN_FORWARD_MPS      0.020f
#define TRACKING_MARKER_REVERSE_REJECT_MPS   0.015f
#define TRACKING_START_BLOCK_CONFIRM_MS     40U
#define TRACKING_START_BLOCK_EXIT_MS        300U
#define TRACKING_TARGET_BLOCK_CONFIRM_MS    150U
#define TRACKING_TARGET_BLOCK_RUNOUT_MS     500U
#define TRACKING_TARGET_BLOCK_RUNOUT_VELOCITY_MPS 0.04f
#define TRACKING_TARGET_BLOCK_LASER_MS      3000U
#define TRACKING_TARGET_BLOCK_DEPART_MS     1200U
#define TRACKING_TARGET_DEPART_VELOCITY_MPS 0.13f
#define TRACKING_TARGET_DEPART_ASSIST_DEG    2.20f
#define TRACKING_TARGET_DEPART_ASSIST_SLEW_DEG_PER_SEC 4.0f
#define TRACKING_TARGET_BLOCK_EXIT_MS       300U
#define TRACKING_FINISH_BLOCK_CONFIRM_MS    40U
#define TRACKING_FINISH_BLOCK_RUNOUT_MS     500U
#define TRACKING_FINISH_BLOCK_RUNOUT_VELOCITY_MPS 0.04f
#define TRACKING_REFIND_CONFIRM_MS          100U
#define TRACKING_LASER_STOP_MAX_V_MPS       0.015f
#define TRACKING_LASER_STOP_MAX_WHEEL_RPM   6.0f
#define TRACKING_LASER_MAX_THETA_ERROR_DEG  0.30f
#define TRACKING_LASER_MAX_RATE_DPS         10.0f
#define TRACKING_LASER_MAX_TURN_RPM         0.50f
#define TRACKING_LOST_ERROR                 7.5f
#define TRACKING_MIN_SPEED_RATIO            0.875f

/* Touch-started target task: calibrate, follow a straight line, then stop on
 * a continuously observed all-white sensor frame. */
#define TARGET_AIM_LINE_CONFIRM_MS          60U
#define TARGET_AIM_WHITE_CONFIRM_MS         40U
#define TARGET_AIM_STOP_ABORT_CONFIRM_MS    100U
#define TARGET_AIM_LINE_BLACK_COUNT_MAX     5U
#define TARGET_AIM_FINISH_ENTRY_MAX_ERROR    1.5f
#define TARGET_AIM_TURN_LIMIT_RPM          16.0f
#define TARGET_AIM_WIDE_START_PROBE_VELOCITY_MPS 0.10f
#define TARGET_AIM_WIDE_START_PROBE_MAX_MS  1000U
#define TARGET_AIM_WIDE_FOLLOW_PROBE_MAX_MS 1000U

/* Independent one-lap square line task (mode 6). Course tracking mode 5 keeps
 * its own marker/laser state machine and does not consume these parameters. */
#define SQUARE_TRACKING_BASE_VELOCITY_MPS       0.12f
#define SQUARE_TRACKING_SHARP_VELOCITY_MPS      0.10f
#define SQUARE_TRACKING_FORWARD_BIAS_DEG         0.0f
#define SQUARE_TRACKING_CURVE_SLOWDOWN_ERROR    2.0f
#define SQUARE_TRACKING_TURN_KP_RPM_PER_ERROR   10.0f
#define SQUARE_TRACKING_TURN_LIMIT_RPM          16.0f
#define SQUARE_TRACKING_TURN_DEADBAND_RPM        2.0f
#define SQUARE_TRACKING_ERROR_LPF_ALPHA         0.65f
#define SQUARE_TRACKING_TURN_DIRECTION          (-1.0f)
#define SQUARE_TRACKING_REFIND_CONFIRM_MS       100U
#define SQUARE_TRACKING_MAX_NEW_FRAME_GAP_MS    TRACKING_DATA_TIMEOUT_MS
/* A corner is a continuous black block confined to either sensor half. The
 * one-channel opposite-side limit rejects normal 0x7c/0xfc line patterns. */
#define SQUARE_TRACKING_CORNER_SIDE_BLACK_MIN   3U
#define SQUARE_TRACKING_CORNER_OTHER_BLACK_MAX  1U
#define SQUARE_TRACKING_CORNER_REVERSE_REJECT_MPS 0.015f
#define SQUARE_TRACKING_CORNER_CONFIRM_MS       40U
#define SQUARE_TRACKING_CORNER_REARM_MS         300U
#define SQUARE_TRACKING_CORNER_RUNOUT_VELOCITY_MPS 0.06f
#define SQUARE_TRACKING_CORNER_RUNOUT_MS         250U
#define SQUARE_TRACKING_CORNER_BRAKE_TIMEOUT_MS 3000U
#define SQUARE_TRACKING_CORNER_TURN_MIN_MS      800U
#define SQUARE_TRACKING_CORNER_TURN_MAX_MS      4000U
#define SQUARE_TRACKING_CORNER_YAW_REACQUIRE_DEG 85.0f
#define SQUARE_TRACKING_CORNER_YAW_FULL_TURN_DEG 88.0f
#define SQUARE_TRACKING_CORNER_SEARCH_RPM        6.0f
#define SQUARE_TRACKING_CORNER_ALIGN_MAX_MS     3000U
#define SQUARE_TRACKING_CORNER_ALIGN_VELOCITY_MPS 0.07f
#define SQUARE_TRACKING_CORNER_ALIGN_MIN_FORWARD_MPS 0.020f
#define SQUARE_TRACKING_CORNER_ALIGN_ERROR      1.2f
#define SQUARE_TRACKING_CORNER_ALIGN_CONFIRM_MS 100U
#define SQUARE_TRACKING_TARGET_CORNER_COUNT     4U
#define SQUARE_TRACKING_LOST_HOLD_MS            200U
#define SQUARE_TRACKING_LOST_HOLD_VELOCITY_MPS  0.06f
#define SQUARE_TRACKING_LOST_HOLD_TURN_LIMIT_RPM 6.0f
#define SQUARE_TRACKING_LOST_BRAKE_TIMEOUT_MS   3000U
#define SQUARE_TRACKING_LOST_SEARCH_TIMEOUT_MS  2500U
#define BALANCE_PID_POLARITY                1.0f
#define BALANCE_PID_POSITION_SIGN           1.0f
#define BALANCE_PID_VELOCITY_SIGN           1.0f
/* 2026-07-09 整车正方向翻转:轮子电流/反馈符号取反后,为保持
 * turn>0 对应的物理旋转方向不变(yaw 陀螺约定未动,左转语义不随
 * 车头翻转而变),左右差速符号必须跟着轮极性一起取反。 */
#define BALANCE_PID_LEFT_YAW_SIGN           1.0f
#define BALANCE_PID_RIGHT_YAW_SIGN          (-1.0f)
#define BALANCE_MANUAL_BIAS_LIMIT_DEG       2.0f
#define BALANCE_WHEEL_RADIUS_M              0.031f
#define BALANCE_RPM_TO_MPS                  \
    (BALANCE_WHEEL_RADIUS_M * 6.283185307179586f / 60.0f)

/*
 * ========================================================
 * 平衡车 IMU 与 Kalman 姿态配置
 * ========================================================
 *
 * 当前 BMI088 为水平安装。
 * 默认假设：传感器 X 轴朝车体前方，Z 轴垂直地面。
 * 前后平衡角使用 Pitch 加速度角，对应 Y 轴陀螺仪角速度。
 * yaw 只作为航向/差速外环参考，不进入直立平衡内环。
 */

#define BALANCE_IMU_SAMPLE_HZ        1000.0f
#define BALANCE_IMU_DT_SEC           (1.0f / BALANCE_IMU_SAMPLE_HZ)

#define BALANCE_AXIS_X               0
#define BALANCE_AXIS_Y               1
#define BALANCE_AXIS_Z               2

#define BALANCE_ACCEL_ANGLE_ROLL     0
#define BALANCE_ACCEL_ANGLE_PITCH    1

/* 当前板子安装方向：前后平衡角落在 Roll / X gyro 上。
 * 2026-07-09 整车正方向翻转:加速度角符号取反(原 -1 -> +1)。 */
#define BALANCE_ACCEL_ANGLE_MODE     BALANCE_ACCEL_ANGLE_ROLL
#define BALANCE_ACCEL_ANGLE_SIGN     1.0f
#define BALANCE_GYRO_AXIS            BALANCE_AXIS_X
/* 实测（电机断开、手动晃车段）d(accel_angle)/dt = -1.08 × gyro_rate：
 * 加速度角与陀螺仪对同一倾倒方向的符号相反,陀螺符号须与加速度角
 * 符号保持"相反"关系才能两源一致。
 * 2026-07-09 整车正方向翻转:加速度角与陀螺符号同步取反(-1 -> +1),
 * 两源相对关系不变。 */
#define BALANCE_GYRO_SIGN            1.0f

/* yaw 只用于底盘航向估计，现场如果方向反了只改符号。 */
#define CHASSIS_YAW_GYRO_AXIS        BALANCE_AXIS_Z
#define CHASSIS_YAW_GYRO_SIGN        1.0f
#define CHASSIS_YAW_RATE_BIAS_DPS    0.136f
#define CHASSIS_YAW_RATE_DEADBAND_DPS 0.05f
#define IMU_YAW_BIAS_KEY_CAL_ENABLE  0U
#define IMU_YAW_BIAS_KEY_CAL_SAMPLES 500U
#define IMU_GYRO_BIAS_CAL_ENABLE     0U
#define IMU_GYRO_BIAS_CAL_SAMPLES    500U
/* Yaw rate scalar Kalman: bigger Q or smaller R = faster response, less smoothing. */
#define CHASSIS_YAW_RATE_KALMAN_Q    0.10f
#define CHASSIS_YAW_RATE_KALMAN_R    0.50f

/* 机械安装固定偏置；开机零点会在此基础上再归零。
 * 注意:该零点存在 ±1~1.5deg 的会话间漂移(疑似加速度计温漂/机械变动):
 *   上午数据(offset=-2.5 时代)零点在显示 +2.0deg -> 改 -0.5f;
 *   下午现场(offset=-0.5 时代)需要 B-1.5 才立得住 -> 改 -2.0f。
 * 若下次又漂,不要再重烧固件追常数:开机后用 B 命令做会话级 trim,
 * 或实现 stand 模式下的慢速零点自学习。
 * 2026-07-09 整车正方向翻转:θ = accel_raw×SIGN − OFFSET,SIGN 取反后
 * OFFSET 必须同步取反(-1.2 -> +1.2),物理平衡点才落回 θ≈0。
 * 2026-07-09 16:05 数据:stand 匀速爬行 +0.072m/s、爬行段 θ 均值 -0.69,
 * 即零点还差 ~0.9deg(已学 trim -0.197 + 残差 -0.69)。按既定流程把
 * 稳定值并回:1.2 - 0.197 - 0.69 ≈ 0.35,残差交给放宽门控后的 trim。 */
#define BALANCE_ANGLE_OFFSET_DEG     (0.35f)
/* Balance car must not treat power-on pose as zero. Use fixed mechanical offset only. */
#define BALANCE_BOOT_ZERO_ENABLE     0

/* 主次关系：陀螺仪积分为主体，加速度角只做慢速零漂修正。
 * 旧值 Q=0.012/Qb=0.003/R=0.5 的加速度修正时间常数约 0.18s，
 * 直立摆动在 ~1Hz，轮子加速度污染（实测 ~14deg，与真实角同量级）
 * 约 2/3 穿透进控制角，且 Qb 偏大使污染同时写坏陀螺零漂估计。
 * 新值时间常数约 1.2s，1Hz 污染穿透降到 ~13%；陀螺零漂本身变化
 * 很慢，慢修正不损失静态精度。若动态下 theta 仍被拽，继续加大 R。 */
#define BALANCE_KALMAN_Q_ANGLE       0.001f
#define BALANCE_KALMAN_Q_BIAS        0.0001f
#define BALANCE_KALMAN_R_MEASURE     5.000f

/* 加速度模长门控：|a| 偏离 1g 时跳过加速度修正，只做陀螺积分，
 * 专门拦截轮子加减速这类瞬态污染。 */
#define BALANCE_ACCEL_GATE_ENABLE    1
#define BALANCE_ACCEL_GRAVITY_MS2    9.80665f
#define BALANCE_ACCEL_NORM_MIN_G     0.75f
#define BALANCE_ACCEL_NORM_MAX_G     1.25f
#define BALANCE_ACCEL_ANGLE_GATE_DEG 10.0f
#define BALANCE_ACCEL_ANGLE_LPF_ALPHA 0.05f

/* DRDY/SPI DMA未触发时允许轮询降级，先保证VOFA+有数据用于排查链路。 */
#define IMU_POLL_FALLBACK_ENABLE     1
#define IMU_POLL_FALLBACK_DIVIDER    1U
#define IMU_POLL_FALLBACK_DT_SEC     0.010f

/* VOFA CSV output mode. 控制/采样高频跑，VOFA只做降采样可视化。 */
#define VOFA_OUTPUT_MODE_ATTITUDE    0
#define VOFA_OUTPUT_MODE_ENCODER     1
#define VOFA_OUTPUT_MODE_CAN_DIAG    2
#define VOFA_OUTPUT_MODE             VOFA_OUTPUT_MODE_ENCODER

/* ENCODER模式通道顺序：
 * I0 左轮目标rpm
 * I1 左轮速度环实际速度rpm
 * I2 左轮命令电流A
 * I3 右轮目标rpm
 * I4 右轮速度环实际速度rpm
 * I5 右轮命令电流A
 *
 * CAN_DIAG模式通道顺序：
 * I0 左轮enabled
 * I1 右轮enabled
 * I2 左轮反馈计数
 * I3 右轮反馈计数
 * I4 最后CAN StdId
 * I5 CAN接收总计数
 *
 * YAW_CAN诊断通道顺序：
 * I0 enable成功进入邮箱次数, I1 enable邮箱失败次数
 * I2 yaw反馈计数, I3 yaw实例反馈计数, I4 yaw enabled
 * I5 最后CAN StdId, I6 最后CAN DLC, I7 CAN接收总计数
 * I8 yaw角度deg, I9 yaw速度rpm, I10 yaw反馈电流A, I11 yaw反馈新鲜
 */
/* BALANCE_CASCADE drive-diagnosis map:
 * I0 theta_deg, I1 theta_ref_deg, I2 theta_rate_dps, I3 speed_theta_ref_deg
 * I4 velocity_ref_mps, I5 velocity_mps, I6 velocity_err_mps
 * I7 balance_ref_rpm, I8 angle_p_ref_rpm, I9 rate_d_ref_rpm
 * I10 avg_ref_rpm, I11 avg_actual_rpm, I12 avg_cmd_a
 * I13 zero_trim_deg, I14 fault, I15 chassis_mode
 */
/* TRACK_ADC fused-control map (raw x1..x8 ADC values are intentionally
 * omitted):
 * I0 track_error, I1 chassis_mode, I2 motion_state
 * I3 command_velocity_mps, I4 target_velocity_mps, I5 actual_velocity_mps
 * I6/I7 left target/actual rpm, I8/I9 right target/actual rpm
 * I10/I11 target/actual theta deg, I12 fused black mask
 * I13 square phase, I14 completed square corner count
 * I15 stop-brake state bitfield
 */
/* STOP_CONTROL map:
 * I0 chassis mode, I1 target phase, I2 motion state
 * I3 command velocity, I4 internal ramp velocity, I5 actual velocity
 * I6 stop-position reference, I7 actual position
 * I8/I9 left target/actual rpm, I10/I11 right target/actual rpm
 * I12/I13 target/actual theta, I14 stop-brake state, I15 turn reference
 * stop-brake bits: b0 damping, b1 positive velocity, b2 negative velocity,
 * b3 position hold, b4 release confirmation, b5 rebound confirmation,
 * b6 high-authority window.
 */
/* TARGET_AIM map keeps I0..I23 backward compatible and appends diagnostics:
 * I24 route phase, I25 task id, I26 task phase, I27 chassis fault,
 * I28 emergency stop, I29 motor feedback OK, I30 motor enabled OK.
 */
#define VOFA_OUTPUT_ENABLE           1
#define VOFA_OUTPUT_DIVIDER          5U
#define VOFA_OUTPUT_PERIOD_MS        100U
#define VOFA_DEBUG_STAGE_MOTOR_FEEDBACK 0U
#define VOFA_DEBUG_STAGE_IMU_ATTITUDE   1U
#define VOFA_DEBUG_STAGE_SPEED_LOOP     2U
#define VOFA_DEBUG_STAGE_BALANCE_CASCADE 3U
#define VOFA_DEBUG_STAGE_TRACK_ADC      4U
#define VOFA_DEBUG_STAGE_YAW_CAN        5U
#define VOFA_DEBUG_STAGE_TARGET_AIM     6U
#define VOFA_DEBUG_STAGE_STOP_CONTROL   7U
#define VOFA_DEBUG_STAGE                VOFA_DEBUG_STAGE_TARGET_AIM
#if VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_ENCODER
#if VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_MOTOR_FEEDBACK
#define VOFA_OUTPUT_CHANNEL_COUNT    12U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_SPEED_LOOP
#define VOFA_OUTPUT_CHANNEL_COUNT    6U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_IMU_ATTITUDE
#define VOFA_OUTPUT_CHANNEL_COUNT    8U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_TRACK_ADC
#define VOFA_OUTPUT_CHANNEL_COUNT    16U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_YAW_CAN
#define VOFA_OUTPUT_CHANNEL_COUNT    12U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_TARGET_AIM
#define VOFA_OUTPUT_CHANNEL_COUNT    31U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_STOP_CONTROL
#define VOFA_OUTPUT_CHANNEL_COUNT    16U
#elif VOFA_DEBUG_STAGE == VOFA_DEBUG_STAGE_BALANCE_CASCADE
#define VOFA_OUTPUT_CHANNEL_COUNT    16U
#else
#define VOFA_OUTPUT_CHANNEL_COUNT    12U
#endif
#elif VOFA_OUTPUT_MODE == VOFA_OUTPUT_MODE_CAN_DIAG
#define VOFA_OUTPUT_CHANNEL_COUNT    6U
#else
#define VOFA_OUTPUT_CHANNEL_COUNT    2U
#endif
#define VOFA_UART_PORT_USART1        1
#define VOFA_UART_PORT_USART6        6
#define VOFA_UART_PORT               VOFA_UART_PORT_USART6

/* 旧通用宏只用于兼容现有代码，新底盘逻辑应使用 LEFT_/RIGHT_ 前缀。 */
#define MOTOR_PID_SPEED_KP       RIGHT_WHEEL_PID_SPEED_KP
#define MOTOR_PID_SPEED_KI       RIGHT_WHEEL_PID_SPEED_KI
#define MOTOR_PID_SPEED_KD       RIGHT_WHEEL_PID_SPEED_KD

#define MOTOR_PID_ANGLE_KP       RIGHT_WHEEL_PID_ANGLE_KP
#define MOTOR_PID_ANGLE_KI       RIGHT_WHEEL_PID_ANGLE_KI
#define MOTOR_PID_ANGLE_KD       RIGHT_WHEEL_PID_ANGLE_KD

#define MOTOR_LIMIT_SPEED        RIGHT_WHEEL_LIMIT_SPEED_RPM
#define MOTOR_LIMIT_CURRENT      RIGHT_WHEEL_LIMIT_CURRENT_A

/*
 * --------------------------------------------------------
 * 旧云台兼容参数
 * --------------------------------------------------------
 * 当前主任务是平衡车底盘，这一段不作为底盘控制配置。
 * 现有工程仍有旧 gimbal_task 引用这些宏，先保留以避免编译断裂。
 */
#define MOTOR_PITCH_MECH_ZERO    0.0f     // Pitch 轴机械零点 (单位: 度)
#define MOTOR_YAW_MECH_ZERO      0.0f     // Yaw 轴机械零点 (单位: 度)

/*
 * --------------------------------------------------------
 * 云台外环 (1000Hz) PID 增稳控制参数 (极度保守)
 * --------------------------------------------------------
 */
// 极性宏：极其重要！如果发生正反馈震荡（一碰就加速甩头），请将其改为 -1.0f
#define PITCH_POLARITY 1.0f
#define YAW_POLARITY   -1.0f

// 针对云台惯量调整：加大 YAW 力度，减小 PITCH 力度
// 注意：一旦方向正确，系统仍然会画大圆发散的原因是缺少阻尼（D环）！
// 现在重新引入并增强 D 环，将其稳死。
#define GIMBAL_PID_PITCH_KP 0.025f
#define GIMBAL_PID_PITCH_KI 0.0001f
#define GIMBAL_PID_PITCH_KD 0.0020f
#define GIMBAL_PID_YAW_KP   0.050f
#define GIMBAL_PID_YAW_KI   0.0002f
#define GIMBAL_PID_YAW_KD   0.0010f

// 编码器硬锁模式 PID (只用于上电初期的极静校准)
#define ENC_LOCK_KP 0.080f
#define ENC_LOCK_KD 0.002f

// 控制死区 (度)，在此误差范围内视为已到达目标，不进行 P 环修正，消除微抖动
#define GIMBAL_PITCH_DEADBAND 0.5f
#define GIMBAL_YAW_DEADBAND   0.2f

// === 核心保护：误差限幅 (极度重要，防画龙防疯转) ===
// 无论外面怎么扰动，系统认为的“最大误差”不超过此值。防止放手瞬间积累极大动能。
#define GIMBAL_MAX_ERR_PITCH  20.0f
#define GIMBAL_MAX_ERR_YAW    20.0f

// 积分项限幅 (防积分饱和)
#define GIMBAL_MAX_I_PITCH    0.3f  // 积分最大只允许提供 0.3A 电流，只用于消除微小稳态误差
#define GIMBAL_MAX_I_YAW      0.3f

// 安全限幅保护 (最大输出指令电流 A，用手能轻松捏住，防烧毁防击穿)
#define GIMBAL_MAX_OUTPUT_CURRENT 1.0f

#endif /* __CONFIG_H__ */
