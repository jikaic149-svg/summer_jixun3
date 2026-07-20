import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("vision_control", ROOT / "25e.py")
vision = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vision)


class FakePort:
    def __init__(self, rx=b""):
        self.rx = rx
        self.writes = []

    def read(self):
        data = self.rx
        self.rx = b""
        return data

    def write(self, payload):
        self.writes.append(bytes(payload))
        return len(payload)


class FakeSocket:
    def __init__(self):
        self.sent = []

    def sendto(self, payload, address):
        self.sent.append((bytes(payload), address))
        return len(payload)


class VisionFrameTest(unittest.TestCase):
    def test_extended_frame_and_legacy_helper_call(self):
        self.assertEqual(
            vision.format_target_frame(3, -2, True),
            "V,3,-2,1,1,0,0\n",
        )
        self.assertEqual(
            vision.format_target_frame(2, -2, True, True, True, 7),
            "V,2,-2,1,1,1,7\n",
        )
        self.assertEqual(
            vision.format_target_frame(8, -9, True, True, True, 7),
            "V,8,-9,1,1,0,7\n",
        )
        self.assertEqual(
            vision.format_target_frame(99, 99, False, True, True, 12),
            "V,0,0,0,0,0,12\n",
        )

    def test_aim_ready_requires_calibration_lock_and_both_deadzones(self):
        self.assertTrue(vision.target_aim_ready(True, True, 2, -2))
        self.assertFalse(vision.target_aim_ready(False, True, 0, 0))
        self.assertFalse(vision.target_aim_ready(True, False, 0, 0))
        self.assertFalse(vision.target_aim_ready(True, True, 3, 0))
        self.assertFalse(vision.target_aim_ready(True, True, 0, -3))

    def test_lock_needs_three_frames_and_one_miss_revokes(self):
        tracker = vision.TargetLockTracker(confirm_frames=3)
        self.assertFalse(tracker.update(True))
        self.assertFalse(tracker.update(True))
        self.assertTrue(tracker.update(True))
        self.assertFalse(tracker.update(False))
        self.assertFalse(tracker.update(True))


class TaskStatusTest(unittest.TestCase):
    def test_parser_accepts_only_complete_consistent_frames(self):
        state = vision.parse_task_status_frame(b"T,5,6,2,1,0,15,1\r\n")
        self.assertEqual(
            state,
            {
                "task_id": 5,
                "phase": 6,
                "shot": 2,
                "aim": 1,
                "laser": 0,
                "milestones": 15,
                "done": 1,
            },
        )
        self.assertIsNone(vision.parse_task_status_frame("T,5,6,2,1,0,15"))
        self.assertIsNone(vision.parse_task_status_frame("T,5,4,2,1,0,15,1"))
        self.assertIsNone(vision.parse_task_status_frame("T,8,6,2,1,0,15,1"))

    def test_bridge_forwards_task_frame_and_keeps_last_good_state(self):
        port = FakePort(b"1,2,3\nT,3,4,1,0,0,2,0\nT,3,bad\n")
        sock = FakeSocket()
        bridge = vision.UartUdpBridge(port)
        bridge.sock = sock
        bridge.available = True

        self.assertEqual(bridge._poll_uart(), 3)
        self.assertEqual(len(sock.sent), 2)
        self.assertEqual(sock.sent[1][0], b"T,3,4,1,0,0,2,0\n")
        self.assertEqual(bridge.task_state["task_id"], 3)
        self.assertEqual(bridge.task_state["phase"], 4)
        self.assertEqual(bridge.task_state_version, 1)
        self.assertIsNotNone(bridge.task_state_received_ms)
        received_ms = bridge.task_state_received_ms
        self.assertTrue(bridge.task_state_is_fresh(received_ms + 500))
        self.assertFalse(bridge.task_state_is_fresh(received_ms + 501))

    def test_competition_status_health_requires_fresh_matching_task(self):
        state = {"task_id": 4}
        self.assertEqual(
            vision.competition_status_health("task4", None, False), "WAIT",
        )
        self.assertEqual(
            vision.competition_status_health("task4", {"task_id": 3}, True),
            "WAIT",
        )
        self.assertEqual(
            vision.competition_status_health("task4", state, False), "LOST",
        )
        self.assertEqual(
            vision.competition_status_health("task4", state, True), "OK",
        )
        self.assertIsNone(
            vision.competition_status_health(vision.STAGE_TRACK, None, False),
        )


class CommandAndMoveTest(unittest.TestCase):
    def test_each_competition_command_uses_safe_page_change_order(self):
        for task_number in range(1, 6):
            port = FakePort()
            command = "task%d" % task_number
            self.assertTrue(vision.send_task_command(port, command))
            self.assertEqual(
                port.writes,
                [
                    b"V,0,0,0,0,0,0\n",
                    b"stand\n",
                    (command + "\n").encode("ascii"),
                ],
            )

    def test_stop_revokes_extended_target_before_stand(self):
        port = FakePort()
        self.assertTrue(vision.send_stop_command(port))
        self.assertEqual(
            port.writes,
            [b"V,0,0,0,0,0,0\n", b"stand\n"],
        )

    def test_move_epoch_only_changes_once_per_confirmed_wait_phase(self):
        self.assertEqual(vision.TARGET_MOVE_DISTANCE_PX, 12)
        self.assertEqual(vision.TARGET_MOVE_CONFIRM_FRAMES, 5)
        tracker = vision.TargetMoveEpochTracker(
            distance_px=10, confirm_frames=3,
        )

        for center in ((0, 0), (20, 0), (40, 0)):
            self.assertEqual(tracker.update(False, True, center), 0)

        self.assertEqual(tracker.update(True, True, (0, 0)), 0)
        self.assertEqual(tracker.update(True, True, (20, 0)), 0)
        self.assertEqual(tracker.update(True, False, None), 0)

        # The lost frame contributes no confirmation, but the last trustworthy
        # center remains the baseline when the displaced target is reacquired.
        self.assertEqual(tracker.update(True, True, (40, 0)), 0)
        self.assertEqual(tracker.update(True, True, (40, 0)), 0)
        self.assertEqual(tracker.update(True, True, (40, 0)), 1)
        for _ in range(10):
            self.assertEqual(tracker.update(True, True, (80, 0)), 1)

        self.assertEqual(tracker.update(False, True, (80, 0)), 1)
        self.assertEqual(tracker.update(True, True, (80, 0)), 1)
        self.assertEqual(tracker.update(True, True, (95, 0)), 1)
        self.assertEqual(tracker.update(True, True, (95, 0)), 1)
        self.assertEqual(tracker.update(True, True, (95, 0)), 2)

    def test_selection_contains_debug_and_five_competition_entries(self):
        rects = vision.selection_button_rects(320, 240)
        self.assertEqual(
            set(rects),
            {"vision", "track", "target", "task1", "task2", "task3", "task4", "task5"},
        )
        for x, y, width, height in rects.values():
            self.assertGreater(width, 0)
            self.assertGreater(height, 0)
            self.assertGreaterEqual(x, 0)
            self.assertGreaterEqual(y, 0)
            self.assertLessEqual(x + width, 320)
            self.assertLessEqual(y + height, 240)


if __name__ == "__main__":
    unittest.main()
