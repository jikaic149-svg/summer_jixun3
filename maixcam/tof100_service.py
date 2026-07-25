"""PMOD_TOF100 通用采集、矩阵方向和深度统计接口。

本模块把 MaixCAM2 硬件初始化与纯 Python 深度统计放在同一文件中，
但不会在导入阶段加载 ``maix``。因此 PC 端可以直接导入并测试矩阵逻辑，
板端则通过 :class:`ToF100Device` 打开真实传感器。

正式业务每帧只应调用一次 ``read_frame()``。显示、ROI 统计和后续点云算法
都复用该帧的 ``matrix``，避免重复采集造成时间不同步。
"""


# PMOD_TOF100 同时使用 I2C 控制线和 SPI2 数据线。这里完全沿用
# Sipeed 官方 MaixCAM2 Python 示例，后续串口/电机选脚时应避开这些引脚。
DEFAULT_PIN_FUNCTIONS = (
    ("A8", "I2C7_SCL"),
    ("A9", "I2C7_SDA"),
    ("B21", "SPI2_CS1"),
    ("B19", "SPI2_MISO"),
    ("B18", "SPI2_MOSI"),
    ("B20", "SPI2_SCK"),
)

SUPPORTED_RESOLUTIONS = (25, 50, 100)


def matrix_shape(matrix):
    """返回深度矩阵的 ``(width, height)``，并拒绝不规则矩阵。

    深度矩阵按 ``matrix[y][x]`` 访问：外层列表是行（y），内层是列（x）。
    """
    if matrix is None or len(matrix) == 0:
        return 0, 0
    if matrix[0] is None or len(matrix[0]) == 0:
        return 0, 0

    width = len(matrix[0])
    for row in matrix:
        if row is None or len(row) != width:
            raise ValueError("depth matrix must be rectangular")
    return width, len(matrix)


def rotate_matrix_180(matrix):
    """将二维矩阵旋转 180°，不修改输入矩阵。"""
    width, height = matrix_shape(matrix)
    if width == 0 or height == 0:
        return []
    return [list(reversed(row)) for row in reversed(matrix)]


def centered_roi(width, height, size):
    """生成位于矩阵中心的正方形 ROI，返回 ``(x, y, w, h)``。

    ROI（Region of Interest）表示只分析整幅深度图中的一小块区域。
    """
    width = int(width)
    height = int(height)
    size = max(1, int(size))
    if width <= 0 or height <= 0:
        return 0, 0, 0, 0

    roi_width = min(size, width)
    roi_height = min(size, height)
    # 偶数分辨率没有唯一的几何中心像素。这里与Sipeed官方驱动的
    # center_point()约定保持一致，使用 width//2、height//2 所在单元。
    x = width // 2 - roi_width // 2
    y = height // 2 - roi_height // 2
    return x, y, roi_width, roi_height


def clamp_roi(roi, width, height):
    """把 ``(x, y, w, h)`` 裁剪到矩阵范围内。``None`` 表示整帧。"""
    width = int(width)
    height = int(height)
    if width <= 0 or height <= 0:
        return 0, 0, 0, 0
    if roi is None:
        return 0, 0, width, height
    if len(roi) != 4:
        raise ValueError("roi must be (x, y, width, height)")

    x, y, roi_width, roi_height = [int(value) for value in roi]
    if roi_width <= 0 or roi_height <= 0:
        return 0, 0, 0, 0

    x1 = max(0, min(width, x))
    y1 = max(0, min(height, y))
    x2 = max(x1, min(width, x + roi_width))
    y2 = max(y1, min(height, y + roi_height))
    return x1, y1, x2 - x1, y2 - y1


def _median(sorted_values):
    """计算已排序数据的中位数；相比平均值不容易被离群点拉偏。"""
    count = len(sorted_values)
    if count == 0:
        return None
    middle = count // 2
    if count % 2:
        return float(sorted_values[middle])
    return (float(sorted_values[middle - 1]) + float(sorted_values[middle])) / 2.0


def _percentile(sorted_values, proportion):
    """线性插值计算分位数，用于观察有效深度的分布范围。"""
    if not sorted_values:
        return None
    proportion = max(0.0, min(1.0, float(proportion)))
    position = (len(sorted_values) - 1) * proportion
    lower = int(position)
    upper = min(len(sorted_values) - 1, lower + 1)
    weight = position - lower
    return (float(sorted_values[lower]) * (1.0 - weight) +
            float(sorted_values[upper]) * weight)


def analyze_depth_roi(matrix, roi=None, valid_min_mm=200,
                      valid_max_mm=1500, min_valid_count=5):
    """统计深度 ROI 的有效值、中位数、MAD 和分位数。

    返回字典始终包含 ``valid`` 和 ``reason``。算法层应先判断 ``valid``，
    不得在无有效深度或有效点过少时使用 ``median_mm``。
    """
    valid_min_mm = int(valid_min_mm)
    valid_max_mm = int(valid_max_mm)
    min_valid_count = max(1, int(min_valid_count))
    if valid_max_mm < valid_min_mm:
        raise ValueError("valid_max_mm must be >= valid_min_mm")

    width, height = matrix_shape(matrix)

    # 先建立字段完整的无效结果。即使矩阵为空，调用者也可以安全地
    # 使用 stats.get("median_mm") 等统一字段，而不必捕获KeyError。
    result = {
        "valid": False,
        "reason": "EMPTY_MATRIX",
        "matrix_width": width,
        "matrix_height": height,
        "roi": (0, 0, 0, 0),
        "total_count": 0,
        "valid_count": 0,
        "valid_ratio": 0.0,
        "median_mm": None,
        "mean_mm": None,
        "depth_min_mm": None,
        "depth_max_mm": None,
        "p10_mm": None,
        "p90_mm": None,
        "mad_mm": None,
    }
    if width == 0 or height == 0:
        return result

    x, y, roi_width, roi_height = clamp_roi(roi, width, height)
    result["roi"] = (x, y, roi_width, roi_height)
    result["total_count"] = roi_width * roi_height
    if roi_width == 0 or roi_height == 0:
        result["reason"] = "EMPTY_ROI"
        return result

    # 遍历ROI，把非数字、过近、过远和传感器异常值排除。
    values = []
    for row_index in range(y, y + roi_height):
        row = matrix[row_index]
        for column_index in range(x, x + roi_width):
            try:
                value = int(row[column_index])
            except (TypeError, ValueError):
                continue
            if valid_min_mm <= value <= valid_max_mm:
                values.append(value)

    # 中位数和分位数都要求有序数据。ROI很小，直接排序的开销可接受。
    values.sort()
    valid_count = len(values)
    result["valid_count"] = valid_count
    result["valid_ratio"] = (float(valid_count) / result["total_count"]
                             if result["total_count"] else 0.0)
    if valid_count == 0:
        result["reason"] = "NO_VALID_DEPTH"
        return result

    # median_mm：ROI的代表距离。
    # MAD：每个有效点到中位数的偏差，再取一次中位数；MAD越小，
    # 通常说明该ROI更接近平整、单一距离的目标表面。
    median_mm = _median(values)
    deviations = sorted(abs(float(value) - median_mm) for value in values)
    result["median_mm"] = median_mm
    result["mean_mm"] = float(sum(values)) / valid_count
    result["depth_min_mm"] = float(values[0])
    result["depth_max_mm"] = float(values[-1])
    result["p10_mm"] = _percentile(values, 0.10)
    result["p90_mm"] = _percentile(values, 0.90)
    result["mad_mm"] = _median(deviations)

    if valid_count < min_valid_count:
        result["reason"] = "TOO_FEW_VALID"
        return result

    result["valid"] = True
    result["reason"] = "OK"
    return result


def format_depth_stats(stats, prefix="TOF100"):
    """把 ROI 统计格式化为便于串口和终端筛选的一行 ASCII 日志。"""
    median = stats.get("median_mm")
    mad = stats.get("mad_mm")
    median_text = "-1" if median is None else "%.1f" % median
    mad_text = "-1" if mad is None else "%.1f" % mad
    return ("%s|valid=%d|reason=%s|roi=%s|points=%d/%d|ratio=%.3f|"
            "median_mm=%s|mad_mm=%s" % (
                prefix,
                1 if stats.get("valid") else 0,
                stats.get("reason", "UNKNOWN"),
                str(stats.get("roi", (0, 0, 0, 0))),
                int(stats.get("valid_count", 0)),
                int(stats.get("total_count", 0)),
                float(stats.get("valid_ratio", 0.0)),
                median_text,
                mad_text,
            ))


class ToF100Device:
    """MaixCAM2 上 PMOD_TOF100 的最小硬件封装。

    上层只需要记住四个主要方法：``open``、``read_frame``、
    ``analyze_frame``、``close``。原始矩阵仍保留在frame中，后续可直接
    交给RGB融合、平面拟合或点云算法使用。
    """

    def __init__(self, resolution=50, spi_bus_num=2, spi_cs_num=-1,
                 valid_min_mm=200, valid_max_mm=1500,
                 display_min_mm=200, display_max_mm=1500,
                 rotate_180=True, configure_pins=True,
                 pin_functions=DEFAULT_PIN_FUNCTIONS):
        resolution = int(resolution)
        if resolution not in SUPPORTED_RESOLUTIONS:
            raise ValueError("resolution must be 25, 50 or 100")
        if int(valid_max_mm) < int(valid_min_mm):
            raise ValueError("valid_max_mm must be >= valid_min_mm")

        # 这些配置同时保存为对象属性，便于测试入口和未来正式程序复用。
        self.resolution = resolution
        self.spi_bus_num = int(spi_bus_num)
        self.spi_cs_num = int(spi_cs_num)
        self.valid_min_mm = int(valid_min_mm)
        self.valid_max_mm = int(valid_max_mm)
        self.display_min_mm = int(display_min_mm)
        self.display_max_mm = int(display_max_mm)
        self.rotate_180 = bool(rotate_180)
        self.configure_pins = bool(configure_pins)
        self.pin_functions = tuple(pin_functions)
        self._tof = None
        self._time = None
        self._sequence = 0

    @property
    def is_open(self):
        return self._tof is not None

    def open(self):
        """配置官方示例引脚并打开 TOF100。重复调用不会重复初始化。"""
        if self._tof is not None:
            return self

        from maix import ext_dev, pinmap, time as maix_time

        # 把MaixCAM2复用引脚切换为TOF100需要的I2C7和SPI2功能。
        if self.configure_pins:
            for pin, function in self.pin_functions:
                result = pinmap.set_pin_function(pin, function)
                if result != 0:
                    raise RuntimeError(
                        "pin mapping failed: %s -> %s, code=%s" %
                        (pin, function, str(result)))

        # 将普通整数配置转换为MaixPy官方枚举。
        resolution_map = {
            25: ext_dev.tof100.Resolution.RES_25x25,
            50: ext_dev.tof100.Resolution.RES_50x50,
            100: ext_dev.tof100.Resolution.RES_100x100,
        }
        # JET只是伪彩图配色；display_min/max只决定颜色映射，不负责
        # 业务有效距离判断。业务过滤在analyze_depth_roi()中完成。
        constructor_args = (
            self.spi_bus_num,
            resolution_map[self.resolution],
            ext_dev.cmap.Cmap.JET,
            self.display_min_mm,
            self.display_max_mm,
        )
        try:
            self._tof = ext_dev.tof100.Tof100(
                *(constructor_args + (self.spi_cs_num,)))
        except TypeError:
            # 较旧固件的 Python 绑定可能尚未暴露 spi_cs_num；默认 -1 时
            # 回退到官方示例使用的五参数构造形式。
            if self.spi_cs_num != -1:
                raise
            self._tof = ext_dev.tof100.Tof100(*constructor_args)
        self._time = maix_time
        self._sequence = 0
        return self

    def close(self):
        """释放 Python 侧设备引用；官方 API 当前没有独立 ``close()``。"""
        self._tof = None
        self._time = None

    def read_frame(self):
        """采集一帧深度矩阵；失败时返回 ``None``。"""
        if self._tof is None:
            raise RuntimeError("TOF100 is not open")

        # 在matrix()前后各记一次时间，用中点作为近似帧时刻。未来转台
        # 运动时，可用这个时间与编码器角度进行近似同步。
        acquire_start_ms = int(self._time.time() * 1000)
        matrix = self._tof.matrix()
        acquire_end_ms = int(self._time.time() * 1000)
        width, height = matrix_shape(matrix)
        if width == 0 or height == 0:
            return None

        # 将MaixPy返回对象复制成普通列表，避免原生缓冲在下一帧变化。
        matrix = [list(row) for row in matrix]
        if self.rotate_180:
            matrix = rotate_matrix_180(matrix)
            width, height = matrix_shape(matrix)

        self._sequence += 1
        timestamp_ms = (acquire_start_ms + acquire_end_ms) // 2

        # frame是TOF通用接口的统一输出。业务算法不要直接依赖底层
        # ext_dev对象，而应消费这个字典。
        return {
            "matrix": matrix,
            "width": width,
            "height": height,
            "sequence": self._sequence,
            "timestamp_ms": timestamp_ms,
            "acquire_start_ms": acquire_start_ms,
            "acquire_end_ms": acquire_end_ms,
            "acquire_duration_ms": acquire_end_ms - acquire_start_ms,
            "rotated_180": self.rotate_180,
        }

    def analyze_frame(self, frame, roi=None, min_valid_count=5):
        """按设备有效距离配置统计指定帧和 ROI。"""
        if frame is None:
            return analyze_depth_roi(
                [], roi, self.valid_min_mm, self.valid_max_mm,
                min_valid_count)
        return analyze_depth_roi(
            frame["matrix"], roi, self.valid_min_mm,
            self.valid_max_mm, min_valid_count)

    def image_from_frame(self, frame):
        """使用当前帧生成伪彩图，不触发第二次传感器采集。"""
        if self._tof is None:
            raise RuntimeError("TOF100 is not open")
        if frame is None:
            return None
        return self._tof.image_from(frame["matrix"])

    def __enter__(self):
        return self.open()

    def __exit__(self, _exception_type, _exception, _traceback):
        self.close()
        return False
