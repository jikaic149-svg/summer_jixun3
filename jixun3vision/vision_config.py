"""Competition vision configuration.

Keep this module limited to plain Python values so it can be imported both by
MaixPy and by the desktop unit tests.
"""

import os


class VisionConfig:
    def __init__(self):
        self.model_filename = "2026LaserTrack_yolo11n_cam2.mud"
        self.model_search_roots = ("/root/models", "/root", "/root/mymodel")
        # This custom AX model currently terminates the Maix process inside
        # detector.detect() without a Python traceback. Keep it opt-in until
        # the model/runtime pair passes the standalone NPU smoke test.
        self.enable_yolo = False
        # Restore the proven 320x240 capture footprint from the 2026-07-12
        # implementation while retaining RGB for red-target recognition.
        self.camera_width = 320
        self.camera_height = 240
        # Official MaixCAM2 docs state that low resolutions may default to a
        # high sensor rate. Pin VI/ISP capture to a bounded supported mode.
        self.camera_fps = 30

        self.confidence = 0.77
        self.iou_threshold = 0.45
        # Keep the acquisition floor conservative, but do not clip highly
        # saturated reds at the positive end of the LAB A channel.
        self.red_threshold = [0, 100, 25, 127, 0, 80]
        self.red_search_ratio = 0.95
        self.red_min_pixels = 12
        self.red_min_area = 20
        self.red_min_axis_ratio = 0.20
        self.red_min_fill_ratio = 0.18
        # Native blob/rectangle scans are the dominant MaixCAM frame cost.
        # Keep their image work and Python candidate iteration strictly bounded.
        self.red_max_roi_width = 320
        self.red_max_roi_height = 320
        self.red_max_blob_candidates = 12
        self.max_detection_candidates = 8
        # Some MaixCAM2 builds terminate inside native find_rects without a
        # Python traceback. Ellipse normalization is the competition-safe
        # default; bounded quad scanning remains available after board testing.
        self.enable_quad_scan = False
        self.geometry_scan_period_frames = 8
        self.geometry_cache_max_frames = 20
        self.geometry_max_roi_width = 360
        self.geometry_max_roi_height = 360
        self.geometry_max_rect_candidates = 8
        self.geometry_box_center_tolerance = 0.18
        self.geometry_box_scale_tolerance = 0.40
        # Reuse a copied YOLO box for one intervening frame. Red blob
        # validation still runs every frame, so loss invalidates immediately.
        self.detector_period_frames = 2

        self.lock_frames = 3
        self.lock_max_jump = 0.20
        self.target_center_alpha = 0.25
        self.geometry_min_quality = 0.45
        self.aim_deadband_x = 5
        self.aim_deadband_y = 5
        self.aim_stable_ms = 300
        self.frame_timeout_ms = 180
        self.camera_retry_delay_ms = 10
        self.error_log_period_ms = 2000
        self.gc_period_frames = 120
        # Recognition and control keep running at camera rate.  Display is an
        # asynchronous native pipeline, so cap submissions below panel refresh
        # to prevent rapidly changing scenes from building a driver backlog.
        self.display_period_ms = 100
        self.display_error_cooldown_ms = 250
        # Native LAB blob scans are intentionally slower than the camera loop.
        # A parked aiming target does not need 30 Hz, while bounding this call
        # substantially reduces multimedia heap and CPU pressure.
        self.recognition_period_ms = 80
        self.recognition_max_hold_ms = 160

        self.calibration_samples = 8
        self.calibration_max_spread_px = 4

        self.manual_exposure = False
        self.exposure = 166

        self.uart_device = "/dev/ttyS2"
        self.uart_baud = 115200
        self.target_send_period_ms = 40
        self.target_yaw_only = False
        self.output_polarity_x = 1
        self.output_polarity_y = 1

        # The competition PC uses one fixed address on the current hotspot.
        self.udp_host = "192.168.237.147"
        self.telemetry_port = 5005
        self.command_port = 5007
        self.status_port = 5008
        self.status_period_ms = 200

    @property
    def red_thresholds(self):
        return [list(self.red_threshold)]

    def resolve_model_path(self, script_path=None):
        if script_path is None:
            script_path = __file__
        roots = [os.path.dirname(os.path.abspath(script_path))]
        for root in self.model_search_roots:
            if root not in roots:
                roots.append(root)
        for root in roots:
            path = os.path.join(root, self.model_filename)
            if os.path.isfile(path):
                return path
        return None

    def setting_names(self):
        return ("EXPOSURE", "RED A MIN", "UDP HOST")

    def setting_value(self, index):
        index = int(index) % 3
        if index == 0:
            return str(self.exposure)
        if index == 1:
            return str(self.red_threshold[2])
        return self.udp_host

    def adjust_setting(self, index, direction):
        index = int(index) % 3
        direction = 1 if direction >= 0 else -1
        if index == 0:
            self.manual_exposure = True
            self.exposure = max(20, min(10000, self.exposure + direction * 20))
        elif index == 1:
            self.red_threshold[2] = max(
                0, min(self.red_threshold[3], self.red_threshold[2] + direction)
            )
        else:
            # Address redirection is deliberate: cycle only between the fixed
            # competition PC and broadcast diagnostics, never edit free text.
            self.udp_host = (
                "255.255.255.255"
                if self.udp_host == "192.168.237.147"
                else "192.168.237.147"
            )
        return self.setting_value(index)
