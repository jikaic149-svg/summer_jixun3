import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "task" / "task_control.c").read_text(encoding="utf-8")


def function_body(name):
    match = re.search(
        r"\b(?:void|uint8_t)\s+" + re.escape(name) +
        r"\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
        SOURCE,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError("missing function: " + name)
    return match.group("body")


class TaskSnapshotSafetyTest(unittest.TestCase):
    def test_snapshot_readers_never_spin(self):
        for name in ("Task_GetOutput", "Task_GetStatus"):
            body = function_body(name)
            self.assertNotIn("for (;;)", body)
            self.assertNotIn("while (", body)
            self.assertIn("TASK_CONTROL_LOCK()", body)
            self.assertIn("TASK_CONTROL_UNLOCK()", body)

    def test_start_stop_writers_use_the_same_lock(self):
        for name in ("Task_Init", "Task_Start", "Task_Stop"):
            body = function_body(name)
            self.assertIn("TASK_CONTROL_LOCK()", body)
            self.assertIn("TASK_CONTROL_UNLOCK()", body)

    def test_periodic_update_uses_short_snapshot_commit(self):
        body = function_body("Task_Update")
        self.assertNotIn("TASK_CONTROL_LOCK()", body)
        self.assertIn("task_publish_snapshot()", body)
        publish_body = function_body("task_publish_snapshot")
        self.assertIn("task_published[next_index]", publish_body)
        self.assertIn("TASK_CONTROL_LOCK()", publish_body)
        self.assertIn("task_published_index = next_index", publish_body)

    def test_removed_preemption_unsafe_sequence_lock(self):
        self.assertNotIn("publish_sequence", SOURCE)


if __name__ == "__main__":
    unittest.main()
