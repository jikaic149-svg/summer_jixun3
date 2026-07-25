"""用传统视觉从一幅 RGB 图像中拟合快递箱的可见正面。

本模块不调用 MaixCAM2、显示屏或 YOLO，输入只是 OpenCV 的 BGR 图像。因此同一套
算法既能在板端运行，也能在 PC 上用保存的照片调阈值和做回归测试。

算法输出的 ``center`` 不是轮廓外接矩形中心，而是四边形两条对角线的交点。箱面
发生透视形变时，这个点通常比 YOLO 框中心更适合做后续水平扫描和过零判断。
"""

import math

import cv2
import numpy as np

import vision_config as config


def clamp_roi(roi, image_width, image_height):
    """把 ``(x, y, w, h)`` 限制在图像范围内并返回整数 ROI。"""
    image_width = max(0, int(image_width))
    image_height = max(0, int(image_height))
    if roi is None:
        return (0, 0, image_width, image_height)

    if len(roi) != 4:
        raise ValueError("roi must be (x, y, width, height)")
    x, y, width, height = [float(value) for value in roi]
    if width <= 0 or height <= 0:
        return (0, 0, 0, 0)

    x1 = max(0, min(image_width, int(math.floor(x))))
    y1 = max(0, min(image_height, int(math.floor(y))))
    x2 = max(0, min(image_width, int(math.ceil(x + width))))
    y2 = max(0, min(image_height, int(math.ceil(y + height))))
    return (x1, y1, max(0, x2 - x1), max(0, y2 - y1))


def expand_box_roi(box, image_width, image_height, expand_ratio=0.12):
    """把 YOLO 框向四周扩大后裁到图像内。

    ``box`` 可以是 ``(x, y, w, h)``，也可以是含同名字段的字典。扩大 ROI 是为了
    避免 YOLO 框恰好压住箱面边缘，使传统视觉拿不到完整闭合轮廓。
    """
    if box is None:
        return None
    if isinstance(box, dict):
        x = box.get("x")
        y = box.get("y")
        width = box.get("w")
        height = box.get("h")
    else:
        x, y, width, height = box
    if x is None or y is None or width is None or height is None:
        return None

    x = float(x)
    y = float(y)
    width = float(width)
    height = float(height)
    expand_ratio = max(0.0, float(expand_ratio))
    pad_x = width * expand_ratio
    pad_y = height * expand_ratio
    return clamp_roi(
        (x - pad_x, y - pad_y,
         width + pad_x * 2.0, height + pad_y * 2.0),
        image_width,
        image_height,
    )


def order_quad_points(points):
    """把四个无序点整理为左上、右上、右下、左下的顺序。"""
    array = np.asarray(points, dtype=np.float32).reshape(-1, 2)
    if len(array) != 4:
        raise ValueError("exactly four points are required")

    center_x = float(np.mean(array[:, 0]))
    center_y = float(np.mean(array[:, 1]))
    ordered = sorted(
        [point for point in array],
        key=lambda point: math.atan2(
            float(point[1]) - center_y,
            float(point[0]) - center_x,
        ),
    )

    # 从最接近左上方的点开始。
    start_index = min(
        range(4),
        key=lambda index: float(ordered[index][0] + ordered[index][1]),
    )
    ordered = ordered[start_index:] + ordered[:start_index]

    # 图像坐标的 y 轴向下。期望顺序 tl -> tr -> br -> bl 的叉积为正。
    cross = (
        (float(ordered[1][0]) - float(ordered[0][0])) *
        (float(ordered[2][1]) - float(ordered[1][1])) -
        (float(ordered[1][1]) - float(ordered[0][1])) *
        (float(ordered[2][0]) - float(ordered[1][0]))
    )
    if cross < 0:
        ordered = [ordered[0], ordered[3], ordered[2], ordered[1]]
    return np.asarray(ordered, dtype=np.float32)


def diagonal_intersection(corners):
    """计算四边形两条对角线 ``0-2`` 与 ``1-3`` 的交点。"""
    ordered = order_quad_points(corners)
    x1, y1 = [float(value) for value in ordered[0]]
    x2, y2 = [float(value) for value in ordered[2]]
    x3, y3 = [float(value) for value in ordered[1]]
    x4, y4 = [float(value) for value in ordered[3]]

    denominator = ((x1 - x2) * (y3 - y4) -
                   (y1 - y2) * (x3 - x4))
    if abs(denominator) < 1e-9:
        return None
    determinant_12 = x1 * y2 - y1 * x2
    determinant_34 = x3 * y4 - y3 * x4
    center_x = ((determinant_12 * (x3 - x4) -
                 (x1 - x2) * determinant_34) / denominator)
    center_y = ((determinant_12 * (y3 - y4) -
                 (y1 - y2) * determinant_34) / denominator)
    return (float(center_x), float(center_y))


def _distance(point_a, point_b):
    return math.hypot(
        float(point_b[0]) - float(point_a[0]),
        float(point_b[1]) - float(point_a[1]),
    )


def _orthogonality_score(points):
    """返回 0~1；四个角越接近直角，分数越高。"""
    scores = []
    for index in range(4):
        previous_point = points[(index - 1) % 4]
        current_point = points[index]
        next_point = points[(index + 1) % 4]
        vector_a = (
            float(previous_point[0]) - float(current_point[0]),
            float(previous_point[1]) - float(current_point[1]),
        )
        vector_b = (
            float(next_point[0]) - float(current_point[0]),
            float(next_point[1]) - float(current_point[1]),
        )
        norm = math.hypot(*vector_a) * math.hypot(*vector_b)
        if norm <= 1e-9:
            return 0.0
        cosine = abs(
            (vector_a[0] * vector_b[0] + vector_a[1] * vector_b[1]) /
            norm
        )
        scores.append(max(0.0, 1.0 - min(1.0, cosine)))
    return sum(scores) / len(scores)


def _verticality_score(points):
    """检查左右两条边是否大致竖直，返回 0~1。"""
    left_length = _distance(points[0], points[3])
    right_length = _distance(points[1], points[2])
    if left_length <= 1e-9 or right_length <= 1e-9:
        return 0.0
    left_horizontal_ratio = abs(
        float(points[3][0]) - float(points[0][0])) / left_length
    right_horizontal_ratio = abs(
        float(points[2][0]) - float(points[1][0])) / right_length
    return max(
        0.0,
        1.0 - (left_horizontal_ratio + right_horizontal_ratio) * 0.5,
    )


def _touches_border(points, width, height, margin):
    margin = max(0, int(margin))
    for point in points:
        x = float(point[0])
        y = float(point[1])
        if (x <= margin or y <= margin or
                x >= width - 1 - margin or y >= height - 1 - margin):
            return True
    return False


def _candidate_metrics(ordered, roi_width, roi_height, pixel_scale=1.0):
    contour = ordered.reshape((-1, 1, 2)).astype(np.float32)
    area_px = abs(float(cv2.contourArea(contour)))
    roi_area = max(1.0, float(roi_width * roi_height))
    area_ratio = area_px / roi_area

    top = _distance(ordered[0], ordered[1])
    right = _distance(ordered[1], ordered[2])
    bottom = _distance(ordered[2], ordered[3])
    left = _distance(ordered[3], ordered[0])
    average_width = (top + bottom) * 0.5
    average_height = (left + right) * 0.5
    aspect_ratio = average_width / max(1e-9, average_height)

    min_rect = cv2.minAreaRect(contour)
    rect_width, rect_height = min_rect[1]
    rect_area = max(1e-9, float(rect_width) * float(rect_height))
    rectangularity = min(1.0, area_px / rect_area)
    orthogonality = _orthogonality_score(ordered)
    verticality = _verticality_score(ordered)
    min_edge_length = min(top, right, bottom, left)
    border_margin = max(
        1, int(round(config.BORDER_MARGIN_PX * float(pixel_scale))))
    touches_border = _touches_border(
        ordered, roi_width, roi_height, border_margin)

    # 面积只占一部分权重，避免“最大的背景矩形”天然胜出。正交性和矩形填充率
    # 负责判断它是否更像箱面，居中程度只作为同等候选之间的轻量排序项。
    center = diagonal_intersection(ordered)
    if center is None:
        center = (
            float(np.mean(ordered[:, 0])),
            float(np.mean(ordered[:, 1])),
        )
    half_diagonal = max(1.0, math.hypot(roi_width, roi_height) * 0.5)
    center_distance = math.hypot(
        center[0] - roi_width * 0.5,
        center[1] - roi_height * 0.5,
    )
    center_score = max(0.0, 1.0 - center_distance / half_diagonal)
    area_score = min(1.0, area_ratio / 0.55)
    score = (
        area_score * 0.28 +
        rectangularity * 0.27 +
        orthogonality * 0.25 +
        verticality * 0.15 +
        center_score * 0.05
    )

    return {
        "area_px": area_px,
        "area_ratio": area_ratio,
        "aspect_ratio": aspect_ratio,
        "rectangularity": rectangularity,
        "orthogonality": orthogonality,
        "verticality": verticality,
        "min_edge_length": min_edge_length,
        "touches_border": touches_border,
        "score": max(0.0, min(1.0, score)),
    }


def _rejection_reason(metrics, pixel_scale=1.0):
    pixel_scale = max(1e-6, float(pixel_scale))
    min_area_px = config.MIN_QUAD_AREA_PX * pixel_scale * pixel_scale
    min_edge_length = config.MIN_EDGE_LENGTH_PX * pixel_scale
    if metrics["area_px"] < min_area_px:
        return "AREA_PX"
    if metrics["area_ratio"] < config.MIN_QUAD_AREA_RATIO:
        return "AREA_SMALL"
    if metrics["area_ratio"] > config.MAX_QUAD_AREA_RATIO:
        return "AREA_LARGE"
    if not (config.MIN_ASPECT_RATIO <= metrics["aspect_ratio"] <=
            config.MAX_ASPECT_RATIO):
        return "ASPECT"
    if metrics["rectangularity"] < config.MIN_RECTANGULARITY:
        return "RECTANGULARITY"
    if metrics["orthogonality"] < config.MIN_ORTHOGONALITY:
        return "ORTHOGONALITY"
    if metrics["verticality"] < config.MIN_VERTICALITY:
        return "VERTICALITY"
    if metrics["min_edge_length"] < min_edge_length:
        return "EDGE_LENGTH"
    if config.REJECT_BORDER_TOUCH and metrics["touches_border"]:
        return "BORDER_TOUCH"
    return None


def _empty_result(reason, roi=(0, 0, 0, 0)):
    """所有失败路径返回相同字段，方便上层安全地写日志和通信。"""
    return {
        "valid": False,
        "reason": reason,
        "roi": tuple(roi),
        "corners": None,
        "center": None,
        "cx": None,
        "cy": None,
        "dx": None,
        "dy": None,
        "area_px": 0.0,
        "area_ratio_roi": 0.0,
        "area_ratio_frame": 0.0,
        "aspect_ratio": 0.0,
        "rectangularity": 0.0,
        "orthogonality": 0.0,
        "verticality": 0.0,
        "score": 0.0,
        "contour_count": 0,
        "quad_candidate_count": 0,
        "rejected_reasons": {},
        "rejected_candidates": [],
    }


def empty_face_result(reason, roi=(0, 0, 0, 0)):
    """供板端主循环构造“YOLO 未检出”等统一失败结果。"""
    return _empty_result(reason, roi)


def _find_contours(binary_image):
    values = cv2.findContours(
        binary_image, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    # OpenCV 4 返回 contours, hierarchy；较旧绑定可能在前面多返回一幅图。
    return values[0] if len(values) == 2 else values[1]


def _scaled_odd_kernel_size(original_size, pixel_scale):
    """按处理比例缩放核尺寸，并保持不小于3的奇数。"""
    size = max(3, int(round(float(original_size) * float(pixel_scale))))
    if size % 2 == 0:
        size += 1
    return size


def detect_box_face(frame_bgr, roi=None, pixel_scale=1.0):
    """在指定 ROI 内寻找最可信的箱体正面四边形。

    返回字段中：

    - ``corners``：全图坐标，顺序为左上、右上、右下、左下；
    - ``center``：两条对角线交点；
    - ``dx/dy``：该交点相对整幅图像中心的像素误差；
    - ``area_px``：四边形投影面积，可留给后续水平扫描曲线使用；
    - ``score``：当前帧候选质量，只用于筛选，不等价于 YOLO 置信度。
    """
    pixel_scale = float(pixel_scale)
    if pixel_scale <= 0:
        raise ValueError("pixel_scale must be positive")
    if frame_bgr is None or not hasattr(frame_bgr, "shape"):
        return _empty_result("EMPTY_FRAME")
    if len(frame_bgr.shape) < 2:
        return _empty_result("BAD_FRAME_SHAPE")

    image_height, image_width = frame_bgr.shape[:2]
    safe_roi = clamp_roi(roi, image_width, image_height)
    roi_x, roi_y, roi_width, roi_height = safe_roi
    result = _empty_result("EMPTY_ROI", safe_roi)
    if roi_width < 8 or roi_height < 8:
        return result

    roi_bgr = frame_bgr[
        roi_y:roi_y + roi_height,
        roi_x:roi_x + roi_width,
    ]
    if len(roi_bgr.shape) == 2:
        gray = roi_bgr
    elif roi_bgr.shape[2] == 4:
        gray = cv2.cvtColor(roi_bgr, cv2.COLOR_BGRA2GRAY)
    else:
        gray = cv2.cvtColor(roi_bgr, cv2.COLOR_BGR2GRAY)

    gaussian_size = _scaled_odd_kernel_size(
        config.GAUSSIAN_KERNEL_SIZE, pixel_scale)
    blurred = cv2.GaussianBlur(
        gray, (gaussian_size, gaussian_size), 0)
    edges = cv2.Canny(
        blurred, int(config.CANNY_LOW), int(config.CANNY_HIGH))

    morph_size = _scaled_odd_kernel_size(
        config.MORPH_KERNEL_SIZE, pixel_scale)
    kernel = cv2.getStructuringElement(
        cv2.MORPH_RECT, (morph_size, morph_size))
    closed = cv2.morphologyEx(
        edges,
        cv2.MORPH_CLOSE,
        kernel,
        iterations=max(1, int(config.MORPH_CLOSE_ITERATIONS)),
    )

    contours = _find_contours(closed)
    result["contour_count"] = len(contours)
    accepted = []
    rejected_reasons = {}
    rejected_candidates = []

    for contour in contours:
        perimeter = float(cv2.arcLength(contour, True))
        if perimeter <= 1e-6:
            continue
        approximation = cv2.approxPolyDP(
            contour,
            float(config.APPROX_EPSILON_RATIO) * perimeter,
            True,
        )
        if len(approximation) != 4 or not cv2.isContourConvex(approximation):
            continue

        result["quad_candidate_count"] += 1
        ordered = order_quad_points(approximation.reshape(4, 2))
        metrics = _candidate_metrics(
            ordered, roi_width, roi_height, pixel_scale)
        reason = _rejection_reason(metrics, pixel_scale)
        if reason is not None:
            rejected_reasons[reason] = rejected_reasons.get(reason, 0) + 1
            # 保留普通 Python 数值，板端日志层无需再访问 OpenCV contour 对象。
            # reason 是筛选顺序中遇到的第一个失败规则；其余指标仍全部输出，便于
            # 判断候选是否还同时违反了其他门限。
            rejected_candidate = dict(metrics)
            rejected_candidate["reason"] = reason
            rejected_candidates.append(rejected_candidate)
            continue
        accepted.append((metrics["score"], ordered, metrics))

    result["rejected_reasons"] = rejected_reasons
    rejected_candidates.sort(
        key=lambda candidate: candidate["score"], reverse=True)
    result["rejected_candidates"] = rejected_candidates
    if not accepted:
        result["reason"] = (
            "NO_QUAD" if result["quad_candidate_count"] == 0
            else "QUAD_REJECTED"
        )
        return result

    accepted.sort(key=lambda item: item[0], reverse=True)
    _score, local_corners, metrics = accepted[0]
    absolute_corners = [
        (float(point[0]) + roi_x, float(point[1]) + roi_y)
        for point in local_corners
    ]
    center = diagonal_intersection(absolute_corners)
    if center is None:
        result["reason"] = "DIAGONAL_PARALLEL"
        return result

    result.update({
        "valid": True,
        "reason": "OK",
        "corners": absolute_corners,
        "center": center,
        "cx": center[0],
        "cy": center[1],
        "dx": center[0] - image_width * 0.5,
        "dy": center[1] - image_height * 0.5,
        "area_px": metrics["area_px"],
        "area_ratio_roi": metrics["area_ratio"],
        "area_ratio_frame": (
            metrics["area_px"] /
            max(1.0, float(image_width * image_height))
        ),
        "aspect_ratio": metrics["aspect_ratio"],
        "rectangularity": metrics["rectangularity"],
        "orthogonality": metrics["orthogonality"],
        "verticality": metrics["verticality"],
        "score": metrics["score"],
    })
    return result


def detect_box_face_from_color(frame_bgr, roi=None, pixel_scale=1.0,
                               lab_low=None, lab_high=None):
    """在LAB颜色掩膜中提取箱面四角。

    这条路径不依赖灰度边缘：先对箱面颜色做 ``inRange``，再进行开/闭运算，
    最后从最大外部连通轮廓近似四边形。它适合在 ``find_blobs`` 判断面积进入
    稳定峰值后低频调用；正常帧不应把它当成每帧YOLO替代品。
    """
    pixel_scale = float(pixel_scale)
    if pixel_scale <= 0:
        raise ValueError("pixel_scale must be positive")
    if frame_bgr is None or not hasattr(frame_bgr, "shape"):
        return _empty_result("EMPTY_FRAME")
    if len(frame_bgr.shape) < 2:
        return _empty_result("BAD_FRAME_SHAPE")

    image_height, image_width = frame_bgr.shape[:2]
    safe_roi = clamp_roi(roi, image_width, image_height)
    roi_x, roi_y, roi_width, roi_height = safe_roi
    result = _empty_result("EMPTY_ROI", safe_roi)
    result.update({
        "method": "COLOR_QUAD",
        "center_source": "COLOR_QUAD",
        "center_is_final": True,
        "color_mask_pixels": 0,
    })
    if roi_width < 8 or roi_height < 8:
        return result

    roi_bgr = frame_bgr[
        roi_y:roi_y + roi_height,
        roi_x:roi_x + roi_width,
    ]
    if len(roi_bgr.shape) == 2:
        roi_bgr = cv2.cvtColor(roi_bgr, cv2.COLOR_GRAY2BGR)
    elif roi_bgr.shape[2] == 4:
        roi_bgr = cv2.cvtColor(roi_bgr, cv2.COLOR_BGRA2BGR)
    lab = cv2.cvtColor(roi_bgr, cv2.COLOR_BGR2LAB)

    if lab_low is None:
        lab_low = getattr(config, "COLOR_QUAD_LAB_LOW", (50, 129, 134))
    if lab_high is None:
        lab_high = getattr(config, "COLOR_QUAD_LAB_HIGH", (125, 137, 148))
    low_array = np.asarray(lab_low, dtype=np.uint8).reshape(3)
    high_array = np.asarray(lab_high, dtype=np.uint8).reshape(3)
    lower = np.minimum(low_array, high_array)
    upper = np.maximum(low_array, high_array)
    mask = cv2.inRange(lab, lower, upper)

    kernel_size = _scaled_odd_kernel_size(
        getattr(config, "COLOR_QUAD_MORPH_KERNEL_SIZE", 5), pixel_scale)
    kernel = cv2.getStructuringElement(
        cv2.MORPH_RECT, (kernel_size, kernel_size))
    open_iterations = max(0, int(getattr(
        config, "COLOR_QUAD_OPEN_ITERATIONS", 1)))
    close_iterations = max(0, int(getattr(
        config, "COLOR_QUAD_CLOSE_ITERATIONS", 3)))
    if open_iterations:
        mask = cv2.morphologyEx(
            mask, cv2.MORPH_OPEN, kernel, iterations=open_iterations)
    if close_iterations:
        mask = cv2.morphologyEx(
            mask, cv2.MORPH_CLOSE, kernel, iterations=close_iterations)

    mask_pixels = int(cv2.countNonZero(mask))
    result["color_mask_pixels"] = mask_pixels
    min_mask_pixels = max(1, int(getattr(
        config, "COLOR_QUAD_MIN_MASK_PIXELS", 1000)))
    if mask_pixels < min_mask_pixels:
        result["reason"] = "COLOR_MASK_SMALL"
        return result

    contours = cv2.findContours(
        mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contours = contours[0] if len(contours) == 2 else contours[1]
    result["contour_count"] = len(contours)
    accepted = []
    rejected_reasons = {}
    rejected_candidates = []
    epsilon_ratio = float(getattr(
        config, "COLOR_QUAD_APPROX_EPSILON_RATIO",
        getattr(config, "APPROX_EPSILON_RATIO", 0.035)))

    for contour in contours:
        perimeter = float(cv2.arcLength(contour, True))
        if perimeter <= 1e-6:
            continue
        approximation = cv2.approxPolyDP(
            contour, epsilon_ratio * perimeter, True)
        if len(approximation) != 4 or not cv2.isContourConvex(approximation):
            continue
        result["quad_candidate_count"] += 1
        ordered = order_quad_points(approximation.reshape(4, 2))
        metrics = _candidate_metrics(
            ordered, roi_width, roi_height, pixel_scale)
        reason = _rejection_reason(metrics, pixel_scale)
        if reason is not None:
            rejected_reasons[reason] = rejected_reasons.get(reason, 0) + 1
            candidate = dict(metrics)
            candidate["reason"] = reason
            rejected_candidates.append(candidate)
            continue
        accepted.append((metrics["score"], ordered, metrics))

    result["rejected_reasons"] = rejected_reasons
    rejected_candidates.sort(
        key=lambda candidate: candidate["score"], reverse=True)
    result["rejected_candidates"] = rejected_candidates
    if not accepted:
        result["reason"] = (
            "COLOR_NO_QUAD" if result["quad_candidate_count"] == 0
            else "COLOR_QUAD_REJECTED"
        )
        return result

    accepted.sort(key=lambda item: item[0], reverse=True)
    _score, local_corners, metrics = accepted[0]
    absolute_corners = [
        (float(point[0]) + roi_x, float(point[1]) + roi_y)
        for point in local_corners
    ]
    center = diagonal_intersection(absolute_corners)
    if center is None:
        result["reason"] = "COLOR_DIAGONAL_PARALLEL"
        return result

    result.update({
        "valid": True,
        "reason": "OK",
        "corners": absolute_corners,
        "center": center,
        "cx": center[0],
        "cy": center[1],
        "dx": center[0] - image_width * 0.5,
        "dy": center[1] - image_height * 0.5,
        "area_px": metrics["area_px"],
        "area_ratio_roi": metrics["area_ratio"],
        "area_ratio_frame": (
            metrics["area_px"] /
            max(1.0, float(image_width * image_height))
        ),
        "aspect_ratio": metrics["aspect_ratio"],
        "rectangularity": metrics["rectangularity"],
        "orthogonality": metrics["orthogonality"],
        "verticality": metrics["verticality"],
        "score": metrics["score"],
    })
    return result


def _line_x_at_y(line, y_value):
    """返回线段延长线在指定y处的x坐标。"""
    (x1, y1), (x2, y2) = line
    delta_y = float(y2) - float(y1)
    if abs(delta_y) <= 1e-6:
        return (float(x1) + float(x2)) * 0.5
    ratio = (float(y_value) - float(y1)) / delta_y
    return float(x1) + ratio * (float(x2) - float(x1))


def detect_vertical_side_edges(frame_bgr, corners,
                               prior_left_x=None, prior_right_x=None):
    """在当前橙框/绿框左右窄带中筛选两条近竖直真实边缘。

    当前框只提供搜索位置，不要求边缘最终组成矩形。候选来自灰度+Canny+
    ``HoughLinesP``，按竖直度、长度、离当前左右边的距离和最近帧预测位置评分。
    最近帧只参与选线，不在这里平滑输出坐标。
    """
    result = {
        "valid": False,
        "reason": "EDGE_BAD_INPUT",
        "left_line": None,
        "right_line": None,
        "midline": None,
        "center": None,
        "left_x": -1.0,
        "right_x": -1.0,
        "left_tilt": -1.0,
        "right_tilt": -1.0,
        "candidate_count": 0,
        "left_candidate_count": 0,
        "right_candidate_count": 0,
        "score": 0.0,
    }
    if frame_bgr is None or not hasattr(frame_bgr, "shape"):
        return result
    if len(frame_bgr.shape) < 2 or corners is None:
        return result
    try:
        ordered = order_quad_points(corners)
    except (TypeError, ValueError):
        return result

    image_height, image_width = frame_bgr.shape[:2]
    top_width = _distance(ordered[0], ordered[1])
    bottom_width = _distance(ordered[3], ordered[2])
    left_height = _distance(ordered[0], ordered[3])
    right_height = _distance(ordered[1], ordered[2])
    box_width = max(4.0, (top_width + bottom_width) * 0.5)
    box_height = max(4.0, (left_height + right_height) * 0.5)
    axis_top_y = max(float(ordered[0][1]), float(ordered[1][1]))
    axis_bottom_y = min(float(ordered[3][1]), float(ordered[2][1]))
    if axis_bottom_y - axis_top_y < box_height * 0.35:
        axis_top_y = float(np.mean(ordered[:2, 1]))
        axis_bottom_y = float(np.mean(ordered[2:, 1]))
    if axis_bottom_y <= axis_top_y + 4.0:
        result["reason"] = "EDGE_BAD_HEIGHT"
        return result
    reference_y = (axis_top_y + axis_bottom_y) * 0.5

    band_half = max(
        float(getattr(config, "VERTICAL_EDGE_MIN_BAND_PX", 12)),
        box_width * float(getattr(
            config, "VERTICAL_EDGE_BAND_RATIO", 0.16)),
    )
    pad_y = box_height * 0.04
    crop = clamp_roi((
        float(np.min(ordered[:, 0])) - band_half,
        float(np.min(ordered[:, 1])) - pad_y,
        float(np.max(ordered[:, 0]) - np.min(ordered[:, 0])) +
            band_half * 2.0,
        float(np.max(ordered[:, 1]) - np.min(ordered[:, 1])) +
            pad_y * 2.0,
    ), image_width, image_height)
    crop_x, crop_y, crop_width, crop_height = crop
    if crop_width < 12 or crop_height < 12:
        result["reason"] = "EDGE_EMPTY_ROI"
        return result

    crop_bgr = frame_bgr[
        crop_y:crop_y + crop_height,
        crop_x:crop_x + crop_width,
    ]
    process_scale = max(0.20, min(1.0, float(getattr(
        config, "VERTICAL_EDGE_PROCESS_SCALE", 0.5))))
    process_width = max(12, int(round(crop_width * process_scale)))
    process_height = max(12, int(round(crop_height * process_scale)))
    scale_x = process_width / float(crop_width)
    scale_y = process_height / float(crop_height)
    if process_width != crop_width or process_height != crop_height:
        crop_bgr = cv2.resize(
            crop_bgr, (process_width, process_height),
            interpolation=cv2.INTER_AREA)
    if len(crop_bgr.shape) == 2:
        gray = crop_bgr
    elif crop_bgr.shape[2] == 4:
        gray = cv2.cvtColor(crop_bgr, cv2.COLOR_BGRA2GRAY)
    else:
        gray = cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2GRAY)
    kernel_size = max(3, int(getattr(
        config, "VERTICAL_EDGE_GAUSSIAN_KERNEL", 3)))
    if kernel_size % 2 == 0:
        kernel_size += 1
    gray = cv2.GaussianBlur(gray, (kernel_size, kernel_size), 0)
    edges = cv2.Canny(
        gray,
        int(getattr(config, "VERTICAL_EDGE_CANNY_LOW", 40)),
        int(getattr(config, "VERTICAL_EDGE_CANNY_HIGH", 120)),
    )
    min_length = max(6, int(round(
        box_height * scale_y * float(getattr(
            config, "VERTICAL_EDGE_MIN_LENGTH_RATIO", 0.25)))))
    max_gap = max(2, int(round(
        box_height * scale_y * float(getattr(
            config, "VERTICAL_EDGE_MAX_GAP_RATIO", 0.08)))))
    hough_lines = cv2.HoughLinesP(
        edges,
        1,
        np.pi / 180.0,
        threshold=max(6, int(getattr(
            config, "VERTICAL_EDGE_HOUGH_THRESHOLD", 18))),
        minLineLength=min_length,
        maxLineGap=max_gap,
    )
    if hough_lines is None:
        result["reason"] = "EDGE_NO_LINES"
        return result

    expected_left = (
        (float(ordered[0][0]), float(ordered[0][1])),
        (float(ordered[3][0]), float(ordered[3][1])),
    )
    expected_right = (
        (float(ordered[1][0]), float(ordered[1][1])),
        (float(ordered[2][0]), float(ordered[2][1])),
    )
    max_tilt = max(0.02, float(getattr(
        config, "VERTICAL_EDGE_MAX_TILT_DX_DY", 0.30)))
    left_candidates = []
    right_candidates = []

    for raw_line in hough_lines.reshape((-1, 4)):
        x1 = crop_x + float(raw_line[0]) / max(1e-6, scale_x)
        y1 = crop_y + float(raw_line[1]) / max(1e-6, scale_y)
        x2 = crop_x + float(raw_line[2]) / max(1e-6, scale_x)
        y2 = crop_y + float(raw_line[3]) / max(1e-6, scale_y)
        if y2 < y1:
            x1, x2 = x2, x1
            y1, y2 = y2, y1
        delta_y = y2 - y1
        if delta_y < box_height * float(getattr(
                config, "VERTICAL_EDGE_MIN_LENGTH_RATIO", 0.25)):
            continue
        tilt = abs(x2 - x1) / max(1.0, delta_y)
        if tilt > max_tilt:
            continue
        line = ((x1, y1), (x2, y2))
        middle_y = (y1 + y2) * 0.5
        middle_x = _line_x_at_y(line, middle_y)
        left_residual = abs(
            middle_x - _line_x_at_y(expected_left, middle_y))
        right_residual = abs(
            middle_x - _line_x_at_y(expected_right, middle_y))
        side = "left" if left_residual <= right_residual else "right"
        residual = min(left_residual, right_residual)
        if residual > band_half:
            continue

        reference_x = _line_x_at_y(line, reference_y)
        vertical_score = max(0.0, 1.0 - tilt / max_tilt)
        length_score = min(1.0, delta_y / max(1.0, box_height * 0.70))
        position_score = max(0.0, 1.0 - residual / band_half)
        prior_x = prior_left_x if side == "left" else prior_right_x
        temporal_score = 0.5
        if prior_x is not None:
            temporal_range = max(20.0, band_half * 1.5)
            temporal_score = max(
                0.0, 1.0 - abs(reference_x - float(prior_x)) /
                temporal_range)
        score = (
            0.48 * vertical_score +
            0.25 * length_score +
            0.20 * position_score +
            0.07 * temporal_score
        )
        candidate = {
            "line": line,
            "reference_x": reference_x,
            "tilt": tilt,
            "score": score,
        }
        if side == "left":
            left_candidates.append(candidate)
        else:
            right_candidates.append(candidate)

    result["candidate_count"] = len(left_candidates) + len(right_candidates)
    result["left_candidate_count"] = len(left_candidates)
    result["right_candidate_count"] = len(right_candidates)
    if not left_candidates or not right_candidates:
        result["reason"] = (
            "EDGE_LEFT_MISSING" if not left_candidates
            else "EDGE_RIGHT_MISSING")
        return result

    left_candidates.sort(key=lambda item: item["score"], reverse=True)
    right_candidates.sort(key=lambda item: item["score"], reverse=True)
    max_pair_candidates = max(1, int(getattr(
        config, "VERTICAL_EDGE_MAX_PAIR_CANDIDATES", 8)))
    expected_left_x = _line_x_at_y(expected_left, reference_y)
    expected_right_x = _line_x_at_y(expected_right, reference_y)
    expected_width = max(1.0, expected_right_x - expected_left_x)
    min_separation = expected_width * float(getattr(
        config, "VERTICAL_EDGE_MIN_SEPARATION_RATIO", 0.45))
    pairs = []
    for left in left_candidates[:max_pair_candidates]:
        for right in right_candidates[:max_pair_candidates]:
            separation = right["reference_x"] - left["reference_x"]
            if separation < min_separation:
                continue
            width_score = max(
                0.0,
                1.0 - abs(separation - expected_width) /
                max(1.0, band_half * 2.0),
            )
            pair_score = (
                0.425 * left["score"] +
                0.425 * right["score"] +
                0.15 * width_score
            )
            pairs.append((pair_score, left, right))
    if not pairs:
        result["reason"] = "EDGE_PAIR_REJECTED"
        return result

    pair_score, left, right = max(pairs, key=lambda item: item[0])
    left_line = (
        (_line_x_at_y(left["line"], axis_top_y), axis_top_y),
        (_line_x_at_y(left["line"], axis_bottom_y), axis_bottom_y),
    )
    right_line = (
        (_line_x_at_y(right["line"], axis_top_y), axis_top_y),
        (_line_x_at_y(right["line"], axis_bottom_y), axis_bottom_y),
    )
    # 水平控制只需要固定参考高度处的左右平均值。中轴强制画成竖线，避免两条
    # Hough边缘斜率的微小量化变化让中轴上端/下端来回摆动。
    center_x = (left["reference_x"] + right["reference_x"]) * 0.5
    midline = (
        (center_x, axis_top_y),
        (center_x, axis_bottom_y),
    )
    center = (center_x, reference_y)
    result.update({
        "valid": True,
        "reason": "OK",
        "left_line": left_line,
        "right_line": right_line,
        "midline": midline,
        "center": center,
        "left_x": left["reference_x"],
        "right_x": right["reference_x"],
        "left_tilt": left["tilt"],
        "right_tilt": right["tilt"],
        "score": pair_score,
    })
    return result


def _remap_scaled_result(result, full_roi, scale_x, scale_y,
                         frame_width, frame_height):
    """把缩小图上的结果恢复为原始相机图像坐标和像素量。"""
    roi_x, roi_y, _roi_width, _roi_height = full_roi
    scale_x = max(1e-9, float(scale_x))
    scale_y = max(1e-9, float(scale_y))
    area_scale = scale_x * scale_y
    edge_scale = (scale_x + scale_y) * 0.5

    mapped = dict(result)
    mapped["roi"] = tuple(full_roi)
    mapped["processing_scale_x"] = scale_x
    mapped["processing_scale_y"] = scale_y
    mapped["processing_scale"] = math.sqrt(area_scale)
    mapped["rejected_reasons"] = dict(
        result.get("rejected_reasons", {}))

    # 日志继续使用原始640x480像素尺度。这样缩放开关改变后，面积和边长门限、
    # 历史扫描曲线以及调参记录仍可以直接比较。
    mapped_candidates = []
    for candidate in result.get("rejected_candidates", []):
        mapped_candidate = dict(candidate)
        mapped_candidate["area_px"] = (
            float(candidate.get("area_px", 0.0)) / area_scale)
        mapped_candidate["min_edge_length"] = (
            float(candidate.get("min_edge_length", 0.0)) / edge_scale)
        mapped_candidate["aspect_ratio"] = (
            float(candidate.get("aspect_ratio", 0.0)) *
            scale_y / scale_x
        )
        mapped_candidates.append(mapped_candidate)
    mapped["rejected_candidates"] = mapped_candidates

    if not result.get("valid"):
        return mapped

    corners = [
        (
            roi_x + float(point[0]) / scale_x,
            roi_y + float(point[1]) / scale_y,
        )
        for point in result["corners"]
    ]
    center = (
        roi_x + float(result["cx"]) / scale_x,
        roi_y + float(result["cy"]) / scale_y,
    )
    area_px = float(result["area_px"]) / area_scale
    mapped.update({
        "corners": corners,
        "center": center,
        "cx": center[0],
        "cy": center[1],
        "dx": center[0] - frame_width * 0.5,
        "dy": center[1] - frame_height * 0.5,
        "area_px": area_px,
        "area_ratio_frame": (
            area_px / max(1.0, float(frame_width * frame_height))
        ),
        "aspect_ratio": (
            float(result["aspect_ratio"]) * scale_y / scale_x
        ),
    })
    return mapped


def detect_box_face_scaled(frame_bgr, roi=None, process_scale=0.5):
    """在缩小后的ROI上检测，并把输出恢复到原始图像坐标。

    面积门限按比例平方缩放，边长和核尺寸按比例缩放；面积输出、角点坐标、中心
    坐标和被拒候选指标再恢复为原始分辨率单位，所以上层控制算法不需要知道内部
    是否采用了半分辨率。
    """
    if frame_bgr is None or not hasattr(frame_bgr, "shape"):
        return _empty_result("EMPTY_FRAME")
    if len(frame_bgr.shape) < 2:
        return _empty_result("BAD_FRAME_SHAPE")

    process_scale = float(process_scale)
    if process_scale <= 0:
        raise ValueError("process_scale must be positive")
    process_scale = min(1.0, process_scale)

    frame_height, frame_width = frame_bgr.shape[:2]
    full_roi = clamp_roi(roi, frame_width, frame_height)
    roi_x, roi_y, roi_width, roi_height = full_roi
    if roi_width < 8 or roi_height < 8:
        result = _empty_result("EMPTY_ROI", full_roi)
        result["processing_scale"] = process_scale
        result["processing_scale_x"] = process_scale
        result["processing_scale_y"] = process_scale
        return result

    if process_scale >= 0.999:
        result = detect_box_face(frame_bgr, full_roi, pixel_scale=1.0)
        result["processing_scale"] = 1.0
        result["processing_scale_x"] = 1.0
        result["processing_scale_y"] = 1.0
        return result

    roi_bgr = frame_bgr[
        roi_y:roi_y + roi_height,
        roi_x:roi_x + roi_width,
    ]
    process_width = max(8, int(round(roi_width * process_scale)))
    process_height = max(8, int(round(roi_height * process_scale)))
    process_width = min(roi_width, process_width)
    process_height = min(roi_height, process_height)
    resized = cv2.resize(
        roi_bgr,
        (process_width, process_height),
        interpolation=cv2.INTER_AREA,
    )
    scale_x = float(process_width) / roi_width
    scale_y = float(process_height) / roi_height
    effective_scale = math.sqrt(scale_x * scale_y)
    result = detect_box_face(
        resized, roi=None, pixel_scale=effective_scale)
    return _remap_scaled_result(
        result,
        full_roi,
        scale_x,
        scale_y,
        frame_width,
        frame_height,
    )
