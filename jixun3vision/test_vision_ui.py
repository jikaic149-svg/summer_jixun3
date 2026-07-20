import unittest

from vision_ui import (
    CONTROL_PREPARE,
    CONTROL_RUNNING,
    PAGE_CONTROL,
    PAGE_MAIN,
    PAGE_RESULT,
    PressReleaseLatch,
    VisionUiController,
    draw_main,
    settings_preview_frame,
)
from vision_config import VisionConfig


class VisionUiStateTest(unittest.TestCase):
    def test_only_three_pages_are_used(self):
        ui = VisionUiController()
        self.assertEqual(ui.page, PAGE_MAIN)
        ui.select_task(2)
        self.assertEqual(ui.page, PAGE_CONTROL)
        ui.finish(True, True, (10, -1), 0)
        self.assertEqual(ui.page, PAGE_RESULT)

    def test_visual_tasks_can_track_before_calibration_and_lock(self):
        ui = VisionUiController()
        ui.select_task(3)
        self.assertTrue(ui.start_running(True, False, True))
        self.assertEqual(ui.control_state, CONTROL_RUNNING)

    def test_visual_tasks_still_require_calibration(self):
        ui = VisionUiController()
        ui.select_task(3)
        self.assertFalse(ui.start_running(False, False, True))
        self.assertEqual(ui.notice, "CAL REQUIRED")

    def test_all_tasks_still_require_uart(self):
        for task_id in range(1, 6):
            ui = VisionUiController()
            ui.select_task(task_id)
            self.assertFalse(ui.start_running(True, True, False))
            self.assertEqual(ui.notice, "UART REQUIRED")

    def test_task1_only_requires_uart(self):
        ui = VisionUiController()
        ui.select_task(1)
        self.assertTrue(ui.start_running(False, False, True))

    def test_restart_returns_to_prepare_without_changing_task(self):
        ui = VisionUiController()
        ui.select_task(5)
        ui.finish(False, False, (-1, -1), 7)
        ui.restart()
        self.assertEqual(ui.page, PAGE_CONTROL)
        self.assertEqual(ui.control_state, CONTROL_PREPARE)
        self.assertEqual(ui.selected_task, 5)

    def test_press_release_must_end_on_same_button(self):
        latch = PressReleaseLatch()
        self.assertIsNone(latch.update(True, "start"))
        self.assertIsNone(latch.update(False, "stop"))
        self.assertIsNone(latch.update(True, "start"))
        self.assertEqual(latch.update(False, "start"), "start")

    def test_main_layout_is_two_columns_by_three_rows(self):
        rects = VisionUiController.main_rects(320, 240)
        self.assertEqual(len(rects), 6)
        self.assertEqual(rects["task1"][0], rects["task3"][0])
        self.assertEqual(rects["task2"][0], rects["task4"][0])

    def test_adjusting_exposure_enables_manual_mode(self):
        config = VisionConfig()
        self.assertFalse(config.manual_exposure)
        config.adjust_setting(0, 1)
        self.assertTrue(config.manual_exposure)
        self.assertEqual(config.exposure, 186)

    def test_set_preview_copies_only_for_red_threshold(self):
        class MockPreview:
            def __init__(self):
                self.binary_calls = []

            def binary(self, thresholds):
                self.binary_calls.append(thresholds)
                return self

        class MockFrame:
            def __init__(self):
                self.copy_count = 0
                self.binary_calls = []
                self.last_preview = None

            def copy(self):
                self.copy_count += 1
                self.last_preview = MockPreview()
                return self.last_preview

            def binary(self, thresholds):
                self.binary_calls.append(thresholds)
                return self

        config = VisionConfig()
        ui = VisionUiController()
        ui.settings_open = True
        frame = MockFrame()

        ui.setting_index = 0
        self.assertIs(settings_preview_frame(frame, ui, config), frame)
        ui.setting_index = 2
        self.assertIs(settings_preview_frame(frame, ui, config), frame)
        self.assertEqual(frame.copy_count, 0)
        self.assertEqual(frame.binary_calls, [])

        ui.setting_index = 1
        preview = settings_preview_frame(frame, ui, config)
        self.assertIs(preview, frame.last_preview)
        self.assertEqual(frame.copy_count, 1)
        self.assertEqual(frame.binary_calls, [])
        self.assertEqual(preview.binary_calls, [config.red_thresholds])

    def test_set_preview_can_reuse_an_independent_display_copy(self):
        class Preview:
            def __init__(self):
                self.copy_count = 0
                self.binary_calls = []

            def copy(self):
                self.copy_count += 1
                return self

            def binary(self, thresholds):
                self.binary_calls.append(thresholds)
                return self

        config = VisionConfig()
        ui = VisionUiController()
        ui.settings_open = True
        ui.setting_index = 1
        preview = Preview()
        self.assertIs(
            settings_preview_frame(preview, ui, config, copy_frame=False),
            preview,
        )
        self.assertEqual(preview.copy_count, 0)
        self.assertEqual(preview.binary_calls, [config.red_thresholds])

    def test_set_draw_keeps_preview_visible_and_button_layout_fixed(self):
        class MockImageModule:
            COLOR_BLACK = "black"
            COLOR_WHITE = "white"
            COLOR_GREEN = "green"
            COLOR_RED = "red"

        class MockCanvas:
            def __init__(self):
                self.rects = []

            def width(self):
                return 320

            def height(self):
                return 240

            def draw_rect(self, x, y, width, height, **kwargs):
                self.rects.append((x, y, width, height, kwargs))

            def draw_string(self, *args, **kwargs):
                pass

            def draw_circle(self, *args, **kwargs):
                pass

        config = VisionConfig()
        ui = VisionUiController()
        ui.settings_open = True
        canvas = MockCanvas()
        expected_buttons = set(VisionUiController.setting_rects(320, 240).values())
        expected_cal_buttons = set(
            VisionUiController.setting_calibration_rects(320, 240).values()
        )

        for setting_index in range(3):
            ui.setting_index = setting_index
            canvas.rects = []
            draw_main(canvas, ui, config, True, True, MockImageModule)
            drawn = {(item[0], item[1], item[2], item[3]) for item in canvas.rects}
            self.assertNotIn((0, 0, 320, 240), drawn)
            self.assertTrue(expected_buttons.issubset(drawn))
            self.assertTrue(expected_cal_buttons.issubset(drawn))

    def test_set_calibration_buttons_do_not_overlap_setting_controls(self):
        setting_rects = VisionUiController.setting_rects(320, 240)
        calibration_rects = VisionUiController.setting_calibration_rects(320, 240)

        def overlaps(first, second):
            return not (
                first[0] + first[2] <= second[0] or
                second[0] + second[2] <= first[0] or
                first[1] + first[3] <= second[1] or
                second[1] + second[3] <= first[1]
            )

        for calibration_rect in calibration_rects.values():
            self.assertGreaterEqual(calibration_rect[0], 0)
            self.assertGreaterEqual(calibration_rect[1], 0)
            self.assertLessEqual(calibration_rect[0] + calibration_rect[2], 320)
            self.assertLessEqual(calibration_rect[1] + calibration_rect[3], 240)
            for setting_rect in setting_rects.values():
                self.assertFalse(overlaps(calibration_rect, setting_rect))

    def test_set_calibration_buttons_return_distinct_action_ids(self):
        ui = VisionUiController()
        ui.settings_open = True
        rects = ui.setting_calibration_rects(320, 240)
        for action in ("set_cal", "set_rst"):
            rect = rects[action]
            self.assertEqual(
                ui.button_at(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 320, 240),
                action,
            )


if __name__ == "__main__":
    unittest.main()
