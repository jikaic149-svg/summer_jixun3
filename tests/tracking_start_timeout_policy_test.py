import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "Application" / "gimbal" / "gimbal_task.c"
).read_text(encoding="utf-8")


class TrackingStartTimeoutPolicyTest(unittest.TestCase):
    def test_tracking_timeout_promotes_running_without_braking(self):
        match = re.search(
            r"balance_start_assist_timer_sec\s*>=\s*"
            r"BALANCE_MODE_START_ASSIST_TIMEOUT_SEC\)\)\s*\{"
            r"(?P<body>.*?)\n\s*\}\n\s*\}",
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        tracking_branch, non_tracking_branch = body.split("} else {", 1)
        self.assertIn("chassis_mode == CHASSIS_MODE_TRACKING", tracking_branch)
        self.assertIn(
            "balance_motion_state = BALANCE_MOTION_RUNNING",
            tracking_branch,
        )
        self.assertNotIn("Chassis_EnterMotionBraking", tracking_branch)
        self.assertIn("Chassis_EnterMotionBraking", non_tracking_branch)

    def test_marker_stop_path_is_unchanged(self):
        self.assertIsNotNone(re.search(
            r"drive_requested == 0U\).*?"
            r"Chassis_EnterMotionBraking\(&drive_stop_edge\)",
            SOURCE,
            re.DOTALL,
        ))


if __name__ == "__main__":
    unittest.main()
