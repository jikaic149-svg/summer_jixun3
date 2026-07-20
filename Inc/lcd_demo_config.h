#ifndef LCD_DEMO_CONFIG_H
#define LCD_DEMO_CONFIG_H

#include <stdint.h>

#define LCD_DEMO_WIDTH                    320U
#define LCD_DEMO_HEIGHT                   240U
#define LCD_DEMO_MADCTL_LANDSCAPE         0xA8U

#define BOARD_HSE_HZ                      25000000U
#define BOARD_HSI_HZ                      16000000U
#define BOARD_CORE_CLOCK_HZ               168000000U
#define BOARD_CLOCK_TIMEOUT_MS            100U

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

#endif
