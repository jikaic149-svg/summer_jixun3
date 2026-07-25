#ifndef LCD_DEMO_CONFIG_H
#define LCD_DEMO_CONFIG_H

#include <stdint.h>

/*
 * Ballistic calibration -- keep at the top for quick field adjustment.
 * A 0-degree horizontal shot starts CANNON_MUZZLE_HEIGHT_CM above ground
 * and reaches ground at CANNON_LEVEL_RANGE_CM.
 */
#define CANNON_MUZZLE_HEIGHT_CM           12.0f
#define CANNON_LEVEL_RANGE_CM             67.5f
#define CANNON_LOW_ARC_MAX_DEG            5.0f
#define CANNON_FIXED_TARGET_HEIGHT_CM     36.5f
#define CANNON_BOX_FRONT_TO_TARGET_CENTER_MM 250U
/*
 * High-arc field calibration: with DIST=100 cm, the projectile reached the
 * position whose box front was 37 cm away.  Both positions are converted to
 * target-center coordinates, so the measured center distance is 37+25 cm.
 */
#define CANNON_HIGH_ARC_REFERENCE_CENTER_CM 100.0f
#define CANNON_HIGH_ARC_MEASURED_FRONT_CM   37.0f
#define CANNON_HIGH_ARC_RANGE_MARGIN         1.013f

#define LCD_DEMO_WIDTH                    320U
#define LCD_DEMO_HEIGHT                   240U
#define LCD_DEMO_MADCTL_LANDSCAPE         0xA8U

#define BOARD_HSE_HZ                      25000000U
#define BOARD_HSI_HZ                      16000000U
#define BOARD_CORE_CLOCK_HZ               168000000U
#define BOARD_APB1_CLOCK_HZ               42000000U
#define BOARD_APB2_CLOCK_HZ               84000000U
#define BOARD_CLOCK_TIMEOUT_MS            100U

#define DAP_VCOM_UART_BAUD_RATE            115200U
#define ESP8266_UART_BAUD_RATE             115200U
#define ESP8266_BOOT_WAIT_MS               3000U
#define ESP8266_AT_TIMEOUT_MS              2000U
#define ESP8266_TCP_CONNECT_TIMEOUT_MS     8000U
#define ESP8266_RECONNECT_INTERVAL_MS      3000U
#define ESP8266_TCP_SERVER_IPV4            "192.168.215.66"
#define ESP8266_TCP_SERVER_PORT            9000
#define GIMBAL_TELEMETRY_PERIOD_MS         100U
#define GIMBAL_VISION_TIMEOUT_MS           500U

/* Play item 1: single-pass target lock and auto fire. */
#define AUTO_AIM_SINGLE_PASS_LOCK          1U
#define AUTO_AIM_PIXEL_ERROR_YAW_POLARITY -1.0f
#define AUTO_AIM_TIMEOUT_MS                120000U
#define AUTO_AIM_SEARCH_LIMIT_DEG          50.0f
#define AUTO_AIM_SEARCH_REACHED_DEG        1.0f
#define AUTO_AIM_CALIBRATION_OFFSET_DEG    2.0f
#define AUTO_AIM_CALIBRATION_REACHED_DEG   0.6f
#define AUTO_AIM_CALIBRATION_MIN_PX_PER_DEG 1.0f
#define AUTO_AIM_YAW_CORRECTION_LIMIT_DEG  6.0f
#define AUTO_AIM_YAW_DEGREES_PER_PIXEL     0.020f
#define AUTO_AIM_YAW_CORRECTION_STEP_MAX_DEG 2.0f
#define AUTO_AIM_CENTERING_TARGET_SPEED_DPS 3.0f
#define AUTO_AIM_X_TOLERANCE_PX            8.0f
#define AUTO_AIM_X_RELEASE_TOLERANCE_PX   16.0f
#define AUTO_AIM_X_RELEASE_CONFIRM_SAMPLES  2U
#define AUTO_AIM_AXIS_TOLERANCE_DEG        1.0f
#define AUTO_AIM_SETTLE_MS                 300U
#define AUTO_AIM_TARGET_LOSS_MS            500U

#define LCD_FSMC_READ_ADDSET              15U
#define LCD_FSMC_READ_DATAST              60U
#define LCD_FSMC_WRITE_ADDSET             3U
#define LCD_FSMC_WRITE_DATAST             8U

#define TOUCH_SAMPLE_PERIOD_MS            5U
#define TOUCH_PRESS_STABLE_SAMPLES        3U
#define TOUCH_RELEASE_STABLE_SAMPLES      3U
#define TOUCH_MEDIAN_SAMPLE_COUNT         5U

/*
 * Board-vendor landscape defaults, corrected for the missing negative X
 * slope in the vendor example. They are a startup baseline, not a substitute
 * for calibration of the actual LCD module.
 */
#ifndef TOUCH_RAW_X_LEFT
#define TOUCH_RAW_X_LEFT                  3680
#endif
#ifndef TOUCH_RAW_X_RIGHT
#define TOUCH_RAW_X_RIGHT                 207
#endif
#ifndef TOUCH_RAW_Y_TOP
#define TOUCH_RAW_Y_TOP                   577
#endif
#ifndef TOUCH_RAW_Y_BOTTOM
#define TOUCH_RAW_Y_BOTTOM                3643
#endif

#define APP_COMMAND_QUEUE_CAPACITY        8U

#define DISTANCE_INPUT_MIN_CM             50U
#define DISTANCE_INPUT_MAX_CM             100U

#define CANNON_FIRE_PULSE_MS              500U
#define CANNON_CHARGE_DURATION_MS          30000U

/*
 * Temporary bench mode: show only pitch, charge, fire and elapsed time.
 * Set to 0U to restore the complete UI and timed automatic firing flow.
 */
#ifndef CANNON_MANUAL_TEST_MODE
#define CANNON_MANUAL_TEST_MODE           0U
#endif

/* PB0 fire and PB1 charge relays conduct when driven high. */
#define CANNON_FIRE_RELAY_ACTIVE_LOW      0U
#define CANNON_CHARGE_RELAY_ACTIVE_LOW    0U

#define QD4310_CAN_BITRATE_HZ             1000000U
#define QD4310_YAW_MOTOR_ID               0x00U
#define QD4310_PITCH_MOTOR_ID             0x01U
#define QD4310_ENABLE_TIMEOUT_MS          250U
#define QD4310_ANGLE_MONITOR_PERIOD_MS    50U

/* Temporary product switch: keep all scan entry points disabled when zero. */
#define GIMBAL_SCAN_ENABLED               0U

#define YAW_SCAN_CONTROL_PERIOD_MS        10U
#define YAW_SCAN_FEEDBACK_TIMEOUT_MS      100U
#define YAW_SCAN_TARGET_LIMIT_DEG         50.0f
#define YAW_SCAN_HARD_LIMIT_DEG           55.0f
#define YAW_SCAN_TARGET_SPEED_DPS         8.0f
#define YAW_SCAN_ENDPOINT_REACHED_DEG     48.0f
#define YAW_SCAN_ENDPOINT_MIN_HOLD_MS     150U
#define YAW_SCAN_ENDPOINT_MAX_HOLD_MS     1200U
#define YAW_SCAN_KP_A_PER_DEG             0.080f
#define YAW_SCAN_KI_A_PER_DEG_S           0.002f
#define YAW_SCAN_KD_A_PER_RPM             0.006f
#define YAW_SCAN_ERROR_LIMIT_DEG          8.0f
#define YAW_SCAN_DEADBAND_DEG             0.25f
#define YAW_SCAN_INTEGRAL_ZONE_DEG        1.5f
#define YAW_SCAN_INTEGRAL_LIMIT_A         0.010f
#define YAW_SCAN_CURRENT_LIMIT_A          0.70f
#define YAW_SCAN_CURRENT_POLARITY         1.0f
#define YAW_SCAN_DIRECTION_CHECK_DEG      1.0f
#define YAW_SCAN_TRACE_CAPACITY           256U

/* Initial pitch hold values; polarity and gains require first-hardware check. */
#define PITCH_HOLD_FEEDBACK_TIMEOUT_MS    100U
#define PITCH_HOLD_HARD_LIMIT_DEG         90.0f
#define PITCH_HOLD_KP_A_PER_DEG           0.010f
#define PITCH_HOLD_KI_A_PER_DEG_S         0.0002f
#define PITCH_HOLD_KD_A_PER_RPM           0.0005f
#define PITCH_HOLD_ERROR_LIMIT_DEG        8.0f
#define PITCH_HOLD_DEADBAND_DEG           0.20f
#define PITCH_HOLD_INTEGRAL_ZONE_DEG      1.0f
#define PITCH_HOLD_INTEGRAL_LIMIT_A       0.010f
#define PITCH_HOLD_CURRENT_LIMIT_A        0.30f
#define PITCH_HOLD_CURRENT_POLARITY       1.0f
#define PITCH_HORIZONTAL_ZERO_RAW         0xEE77U
#define PITCH_MANUAL_TARGET_LIMIT_DEG     50.0f
#define PITCH_TARGET_LIMIT_DEG            85.0f

#if (QD4310_YAW_MOTOR_ID > 0x0FU) || (QD4310_PITCH_MOTOR_ID > 0x0FU)
#error "QD4310 motor IDs must be in the range 0x00..0x0F"
#endif
#if QD4310_YAW_MOTOR_ID == QD4310_PITCH_MOTOR_ID
#error "QD4310 yaw and pitch motor IDs must be unique"
#endif
#if QD4310_ANGLE_MONITOR_PERIOD_MS == 0U
#error "QD4310 angle monitor period must be greater than zero"
#endif
#if (GIMBAL_SCAN_ENABLED != 0U) && (GIMBAL_SCAN_ENABLED != 1U)
#error "GIMBAL_SCAN_ENABLED must be 0 or 1"
#endif
#if (CANNON_FIRE_RELAY_ACTIVE_LOW != 0U) && \
    (CANNON_FIRE_RELAY_ACTIVE_LOW != 1U)
#error "CANNON_FIRE_RELAY_ACTIVE_LOW must be 0 or 1"
#endif
#if (CANNON_CHARGE_RELAY_ACTIVE_LOW != 0U) && \
    (CANNON_CHARGE_RELAY_ACTIVE_LOW != 1U)
#error "CANNON_CHARGE_RELAY_ACTIVE_LOW must be 0 or 1"
#endif
#if CANNON_FIRE_PULSE_MS == 0U
#error "CANNON_FIRE_PULSE_MS must be greater than zero"
#endif
#if CANNON_CHARGE_DURATION_MS == 0U
#error "CANNON_CHARGE_DURATION_MS must be greater than zero"
#endif
#if (CANNON_MANUAL_TEST_MODE != 0U) && (CANNON_MANUAL_TEST_MODE != 1U)
#error "CANNON_MANUAL_TEST_MODE must be 0 or 1"
#endif
#if AUTO_AIM_TIMEOUT_MS <= (CANNON_CHARGE_DURATION_MS + \
                            AUTO_AIM_SETTLE_MS + CANNON_FIRE_PULSE_MS)
#error "AUTO_AIM_TIMEOUT_MS is too short"
#endif
#if (AUTO_AIM_SINGLE_PASS_LOCK != 0U) && \
    (AUTO_AIM_SINGLE_PASS_LOCK != 1U)
#error "AUTO_AIM_SINGLE_PASS_LOCK must be 0 or 1"
#endif
#endif
