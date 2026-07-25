#ifndef APPLICATION_TFT_THEME_ASSETS_H
#define APPLICATION_TFT_THEME_ASSETS_H

#include <stdint.h>

#define TFT_THEME_WIDTH 320U
#define TFT_THEME_HEIGHT 240U
#define TFT_THEME_COUNT 2U
#define TFT_THEME_PIXEL_COUNT (TFT_THEME_WIDTH * TFT_THEME_HEIGHT)

extern const uint16_t g_tft_theme_1[TFT_THEME_PIXEL_COUNT];
extern const uint16_t g_tft_theme_2[TFT_THEME_PIXEL_COUNT];
extern const uint16_t *const g_tft_themes[TFT_THEME_COUNT];

#endif
