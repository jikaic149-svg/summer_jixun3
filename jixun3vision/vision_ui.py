"""Three-page competition UI for MaixCAM2."""

import math


PAGE_MAIN = "MAIN"
PAGE_CONTROL = "CONTROL"
PAGE_RESULT = "RESULT"
CONTROL_PREPARE = "PREPARE"
CONTROL_RUNNING = "RUNNING"
_ELLIPSE_UNIT_POINTS = tuple(
    (
        math.cos(step * math.pi * 2.0 / 24.0),
        math.sin(step * math.pi * 2.0 / 24.0),
    )
    for step in range(25)
)


def point_in_rect(x, y, rect):
    return rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]


class PressReleaseLatch:
    def __init__(self):
        self.was_pressed = False
        self.armed = None

    def update(self, pressed, button):
        clicked = None
        if pressed and not self.was_pressed:
            self.armed = button
        elif not pressed and self.was_pressed:
            if self.armed is not None and self.armed == button:
                clicked = self.armed
            self.armed = None
        self.was_pressed = bool(pressed)
        return clicked


class VisionUiController:
    def __init__(self):
        self.page = PAGE_MAIN
        self.control_state = CONTROL_PREPARE
        self.settings_open = False
        self.settings_laser_requested = False
        self.setting_index = 0
        self.selected_task = 0
        self.notice = ""
        self.completed = False
        self.stopped = False
        self.parked = False
        self.shot_scores = [-1, -1]
        self.final_milestones = 0
        self.final_error = ""

    def select_task(self, task_id):
        task_id = int(task_id)
        if task_id < 1 or task_id > 5:
            return False
        self.selected_task = task_id
        self.page = PAGE_CONTROL
        self.control_state = CONTROL_PREPARE
        self.settings_open = False
        self.settings_laser_requested = False
        self.notice = ""
        self.completed = False
        self.stopped = False
        self.parked = False
        self.shot_scores = [-1, -1]
        self.final_milestones = 0
        self.final_error = ""
        return True

    def can_start(self, calibrated, locked, uart_ready):
        del locked
        if not uart_ready:
            return False, "UART REQUIRED"
        if self.selected_task >= 2 and not calibrated:
            return False, "CAL REQUIRED"
        return True, ""

    def start_running(self, calibrated, locked, uart_ready):
        ready, reason = self.can_start(calibrated, locked, uart_ready)
        self.notice = reason
        if not ready:
            return False
        self.control_state = CONTROL_RUNNING
        return True

    def finish(self, completed, parked, shot_scores, milestones, error=""):
        self.completed = bool(completed)
        self.stopped = not self.completed
        self.parked = bool(parked)
        self.shot_scores = list(shot_scores[:2])
        while len(self.shot_scores) < 2:
            self.shot_scores.append(-1)
        self.final_milestones = int(milestones)
        self.final_error = str(error or "")
        self.page = PAGE_RESULT

    def home(self):
        self.page = PAGE_MAIN
        self.control_state = CONTROL_PREPARE
        self.settings_open = False
        self.settings_laser_requested = False
        self.notice = ""

    def restart(self):
        if self.selected_task:
            task_id = self.selected_task
            self.select_task(task_id)

    @staticmethod
    def main_rects(width, height):
        margin = 8
        gap = 8
        top = 38
        bottom = height - 8
        cell_w = (width - 2 * margin - gap) // 2
        cell_h = (bottom - top - 2 * gap) // 3
        names = ("task1", "task2", "task3", "task4", "task5", "set")
        rects = {}
        for index, name in enumerate(names):
            column = index % 2
            row = index // 2
            rects[name] = (
                margin + column * (cell_w + gap),
                top + row * (cell_h + gap),
                cell_w,
                cell_h,
            )
        return rects

    @staticmethod
    def setting_rects(width, height):
        margin = 8
        gap = 8
        button_w = (width - margin * 2 - gap * 3) // 4
        y = height - 48
        return {
            "item": (margin, y, button_w, 38),
            "minus": (margin + button_w + gap, y, button_w, 38),
            "plus": (margin + 2 * (button_w + gap), y, button_w, 38),
            "back": (margin + 3 * (button_w + gap), y, button_w, 38),
        }

    @staticmethod
    def setting_calibration_rects(width, height):
        """Fixed CAL/RST row, separate from ITEM/-/+/BACK controls."""
        margin = 8
        gap = 8
        button_width = (width - 2 * margin - gap) // 2
        y = height - 90
        return {
            "set_cal": (margin, y, button_width, 34),
            "set_rst": (margin + button_width + gap, y, button_width, 34),
        }

    @staticmethod
    def prepare_rects(width, height):
        margin = 5
        gap = 5
        button_w = (width - 2 * margin - 3 * gap) // 4
        y = height - 39
        names = ("cal", "rst", "home", "start")
        return {
            name: (margin + index * (button_w + gap), y, button_w, 34)
            for index, name in enumerate(names)
        }

    @staticmethod
    def running_rects(width, height):
        return {"stop": (width - 66, height - 39, 61, 34)}

    @staticmethod
    def result_rects(width, height):
        return {
            "home": (10, height - 48, (width - 30) // 2, 38),
            "restart": (20 + (width - 30) // 2, height - 48, (width - 30) // 2, 38),
        }

    def button_at(self, x, y, width, height):
        if x < 0 or y < 0:
            return None
        if self.page == PAGE_MAIN:
            if self.settings_open:
                rects = self.setting_rects(width, height).copy()
                rects.update(self.setting_calibration_rects(width, height))
            else:
                rects = self.main_rects(width, height)
        elif self.page == PAGE_CONTROL:
            rects = self.prepare_rects(width, height) if self.control_state == CONTROL_PREPARE else self.running_rects(width, height)
        else:
            rects = self.result_rects(width, height)
        for name, rect in rects.items():
            if point_in_rect(x, y, rect):
                return name
        return None


def _color(image_module, name, rgb):
    value = getattr(image_module, name, None)
    if value is not None:
        return value
    return image_module.Color.from_rgb(*rgb)


def _fit_text(text, width, pixels_per_char=8):
    count = max(1, int(width) // pixels_per_char)
    text = str(text)
    if len(text) <= count:
        return text
    return text[:max(1, count - 1)] + ">"


def _draw_centered(img, rect, text, color, scale=1.0):
    x, y, width, height = rect
    text = _fit_text(text, width - 4, max(1, int(8 * scale)))
    text_width = int(len(text) * 8 * scale)
    text_height = int(16 * scale)
    img.draw_string(
        x + max(2, (width - text_width) // 2),
        y + max(2, (height - text_height) // 2),
        text,
        color=color,
        scale=scale,
    )


def _draw_button(img, rect, label, image_module, armed=False, primary=False):
    black = _color(image_module, "COLOR_BLACK", (0, 0, 0))
    white = _color(image_module, "COLOR_WHITE", (255, 255, 255))
    fill = white if armed else black
    text = black if armed else white
    img.draw_rect(*rect, color=fill, thickness=-1)
    img.draw_rect(*rect, color=white, thickness=3 if primary or armed else 1)
    _draw_centered(img, rect, label, text)


def settings_preview_frame(frame, controller, config, copy_frame=True):
    """Return a display-only SET preview without mutating recognition input."""
    if not controller.settings_open or controller.setting_index != 1:
        return frame
    preview = frame
    if copy_frame:
        try:
            preview = frame.copy()
        except Exception:
            return frame
    try:
        converted = preview.binary(config.red_thresholds)
        if converted is not None:
            preview = converted
    except Exception:
        # Keep the copied color frame on runtimes without binary preview support.
        pass
    return preview


def draw_target_overlay(img, observation, calibration, image_module):
    if observation is not None and observation.target_box is not None:
        yellow = _color(image_module, "COLOR_YELLOW", (255, 220, 0))
        img.draw_rect(*observation.target_box, color=yellow, thickness=1)
    if observation is not None and observation.quad:
        cyan = _color(image_module, "COLOR_CYAN", (0, 220, 220))
        for index, point in enumerate(observation.quad):
            other = observation.quad[(index + 1) % 4]
            img.draw_line(
                int(point[0]), int(point[1]), int(other[0]), int(other[1]),
                color=cyan, thickness=1,
            )
    if observation is not None and observation.ellipse is not None:
        red = _color(image_module, "COLOR_RED", (255, 40, 40))
        green = _color(image_module, "COLOR_GREEN", (40, 220, 80))
        yellow = _color(image_module, "COLOR_YELLOW", (255, 220, 0))
        cx, cy, major, minor, angle = observation.ellipse
        radius_a = major * 0.5
        radius_b = minor * 0.5
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        last = None
        for unit_x, unit_y in _ELLIPSE_UNIT_POINTS:
            local_x = radius_a * unit_x
            local_y = radius_b * unit_y
            point = (
                int(cx + local_x * cos_a - local_y * sin_a),
                int(cy + local_x * sin_a + local_y * cos_a),
            )
            if last is not None:
                img.draw_line(last[0], last[1], point[0], point[1], color=red, thickness=2)
            last = point
        marker = green if observation.locked else yellow
        img.draw_cross(int(cx), int(cy), color=marker, size=8, thickness=2)
    blue = _color(image_module, "COLOR_BLUE", (40, 140, 255))
    img.draw_cross(
        calibration.reference_x, calibration.reference_y,
        color=blue, size=10, thickness=2,
    )


def _draw_header(img, title, uart_ok, udp_ok, image_module):
    black = _color(image_module, "COLOR_BLACK", (0, 0, 0))
    white = _color(image_module, "COLOR_WHITE", (255, 255, 255))
    green = _color(image_module, "COLOR_GREEN", (40, 220, 80))
    red = _color(image_module, "COLOR_RED", (255, 40, 40))
    width = img.width()
    img.draw_rect(0, 0, width, 30, color=black, thickness=-1)
    img.draw_string(6, 7, _fit_text(title, width - 120), color=white)
    img.draw_circle(width - 88, 15, 5, color=green if uart_ok else red, thickness=-1)
    img.draw_string(width - 79, 7, "U", color=white)
    img.draw_circle(width - 42, 15, 5, color=green if udp_ok else red, thickness=-1)
    img.draw_string(width - 33, 7, "D", color=white)


def draw_main(img, controller, config, uart_ok, udp_ok, image_module, armed=None):
    black = _color(image_module, "COLOR_BLACK", (0, 0, 0))
    white = _color(image_module, "COLOR_WHITE", (255, 255, 255))
    if not controller.settings_open:
        img.draw_rect(0, 0, img.width(), img.height(), color=black, thickness=-1)
        _draw_header(img, "TASK SELECT", uart_ok, udp_ok, image_module)
        labels = {
            "task1": "TASK 1", "task2": "TASK 2", "task3": "TASK 3",
            "task4": "TASK 4", "task5": "TASK 5", "set": "SET",
        }
        for name, rect in controller.main_rects(img.width(), img.height()).items():
            _draw_button(img, rect, labels[name], image_module, armed == name, name == "task5")
        return

    name = config.setting_names()[controller.setting_index]
    value = config.setting_value(controller.setting_index)
    _draw_header(
        img, "SET %d/3" % (controller.setting_index + 1),
        uart_ok, udp_ok, image_module,
    )
    img.draw_rect(0, 30, img.width(), 38, color=black, thickness=-1)
    _draw_centered(
        img, (6, 32, img.width() - 12, 32),
        "%s: %s" % (name, value), white,
    )
    if controller.notice:
        img.draw_rect(0, 68, img.width(), 20, color=black, thickness=-1)
        _draw_centered(
            img, (4, 69, img.width() - 8, 18),
            controller.notice, white,
        )
    cal_labels = {"set_cal": "CAL", "set_rst": "RST"}
    for button, rect in controller.setting_calibration_rects(
        img.width(), img.height(),
    ).items():
        _draw_button(
            img, rect, cal_labels[button], image_module,
            armed == button, button == "set_cal",
        )
    labels = {"item": "ITEM", "minus": "-", "plus": "+", "back": "BACK"}
    for button, rect in controller.setting_rects(img.width(), img.height()).items():
        _draw_button(img, rect, labels[button], image_module, armed == button)


def draw_control(
    img, controller, observation, calibration, task_status, dx, dy,
    score, shot_count, dwell_ms, uart_ok, udp_ok, image_module, armed=None,
):
    phase = task_status.phase if task_status.valid else controller.control_state
    _draw_header(
        img, "T%d %s" % (controller.selected_task, phase),
        uart_ok, udp_ok, image_module,
    )
    draw_target_overlay(img, observation, calibration, image_module)
    black = _color(image_module, "COLOR_BLACK", (0, 0, 0))
    white = _color(image_module, "COLOR_WHITE", (255, 255, 255))
    yellow = _color(image_module, "COLOR_YELLOW", (255, 220, 0))
    if controller.control_state == CONTROL_PREPARE:
        state = "LOCK" if observation.locked else "LOST"
        if calibration.pending:
            current, total = calibration.progress
            status = "CAL %d/%d  %s" % (current, total, state)
        else:
            status = "%s  CAL:%s" % (state, "OK" if calibration.calibrated else "NO")
        img.draw_rect(0, 30, img.width(), 20, color=black, thickness=-1)
        _draw_centered(img, (0, 31, img.width(), 18), controller.notice or status, yellow)
        labels = {"cal": "CAL", "rst": "RST", "home": "HOME", "start": "START"}
        for name, rect in controller.prepare_rects(img.width(), img.height()).items():
            _draw_button(img, rect, labels[name], image_module, armed == name, name == "start")
        return

    footer_y = img.height() - 39
    img.draw_rect(0, footer_y, img.width() - 71, 34, color=black, thickness=-1)
    state = "LOCK" if observation.locked else "LOST"
    line1 = "%s d:%+d/%+d S:%s N:%d" % (
        state, int(dx), int(dy), "?" if score < 0 else str(score), int(shot_count),
    )
    img.draw_string(4, footer_y + 3, _fit_text(line1, img.width() - 78), color=white)
    progress = max(0, min(3000, int(dwell_ms)))
    bar_width = img.width() - 80
    img.draw_rect(4, footer_y + 23, bar_width, 5, color=white, thickness=1)
    if progress:
        img.draw_rect(4, footer_y + 23, int(bar_width * progress / 3000.0), 5, color=yellow, thickness=-1)
    rect = controller.running_rects(img.width(), img.height())["stop"]
    _draw_button(img, rect, "STOP", image_module, armed == "stop", True)


def draw_result(img, controller, uart_ok, udp_ok, tx_count, rx_count, image_module, armed=None):
    black = _color(image_module, "COLOR_BLACK", (0, 0, 0))
    white = _color(image_module, "COLOR_WHITE", (255, 255, 255))
    green = _color(image_module, "COLOR_GREEN", (40, 220, 80))
    yellow = _color(image_module, "COLOR_YELLOW", (255, 220, 0))
    img.draw_rect(0, 0, img.width(), img.height(), color=black, thickness=-1)
    _draw_header(img, "RESULT T%d" % controller.selected_task, uart_ok, udp_ok, image_module)
    state = "COMPLETE" if controller.completed else "STOPPED"
    _draw_centered(img, (10, 40, img.width() - 20, 35), state, green if controller.completed else yellow, 1.3)
    score1 = "?" if controller.shot_scores[0] < 0 else str(controller.shot_scores[0])
    score2 = "?" if controller.shot_scores[1] < 0 else str(controller.shot_scores[1])
    img.draw_string(24, 84, "SHOT 1: %s   SHOT 2: %s" % (score1, score2), color=white)
    img.draw_string(24, 108, "PARK: %s" % ("OK" if controller.parked else "NO"), color=white)
    if controller.selected_task == 5:
        labels = ("TRACK", "SHOT1", "SHOT2", "RETURN")
        text = " ".join(
            "%s:%s" % (label, "Y" if controller.final_milestones & (1 << index) else "N")
            for index, label in enumerate(labels)
        )
        img.draw_string(10, 132, _fit_text(text, img.width() - 20), color=white)
    img.draw_string(24, 155, "UART TX/RX: %d/%d" % (tx_count, rx_count), color=white)
    if controller.final_error:
        img.draw_string(10, 177, _fit_text("ERR: " + controller.final_error, img.width() - 20), color=yellow)
    labels = {"home": "HOME", "restart": "RESTART"}
    for name, rect in controller.result_rects(img.width(), img.height()).items():
        _draw_button(img, rect, labels[name], image_module, armed == name, name == "restart")


def read_touch(ts, img, disp, image_module):
    try:
        screen_x, screen_y, pressed = ts.read()
        x, y = image_module.resize_map_pos_reverse(
            img.width(), img.height(), disp.width(), disp.height(),
            image_module.Fit.FIT_CONTAIN, screen_x, screen_y,
        )
        return (
            max(0, min(img.width() - 1, int(x))),
            max(0, min(img.height() - 1, int(y))),
            bool(pressed),
        )
    except Exception:
        return -1, -1, False
