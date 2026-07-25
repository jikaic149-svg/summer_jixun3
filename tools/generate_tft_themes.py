"""Convert two 320x240 RGB images into compile-time RGB565 assets."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
INPUTS = (ROOT / "tft_theme_1.jpg", ROOT / "tft_theme_2.jpg")
OUTPUT_C = ROOT / "Application" / "ui" / "tft_theme_assets.c"
OUTPUT_H = ROOT / "Application" / "ui" / "tft_theme_assets.h"
WIDTH = 320
HEIGHT = 240


def rgb565(red, green, blue):
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def load_pixels(path):
    with Image.open(path).convert("RGB") as image:
        if image.size != (WIDTH, HEIGHT):
            raise ValueError("%s must be %dx%d" % (path, WIDTH, HEIGHT))
        return [rgb565(*pixel) for pixel in image.getdata()]


def write_array(output, name, pixels):
    output.write("const uint16_t %s[TFT_THEME_PIXEL_COUNT] = {\n" % name)
    for start in range(0, len(pixels), 12):
        row = pixels[start:start + 12]
        output.write("    " + ", ".join("0x%04XU" % value for value in row) + ",\n")
    output.write("};\n\n")


def main():
    themes = [load_pixels(path) for path in INPUTS]
    OUTPUT_H.write_text(
        "#ifndef APPLICATION_TFT_THEME_ASSETS_H\n"
        "#define APPLICATION_TFT_THEME_ASSETS_H\n\n"
        "#include <stdint.h>\n\n"
        "#define TFT_THEME_WIDTH 320U\n"
        "#define TFT_THEME_HEIGHT 240U\n"
        "#define TFT_THEME_COUNT 2U\n"
        "#define TFT_THEME_PIXEL_COUNT (TFT_THEME_WIDTH * TFT_THEME_HEIGHT)\n\n"
        "extern const uint16_t g_tft_theme_1[TFT_THEME_PIXEL_COUNT];\n"
        "extern const uint16_t g_tft_theme_2[TFT_THEME_PIXEL_COUNT];\n"
        "extern const uint16_t *const g_tft_themes[TFT_THEME_COUNT];\n\n"
        "#endif\n",
        encoding="utf-8",
    )
    with OUTPUT_C.open("w", encoding="utf-8", newline="\n") as output:
        output.write('#include "tft_theme_assets.h"\n\n')
        write_array(output, "g_tft_theme_1", themes[0])
        write_array(output, "g_tft_theme_2", themes[1])
        output.write("const uint16_t *const g_tft_themes[TFT_THEME_COUNT] = {\n")
        output.write("    g_tft_theme_1, g_tft_theme_2\n};\n")
    print("generated", OUTPUT_C, OUTPUT_H)


if __name__ == "__main__":
    main()
