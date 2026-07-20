import math
import unittest

from target_recognition import (
    EllipseLockTracker,
    PerspectiveNormalizer,
    ShotScoreAccumulator,
    ShotScoreSession,
    TargetMovementDetector,
    TargetObservation,
    TargetRecognizer,
    ellipse_normalized_distance,
    ring_score_for_normalized_distance,
)
from vision_config import VisionConfig


class FakeDetection:
    def __init__(self, x=0, y=0, w=200, h=200, score=0.9):
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.score = score


class FakeBlob:
    def __init__(self, x, y, w, h, pixels, angle=0.0):
        self.values = (x, y, w, h)
        self._pixels = pixels
        self._angle = angle
        self.access_count = 0

    def __getitem__(self, index):
        self.access_count += 1
        return self.values[index]

    def cx(self):
        return self.values[0] + self.values[2] // 2

    def cy(self):
        return self.values[1] + self.values[3] // 2

    def pixels(self):
        return self._pixels

    def rotation_rad(self):
        return self._angle


class FakeRect:
    def __init__(self, corners):
        self._corners = corners

    def corners(self):
        return self._corners


class FakeImage:
    def __init__(self, blobs, rects=None, width=640, height=480):
        self.blobs = blobs
        self.rects = list(rects or [])
        self._width = width
        self._height = height
        self.rect_calls = []
        self.blob_calls = []

    def width(self):
        return self._width

    def height(self):
        return self._height

    def find_rects(self, **kwargs):
        self.rect_calls.append(kwargs)
        return self.rects

    def find_blobs(self, *_args, **kwargs):
        self.blob_calls.append(kwargs)
        return self.blobs


def observation_for(ellipse):
    observation = TargetObservation()
    observation.detected = True
    observation.locked = True
    observation.ellipse = ellipse
    observation.center = (int(ellipse[0]), int(ellipse[1]))
    return observation


class PerspectiveGeometryTest(unittest.TestCase):
    def test_skewed_quad_maps_to_square(self):
        quad = ((20, 30), (220, 10), (190, 180), (40, 210))
        normalizer = PerspectiveNormalizer.from_quad(quad)
        self.assertIsNotNone(normalizer)
        expected = ((0, 0), (1000, 0), (1000, 1000), (0, 1000))
        for actual, target in zip(quad, expected):
            mapped = normalizer.point(actual)
            self.assertAlmostEqual(mapped[0], target[0], places=5)
            self.assertAlmostEqual(mapped[1], target[1], places=5)

    def test_ellipse_distance_handles_viewpoint_squash(self):
        circle_distance = ellipse_normalized_distance((140, 100), (100, 100, 40, 40, 0))
        ellipse_distance = ellipse_normalized_distance((180, 100), (100, 100, 80, 20, 0))
        self.assertAlmostEqual(circle_distance, 2.0)
        self.assertAlmostEqual(ellipse_distance, 2.0)
        self.assertEqual(ring_score_for_normalized_distance(circle_distance), 9)
        self.assertEqual(ring_score_for_normalized_distance(ellipse_distance), 9)

    def test_rotated_ellipse_uses_local_axes(self):
        angle = math.pi / 4
        point = (100 + 40 / math.sqrt(2), 100 + 40 / math.sqrt(2))
        self.assertAlmostEqual(
            ellipse_normalized_distance(point, (100, 100, 80, 20, angle)),
            1.0,
            places=5,
        )


class TrackingAndScoreTest(unittest.TestCase):
    def test_lock_requires_current_stable_frames_and_loss_invalidates(self):
        tracker = EllipseLockTracker(stable_frames=3, max_jump=0.05)
        self.assertFalse(tracker.update((0.50, 0.50)))
        self.assertFalse(tracker.update((0.51, 0.49)))
        self.assertTrue(tracker.update((0.50, 0.50)))
        self.assertFalse(tracker.update(None))
        self.assertFalse(tracker.locked)

    def test_move_requires_changed_target_to_be_stable(self):
        detector = TargetMovementDetector(stable_frames=3)
        self.assertTrue(detector.arm(observation_for((100, 100, 80, 40, 0))))
        self.assertFalse(detector.update(observation_for((100, 100, 100, 50, 0))))
        self.assertFalse(detector.update(observation_for((101, 100, 100, 50, 0))))
        self.assertTrue(detector.update(observation_for((100, 101, 100, 50, 0))))
        self.assertEqual(detector.epoch, 1)

    def test_score_accumulator_prefers_mode_and_has_median_fallback(self):
        accumulator = ShotScoreAccumulator()
        for value in (8, 9, 9, 10, -1):
            accumulator.add(value)
        self.assertEqual(accumulator.result(), 9)
        accumulator.reset()
        for value in (7, 8, 9, 10):
            accumulator.add(value)
        self.assertEqual(accumulator.result(), 8)

    def test_two_dwell_results_use_completed_count_as_slot(self):
        session = ShotScoreSession()
        session.update(True, 0, 8, 100)
        session.update(True, 0, 9, 200)
        self.assertTrue(session.update(False, 1, -1, 3100))
        session.update(True, 1, 10, 4000)
        session.update(True, 1, 10, 4100)
        self.assertTrue(session.update(False, 2, -1, 7100))
        self.assertEqual(session.results, [8, 10])
        self.assertEqual(session.completed, 2)

    def test_frame_adapter_accepts_strongly_squashed_red_ellipse(self):
        config = VisionConfig()
        config.lock_frames = 3
        recognizer = TargetRecognizer(config)
        frame = FakeImage([FakeBlob(50, 88, 100, 25, 1500)])
        detection = FakeDetection()
        first = recognizer.update(frame, [detection], aim_point=(100, 100))
        second = recognizer.update(frame, [detection], aim_point=(100, 100))
        third = recognizer.update(frame, [detection], aim_point=(100, 100))
        self.assertFalse(first.locked)
        self.assertFalse(second.locked)
        self.assertTrue(third.locked)
        self.assertTrue(third.control_valid)
        self.assertEqual(third.geometry, "ellipse")
        self.assertEqual(third.score, 10)

    def test_locked_center_uses_proven_quarter_rate_ema(self):
        config = VisionConfig()
        config.lock_frames = 1
        config.target_center_alpha = 0.25
        recognizer = TargetRecognizer(config)
        detection = FakeDetection()

        first = recognizer.update(
            FakeImage([FakeBlob(80, 80, 40, 40, 1000)]), [detection],
        )
        second = recognizer.update(
            FakeImage([FakeBlob(100, 80, 40, 40, 1000)]), [detection],
        )

        self.assertEqual(first.center, (100, 100))
        self.assertEqual(second.center, (105, 100))

    def test_existing_red_track_resists_an_equal_center_distractor(self):
        config = VisionConfig()
        config.lock_frames = 3
        config.target_center_alpha = 1.0
        recognizer = TargetRecognizer(config)
        detection = FakeDetection()
        tracked = FakeBlob(20, 80, 40, 40, 1000)
        distractor = FakeBlob(80, 80, 40, 40, 1000)

        first = recognizer.update(FakeImage([tracked]), [detection])
        second = recognizer.update(
            FakeImage([tracked, distractor]), [detection],
        )
        third = recognizer.update(
            FakeImage([tracked, distractor]), [detection],
        )

        self.assertFalse(first.locked)
        self.assertFalse(second.locked)
        self.assertTrue(third.locked)
        self.assertEqual(third.ellipse[0:2], (40.0, 100.0))

    def test_lost_frame_clears_red_candidate_association(self):
        config = VisionConfig()
        config.lock_frames = 1
        config.target_center_alpha = 1.0
        recognizer = TargetRecognizer(config)
        detection = FakeDetection()
        previous = FakeBlob(20, 80, 40, 40, 1000)
        centered = FakeBlob(80, 80, 40, 40, 1000)

        locked = recognizer.update(FakeImage([previous]), [detection])
        lost = recognizer.update(FakeImage([]), [detection])
        reacquired = recognizer.update(
            FakeImage([previous, centered]), [detection],
        )

        self.assertTrue(locked.control_valid)
        self.assertFalse(lost.locked)
        self.assertFalse(lost.control_valid)
        self.assertEqual(reacquired.ellipse[0:2], (100.0, 100.0))

    def test_move_epoch_is_preserved_across_lost_frames_and_reset_per_session(self):
        config = VisionConfig()
        recognizer = TargetRecognizer(config)
        recognizer.movement.epoch = 2
        lost = recognizer.update(FakeImage([]), [], movement_enabled=True)
        self.assertEqual(lost.move_epoch, 2)
        recognizer.reset_session()
        reset_lost = recognizer.update(FakeImage([]), [])
        self.assertEqual(reset_lost.move_epoch, 0)


class BoundedPerformanceTest(unittest.TestCase):
    def _recognizer_and_frame(self, scan_period=4, rects=None):
        config = VisionConfig()
        config.lock_frames = 1
        config.enable_quad_scan = True
        config.geometry_scan_period_frames = scan_period
        config.geometry_cache_max_frames = 20
        recognizer = TargetRecognizer(config)
        frame = FakeImage(
            [FakeBlob(75, 75, 50, 50, 1800)],
            rects=rects,
            width=800,
            height=600,
        )
        return config, recognizer, frame

    def test_failed_rectangle_scan_is_not_retried_every_frame(self):
        _config, recognizer, frame = self._recognizer_and_frame(scan_period=4)
        detection = FakeDetection()
        for frame_number in range(10):
            observation = recognizer.update(frame, [detection], now_ms=frame_number)
            self.assertTrue(observation.detected)
        self.assertEqual(len(frame.rect_calls), 3)

    def test_valid_quad_and_homography_are_cached_between_scheduled_scans(self):
        rect = FakeRect(((0, 0), (200, 0), (200, 200), (0, 200)))
        _config, recognizer, frame = self._recognizer_and_frame(
            scan_period=5, rects=[rect],
        )
        detection = FakeDetection()
        geometries = []
        for frame_number in range(12):
            geometries.append(
                recognizer.update(frame, [detection], now_ms=frame_number).geometry
            )
        self.assertEqual(len(frame.rect_calls), 3)
        self.assertEqual(set(geometries), {"quad"})

    def test_large_detection_has_bounded_native_scan_rois(self):
        config = VisionConfig()
        config.lock_frames = 1
        config.enable_quad_scan = True
        recognizer = TargetRecognizer(config)
        frame = FakeImage(
            [FakeBlob(590, 340, 100, 25, 1600)],
            width=1280,
            height=720,
        )
        detection = FakeDetection(0, 0, 1280, 720)
        recognizer.update(frame, [detection])
        rect_roi = frame.rect_calls[0]["roi"]
        blob_roi = frame.blob_calls[0]["roi"]
        self.assertLessEqual(rect_roi[2], config.geometry_max_roi_width)
        self.assertLessEqual(rect_roi[3], config.geometry_max_roi_height)
        self.assertLessEqual(blob_roi[2], config.red_max_roi_width)
        self.assertLessEqual(blob_roi[3], config.red_max_roi_height)
        self.assertGreaterEqual(rect_roi[0], 0)
        self.assertGreaterEqual(blob_roi[1], 0)

    def test_detection_jump_invalidates_cache_and_scans_immediately(self):
        rect = FakeRect(((0, 0), (200, 0), (200, 200), (0, 200)))
        _config, recognizer, frame = self._recognizer_and_frame(
            scan_period=20, rects=[rect],
        )
        recognizer.update(frame, [FakeDetection(0, 0, 200, 200)])
        self.assertEqual(len(frame.rect_calls), 1)
        recognizer.update(frame, [FakeDetection(400, 200, 200, 200)])
        self.assertEqual(len(frame.rect_calls), 2)

    def test_blob_candidate_iteration_is_bounded(self):
        config = VisionConfig()
        config.lock_frames = 1
        config.red_max_blob_candidates = 3
        blobs = [FakeBlob(80, 80, 40, 40, 1000) for _index in range(12)]
        recognizer = TargetRecognizer(config)
        frame = FakeImage(blobs)
        recognizer.update(frame, [FakeDetection()])
        self.assertGreater(blobs[2].access_count, 0)
        self.assertEqual(blobs[3].access_count, 0)
        self.assertEqual(frame.blob_calls[0]["x_stride"], 2)
        self.assertEqual(frame.blob_calls[0]["y_stride"], 2)

    def test_target_loss_still_invalidates_control_immediately(self):
        config, recognizer, frame = self._recognizer_and_frame(scan_period=8)
        locked = recognizer.update(frame, [FakeDetection()])
        self.assertTrue(locked.control_valid)
        blob_calls = len(frame.blob_calls)
        lost = recognizer.update(frame, [])
        self.assertFalse(lost.locked)
        self.assertFalse(lost.control_valid)
        self.assertEqual(len(frame.blob_calls), blob_calls)

    def test_fully_outside_detection_never_reaches_native_scans(self):
        config = VisionConfig()
        recognizer = TargetRecognizer(config)
        frame = FakeImage([], width=320, height=240)
        outside = FakeDetection(400, 300, 100, 80)
        observation = recognizer.update(frame, [outside])
        self.assertFalse(observation.detected)
        self.assertFalse(observation.control_valid)
        self.assertEqual(frame.rect_calls, [])
        self.assertEqual(frame.blob_calls, [])

    def test_partially_outside_detection_is_clipped_before_both_native_scans(self):
        config = VisionConfig()
        config.lock_frames = 1
        config.enable_quad_scan = True
        recognizer = TargetRecognizer(config)
        frame = FakeImage(
            [FakeBlob(10, 10, 40, 20, 600)], width=320, height=240,
        )
        partial = FakeDetection(-80, -40, 200, 160)
        observation = recognizer.update(frame, [partial])
        self.assertEqual(observation.target_box, (0, 0, 120, 120))
        for call in (frame.rect_calls[0], frame.blob_calls[0]):
            x, y, width, height = call["roi"]
            self.assertGreaterEqual(x, 0)
            self.assertGreaterEqual(y, 0)
            self.assertGreater(width, 0)
            self.assertGreater(height, 0)
            self.assertLessEqual(x + width, 320)
            self.assertLessEqual(y + height, 240)

    def test_competition_default_never_calls_unstable_native_rectangle_scan(self):
        config = VisionConfig()
        config.lock_frames = 1
        self.assertFalse(config.enable_quad_scan)
        recognizer = TargetRecognizer(config)
        frame = FakeImage(
            [FakeBlob(75, 88, 50, 25, 900)],
            rects=[FakeRect(((0, 0), (200, 0), (200, 200), (0, 200)))],
        )
        observation = recognizer.update(frame, [FakeDetection()])
        self.assertEqual(frame.rect_calls, [])
        self.assertEqual(observation.geometry, "ellipse")
        self.assertTrue(observation.control_valid)


if __name__ == "__main__":
    unittest.main()
