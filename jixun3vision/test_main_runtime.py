import unittest

from main import (
    DetectionSnapshot,
    RuntimeFaultGate,
    VISION_LOSS_TRACK_CONTINUE,
    VISION_LOSS_WAIT_RECOVERY,
    VISION_LOSS_WAIT_RELOCK,
    _handle_runtime_vision_timeout,
    _handle_click,
    _close_resource,
    _recognition_required,
    _snapshot_detections,
)
from target_recognition import TargetObservation
from vision_config import VisionConfig
from visual_calibration import VisualAimCalibration
from vision_ui import PAGE_CONTROL, PAGE_MAIN, PAGE_RESULT, VisionUiController
from vision_ui import CONTROL_RUNNING


class CallableDetection:
    def x(self):
        return -4

    def y(self):
        return 3

    def w(self):
        return 120

    def h(self):
        return 90

    def score(self):
        return 0.8


class MainRuntimeTest(unittest.TestCase):
    class RuntimeLossLink:
        def __init__(self):
            self.events = []

        def invalidate_vision(self, now_ms, move_epoch=0, force=True):
            self.events.append(("invalid", now_ms, move_epoch, force))
            return True

        def safe_stop(self, now_ms):
            self.events.append(("stop", now_ms))
            return True

    @staticmethod
    def _running_task(task_id):
        ui = VisionUiController()
        ui.select_task(task_id)
        ui.control_state = CONTROL_RUNNING
        return ui

    def _assert_runtime_loss_keeps_task_active(self, task_id, phase, expected):
        class Status:
            pass

        ui = self._running_task(task_id)
        status = Status()
        status.phase = phase
        link = self.RuntimeLossLink()

        action = _handle_runtime_vision_timeout(
            ui, status, link, 1234, move_epoch=7,
        )

        self.assertEqual(action, expected)
        self.assertEqual(ui.page, PAGE_CONTROL)
        self.assertEqual(ui.control_state, CONTROL_RUNNING)
        self.assertEqual(link.events, [("invalid", 1234, 7, True)])

    def test_task1_camera_timeout_does_not_stop_tracking(self):
        self._assert_runtime_loss_keeps_task_active(
            1, "TRACK_TO_FINISH", VISION_LOSS_TRACK_CONTINUE,
        )

    def test_task2_and_task3_track_center_ignore_visual_timeout(self):
        for task_id in (2, 3):
            with self.subTest(task_id=task_id):
                self._assert_runtime_loss_keeps_task_active(
                    task_id, "TRACK_TO_CENTER", VISION_LOSS_TRACK_CONTINUE,
                )

    def test_aim_and_laser_loss_wait_without_stopping_task(self):
        for task_id in (2, 3, 4, 5):
            for phase in ("AIM_ACQUIRE", "LASER_DWELL", "WAIT_TARGET_MOVE"):
                with self.subTest(task_id=task_id, phase=phase):
                    self._assert_runtime_loss_keeps_task_active(
                        task_id, phase, VISION_LOSS_WAIT_RECOVERY,
                    )

    def test_task4_and_task5_return_loss_waits_for_relock(self):
        for task_id in (4, 5):
            with self.subTest(task_id=task_id):
                self._assert_runtime_loss_keeps_task_active(
                    task_id, "TRACK_TO_FINISH", VISION_LOSS_WAIT_RELOCK,
                )

    def test_recognition_runs_on_set_and_visual_control_tasks(self):
        ui = VisionUiController()
        self.assertEqual(ui.page, PAGE_MAIN)
        self.assertFalse(_recognition_required(ui))
        ui.settings_open = True
        self.assertTrue(_recognition_required(ui))
        ui.settings_open = False
        ui.select_task(1)
        self.assertEqual(ui.page, PAGE_CONTROL)
        self.assertFalse(_recognition_required(ui))
        ui.select_task(2)
        self.assertTrue(_recognition_required(ui))
        ui.page = PAGE_RESULT
        self.assertFalse(_recognition_required(ui))

    def test_set_laser_and_calibration_click_sequence(self):
        class Link:
            def __init__(self):
                self.events = []
                self.telemetry_host = ""
                self.command_host = ""

            def send_vision(self, *args, **kwargs):
                self.events.append(("vision", args, kwargs))
                return True

            def send_command(self, command):
                self.events.append(("command", command))
                return True

        ui = VisionUiController()
        config = VisionConfig()
        calibration = VisualAimCalibration(208, 160)
        observation = TargetObservation()
        link = Link()

        _handle_click(
            "set", ui, config, calibration, observation, link, 100,
            lambda: None,
        )
        self.assertTrue(ui.settings_open)
        self.assertTrue(ui.settings_laser_requested)
        self.assertEqual([event[0] for event in link.events], ["vision", "command"])
        self.assertEqual(link.events[1], ("command", "cal_laser_on"))

        _handle_click(
            "set_cal", ui, config, calibration, observation, link, 110,
            lambda: None,
        )
        self.assertTrue(calibration.pending)
        _handle_click(
            "set_rst", ui, config, calibration, observation, link, 120,
            lambda: None,
        )
        self.assertFalse(calibration.pending)

        _handle_click(
            "back", ui, config, calibration, observation, link, 130,
            lambda: None,
        )
        self.assertFalse(ui.settings_open)
        self.assertFalse(ui.settings_laser_requested)
        self.assertEqual(link.events[-1], ("command", "cal_laser_off"))

    def test_native_detections_are_copied_and_bounded(self):
        snapshots = _snapshot_detections(
            [CallableDetection(), CallableDetection(), CallableDetection()], 2,
        )
        self.assertEqual(len(snapshots), 2)
        self.assertIsInstance(snapshots[0], DetectionSnapshot)
        self.assertEqual(
            (snapshots[0].x, snapshots[0].y, snapshots[0].w,
             snapshots[0].h, snapshots[0].score),
            (-4, 3, 120, 90, 0.8),
        )

    def test_stable_full_frame_detector_does_not_need_native_model(self):
        config = VisionConfig()
        self.assertFalse(config.enable_yolo)
        detection = DetectionSnapshot.from_box(
            0, 0, config.camera_width, config.camera_height,
        )
        self.assertEqual(
            (detection.x, detection.y, detection.w, detection.h),
            (0, 0, config.camera_width, config.camera_height),
        )

    def test_repeated_fault_logs_are_throttled(self):
        gate = RuntimeFaultGate(2000)
        self.assertTrue(gate.report("detect", "bad", 100))
        self.assertFalse(gate.report("detect", "bad", 200))
        self.assertTrue(gate.report("detect", "bad", 2200))
        self.assertEqual(gate.counts["detect"], 3)

    def test_resource_close_is_idempotent_at_call_site(self):
        class Resource:
            def __init__(self):
                self.closed = 0

            def close(self):
                self.closed += 1

        resource = Resource()
        _close_resource(resource)
        self.assertEqual(resource.closed, 1)


if __name__ == "__main__":
    unittest.main()
