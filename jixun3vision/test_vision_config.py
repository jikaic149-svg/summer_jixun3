import unittest

from vision_config import VisionConfig


class VisionConfigUdpTest(unittest.TestCase):
    def test_camera_load_is_explicitly_bounded(self):
        config = VisionConfig()

        self.assertEqual(config.camera_fps, 30)
        self.assertEqual((config.camera_width, config.camera_height), (320, 240))
        self.assertEqual(config.camera_retry_delay_ms, 10)
        self.assertGreaterEqual(config.display_period_ms, 100)

    def test_current_wlan_is_the_default_udp_destination(self):
        config = VisionConfig()

        self.assertEqual(config.udp_host, "192.168.237.147")
        self.assertEqual(config.telemetry_port, 5005)
        self.assertEqual(config.command_port, 5007)
        self.assertEqual(config.status_port, 5008)

    def test_red_threshold_keeps_saturated_red_in_range(self):
        config = VisionConfig()

        self.assertEqual(config.red_threshold, [0, 100, 25, 127, 0, 80])

    def test_udp_setting_toggles_current_wlan_and_broadcast(self):
        config = VisionConfig()

        self.assertEqual(config.adjust_setting(2, 1), "255.255.255.255")
        self.assertEqual(config.adjust_setting(2, 1), "192.168.237.147")


if __name__ == "__main__":
    unittest.main()
