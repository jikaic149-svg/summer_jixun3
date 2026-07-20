import unittest

from visual_calibration import VisualAimCalibration


class VisualAimCalibrationTest(unittest.TestCase):
    def test_default_reference_is_screen_center(self):
        calibration = VisualAimCalibration(208)
        self.assertEqual(calibration.reference_x, 208)
        self.assertEqual(calibration.raw_dx(197), -11)
        self.assertEqual(calibration.output_dx(197), -11)

    def test_unstable_samples_do_not_calibrate(self):
        calibration = VisualAimCalibration(208, sample_frames=5, max_spread_px=4)
        calibration.start()
        for x in (190, 200, 190, 200, 190, 200):
            self.assertFalse(calibration.update((x, 150)))
        self.assertTrue(calibration.pending)
        self.assertFalse(calibration.calibrated)
        self.assertEqual(calibration.reference_x, 208)

    def test_stable_samples_calibrate_to_average(self):
        calibration = VisualAimCalibration(208, sample_frames=5, max_spread_px=4)
        calibration.start()
        for x in (196, 197, 198, 197):
            self.assertFalse(calibration.update((x, 150)))
        self.assertTrue(calibration.update((197, 150)))
        self.assertFalse(calibration.pending)
        self.assertTrue(calibration.calibrated)
        self.assertEqual(calibration.reference_x, 197)
        self.assertEqual(calibration.output_dx(197), 0)

    def test_missing_target_restarts_collection(self):
        calibration = VisualAimCalibration(208, sample_frames=3, max_spread_px=4)
        calibration.start()
        calibration.update((197, 150))
        calibration.update(None)
        self.assertEqual(calibration.samples, [])
        self.assertTrue(calibration.pending)

    def test_reset_restores_screen_center(self):
        calibration = VisualAimCalibration(208, sample_frames=3, max_spread_px=4)
        calibration.start()
        for x in (196, 197, 198):
            calibration.update((x, 150))
        calibration.reset()
        self.assertEqual(calibration.reference_x, 208)
        self.assertFalse(calibration.calibrated)
        self.assertFalse(calibration.pending)

    def test_unstable_y_axis_does_not_calibrate(self):
        calibration = VisualAimCalibration(
            208, 120, sample_frames=4, max_spread_px=4,
        )
        calibration.start()
        for point in ((197, 100), (197, 110), (198, 100), (197, 110)):
            self.assertFalse(calibration.update(point))
        self.assertFalse(calibration.calibrated)

    def test_two_axis_reference_and_error(self):
        calibration = VisualAimCalibration(
            208, 120, sample_frames=3, max_spread_px=4,
        )
        calibration.start()
        for point in ((196, 118), (197, 119), (198, 120)):
            calibration.update(point)
        self.assertEqual((calibration.reference_x, calibration.reference_y), (197, 119))
        self.assertEqual(calibration.output_error((200, 121)), (3, 2))

    def test_invalid_red_center_restarts_consecutive_window(self):
        calibration = VisualAimCalibration(
            208, 120, sample_frames=3, max_spread_px=4,
        )
        calibration.start()
        calibration.update((197, 119))
        calibration.update((198,))
        self.assertEqual(calibration.progress, (0, 3))
        self.assertTrue(calibration.pending)
        for point in ((196, 118), (197, 119), (198, 120)):
            calibration.update(point)
        self.assertTrue(calibration.calibrated)

    def test_rst_during_collection_clears_progress_and_restores_both_axes(self):
        calibration = VisualAimCalibration(
            208, 120, sample_frames=3, max_spread_px=4,
        )
        calibration.start()
        calibration.update((190, 100))
        self.assertEqual(calibration.progress, (1, 3))
        self.assertTrue(calibration.reset())
        self.assertEqual(calibration.progress, (0, 3))
        self.assertEqual((calibration.reference_x, calibration.reference_y), (208, 120))
        self.assertFalse(calibration.pending)


if __name__ == "__main__":
    unittest.main()
