#ifndef APPLICATION_UI_FONT_H
#define APPLICATION_UI_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define UI_FONT_HEIGHT 16U

typedef struct {
    uint32_t codepoint;
    uint8_t width;
    uint16_t rows[UI_FONT_HEIGHT];
} ui_font_glyph_t;

const ui_font_glyph_t *ui_font_find(uint32_t codepoint);
bool ui_font_utf8_next(const char **cursor, uint32_t *codepoint);
uint16_t ui_font_text_width(const char *text);

#endif
