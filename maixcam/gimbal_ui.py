"""320x240 mirror of the STM32 target UI for a MaixCam display."""

LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 240
DISTANCE_MIN_CM = 50
DISTANCE_MAX_CM = 100
YAW_TARGET_LIMIT_DEG = 50
PITCH_TARGET_LIMIT_DEG = 50
SCAN_ENABLED = False

PAGE_MAIN = "MAIN"
PAGE_DISTANCE = "DISTANCE"
PAGE_ANGLE = "ANGLE"

COMMAND_FIRE_DISTANCE = "FIRE_DISTANCE"
COMMAND_FIXED_TARGET = "FIXED_TARGET"
COMMAND_AUTO_SEARCH = "AUTO_SEARCH"
COMMAND_SWEEP = "SWEEP"
COMMAND_START = "START"
COMMAND_STOP = "STOP"
COMMAND_SET_DISTANCE = "SET_DISTANCE"
COMMAND_SET_YAW_TARGET = "SET_YAW_TARGET"
COMMAND_SET_PITCH_TARGET = "SET_PITCH_TARGET"

MAIN_RECTS = {
    "distance": (8, 34, 160, 42),
    "fire": (176, 34, 136, 42),
    "yaw": (112, 82, 96, 38),
    "pitch": (216, 82, 96, 38),
    "fixed": (8, 126, 96, 48),
    "auto": (112, 126, 96, 48),
    "sweep": (216, 126, 96, 48),
    "start": (8, 180, 148, 42),
    "stop": (164, 180, 148, 42),
}

KEY_RECTS = {
    "1": (8, 36, 44, 42), "2": (60, 36, 44, 42), "3": (112, 36, 44, 42),
    "4": (8, 81, 44, 42), "5": (60, 81, 44, 42), "6": (112, 81, 44, 42),
    "7": (8, 126, 44, 42), "8": (60, 126, 44, 42), "9": (112, 126, 44, 42),
    "clear": (8, 171, 44, 42), "0": (60, 171, 44, 42), "back": (112, 171, 44, 42),
    "confirm": (176, 128, 136, 42), "cancel": (176, 176, 136, 42),
}

STATUS_RECT = (8, 82, 96, 38)
AZIMUTH_RECT = (112, 82, 96, 38)
ELEVATION_RECT = (216, 82, 96, 38)
FOOTER_RECT = (8, 224, 304, 16)
DISTANCE_DISPLAY_RECT = (176, 36, 136, 48)
DISTANCE_RANGE_RECT = (176, 92, 136, 28)


def point_in_rect(x, y, rect):
    return rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]


class GimbalUi:
    def __init__(self):
        self.page = PAGE_MAIN
        self.armed = None
        self.was_pressed = False
        self.distance_cm = 75
        self.distance_input = ""
        self.input_started = False
        self.runtime = "IDLE"
        self.feedback = "等待云台连接"
        self.azimuth_degrees = 0
        self.elevation_degrees = 0
        self.yaw_target_degrees = 0
        self.pitch_target_degrees = 0
        self.angle_axis = "yaw"
        self.angle_negative = False
        self.link_online = False
        self.last_telemetry_ms = 0

    def _widget_at(self, x, y):
        rects = MAIN_RECTS if self.page == PAGE_MAIN else KEY_RECTS
        for name, rect in rects.items():
            if point_in_rect(x, y, rect):
                return name
        return None

    def touch(self, x, y, pressed):
        widget = self._widget_at(x, y)
        clicked = None
        if pressed and not self.was_pressed:
            self.armed = widget
        elif not pressed and self.was_pressed:
            if self.armed is not None and self.armed == widget:
                clicked = self.armed
            self.armed = None
        self.was_pressed = bool(pressed)
        return self._activate(clicked) if clicked is not None else None

    def _activate(self, widget):
        if self.page == PAGE_MAIN:
            if widget == "distance":
                self.page = PAGE_DISTANCE
                self.distance_input = str(self.distance_cm)
                self.input_started = False
                return None
            if widget in ("yaw", "pitch"):
                self.page = PAGE_ANGLE
                self.angle_axis = widget
                target = (self.yaw_target_degrees if widget == "yaw"
                          else self.pitch_target_degrees)
                self.angle_negative = target < 0
                self.distance_input = str(abs(target))
                self.input_started = False
                return None
            if widget == "sweep" and not SCAN_ENABLED:
                self.feedback = "SCAN OFF"
                return None
            commands = {
                "fire": COMMAND_FIRE_DISTANCE,
                "fixed": COMMAND_FIXED_TARGET,
                "auto": COMMAND_AUTO_SEARCH,
                "sweep": COMMAND_SWEEP,
                "start": COMMAND_START,
                "stop": COMMAND_STOP,
            }
            command = commands.get(widget)
            return (command, self.distance_cm) if command is not None else None

        if widget in "0123456789" and len(widget) == 1:
            if not self.input_started:
                self.distance_input = ""
                self.input_started = True
            if len(self.distance_input) < 3:
                self.distance_input += widget
        elif widget == "clear":
            if self.page == PAGE_ANGLE:
                self.angle_negative = not self.angle_negative
            else:
                self.distance_input = ""
                self.input_started = True
        elif widget == "back":
            self.distance_input = self.distance_input[:-1]
            self.input_started = True
        elif widget == "cancel":
            self.page = PAGE_MAIN
        elif widget == "confirm" and self.input_valid():
            value = int(self.distance_input)
            if self.page == PAGE_DISTANCE:
                self.distance_cm = value
                self.page = PAGE_MAIN
                return COMMAND_SET_DISTANCE, self.distance_cm
            angle = -value if self.angle_negative and value != 0 else value
            self.page = PAGE_MAIN
            if self.angle_axis == "yaw":
                self.yaw_target_degrees = angle
                return COMMAND_SET_YAW_TARGET, angle
            self.pitch_target_degrees = angle
            return COMMAND_SET_PITCH_TARGET, angle
        return None

    def input_valid(self):
        if not self.distance_input.isdigit():
            return False
        value = int(self.distance_input)
        if self.page == PAGE_DISTANCE:
            return DISTANCE_MIN_CM <= value <= DISTANCE_MAX_CM
        return value <= (YAW_TARGET_LIMIT_DEG if self.angle_axis == "yaw"
                         else PITCH_TARGET_LIMIT_DEG)

    def apply_telemetry(self, values):
        self.link_online = True
        self.last_telemetry_ms = int(values.get("ms", self.last_telemetry_ms))
        self.runtime = values.get("status", self.runtime)
        self.feedback = values.get("feedback", self.feedback).replace("_", " ")
        self.distance_cm = int(values.get("distance", self.distance_cm))
        self.azimuth_degrees = int(round(int(values.get("yaw_mdeg", 0)) / 1000.0))
        self.elevation_degrees = int(round(int(values.get("pitch_mdeg", 0)) / 1000.0))
        self.yaw_target_degrees = int(round(int(values.get("target_mdeg", 0)) / 1000.0))
        self.pitch_target_degrees = int(round(int(values.get("pitch_target_mdeg", 0)) / 1000.0))

    def set_ack(self, command, result):
        self.feedback = "%s %s" % (command, result)


def _color(image_module, rgb):
    return image_module.Color.from_rgb(*rgb)


COLORS = {
    "background": (239, 235, 239), "header": (8, 8, 8), "panel": (255, 255, 255),
    "border": (189, 190, 189), "text": (24, 24, 24), "muted": (107, 105, 107),
    "primary": (24, 146, 255), "pressed": (8, 109, 189), "success": (33, 194, 49),
    "warning": (255, 166, 0), "danger": (239, 24, 33), "danger_pressed": (189, 0, 0),
}


def _fill(img, rect, color):
    img.draw_rect(*rect, color=color, thickness=-1)


def _border(img, rect, color):
    img.draw_rect(*rect, color=color, thickness=1)


def _text_size(image_module, text):
    size = image_module.string_size(str(text))
    width = size.width() if hasattr(size, "width") else size[0]
    height = size.height() if hasattr(size, "height") else size[1]
    return int(width), int(height)


def _center_text(img, image_module, rect, text, color):
    width, height = _text_size(image_module, text)
    x = rect[0] + max(2, (rect[2] - width) // 2)
    y = rect[1] + max(1, (rect[3] - height) // 2)
    img.draw_string(x, y, str(text), color=color)


def _button(img, image_module, ui, name, rect, label, normal, pressed):
    background = pressed if ui.armed == name else normal
    _fill(img, rect, background)
    _border(img, rect, background)
    _center_text(img, image_module, rect, label, _color(image_module, COLORS["panel"]))


def draw_ui(img, image_module, ui):
    colors = {name: _color(image_module, rgb) for name, rgb in COLORS.items()}
    _fill(img, (0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT), colors["background"])
    _fill(img, (0, 0, LOGICAL_WIDTH, 28), colors["header"])
    _center_text(img, image_module, (0, 0, LOGICAL_WIDTH, 28),
                 "电磁炮击靶系统" if ui.page == PAGE_MAIN else
                 ("设置距离" if ui.page == PAGE_DISTANCE else
                  ("设置YAW目标" if ui.angle_axis == "yaw" else "设置PITCH目标")),
                 colors["panel"])

    if ui.page in (PAGE_DISTANCE, PAGE_ANGLE):
        labels = {"clear": "+/-" if ui.page == PAGE_ANGLE else "清",
                  "back": "退", "confirm": "确认", "cancel": "取消"}
        for name, rect in KEY_RECTS.items():
            normal = colors["panel"]
            pressed = colors["border"]
            if name == "confirm":
                normal, pressed = colors["success"], colors["pressed"]
            elif name == "cancel":
                normal, pressed = colors["danger"], colors["danger_pressed"]
            _button(img, image_module, ui, name, rect, labels.get(name, name), normal, pressed)
        _fill(img, DISTANCE_DISPLAY_RECT, colors["panel"])
        _border(img, DISTANCE_DISPLAY_RECT, colors["primary"] if ui.input_valid() else colors["danger"])
        input_text = ui.distance_input or "--"
        if (ui.page == PAGE_ANGLE and ui.angle_negative and
                ui.distance_input and int(ui.distance_input) != 0):
            input_text = "-" + input_text
        _center_text(img, image_module, DISTANCE_DISPLAY_RECT, input_text, colors["text"])
        _center_text(img, image_module, DISTANCE_RANGE_RECT,
                     (("范围 50-100 cm" if ui.input_valid() else "请输入 50-100")
                      if ui.page == PAGE_DISTANCE else
                      ("YAW -50..50°" if ui.angle_axis == "yaw" else "PITCH -50..50°")),
                     colors["muted"] if ui.input_valid() else colors["danger"])
        return

    _fill(img, MAIN_RECTS["distance"], colors["panel"])
    _border(img, MAIN_RECTS["distance"], colors["primary"] if ui.armed == "distance" else colors["border"])
    img.draw_string(16, 38, "距离", color=colors["muted"])
    img.draw_string(16, 56, "%d cm" % ui.distance_cm, color=colors["text"])
    _button(img, image_module, ui, "fire", MAIN_RECTS["fire"], "距离定位", colors["primary"], colors["pressed"])

    runtime_text = {"IDLE": "待机", "RUNNING": "运行", "COMPLETE": "完成", "READY": "READY", "FAULT": "故障"}.get(ui.runtime, ui.runtime)
    cards = ((STATUS_RECT, "状态", runtime_text),
             (AZIMUTH_RECT, "YAW", "A%d T%d" % (ui.azimuth_degrees, ui.yaw_target_degrees)),
             (ELEVATION_RECT, "PITCH", "A%d T%d" % (ui.elevation_degrees, ui.pitch_target_degrees)))
    for rect, label, value in cards:
        _fill(img, rect, colors["panel"])
        _border(img, rect, colors["border"])
        img.draw_string(rect[0] + 6, rect[1] + 2, label, color=colors["muted"])
        img.draw_string(rect[0] + 6, rect[1] + 20, value,
                        color=colors["danger"] if ui.runtime == "FAULT" and label == "状态" else colors["text"])

    _button(img, image_module, ui, "fixed", MAIN_RECTS["fixed"], "固定靶", colors["primary"], colors["pressed"])
    _button(img, image_module, ui, "auto", MAIN_RECTS["auto"], "自动搜索", colors["primary"], colors["pressed"])
    if SCAN_ENABLED:
        _button(img, image_module, ui, "sweep", MAIN_RECTS["sweep"], "往复扫描", colors["primary"], colors["pressed"])
    else:
        _fill(img, MAIN_RECTS["sweep"], colors["border"])
        _border(img, MAIN_RECTS["sweep"], colors["border"])
        _center_text(img, image_module, MAIN_RECTS["sweep"], "SCAN OFF", colors["muted"])
    _button(img, image_module, ui, "start", MAIN_RECTS["start"], "启动", colors["success"], colors["pressed"])
    _button(img, image_module, ui, "stop", MAIN_RECTS["stop"], "停止", colors["danger"], colors["danger_pressed"])

    _fill(img, FOOTER_RECT, colors["panel"])
    _border(img, FOOTER_RECT, colors["border"])
    link = "TCP" if ui.link_online else "OFFLINE"
    _center_text(img, image_module, FOOTER_RECT, "%s | %s" % (link, ui.feedback), colors["text"])
