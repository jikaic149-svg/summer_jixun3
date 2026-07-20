"""Red target recognition and perspective-aware scoring.

The geometry and state classes in this file intentionally avoid numpy,
dataclasses, and Maix imports.  They are unit-testable on a PC and usable on
the constrained MaixPy runtime.  Only TargetRecognizer's frame adapter calls
Maix image methods.
"""

import math


def clamp(value, low, high):
    return max(low, min(high, value))


def bounded_center_roi(box, ratio, max_width, max_height, image_size=None):
    """Return a center-preserving ROI with fixed dimension limits."""
    x, y, width, height = [int(value) for value in box]
    ratio = clamp(float(ratio), 0.05, 1.0)
    roi_width = max(1, min(int(width * ratio), max(1, int(max_width))))
    roi_height = max(1, min(int(height * ratio), max(1, int(max_height))))
    center_x = x + width * 0.5
    center_y = y + height * 0.5
    roi_x = int(round(center_x - roi_width * 0.5))
    roi_y = int(round(center_y - roi_height * 0.5))
    if image_size is not None:
        image_width = max(1, int(image_size[0]))
        image_height = max(1, int(image_size[1]))
        roi_width = min(roi_width, image_width)
        roi_height = min(roi_height, image_height)
        roi_x = max(0, min(image_width - roi_width, roi_x))
        roi_y = max(0, min(image_height - roi_height, roi_y))
    return [roi_x, roi_y, roi_width, roi_height]


def clip_box_to_image(box, image_size):
    """Intersect a detection with the image; return None for no area."""
    x, y, width, height = [int(value) for value in box]
    if width <= 0 or height <= 0:
        return None
    if image_size is None:
        return x, y, width, height
    image_width = max(1, int(image_size[0]))
    image_height = max(1, int(image_size[1]))
    x1 = max(0, min(image_width, x))
    y1 = max(0, min(image_height, y))
    x2 = max(0, min(image_width, x + width))
    y2 = max(0, min(image_height, y + height))
    if x2 <= x1 or y2 <= y1:
        return None
    return x1, y1, x2 - x1, y2 - y1


def polygon_area(points):
    if not points or len(points) < 3:
        return 0.0
    total = 0.0
    for index, point in enumerate(points):
        other = points[(index + 1) % len(points)]
        total += point[0] * other[1] - point[1] * other[0]
    return abs(total) * 0.5


def order_quad(points):
    if points is None or len(points) != 4:
        return None
    values = [(float(point[0]), float(point[1])) for point in points]
    cx = sum(point[0] for point in values) * 0.25
    cy = sum(point[1] for point in values) * 0.25
    values.sort(key=lambda point: math.atan2(point[1] - cy, point[0] - cx))
    first = min(range(4), key=lambda index: values[index][0] + values[index][1])
    values = values[first:] + values[:first]
    if values[1][0] < values[-1][0]:
        values = [values[0], values[3], values[2], values[1]]
    return values


def _solve_linear(matrix, values):
    """Solve a small dense linear system using pivoted Gauss-Jordan."""
    size = len(values)
    work = [list(matrix[row]) + [float(values[row])] for row in range(size)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(work[row][column]))
        if abs(work[pivot][column]) < 1e-10:
            return None
        work[column], work[pivot] = work[pivot], work[column]
        factor = work[column][column]
        work[column] = [item / factor for item in work[column]]
        for row in range(size):
            if row == column:
                continue
            factor = work[row][column]
            if abs(factor) < 1e-14:
                continue
            work[row] = [
                work[row][index] - factor * work[column][index]
                for index in range(size + 1)
            ]
    return [work[index][-1] for index in range(size)]


def homography_from_points(source, destination):
    """Return a row-major 3x3 homography mapping four source points."""
    if len(source) != 4 or len(destination) != 4:
        return None
    matrix = []
    values = []
    for src, dst in zip(source, destination):
        x, y = float(src[0]), float(src[1])
        u, v = float(dst[0]), float(dst[1])
        matrix.append([x, y, 1.0, 0.0, 0.0, 0.0, -u * x, -u * y])
        values.append(u)
        matrix.append([0.0, 0.0, 0.0, x, y, 1.0, -v * x, -v * y])
        values.append(v)
    solved = _solve_linear(matrix, values)
    if solved is None:
        return None
    return solved + [1.0]


def project_point(matrix, point):
    x, y = float(point[0]), float(point[1])
    denominator = matrix[6] * x + matrix[7] * y + matrix[8]
    if abs(denominator) < 1e-10:
        return None
    return (
        (matrix[0] * x + matrix[1] * y + matrix[2]) / denominator,
        (matrix[3] * x + matrix[4] * y + matrix[5]) / denominator,
    )


class PerspectiveNormalizer:
    def __init__(self, matrix=None, size=1000.0):
        self.matrix = matrix
        self.size = float(size)

    @classmethod
    def from_quad(cls, corners, size=1000.0):
        ordered = order_quad(corners)
        if ordered is None or polygon_area(ordered) < 4.0:
            return None
        destination = (
            (0.0, 0.0),
            (float(size), 0.0),
            (float(size), float(size)),
            (0.0, float(size)),
        )
        matrix = homography_from_points(ordered, destination)
        if matrix is None:
            return None
        return cls(matrix, size)

    def point(self, point):
        if self.matrix is None:
            return float(point[0]), float(point[1])
        return project_point(self.matrix, point)

    def ellipse(self, ellipse):
        """Map ellipse center/axis endpoints and return canonical radii."""
        cx, cy, major, minor, angle = ellipse
        radius_major = max(0.5, float(major) * 0.5)
        radius_minor = max(0.5, float(minor) * 0.5)
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        center = self.point((cx, cy))
        major_point = self.point(
            (cx + radius_major * cos_a, cy + radius_major * sin_a)
        )
        minor_point = self.point(
            (cx - radius_minor * sin_a, cy + radius_minor * cos_a)
        )
        if center is None or major_point is None or minor_point is None:
            return None
        radius_a = math.hypot(major_point[0] - center[0], major_point[1] - center[1])
        radius_b = math.hypot(minor_point[0] - center[0], minor_point[1] - center[1])
        return center, max(0.5, radius_a), max(0.5, radius_b)


def ellipse_normalized_distance(point, ellipse):
    """Distance from an image point in units of the fitted ellipse radius."""
    cx, cy, major, minor, angle = ellipse
    radius_major = max(0.5, float(major) * 0.5)
    radius_minor = max(0.5, float(minor) * 0.5)
    dx = float(point[0]) - cx
    dy = float(point[1]) - cy
    cos_a = math.cos(angle)
    sin_a = math.sin(angle)
    local_x = dx * cos_a + dy * sin_a
    local_y = -dx * sin_a + dy * cos_a
    return math.sqrt(
        (local_x / radius_major) ** 2 + (local_y / radius_minor) ** 2
    )


def ring_score_for_normalized_distance(distance):
    """Map bullseye-radius distance to target score, with 10 inside red."""
    value = max(0.0, float(distance))
    if value <= 1.0:
        return 10
    return max(0, 11 - int(math.ceil(value - 1e-9)))


class TargetObservation:
    def __init__(self):
        self.detected = False
        self.locked = False
        self.control_valid = False
        self.center = None
        self.ellipse = None
        self.target_box = None
        self.quad = None
        self.geometry = "none"
        self.quality = 0.0
        self.score = -1
        self.move_epoch = 0
        self.timestamp_ms = 0
        self.blob_rect = None
        self.normalizer = None
        self.canonical_center = None
        self.canonical_radii = None

    def score_at(self, point):
        if not self.locked or point is None or self.ellipse is None:
            return -1
        if self.normalizer is not None and self.canonical_center is not None:
            projected = self.normalizer.point(point)
            if projected is None or self.canonical_radii is None:
                return -1
            radius = sum(self.canonical_radii) * 0.5
            distance = math.hypot(
                projected[0] - self.canonical_center[0],
                projected[1] - self.canonical_center[1],
            ) / max(0.5, radius)
        else:
            distance = ellipse_normalized_distance(point, self.ellipse)
        return ring_score_for_normalized_distance(distance)


class EllipseLockTracker:
    """Require stable current-frame candidates before emitting control data."""

    def __init__(self, stable_frames=3, max_jump=0.20):
        self.stable_frames = max(1, int(stable_frames))
        self.max_jump = max(0.01, float(max_jump))
        self.last_key = None
        self.stable_count = 0
        self.locked = False

    def reset(self):
        self.last_key = None
        self.stable_count = 0
        self.locked = False

    def update(self, key):
        if key is None:
            self.stable_count = 0
            self.locked = False
            return False
        key = (float(key[0]), float(key[1]))
        if self.last_key is None:
            self.stable_count = 1
        else:
            jump = math.hypot(key[0] - self.last_key[0], key[1] - self.last_key[1])
            self.stable_count = self.stable_count + 1 if jump <= self.max_jump else 1
        self.last_key = key
        self.locked = self.stable_count >= self.stable_frames
        return self.locked


class TargetMovementDetector:
    """Confirm a physical target move while the vehicle is stationary."""

    def __init__(
        self,
        center_ratio=0.12,
        scale_ratio=0.12,
        stable_frames=3,
        stable_tolerance=0.06,
    ):
        self.center_ratio = float(center_ratio)
        self.scale_ratio = float(scale_ratio)
        self.stable_frames = max(1, int(stable_frames))
        self.stable_tolerance = float(stable_tolerance)
        self.baseline = None
        self.changed_last = None
        self.changed_count = 0
        self.epoch = 0

    @staticmethod
    def _metrics(observation):
        if (
            observation is None or observation.ellipse is None or
            not observation.locked
        ):
            return None
        cx, cy, major, minor, _angle = observation.ellipse
        scale = math.sqrt(max(1.0, float(major) * float(minor)))
        return float(cx), float(cy), scale

    def arm(self, observation):
        self.baseline = self._metrics(observation)
        self.changed_last = None
        self.changed_count = 0
        return self.baseline is not None

    def disarm(self):
        self.baseline = None
        self.changed_last = None
        self.changed_count = 0

    def update(self, observation):
        current = self._metrics(observation)
        if current is None:
            self.changed_count = 0
            return False
        if self.baseline is None:
            self.arm(observation)
            return False

        bx, by, base_scale = self.baseline
        cx, cy, scale = current
        center_change = math.hypot(cx - bx, cy - by) / max(1.0, base_scale)
        scale_change = abs(scale / max(1.0, base_scale) - 1.0)
        if center_change < self.center_ratio and scale_change < self.scale_ratio:
            self.changed_count = 0
            self.changed_last = None
            return False

        if self.changed_last is None:
            self.changed_count = 1
        else:
            lx, ly, last_scale = self.changed_last
            jitter = math.hypot(cx - lx, cy - ly) / max(1.0, scale)
            scale_jitter = abs(scale / max(1.0, last_scale) - 1.0)
            if jitter <= self.stable_tolerance and scale_jitter <= self.stable_tolerance:
                self.changed_count += 1
            else:
                self.changed_count = 1
        self.changed_last = current
        if self.changed_count < self.stable_frames:
            return False

        self.epoch += 1
        self.baseline = current
        self.changed_last = None
        self.changed_count = 0
        return True


class AimReadyGate:
    def __init__(self, stable_ms=300):
        self.stable_ms = max(0, int(stable_ms))
        self.since_ms = None
        self.ready = False

    def reset(self):
        self.since_ms = None
        self.ready = False

    def update(self, condition, now_ms):
        if not condition:
            self.reset()
            return False
        if self.since_ms is None:
            self.since_ms = int(now_ms)
        self.ready = int(now_ms) - self.since_ms >= self.stable_ms
        return self.ready


class ShotScoreAccumulator:
    def __init__(self):
        self.samples = []

    def reset(self):
        self.samples = []

    def add(self, score):
        score = int(score)
        if 0 <= score <= 10:
            self.samples.append(score)

    def result(self):
        if not self.samples:
            return -1
        counts = {}
        for value in self.samples:
            counts[value] = counts.get(value, 0) + 1
        best_count = max(counts.values())
        modes = [value for value, count in counts.items() if count == best_count]
        if len(modes) == 1:
            return modes[0]
        ordered = sorted(self.samples)
        return ordered[(len(ordered) - 1) // 2]


class ShotScoreSession:
    """Lock two dwell results using MCU shot_index=completed shot count."""

    def __init__(self):
        self.accumulators = [ShotScoreAccumulator(), ShotScoreAccumulator()]
        self.results = [-1, -1]
        self.active_shot = 0
        self.completed = 0
        self.previous_laser = False
        self.dwell_started_ms = 0

    def reset(self):
        for accumulator in self.accumulators:
            accumulator.reset()
        self.results = [-1, -1]
        self.active_shot = 0
        self.completed = 0
        self.previous_laser = False
        self.dwell_started_ms = 0

    def update(self, laser, completed_count, score, now_ms):
        laser = bool(laser)
        slot = max(0, min(1, int(completed_count)))
        finished_now = False
        if laser:
            if not self.previous_laser or slot != self.active_shot:
                self.active_shot = slot
                self.accumulators[slot].reset()
                self.dwell_started_ms = int(now_ms)
            self.accumulators[self.active_shot].add(score)
        elif self.previous_laser:
            self.results[self.active_shot] = self.accumulators[self.active_shot].result()
            self.completed = max(self.completed, self.active_shot + 1)
            self.dwell_started_ms = 0
            finished_now = True
        self.previous_laser = laser
        return finished_now

    def dwell_ms(self, now_ms):
        if not self.previous_laser or not self.dwell_started_ms:
            return 0
        return max(0, int(now_ms) - self.dwell_started_ms)


def _value(obj, name, default=None):
    try:
        value = getattr(obj, name)
        return value() if callable(value) else value
    except Exception:
        return default


def _box_tuple(obj):
    x = _value(obj, "x", 0)
    y = _value(obj, "y", 0)
    w = _value(obj, "w", 0)
    h = _value(obj, "h", 0)
    return int(x), int(y), int(w), int(h)


def _image_size(img):
    width = _value(img, "width", None)
    height = _value(img, "height", None)
    if width is None or height is None:
        return None
    return max(1, int(width)), max(1, int(height))


def _blob_tuple(blob):
    try:
        x, y, w, h = int(blob[0]), int(blob[1]), int(blob[2]), int(blob[3])
    except Exception:
        x, y, w, h = _box_tuple(blob)
    cx = _value(blob, "cx", x + w * 0.5)
    cy = _value(blob, "cy", y + h * 0.5)
    pixels = _value(blob, "pixels", w * h)
    angle = _value(blob, "rotation_rad", None)
    if angle is None:
        angle = _value(blob, "rotation", 0.0)
        if abs(float(angle)) > math.pi * 2.0:
            angle = math.radians(float(angle))
    return (
        int(x), int(y), int(w), int(h), float(cx), float(cy),
        max(0.0, float(pixels)), float(angle),
    )


class TargetRecognizer:
    """Maix frame adapter around the pure geometry and lock state."""

    def __init__(self, config):
        self.config = config
        self.tracker = EllipseLockTracker(config.lock_frames, config.lock_max_jump)
        self.movement = TargetMovementDetector()
        self.move_monitor_enabled = False
        self.last_observation = TargetObservation()
        self.smoothed_center = None
        self.frame_index = 0
        self.quad_cache = None
        self.quad_normalizer_cache = None
        self.quad_cache_box = None
        self.quad_cache_frame = -1
        self.quad_last_scan_frame = -1000000

    def reset_session(self):
        self.tracker.reset()
        self.movement = TargetMovementDetector()
        self.move_monitor_enabled = False
        self.last_observation = TargetObservation()
        self.smoothed_center = None
        self.frame_index = 0
        self.quad_cache = None
        self.quad_normalizer_cache = None
        self.quad_cache_box = None
        self.quad_cache_frame = -1
        self.quad_last_scan_frame = -1000000

    def _best_detection(self, detections):
        if not detections:
            return None
        best = None
        best_rank = -1.0
        for index, item in enumerate(detections):
            if index >= self.config.max_detection_candidates:
                break
            box = _box_tuple(item)
            rank = float(_value(item, "score", 0.0)) * max(1, box[2] * box[3])
            if rank > best_rank:
                best = item
                best_rank = rank
        return best

    def _boxes_related(self, previous, current):
        if previous is None or current is None:
            return False
        px, py, pw, ph = previous
        cx, cy, cw, ch = current
        if min(pw, ph, cw, ch) <= 0:
            return False
        previous_center = (px + pw * 0.5, py + ph * 0.5)
        current_center = (cx + cw * 0.5, cy + ch * 0.5)
        center_delta = math.hypot(
            previous_center[0] - current_center[0],
            previous_center[1] - current_center[1],
        ) / max(1.0, math.hypot(pw, ph))
        scale_delta = max(
            abs(float(cw) / pw - 1.0),
            abs(float(ch) / ph - 1.0),
        )
        return (
            center_delta <= self.config.geometry_box_center_tolerance and
            scale_delta <= self.config.geometry_box_scale_tolerance
        )

    def _clear_quad_cache(self):
        self.quad_cache = None
        self.quad_normalizer_cache = None
        self.quad_cache_box = None
        self.quad_cache_frame = -1

    def _target_geometry(self, img, detection, clipped_box=None):
        if not self.config.enable_quad_scan:
            self._clear_quad_cache()
            return None, None
        box = clipped_box or clip_box_to_image(_box_tuple(detection), _image_size(img))
        if box is None:
            self._clear_quad_cache()
            return None, None
        x, y, w, h = box
        if w < 8 or h < 8:
            self._clear_quad_cache()
            return None, None

        current_box = (x, y, w, h)
        cache_fresh = (
            self.quad_cache is not None and
            self.frame_index - self.quad_cache_frame <=
            self.config.geometry_cache_max_frames
        )
        if cache_fresh and not self._boxes_related(self.quad_cache_box, current_box):
            self._clear_quad_cache()
            cache_fresh = False
            # A target jump is a new association and must not inherit the old
            # scan schedule delay.
            self.quad_last_scan_frame = -1000000
        elif not cache_fresh and self.quad_cache is not None:
            self._clear_quad_cache()

        scan_due = (
            self.frame_index - self.quad_last_scan_frame >=
            max(1, int(self.config.geometry_scan_period_frames))
        )
        if not scan_due:
            if cache_fresh:
                return self.quad_cache, self.quad_normalizer_cache
            return None, None

        self.quad_last_scan_frame = self.frame_index
        roi = bounded_center_roi(
            current_box,
            1.0,
            self.config.geometry_max_roi_width,
            self.config.geometry_max_roi_height,
            _image_size(img),
        )
        try:
            rects = img.find_rects(roi=roi, threshold=2000)
        except Exception:
            if cache_fresh:
                return self.quad_cache, self.quad_normalizer_cache
            return None, None
        best = None
        best_area = 0.0
        for index, rect in enumerate(rects or ()):
            if index >= self.config.geometry_max_rect_candidates:
                break
            corners = order_quad(_value(rect, "corners", None))
            area = polygon_area(corners)
            if area > best_area and area >= roi[2] * roi[3] * 0.20:
                best = corners
                best_area = area

        if best is not None:
            normalizer = PerspectiveNormalizer.from_quad(best)
            if normalizer is not None:
                self.quad_cache = best
                self.quad_normalizer_cache = normalizer
                self.quad_cache_box = current_box
                self.quad_cache_frame = self.frame_index
                return best, normalizer
        if cache_fresh:
            return self.quad_cache, self.quad_normalizer_cache
        return None, None

    def _red_candidate(self, img, detection, quad, normalizer=None, clipped_box=None):
        box = clipped_box or clip_box_to_image(_box_tuple(detection), _image_size(img))
        if box is None:
            return None
        x, y, w, h = box
        roi = bounded_center_roi(
            (x, y, w, h),
            self.config.red_search_ratio,
            self.config.red_max_roi_width,
            self.config.red_max_roi_height,
            _image_size(img),
        )
        roi_area = roi[2] * roi[3]
        dynamic_pixels = max(self.config.red_min_pixels, roi_area // 1500)
        dynamic_area = max(self.config.red_min_area, roi_area // 1200)
        try:
            blobs = img.find_blobs(
                self.config.red_thresholds,
                roi=roi,
                pixels_threshold=dynamic_pixels,
                area_threshold=dynamic_area,
                merge=True,
                margin=3,
                x_stride=2,
                y_stride=2,
            )
        except Exception:
            return None

        if normalizer is None and quad:
            normalizer = PerspectiveNormalizer.from_quad(quad)
        association_key = (
            self.tracker.last_key if self.tracker.stable_count > 0 else None
        )
        best = None
        best_rank = -1e9
        for index, blob in enumerate(blobs or ()):
            if index >= self.config.red_max_blob_candidates:
                break
            bx, by, bw, bh, cx, cy, pixels, angle = _blob_tuple(blob)
            if bw < 2 or bh < 2:
                continue
            major = float(max(bw, bh))
            minor = float(min(bw, bh))
            if bh > bw:
                angle += math.pi * 0.5
            axis_ratio = minor / max(1.0, major)
            fill = pixels / max(1.0, math.pi * major * minor * 0.25)
            if axis_ratio < self.config.red_min_axis_ratio:
                continue
            if fill < self.config.red_min_fill_ratio:
                continue
            ellipse = (cx, cy, major, minor, angle)
            canonical = normalizer.ellipse(ellipse) if normalizer else None
            if canonical:
                center, radius_a, radius_b = canonical
                key = (center[0] / normalizer.size, center[1] / normalizer.size)
                canonical_roundness = min(radius_a, radius_b) / max(radius_a, radius_b)
                center_offset = math.hypot(key[0] - 0.5, key[1] - 0.5)
                rank_offset = center_offset
                if association_key is not None:
                    rank_offset = math.hypot(
                        key[0] - association_key[0],
                        key[1] - association_key[1],
                    )
                rank = fill + canonical_roundness - rank_offset + pixels / max(1.0, w * h)
                quality = clamp(
                    0.35 + 0.25 * min(1.0, fill) +
                    0.25 * canonical_roundness + 0.15 * max(0.0, 1.0 - center_offset),
                    0.0,
                    1.0,
                )
            else:
                center = None
                radius_a = radius_b = None
                key = ((cx - x) / max(1.0, w), (cy - y) / max(1.0, h))
                center_offset = math.hypot(key[0] - 0.5, key[1] - 0.5)
                rank_offset = center_offset
                if association_key is not None:
                    rank_offset = math.hypot(
                        key[0] - association_key[0],
                        key[1] - association_key[1],
                    )
                rank = fill + axis_ratio - rank_offset + pixels / max(1.0, w * h)
                quality = clamp(
                    0.20 + 0.35 * min(1.0, fill) + 0.30 * axis_ratio +
                    0.15 * max(0.0, 1.0 - center_offset),
                    0.0,
                    0.85,
                )
            if rank > best_rank:
                best_rank = rank
                best = {
                    "ellipse": ellipse,
                    "rect": (bx, by, bw, bh),
                    "key": key,
                    "quality": quality,
                    "normalizer": normalizer,
                    "canonical_center": center,
                    "canonical_radii": None if radius_a is None else (radius_a, radius_b),
                }
        return best

    def update(self, img, detections, aim_point=None, now_ms=0, movement_enabled=False):
        self.frame_index += 1
        observation = TargetObservation()
        observation.timestamp_ms = int(now_ms)
        observation.move_epoch = self.movement.epoch
        detection = self._best_detection(detections)
        if detection is None:
            self.tracker.update(None)
            self.smoothed_center = None
            if movement_enabled:
                self.movement.update(None)
            self.last_observation = observation
            return observation

        clipped_box = clip_box_to_image(_box_tuple(detection), _image_size(img))
        if clipped_box is None or clipped_box[2] < 2 or clipped_box[3] < 2:
            self.tracker.update(None)
            self.smoothed_center = None
            self.last_observation = observation
            return observation
        observation.target_box = clipped_box
        quad, normalizer = self._target_geometry(img, detection, clipped_box)
        candidate = self._red_candidate(
            img, detection, quad, normalizer, clipped_box,
        )
        if candidate is None:
            self.tracker.update(None)
            self.smoothed_center = None
            self.last_observation = observation
            return observation

        observation.detected = True
        observation.quad = quad
        observation.ellipse = candidate["ellipse"]
        raw_center = (
            float(candidate["ellipse"][0]),
            float(candidate["ellipse"][1]),
        )
        observation.blob_rect = candidate["rect"]
        observation.normalizer = candidate["normalizer"]
        observation.canonical_center = candidate["canonical_center"]
        observation.canonical_radii = candidate["canonical_radii"]
        observation.geometry = "quad" if quad else "ellipse"
        observation.quality = candidate["quality"]
        observation.locked = self.tracker.update(candidate["key"])
        if self.smoothed_center is None or self.tracker.stable_count <= 1:
            self.smoothed_center = raw_center
        else:
            alpha = clamp(self.config.target_center_alpha, 0.01, 1.0)
            self.smoothed_center = (
                self.smoothed_center[0] * (1.0 - alpha) + raw_center[0] * alpha,
                self.smoothed_center[1] * (1.0 - alpha) + raw_center[1] * alpha,
            )
        observation.center = (
            int(round(self.smoothed_center[0])),
            int(round(self.smoothed_center[1])),
        )
        observation.control_valid = (
            observation.locked and
            observation.quality >= self.config.geometry_min_quality
        )
        if aim_point is not None:
            observation.score = observation.score_at(aim_point)

        if movement_enabled:
            if not self.move_monitor_enabled:
                self.movement.arm(observation)
            self.movement.update(observation)
        elif self.move_monitor_enabled:
            self.movement.disarm()
        self.move_monitor_enabled = bool(movement_enabled)
        observation.move_epoch = self.movement.epoch
        self.last_observation = observation
        return observation
