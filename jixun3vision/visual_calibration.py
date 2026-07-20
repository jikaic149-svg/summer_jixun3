class VisualAimCalibration:
    """Session-only two-axis visual reference calibration."""

    def __init__(
        self,
        screen_center_x,
        screen_center_y=None,
        sample_frames=8,
        max_spread_px=4,
    ):
        self.screen_center_x = int(screen_center_x)
        self.screen_center_y = int(
            screen_center_x if screen_center_y is None else screen_center_y
        )
        self.sample_frames = max(1, int(sample_frames))
        self.max_spread_px = max(0, int(max_spread_px))
        self.reference_x = self.screen_center_x
        self.reference_y = self.screen_center_y
        self.calibrated = False
        self.pending = False
        self.samples = []

    def start(self):
        self.pending = True
        self.calibrated = False
        self.samples = []
        return True

    def reset(self):
        self.reference_x = self.screen_center_x
        self.reference_y = self.screen_center_y
        self.calibrated = False
        self.pending = False
        self.samples = []
        return True

    def update(self, red_center):
        """Consume a point and complete after a stable X/Y sample window."""
        if not self.pending:
            return False
        if red_center is None:
            self.samples = []
            return False

        try:
            sample_x = int(red_center[0])
            sample_y = int(red_center[1])
        except (TypeError, ValueError, IndexError, OverflowError):
            self.samples = []
            return False
        self.samples.append((sample_x, sample_y))
        if len(self.samples) > self.sample_frames:
            self.samples.pop(0)
        if len(self.samples) < self.sample_frames:
            return False

        xs = [point[0] for point in self.samples]
        ys = [point[1] for point in self.samples]
        if max(xs) - min(xs) > self.max_spread_px:
            return False
        if max(ys) - min(ys) > self.max_spread_px:
            return False

        self.reference_x = int(sum(xs) / len(xs) + 0.5)
        self.reference_y = int(sum(ys) / len(ys) + 0.5)
        self.calibrated = True
        self.pending = False
        self.samples = []
        return True

    def raw_error(self, point):
        return (
            int(point[0]) - self.screen_center_x,
            int(point[1]) - self.screen_center_y,
        )

    def output_error(self, point):
        return (
            int(point[0]) - self.reference_x,
            int(point[1]) - self.reference_y,
        )

    # Backward-compatible yaw helpers used by the original tests.
    def raw_dx(self, red_center_x):
        return int(red_center_x) - self.screen_center_x

    def output_dx(self, red_center_x):
        return int(red_center_x) - self.reference_x

    @property
    def progress(self):
        return len(self.samples), self.sample_frames

    @property
    def state_text(self):
        if self.pending:
            return "collect %d/%d" % (len(self.samples), self.sample_frames)
        return "on" if self.calibrated else "off"
