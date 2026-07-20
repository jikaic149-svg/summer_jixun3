from __future__ import annotations

import re
import unittest
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


class LcdUiStaticTest(unittest.TestCase):
    def test_only_requested_layers_are_absent(self) -> None:
        self.assertFalse((ROOT / "task").exists())
        self.assertFalse((ROOT / "mode").exists())
        self.assertTrue((ROOT / "bsp").is_dir())
        self.assertTrue((ROOT / "Device").is_dir())

    def test_vet6_linker_memory(self) -> None:
        linker = (ROOT / "linker/STM32F407VETX_FLASH.ld").read_text("utf-8")
        self.assertRegex(linker, r"FLASH\s+\(rx\).*LENGTH\s*=\s*512K")
        self.assertRegex(linker, r"RAM\s+\(xrw\).*LENGTH\s*=\s*128K")
        self.assertRegex(linker, r"CCMRAM\s+\(xrw\).*LENGTH\s*=\s*64K")

    def test_new_target_does_not_compile_legacy_task_or_mode(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
        self.assertNotRegex(cmake, r"(?i)(?:^|[/\\])task(?:[/\\]|\.)")
        self.assertNotRegex(cmake, r"(?i)(?:^|[/\\])mode(?:[/\\]|\.)")
        self.assertIn("Src/lcd_demo_main.c", cmake)

    def test_touch_mapping_endpoints(self) -> None:
        def map_axis(raw: int, start: int, end: int, count: int) -> int:
            mapped = int((raw - start) * (count - 1) / (end - start))
            return max(0, min(count - 1, mapped))

        self.assertEqual(map_axis(3680, 3680, 207, 320), 0)
        self.assertEqual(map_axis(207, 3680, 207, 320), 319)
        self.assertEqual(map_axis(577, 577, 3643, 240), 0)
        self.assertEqual(map_axis(3643, 577, 3643, 240), 239)

    def test_bus_addresses_and_touch_pins_match_mapping(self) -> None:
        lcd_bus = (ROOT / "bsp/bsp_lcd_fsmc.c").read_text("utf-8")
        touch_bus = (ROOT / "bsp/bsp_xpt2046_io.c").read_text("utf-8")
        self.assertIn("0x60000000UL", lcd_bus)
        self.assertIn("0x60020000UL", lcd_bus)
        for define in (
            "TOUCH_CS_PIN     13U",
            "TOUCH_SCK_PIN     0U",
            "TOUCH_MOSI_PIN    2U",
            "TOUCH_MISO_PIN    3U",
            "TOUCH_PEN_PIN     4U",
        ):
            self.assertIn(define, touch_bus)

    def test_touch_adc_uses_explicit_12_bit_busy_timing(self) -> None:
        touch_bus = (ROOT / "bsp/bsp_xpt2046_io.c").read_text("utf-8")
        touch_device = (ROOT / "Device/xpt2046/xpt2046.c").read_text("utf-8")

        self.assertIn("bsp_xpt2046_receive_12bit", touch_bus)
        self.assertIn("TOUCH_SPI_HALF_PERIOD_LOOPS    60U", touch_bus)
        self.assertIn("TOUCH_CONVERSION_DELAY_LOOPS", touch_bus)
        self.assertIn("input & 0x0FFFU", touch_bus)
        self.assertNotRegex(touch_device, r"value\s*>>\s*3U")
        self.assertIn("XPT2046_ADC_MAX_VALUE", touch_device)
        self.assertIn("*raw_x == 0U", touch_device)
        self.assertIn("*raw_y == XPT2046_ADC_MAX_VALUE", touch_device)
        self.assertIn(
            "(void)read_channel(XPT2046_LANDSCAPE_X_COMMAND);",
            touch_device,
        )

    def test_ui_strings_have_compiled_glyphs(self) -> None:
        ui_source = (ROOT / "Application/ui/target_ui.c").read_text("utf-8")
        font_source = (ROOT / "Application/ui/ui_font.c").read_text("utf-8")
        required = {ord(char) for char in ui_source if ord(char) > 127}
        available = {
            int(value, 16)
            for value in re.findall(r"\{0x([0-9A-Fa-f]+)U,", font_source)
        }
        self.assertFalse(required - available)

    def test_main_telemetry_uses_aim_angles(self) -> None:
        ui_source = (ROOT / "Application/ui/target_ui.c").read_text("utf-8")
        self.assertIn('"方位"', ui_source)
        self.assertIn('"仰角"', ui_source)
        self.assertNotIn('"命中"', ui_source)
        self.assertNotIn('"时间"', ui_source)

    def test_documented_previews_are_native_resolution(self) -> None:
        for name in ("ui_main_preview.png", "ui_distance_preview.png"):
            with Image.open(ROOT / "docs" / name) as image:
                self.assertEqual(image.size, (320, 240))


if __name__ == "__main__":
    unittest.main()
