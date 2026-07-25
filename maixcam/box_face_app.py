"""MaixCAM2 箱体正面识别主循环。

``YOLO_MODEL_PATH`` 为空时在全图/手动 ROI 内定位；非空时由 YOLO11 先提供
粗 ROI。ROI 内的中心定位方法可在配置中二选一：

1. OpenCV 灰度、Canny、形态学和四边形拟合；
2. Maix 原生 LAB ``find_blobs()`` 连通域。

两种方法均不直接把 YOLO 框中心当成最终中心，输出字段和显示接口保持一致。
"""

import os

import cv2
import numpy as np

from maix import app, camera, display, image, nn, time

import vision_config as config
from box_face_geometry import (
    clamp_roi,
    detect_box_face,
    detect_box_face_scaled,
    detect_vertical_side_edges,
    empty_face_result,
    expand_box_roi,
)
from tof100_service import ToF100Device, centered_roi
from uart_link import VisionTofUartLink


COLOR_GREEN = (0, 255, 0)
COLOR_RED = (0, 0, 255)
COLOR_YELLOW = (0, 255, 255)
COLOR_MAGENTA = (255, 0, 255)
COLOR_CYAN = (255, 255, 0)
COLOR_WHITE = (255, 255, 255)

# 显示层直接在Maix原生图像上绘制，避免 ``cv2image`` 再复制一幅640x640图像。
NATIVE_GREEN = image.Color.from_rgb(0, 255, 0)
NATIVE_RED = image.Color.from_rgb(255, 0, 0)
NATIVE_YELLOW = image.Color.from_rgb(255, 255, 0)
NATIVE_MAGENTA = image.Color.from_rgb(255, 0, 255)
NATIVE_CYAN = image.Color.from_rgb(0, 255, 255)
NATIVE_WHITE = image.Color.from_rgb(255, 255, 255)
NATIVE_ORANGE = image.Color.from_rgb(255, 140, 0)


def resolve_model_path():
    """把配置中的相对路径转换为相对于本文件的绝对路径。"""
    model_path = str(config.YOLO_MODEL_PATH or "").strip()
    if not model_path:
        return None
    if not os.path.isabs(model_path):
        model_path = os.path.join(os.path.dirname(__file__), model_path)
    model_path = os.path.normpath(model_path)
    if not os.path.exists(model_path):
        raise FileNotFoundError("YOLO model not found: %s" % model_path)
    return model_path


def _snapshot_detection(obj):
    """立即复制 native YOLO 对象字段，后续只使用普通 Python 字典。"""
    return {
        "x": float(obj.x),
        "y": float(obj.y),
        "w": float(obj.w),
        "h": float(obj.h),
        "class_id": int(obj.class_id),
        "score": float(obj.score),
    }


def choose_yolo_target(objects):
    """筛选目标类别，并优先选择面积大、置信度高的粗框。"""
    candidates = []
    for obj in objects or ():
        detection = _snapshot_detection(obj)
        if detection["w"] <= 0 or detection["h"] <= 0:
            continue
        if (config.YOLO_TARGET_CLASS_ID >= 0 and
                detection["class_id"] != config.YOLO_TARGET_CLASS_ID):
            continue
        area = detection["w"] * detection["h"]
        # 面积是主排序量，置信度用于相近目标之间的选择。比赛场景若同时出现多个箱子，
        # 后续应再加入角度连续性或电控给出的搜索方向，不能只靠这一项。
        candidates.append((area * (0.5 + detection["score"]), detection))
    if not candidates:
        return None
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def _predict_cached_yolo_target(last_target, previous_target,
                                last_frame, previous_frame, current_frame,
                                image_width, image_height):
    """在跳过YOLO的中间帧线性预测粗框，并限制在当前图像内。"""
    if last_target is None:
        return None

    age = max(0, int(current_frame) - int(last_frame))
    predicted = dict(last_target)
    if (bool(getattr(config, "YOLO_PREDICT_CACHED_BOX", True)) and
            previous_target is not None and
            int(last_frame) > int(previous_frame)):
        frame_delta = float(int(last_frame) - int(previous_frame))
        for key in ("x", "y", "w", "h"):
            velocity = (
                float(last_target[key]) - float(previous_target[key])
            ) / frame_delta
            predicted[key] = float(last_target[key]) + velocity * age

    width = max(1.0, min(float(image_width), float(predicted["w"])))
    height = max(1.0, min(float(image_height), float(predicted["h"])))
    x = max(0.0, min(float(image_width) - width, float(predicted["x"])))
    y = max(0.0, min(float(image_height) - height, float(predicted["y"])))
    predicted.update({
        "x": x,
        "y": y,
        "w": width,
        "h": height,
        "cached": True,
        "age": age,
    })
    return predicted


def _is_near_yolo_target(target, image_width, image_height):
    """判断YOLO框是否已大到或靠边到可能看不全箱面。"""
    if target is None or image_width <= 0 or image_height <= 0:
        return False
    area_ratio = (
        float(target["w"]) * float(target["h"]) /
        max(1.0, float(image_width * image_height))
    )
    if area_ratio >= float(getattr(config, "YOLO_NEAR_AREA_RATIO", 0.55)):
        return True
    margin = max(0.0, float(getattr(
        config, "YOLO_NEAR_BORDER_MARGIN_PX", 3)))
    return (
        float(target["x"]) <= margin or
        float(target["y"]) <= margin or
        float(target["x"]) + float(target["w"]) >= image_width - margin or
        float(target["y"]) + float(target["h"]) >= image_height - margin
    )


def _full_track_snapshot(result, yolo_target, frame_index):
    """保存一帧有效中心，供目标部分出画时短时预测。"""
    return {
        "frame": int(frame_index),
        "cx": float(result["cx"]),
        "cy": float(result["cy"]),
        "yolo": None if yolo_target is None else dict(yolo_target),
    }


def _attach_partial_track(result, yolo_target, frame_index,
                          last_track, previous_track):
    """给结果附加独立track字段；绝不改变当前定位结果的 ``valid``。"""
    if result.get("valid") and result.get("center_is_final", True):
        track_source = (
            "EDGE_QUAD" if result.get("method") == "EDGE_QUAD"
            else ("COLOR_QUAD" if result.get("method") == "COLOR_QUAD"
            else "QUAD"
            )
        )
        result.update({
            "track_valid": True,
            "track_source": track_source,
            "track_cx": float(result["cx"]),
            "track_cy": float(result["cy"]),
            "track_dx": float(result["dx"]),
            "track_dy": float(result.get("dy", 0.0)),
            "track_age": 0,
        })
        return

    if result.get("valid") and not result.get("center_is_final", True):
        result.update({
            "track_valid": True,
            "track_source": result.get("center_source", "GUIDE"),
            "track_cx": float(result["cx"]),
            "track_cy": float(result["cy"]),
            "track_dx": float(result["dx"]),
            "track_dy": float(result.get("dy", 0.0)),
            "track_age": 0,
        })
        return

    result.update({
        "track_valid": False,
        "track_source": "NONE",
        "track_cx": -1.0,
        "track_cy": -1.0,
        "track_dx": -1.0,
        "track_dy": -1.0,
        "track_age": -1,
    })
    if (not bool(getattr(config, "PARTIAL_TRACK_ENABLED", True)) or
            last_track is None or yolo_target is None):
        return

    age = int(frame_index) - int(last_track["frame"])
    max_age = max(1, int(getattr(config, "PARTIAL_TRACK_MAX_FRAMES", 8)))
    if age <= 0 or age > max_age:
        return

    velocity_x = 0.0
    velocity_y = 0.0
    source = "HOLD"
    if (previous_track is not None and
            int(last_track["frame"]) > int(previous_track["frame"])):
        delta = float(int(last_track["frame"]) -
                      int(previous_track["frame"]))
        velocity_x = (float(last_track["cx"]) -
                      float(previous_track["cx"])) / delta
        velocity_y = (float(last_track["cy"]) -
                      float(previous_track["cy"])) / delta
        source = "VELOCITY"
    elif last_track.get("yolo") is not None:
        old_box = last_track["yolo"]
        old_x = float(old_box["x"]) + float(old_box["w"]) * 0.5
        old_y = float(old_box["y"]) + float(old_box["h"]) * 0.5
        new_x = float(yolo_target["x"]) + float(yolo_target["w"]) * 0.5
        new_y = float(yolo_target["y"]) + float(yolo_target["h"]) * 0.5
        velocity_x = (new_x - old_x) / max(1.0, float(age))
        velocity_y = (new_y - old_y) / max(1.0, float(age))
        source = "YOLO_SHIFT"

    track_cx = float(last_track["cx"]) + velocity_x * age
    track_cy = float(last_track["cy"]) + velocity_y * age
    frame_width = float(result.get("frame_width", 1.0))
    frame_height = float(result.get("frame_height", 1.0))
    result.update({
        "track_valid": True,
        "track_source": source,
        "track_cx": track_cx,
        "track_cy": track_cy,
        "track_dx": track_cx - frame_width * 0.5,
        "track_dy": track_cy - frame_height * 0.5,
        "track_age": age,
    })


def _create_pipeline():
    """按模型路径自动创建 YOLO 模式或纯传统视觉模式的原生相机。"""
    model_path = resolve_model_path()
    if model_path is None:
        detector = None
        cam = camera.Camera(
            config.CAMERA_WIDTH,
            config.CAMERA_HEIGHT,
            fps=config.CAMERA_FPS,
            buff_num=config.CAMERA_BUFFER_COUNT,
        )
        mode = "TRADITIONAL_ONLY"
    else:
        detector = nn.YOLO11(
            model=model_path,
            dual_buff=config.YOLO_DUAL_BUFF,
        )
        cam = camera.Camera(
            detector.input_width(),
            detector.input_height(),
            detector.input_format(),
            fps=config.CAMERA_FPS,
            buff_num=config.CAMERA_BUFFER_COUNT,
        )
        mode = "YOLO_PLUS_TRADITIONAL"
    return detector, cam, mode, model_path


def _full_resolution_fallback_due(consecutive_misses):
    """判断本次快速检测失败后是否应该运行一次原分辨率兜底。"""
    if not bool(getattr(config, "ENABLE_FULL_RES_FALLBACK", True)):
        return False
    if float(getattr(config, "PROCESS_SCALE", 0.5)) >= 0.999:
        return False
    first_miss = max(1, int(getattr(
        config, "FULL_RES_FALLBACK_AFTER_MISSES", 2)))
    interval = max(1, int(getattr(
        config, "FULL_RES_FALLBACK_INTERVAL", 3)))
    if consecutive_misses < first_miss:
        return False
    return (consecutive_misses - first_miss) % interval == 0


def _run_face_detection(frame_bgr, search_roi, consecutive_misses,
                        process_scale_override=None):
    """逐帧运行缩放检测，并在连续失败时低频执行原分辨率兜底。"""
    configured_scale = (
        getattr(config, "PROCESS_SCALE", 0.5)
        if process_scale_override is None else process_scale_override
    )
    process_scale = max(0.05, min(1.0, float(configured_scale)))
    fast_result = detect_box_face_scaled(
        frame_bgr, search_roi, process_scale=process_scale)
    fast_result["source"] = (
        "FULL" if process_scale >= 0.999 else "SCALED")
    fast_result["method"] = "OPENCV_QUAD"
    fast_result["fallback_used"] = False

    if fast_result.get("valid"):
        fast_result["fast_miss_count"] = 0
        return fast_result, 0

    miss_count = int(consecutive_misses) + 1
    fast_result["fast_miss_count"] = miss_count
    if not _full_resolution_fallback_due(miss_count):
        return fast_result, miss_count

    full_result = detect_box_face(frame_bgr, search_roi, pixel_scale=1.0)
    full_result["source"] = "FULL_FALLBACK"
    full_result["method"] = "OPENCV_QUAD"
    full_result["processing_scale"] = 1.0
    full_result["processing_scale_x"] = 1.0
    full_result["processing_scale_y"] = 1.0
    full_result["fallback_used"] = True
    full_result["fast_reason"] = fast_result.get("reason", "UNKNOWN")
    full_result["fast_miss_count"] = miss_count
    if full_result.get("valid"):
        return full_result, 0
    return full_result, miss_count


def _read_native_member(obj, name, default):
    """兼容Maix原生对象的属性式和方法式字段。"""
    value = getattr(obj, name, default)
    if callable(value):
        try:
            value = value()
        except Exception:
            return default
    return value


def _snapshot_blob(blob):
    """把Maix blob复制成普通字典，避免后续持有native对象。"""
    try:
        x = int(blob[0])
        y = int(blob[1])
        width = int(blob[2])
        height = int(blob[3])
    except Exception:
        x = int(_read_native_member(blob, "x", 0))
        y = int(_read_native_member(blob, "y", 0))
        width = int(_read_native_member(blob, "w", 0))
        height = int(_read_native_member(blob, "h", 0))
    pixels = int(_read_native_member(blob, "pixels", width * height))
    centroid_x = float(_read_native_member(
        blob, "cx", x + width * 0.5))
    centroid_y = float(_read_native_member(
        blob, "cy", y + height * 0.5))
    return {
        "x": x,
        "y": y,
        "w": width,
        "h": height,
        "pixels": pixels,
        "centroid_x": centroid_x,
        "centroid_y": centroid_y,
    }


def _horizontal_border_touch(x, width, image_width):
    """判断一个全图坐标框是否贴到整幅图像的左/右边缘。"""
    if not bool(getattr(
            config, "HORIZONTAL_BORDER_TOUCH_INCOMPLETE", True)):
        return False
    margin = max(
        0.0,
        float(getattr(config, "HORIZONTAL_BORDER_MARGIN_PX", 3)),
        float(image_width) * max(0.0, float(getattr(
            config, "HORIZONTAL_BORDER_MARGIN_RATIO", 0.025))),
    )
    return (
        float(x) <= margin or
        float(x) + float(width) >= float(image_width) - margin
    )


def _run_find_blobs_detection(frame, search_roi):
    """在YOLO ROI内运行Maix原生LAB find_blobs，并适配统一结果格式。"""
    image_width = int(frame.width())
    image_height = int(frame.height())
    roi = clamp_roi(search_roi, image_width, image_height)
    roi_x, roi_y, roi_width, roi_height = roi
    result = empty_face_result("NO_BLOB")
    result.update({
        "roi": roi,
        "source": "FIND_BLOBS",
        "processing_scale": 1.0,
        "processing_scale_x": 1.0,
        "processing_scale_y": 1.0,
        "fallback_used": False,
        "fast_miss_count": 0,
        "method": "NATIVE_FIND_BLOBS",
        "center_source": "BLOB_GUIDE",
        "center_is_final": False,
        "horizontal_valid": False,
        "horizontal_border_touch": False,
        "incomplete": False,
        "blob_pixels": 0,
        "threshold_pixels_total": 0,
        "blob_fill": 0.0,
        "blob_count": 0,
        "blob_rect": None,
    })
    if roi_width < 4 or roi_height < 4:
        result["reason"] = "EMPTY_ROI"
        return result

    roi_area = max(1, roi_width * roi_height)
    pixels_threshold = max(
        int(getattr(config, "FIND_BLOBS_MIN_PIXELS", 40)),
        int(round(roi_area * float(getattr(
            config, "FIND_BLOBS_PIXELS_RATIO_ROI", 0.01)))),
    )
    area_threshold = max(
        int(getattr(config, "FIND_BLOBS_MIN_AREA", 100)),
        int(round(roi_area * float(getattr(
            config, "FIND_BLOBS_AREA_RATIO_ROI", 0.03)))),
    )
    blobs = frame.find_blobs(
        config.FIND_BLOBS_LAB_THRESHOLDS,
        roi=[roi_x, roi_y, roi_width, roi_height],
        pixels_threshold=pixels_threshold,
        area_threshold=area_threshold,
        merge=bool(getattr(config, "FIND_BLOBS_MERGE", True)),
        margin=max(0, int(getattr(config, "FIND_BLOBS_MARGIN", 3))),
    )
    snapshots = []
    max_candidates = max(1, int(getattr(
        config, "FIND_BLOBS_MAX_CANDIDATES", 8)))
    for index, blob in enumerate(blobs or ()):
        if index >= max_candidates:
            break
        snapshots.append(_snapshot_blob(blob))
    result["blob_count"] = len(snapshots)
    result["threshold_pixels_total"] = sum(
        int(candidate["pixels"]) for candidate in snapshots)
    if not snapshots:
        return result

    min_width = roi_width * float(getattr(
        config, "FIND_BLOBS_MIN_WIDTH_RATIO_ROI", 0.20))
    min_height = roi_height * float(getattr(
        config, "FIND_BLOBS_MIN_HEIGHT_RATIO_ROI", 0.20))
    accepted = []
    for candidate in snapshots:
        if candidate["w"] < min_width or candidate["h"] < min_height:
            continue
        box_area = max(1.0, float(candidate["w"] * candidate["h"]))
        fill = max(0.0, min(1.0, candidate["pixels"] / box_area))
        rank = float(candidate["pixels"]) * (0.5 + fill)
        accepted.append((rank, candidate, fill, box_area))
    if not accepted:
        result["reason"] = "BLOB_REJECTED"
        result["rejected_reasons"] = {"BLOB_SIZE": len(snapshots)}
        return result

    accepted.sort(key=lambda item: item[0], reverse=True)
    _rank, best, fill, box_area = accepted[0]
    x = float(best["x"])
    y = float(best["y"])
    width = float(best["w"])
    height = float(best["h"])
    border_touch = _horizontal_border_touch(x, width, image_width)

    # 对矩形正面而言，色块外接框中心比像素质心更不易被亮斑和阴影拉偏；同时保留
    # 原生质心字段，便于实机比较两者差异。
    center_x = x + width * 0.5
    center_y = y + height * 0.5
    area_ratio_roi = box_area / roi_area
    score = max(0.0, min(
        1.0,
        0.70 * fill + 0.30 * min(1.0, area_ratio_roi / 0.60),
    ))
    corners = [
        (x, y),
        (x + width, y),
        (x + width, y + height),
        (x, y + height),
    ]
    result.update({
        "valid": True,
        "reason": "OK",
        "corners": corners,
        "center": (center_x, center_y),
        "cx": center_x,
        "cy": center_y,
        "dx": center_x - image_width * 0.5,
        "dy": center_y - image_height * 0.5,
        "horizontal_valid": True,
        "area_px": box_area,
        "area_ratio_roi": area_ratio_roi,
        "area_ratio_frame": (
            box_area / max(1.0, float(image_width * image_height))
        ),
        "aspect_ratio": width / max(1.0, height),
        "rectangularity": fill,
        "orthogonality": 1.0,
        "verticality": 1.0,
        "score": score,
        "blob_pixels": int(best["pixels"]),
        "blob_fill": fill,
        "blob_centroid_x": float(best["centroid_x"]),
        "blob_centroid_y": float(best["centroid_y"]),
        "blob_rect": (x, y, width, height),
        "horizontal_border_touch": border_touch,
        "incomplete": border_touch,
    })
    return result


def _attach_midline_from_corners(result, image_width, image_height):
    """用橙色Blob框或绿色四边形的左右两边计算水平中轴线。

    ``corners`` 顺序固定为左上、右上、右下、左下。中轴线上端是左右上角
    的中点，下端是左右下角的中点；控制中心取这条线段的中点。这里没有时间
    滤波和丢帧保持，因此云台运动时每个有效帧都会立即更新 ``cx/dx``。
    """
    result["horizontal_valid"] = False
    result["midline"] = None
    corners = result.get("corners")
    if not result.get("valid") or corners is None or len(corners) != 4:
        return result

    points = [
        (float(point[0]), float(point[1]))
        for point in corners
    ]
    top_mid = (
        (points[0][0] + points[1][0]) * 0.5,
        (points[0][1] + points[1][1]) * 0.5,
    )
    bottom_mid = (
        (points[3][0] + points[2][0]) * 0.5,
        (points[3][1] + points[2][1]) * 0.5,
    )
    center = (
        (top_mid[0] + bottom_mid[0]) * 0.5,
        (top_mid[1] + bottom_mid[1]) * 0.5,
    )
    result.update({
        "center": center,
        "cx": center[0],
        "cy": center[1],
        "dx": center[0] - float(image_width) * 0.5,
        "dy": center[1] - float(image_height) * 0.5,
        "horizontal_valid": True,
        "midline": (top_mid, bottom_mid),
        "midline_source": (
            "QUAD_SIDES" if result.get("center_is_final", True)
            else "BLOB_SIDES"),
    })
    return result


def _new_vertical_edge_state():
    """最近帧只保存竖边横坐标，用于下一帧候选选择，不平滑输出。"""
    return {
        "left_history": [],
        "right_history": [],
        "misses": 0,
        "filtered_cx": None,
    }


def _predict_vertical_edge_x(history):
    """按最近两帧速度预测候选位置；返回值只进入候选评分。"""
    if not history:
        return None
    if len(history) == 1:
        return float(history[-1])
    return float(history[-1]) + (
        float(history[-1]) - float(history[-2]))


def _vertical_edge_priors(state):
    return (
        _predict_vertical_edge_x(state.get("left_history", [])),
        _predict_vertical_edge_x(state.get("right_history", [])),
    )


def _apply_vertical_edge_result(result, edge_result, state,
                                image_width, image_height):
    """有效时用真实竖边覆盖框边中轴；失败时保留本帧框边中轴。"""
    result.update({
        "edge_valid": bool(edge_result.get("valid")),
        "edge_reason": edge_result.get("reason", "UNKNOWN"),
        "edge_candidate_count": int(edge_result.get("candidate_count", 0)),
        "edge_left_candidates": int(edge_result.get(
            "left_candidate_count", 0)),
        "edge_right_candidates": int(edge_result.get(
            "right_candidate_count", 0)),
        "edge_left_tilt": float(edge_result.get("left_tilt", -1.0)),
        "edge_right_tilt": float(edge_result.get("right_tilt", -1.0)),
        "edge_score": float(edge_result.get("score", 0.0)),
        "edge_left_line": edge_result.get("left_line"),
        "edge_right_line": edge_result.get("right_line"),
    })
    if not edge_result.get("valid"):
        state["misses"] = int(state.get("misses", 0)) + 1
        max_misses = max(0, int(getattr(
            config, "VERTICAL_EDGE_HISTORY_MAX_MISSES", 2)))
        if state["misses"] > max_misses:
            state["left_history"] = []
            state["right_history"] = []
        return result

    state["misses"] = 0
    history_frames = max(1, int(getattr(
        config, "VERTICAL_EDGE_HISTORY_FRAMES", 4)))
    for key, value in (
            ("left_history", edge_result.get("left_x")),
            ("right_history", edge_result.get("right_x"))):
        history = state.setdefault(key, [])
        history.append(float(value))
        if len(history) > history_frames:
            del history[:-history_frames]

    center = edge_result["center"]
    result.update({
        "center": center,
        "cx": float(center[0]),
        "cy": float(center[1]),
        "dx": float(center[0]) - float(image_width) * 0.5,
        "dy": float(center[1]) - float(image_height) * 0.5,
        "horizontal_valid": True,
        "midline": edge_result["midline"],
        "midline_source": "EDGE_SIDES",
    })
    return result


def _stabilize_horizontal_axis(result, state, image_width):
    """用死区和自适应EMA稳定中轴；大位移仍在当前帧快速跟随。

    青色左右边保持原始检测结果不变，只把控制/显示中轴强制为经过低延迟处理的
    竖线。这里没有固定帧窗口，也不会在无效帧伪造有效中心。
    """
    if (not result.get("valid") or
            not result.get("horizontal_valid") or
            result.get("center") is None or
            result.get("midline") is None):
        max_misses = max(0, int(getattr(
            config, "VERTICAL_EDGE_HISTORY_MAX_MISSES", 2)))
        if int(state.get("misses", 0)) > max_misses:
            state["filtered_cx"] = None
        result["axis_filter_alpha"] = 0.0
        result["axis_filter_error"] = 0.0
        return result

    raw_cx = float(result["cx"])
    filtered_cx = state.get("filtered_cx")
    if filtered_cx is None:
        filtered_cx = raw_cx
        error = 0.0
        alpha = 1.0
    else:
        filtered_cx = float(filtered_cx)
        error = raw_cx - filtered_cx
        absolute_error = abs(error)
        deadband = max(0.0, float(getattr(
            config, "AXIS_FILTER_DEADBAND_PX", 1.5)))
        fast_threshold = max(deadband, float(getattr(
            config, "AXIS_FILTER_FAST_THRESHOLD_PX", 6.0)))
        if absolute_error <= deadband:
            alpha = 0.0
        elif absolute_error <= fast_threshold:
            alpha = max(0.0, min(1.0, float(getattr(
                config, "AXIS_FILTER_SLOW_ALPHA", 0.30))))
        else:
            alpha = max(0.0, min(1.0, float(getattr(
                config, "AXIS_FILTER_FAST_ALPHA", 0.80))))
        filtered_cx += alpha * error
    state["filtered_cx"] = filtered_cx

    center_y = float(result["center"][1])
    midline = result["midline"]
    top_y = float(midline[0][1])
    bottom_y = float(midline[1][1])
    result.update({
        "axis_raw_cx": raw_cx,
        "axis_filter_error": error,
        "axis_filter_alpha": alpha,
        "center": (filtered_cx, center_y),
        "cx": filtered_cx,
        "dx": filtered_cx - float(image_width) * 0.5,
        "midline": ((filtered_cx, top_y), (filtered_cx, bottom_y)),
    })
    return result


def _new_area_peak_state():
    """创建颜色面积峰值跟踪状态。状态只服务于当前一次扫描。"""
    return {
        "history": [],
        "ema": None,
        "peak_area": 0.0,
        "peak_frame": -1,
        "confirmed": False,
        "corner_mode": False,
        "corner_misses": 0,
        "last_guide": None,
        "last_corner": None,
    }


def _update_area_peak_state(state, result, frame_index):
    """判断find_blobs颜色像素面积是否已经形成稳定峰值。

    这里不把单帧最大值直接当作正面，而是要求最近窗口内的总阈值面积变化小于
    容差。只要面积形成平台即可确认，不再要求Blob中心稳定或峰值出现在窗口末尾。
    """
    # 使用所有find_blobs候选的阈值像素总和，而不是误差较大的最佳Blob外接框。
    area = float(result.get(
        "threshold_pixels_total", result.get("blob_pixels", 0.0)))
    if (not result.get("valid") or area <= 0.0 or
            result.get("incomplete")):
        state["history"] = []
        state["ema"] = None
        state["peak_area"] = 0.0
        state["peak_frame"] = -1
        state["confirmed"] = False
        return {
            "stable": False,
            "area": 0.0,
            "peak_area": 0.0,
            "peak_frame": -1,
            "peak_age": -1,
        }

    alpha = 0.35
    previous_ema = state.get("ema")
    ema = area if previous_ema is None else (
        alpha * area + (1.0 - alpha) * float(previous_ema)
    )
    state["ema"] = ema
    history = state.setdefault("history", [])
    history.append((int(frame_index), float(ema)))
    window = max(3, int(getattr(
        config, "AREA_PEAK_WINDOW_FRAMES", 8)))
    if len(history) > window:
        del history[:-window]

    peak_frame, peak_area = max(history, key=lambda item: item[1])
    state["peak_area"] = float(peak_area)
    state["peak_frame"] = int(peak_frame)
    tolerance = max(0.0, float(getattr(
        config, "AREA_PEAK_TOLERANCE", 0.08)))
    confirm_frames = max(3, int(getattr(
        config, "AREA_PEAK_CONFIRM_FRAMES", 5)))
    values = [item[1] for item in history]
    span_ratio = (
        (max(values) - min(values)) / max(1.0, float(peak_area))
    )
    stable = (
        len(history) >= confirm_frames and
        span_ratio <= tolerance and
        ema >= peak_area * (1.0 - tolerance)
    )
    release_ratio = max(0.0, min(1.0, float(getattr(
        config, "AREA_PEAK_RELEASE_RATIO", 0.86))))
    if state.get("confirmed") and area < peak_area * release_ratio:
        state["confirmed"] = False
    elif stable:
        state["confirmed"] = True

    return {
        "stable": bool(state.get("confirmed")),
        "area": float(ema),
        "peak_area": float(peak_area),
        "peak_frame": int(peak_frame),
        "peak_age": int(frame_index) - int(peak_frame),
    }


def _run_green_edge_quad_refinement(frame, search_roi, guide_result,
                                    frame_bgr=None):
    """面积稳定后切回早期灰度+Canny四边形绿框，不使用颜色轮廓定中心。"""
    if frame_bgr is None:
        frame_bgr = image.image2cv(frame, ensure_bgr=True, copy=True)
    image_height, image_width = frame_bgr.shape[:2]
    result = detect_box_face_scaled(
        frame_bgr,
        search_roi,
        process_scale=max(0.20, min(1.0, float(getattr(
            config, "GREEN_QUAD_PROCESS_SCALE", 0.5)))),
    )
    result["source"] = "EDGE_QUAD"
    result["method"] = "EDGE_QUAD"
    result["fallback_used"] = False
    result["center_source"] = "EDGE_QUAD"
    result["center_is_final"] = bool(result.get("valid"))
    result["horizontal_border_touch"] = False
    result["incomplete"] = False
    result["guide_cx"] = float(guide_result.get("cx", -1.0))
    result["guide_cy"] = float(guide_result.get("cy", -1.0))
    result["guide_dx"] = float(guide_result.get("dx", -1.0))
    result["guide_area_px"] = float(guide_result.get(
        "threshold_pixels_total", guide_result.get("blob_pixels", 0.0)))
    result["threshold_pixels_total"] = int(guide_result.get(
        "threshold_pixels_total", guide_result.get("blob_pixels", 0)))
    result["blob_pixels"] = int(guide_result.get("blob_pixels", 0))
    result["blob_fill"] = float(guide_result.get("blob_fill", 0.0))
    result["blob_count"] = int(guide_result.get("blob_count", 0))
    result["blob_centroid_x"] = float(
        guide_result.get("blob_centroid_x", -1.0))
    result["blob_centroid_y"] = float(
        guide_result.get("blob_centroid_y", -1.0))

    if result.get("valid") and result.get("corners"):
        x_values = [float(point[0]) for point in result["corners"]]
        border_touch = _horizontal_border_touch(
            min(x_values), max(x_values) - min(x_values), image_width)
        if border_touch:
            result["valid"] = False
            result["reason"] = "EDGE_QUAD_HORIZONTAL_BORDER"
            result["center_is_final"] = False
            result["horizontal_border_touch"] = True
            result["incomplete"] = True
    return result


def _selected_center_method():
    """检查两个布尔开关互斥，并返回本次实际使用的方法名。"""
    use_quad = bool(getattr(config, "USE_OPENCV_QUAD_METHOD", True))
    use_blobs = bool(getattr(
        config, "USE_NATIVE_FIND_BLOBS_METHOD", False))
    if use_quad == use_blobs:
        raise ValueError(
            "exactly one of USE_OPENCV_QUAD_METHOD and "
            "USE_NATIVE_FIND_BLOBS_METHOD must be True"
        )
    return "OPENCV_QUAD" if use_quad else "NATIVE_FIND_BLOBS"


def _draw_crosshair(canvas):
    height, width = canvas.shape[:2]
    center_x = width // 2
    center_y = height // 2
    half_length = 12
    gap = 3
    cv2.line(
        canvas, (center_x - half_length, center_y),
        (center_x - gap, center_y), COLOR_CYAN, 2)
    cv2.line(
        canvas, (center_x + gap, center_y),
        (center_x + half_length, center_y), COLOR_CYAN, 2)
    cv2.line(
        canvas, (center_x, center_y - half_length),
        (center_x, center_y - gap), COLOR_CYAN, 2)
    cv2.line(
        canvas, (center_x, center_y + gap),
        (center_x, center_y + half_length), COLOR_CYAN, 2)


def _draw_roi(canvas, roi):
    if roi is None:
        return
    x, y, width, height = roi
    if width <= 0 or height <= 0:
        return
    cv2.rectangle(
        canvas,
        (int(x), int(y)),
        (int(x + width - 1), int(y + height - 1)),
        COLOR_YELLOW,
        1,
    )


def _draw_yolo_box(canvas, detection):
    if detection is None:
        return
    x1 = int(round(detection["x"]))
    y1 = int(round(detection["y"]))
    x2 = int(round(detection["x"] + detection["w"]))
    y2 = int(round(detection["y"] + detection["h"]))
    cv2.rectangle(canvas, (x1, y1), (x2, y2), COLOR_MAGENTA, 2)
    cv2.putText(
        canvas,
        "YOLO c%d %.2f" % (
            detection["class_id"], detection["score"]),
        (x1, max(16, y1 - 5)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.48,
        COLOR_MAGENTA,
        1,
        cv2.LINE_AA,
    )


def draw_debug_overlay(canvas, result, search_roi, yolo_target, fps):
    """在 OpenCV BGR 图像上画粗框、精定位四边形、中心和质量指标。"""
    _draw_crosshair(canvas)
    _draw_roi(canvas, search_roi)
    _draw_yolo_box(canvas, yolo_target)

    valid = bool(result.get("valid"))
    is_guide = valid and not result.get("center_is_final", True)
    color = COLOR_YELLOW if is_guide else (COLOR_GREEN if valid else COLOR_RED)
    corners = result.get("corners")
    center = result.get("center")
    midline = result.get("midline")
    if valid and corners is not None and center is not None:
        points = np.asarray(corners, dtype=np.int32).reshape((-1, 1, 2))
        cv2.polylines(
            canvas, [points], True, color, 2, cv2.LINE_AA)
        if result.get("edge_valid"):
            for edge_line in (
                    result.get("edge_left_line"),
                    result.get("edge_right_line")):
                if edge_line is None:
                    continue
                edge_top = tuple(
                    int(round(value)) for value in edge_line[0])
                edge_bottom = tuple(
                    int(round(value)) for value in edge_line[1])
                cv2.line(
                    canvas, edge_top, edge_bottom,
                    COLOR_CYAN, 2, cv2.LINE_AA)
        if result.get("horizontal_valid") and midline is not None:
            top = tuple(int(round(value)) for value in midline[0])
            bottom = tuple(int(round(value)) for value in midline[1])
            cv2.line(canvas, top, bottom, color, 3, cv2.LINE_AA)
        center_point = (int(round(center[0])), int(round(center[1])))
        cv2.circle(canvas, center_point, 5, color, -1)

    method_short = "GUIDE" if is_guide else (
        "EDGE" if result.get("method") == "EDGE_QUAD" else
        ("COLOR" if result.get("method") == "COLOR_QUAD" else
        ("BLOB" if result.get("method") == "NATIVE_FIND_BLOBS" else "QUAD")
        )
    )
    status_text = (
        ("FACE GUIDE EDGE" if result.get("incomplete")
         else "FACE GUIDE BLOB") if is_guide else
        ("FACE OK %s" % method_short if valid else
        "FACE %s %s" % (result.get("reason", "INVALID"), method_short)
        )
    )
    cv2.putText(
        canvas, status_text, (8, 22),
        cv2.FONT_HERSHEY_SIMPLEX, 0.58, color, 2, cv2.LINE_AA)

    if valid:
        metrics_text = "dx:%+.1f area:%.0f score:%.2f" % (
            result["dx"], result["area_px"], result["score"])
    elif result.get("method") == "NATIVE_FIND_BLOBS":
        metrics_text = "blob:%d" % result.get("blob_count", 0)
    else:
        metrics_text = "quad:%d contour:%d" % (
            result.get("quad_candidate_count", 0),
            result.get("contour_count", 0),
        )
    cv2.putText(
        canvas, metrics_text, (8, 44),
        cv2.FONT_HERSHEY_SIMPLEX, 0.48, color, 1, cv2.LINE_AA)
    source = result.get("source", "-")
    process_scale = float(result.get("processing_scale", 1.0))
    cv2.putText(
        canvas, "FPS %.1f %s x%.2f" % (fps, source, process_scale), (8, 64),
        cv2.FONT_HERSHEY_SIMPLEX, 0.45, COLOR_WHITE, 1, cv2.LINE_AA)


def draw_debug_overlay_native(canvas, result, search_roi, yolo_target, fps):
    """直接在Maix图像上绘制，避免OpenCV数组再转换、复制成显示图像。"""
    width = int(canvas.width())
    height = int(canvas.height())
    center_x = width // 2
    center_y = height // 2
    half_length = 12
    gap = 3
    canvas.draw_line(center_x - half_length, center_y,
                     center_x - gap, center_y, NATIVE_CYAN, 2)
    canvas.draw_line(center_x + gap, center_y,
                     center_x + half_length, center_y, NATIVE_CYAN, 2)
    canvas.draw_line(center_x, center_y - half_length,
                     center_x, center_y - gap, NATIVE_CYAN, 2)
    canvas.draw_line(center_x, center_y + gap,
                     center_x, center_y + half_length, NATIVE_CYAN, 2)

    if search_roi is not None:
        x, y, roi_width, roi_height = [int(round(v)) for v in search_roi]
        if roi_width > 0 and roi_height > 0:
            canvas.draw_rect(x, y, roi_width, roi_height,
                             NATIVE_YELLOW, 1)

    if yolo_target is not None:
        x = int(round(yolo_target["x"]))
        y = int(round(yolo_target["y"]))
        box_width = int(round(yolo_target["w"]))
        box_height = int(round(yolo_target["h"]))
        canvas.draw_rect(x, y, box_width, box_height,
                         NATIVE_MAGENTA, 2)
        age = int(yolo_target.get("age", 0))
        prefix = "YOLO~%d" % age if age > 0 else "YOLO"
        canvas.draw_string(
            x, max(0, y - 18),
            "%s c%d %.2f" % (
                prefix,
                yolo_target["class_id"],
                yolo_target["score"],
            ),
            NATIVE_MAGENTA,
        )

    valid = bool(result.get("valid"))
    is_guide = valid and not result.get("center_is_final", True)
    corners = result.get("corners")
    center = result.get("center")
    midline = result.get("midline")
    draw_color = NATIVE_ORANGE if is_guide else NATIVE_GREEN
    if valid and corners is not None and center is not None:
        points = [
            (int(round(point[0])), int(round(point[1])))
            for point in corners
        ]
        for index in range(4):
            x1, y1 = points[index]
            x2, y2 = points[(index + 1) % 4]
            canvas.draw_line(x1, y1, x2, y2, draw_color, 2)
        if result.get("edge_valid"):
            for edge_line in (
                    result.get("edge_left_line"),
                    result.get("edge_right_line")):
                if edge_line is None:
                    continue
                edge_top_x, edge_top_y = [
                    int(round(value)) for value in edge_line[0]]
                edge_bottom_x, edge_bottom_y = [
                    int(round(value)) for value in edge_line[1]]
                canvas.draw_line(
                    edge_top_x, edge_top_y,
                    edge_bottom_x, edge_bottom_y,
                    NATIVE_CYAN, 2)
        if result.get("horizontal_valid") and midline is not None:
            top_x, top_y = [int(round(value)) for value in midline[0]]
            bottom_x, bottom_y = [int(round(value)) for value in midline[1]]
            canvas.draw_line(
                top_x, top_y, bottom_x, bottom_y, draw_color, 3)
        canvas.draw_circle(int(round(center[0])), int(round(center[1])),
                           5, draw_color, -1)
    elif result.get("track_valid"):
        track_x = float(result.get("track_cx", -1.0))
        track_y = float(result.get("track_cy", -1.0))
        draw_x = max(10, min(width - 11, int(round(track_x))))
        draw_y = max(10, min(height - 11, int(round(track_y))))
        canvas.draw_line(draw_x - 8, draw_y, draw_x + 8, draw_y,
                         NATIVE_ORANGE, 2)
        canvas.draw_line(draw_x, draw_y - 8, draw_x, draw_y + 8,
                         NATIVE_ORANGE, 2)
        direction = ""
        if track_x < 0:
            direction = " LEFT"
        elif track_x >= width:
            direction = " RIGHT"
        canvas.draw_string(
            8, 64,
            "TRACK %s age:%d%s" % (
                result.get("track_source", "-"),
                int(result.get("track_age", -1)),
                direction,
            ),
            NATIVE_ORANGE,
        )

    color = draw_color if valid else NATIVE_RED
    near_text = " NEAR" if result.get("near_target") else ""
    method_short = "GUIDE" if is_guide else (
        "EDGE" if result.get("method") == "EDGE_QUAD" else
        ("COLOR" if result.get("method") == "COLOR_QUAD" else
        ("BLOB" if result.get("method") == "NATIVE_FIND_BLOBS" else "QUAD")
        )
    )
    status_text = (
        (("FACE GUIDE EDGE%s" if result.get("incomplete")
          else "FACE GUIDE BLOB%s") % near_text) if is_guide else
        ("FACE OK %s%s" % (method_short, near_text) if valid else
        "FACE %s %s%s" % (
            result.get("reason", "INVALID"), method_short, near_text)
        )
    )
    canvas.draw_string(8, 4, status_text, color)
    if valid:
        metrics_text = "dx:%+.1f area:%.0f score:%.2f" % (
            result["dx"], result["area_px"], result["score"])
    elif result.get("method") == "NATIVE_FIND_BLOBS":
        metrics_text = "blob:%d" % result.get("blob_count", 0)
    else:
        metrics_text = "quad:%d contour:%d" % (
            result.get("quad_candidate_count", 0),
            result.get("contour_count", 0),
        )
    canvas.draw_string(8, 24, metrics_text, color)
    canvas.draw_string(
        8, 44,
        "FPS %.1f %s x%.2f" % (
            fps,
            result.get("source", "-"),
            float(result.get("processing_scale", 1.0)),
        ),
        NATIVE_WHITE,
    )


def _format_face_log(result, yolo_target, fps, frame_index,
                     yolo_ran=True, stage_timing=None):
    if result.get("valid"):
        cx = "%.2f" % result["cx"]
        cy = "%.2f" % result["cy"]
        dx = "%.2f" % result["dx"]
        area_px = "%.1f" % result["area_px"]
        area_ratio = "%.5f" % result["area_ratio_frame"]
        face_score = "%.3f" % result["score"]
    else:
        cx = cy = dx = area_px = area_ratio = face_score = "-1"
    yolo_score = (
        "-1" if yolo_target is None
        else "%.3f" % yolo_target["score"]
    )
    rejected_reasons = result.get("rejected_reasons", {})
    if rejected_reasons:
        reject_summary = ",".join(
            "%s:%d" % (reason, rejected_reasons[reason])
            for reason in sorted(rejected_reasons)
        )
    else:
        reject_summary = "NONE"
    source = result.get("source", "NONE")
    method = result.get("method", "UNKNOWN")
    center_source = result.get("center_source", "FINAL")
    center_final = 1 if result.get("center_is_final", True) else 0
    horizontal_valid = 1 if result.get("horizontal_valid") else 0
    midline_source = result.get("midline_source", "NONE")
    edge_valid = 1 if result.get("edge_valid") else 0
    edge_reason = result.get("edge_reason", "NONE")
    edge_candidates = int(result.get("edge_candidate_count", 0))
    edge_left_candidates = int(result.get("edge_left_candidates", 0))
    edge_right_candidates = int(result.get("edge_right_candidates", 0))
    edge_left_tilt = float(result.get("edge_left_tilt", -1.0))
    edge_right_tilt = float(result.get("edge_right_tilt", -1.0))
    edge_score = float(result.get("edge_score", 0.0))
    area_peak_stable = 1 if result.get("area_peak_stable") else 0
    area_peak = float(result.get("area_peak", -1.0))
    guide_area = float(result.get("guide_area_px", -1.0))
    threshold_pixels_total = int(result.get("threshold_pixels_total", -1))
    corner_mode = 1 if result.get("corner_mode") else 0
    axis_raw_cx = float(result.get("axis_raw_cx", -1.0))
    axis_filter_error = float(result.get("axis_filter_error", 0.0))
    axis_filter_alpha = float(result.get("axis_filter_alpha", 0.0))
    color_mask_pixels = int(result.get("color_mask_pixels", -1))
    incomplete = 1 if result.get("incomplete") else 0
    horizontal_border_touch = (
        1 if result.get("horizontal_border_touch") else 0)
    refine_reason = result.get("refine_reason", "NONE")
    if method in ("NATIVE_FIND_BLOBS", "COLOR_QUAD", "EDGE_QUAD"):
        blob_count = int(result.get("blob_count", 0))
        blob_pixels = int(result.get("blob_pixels", 0))
        blob_fill = float(result.get("blob_fill", 0.0))
        blob_cx = float(result.get("blob_centroid_x", -1.0))
        blob_cy = float(result.get("blob_centroid_y", -1.0))
    else:
        blob_count = -1
        blob_pixels = -1
        blob_fill = -1.0
        blob_cx = -1.0
        blob_cy = -1.0
    process_scale = float(result.get("processing_scale", 1.0))
    fallback_used = 1 if result.get("fallback_used") else 0
    fast_miss_count = int(result.get("fast_miss_count", 0))
    yolo_age = 0 if yolo_target is None else int(yolo_target.get("age", 0))
    near_target = 1 if result.get("near_target") else 0
    track_valid = 1 if result.get("track_valid") else 0
    track_source = result.get("track_source", "NONE")
    track_cx = float(result.get("track_cx", -1.0))
    track_dx = float(result.get("track_dx", -1.0))
    track_age = int(result.get("track_age", -1))
    yolo_area_ratio = -1.0
    if yolo_target is not None:
        frame_width = max(1.0, float(result.get("frame_width", 1.0)))
        frame_height = max(1.0, float(result.get("frame_height", 1.0)))
        yolo_area_ratio = (
            float(yolo_target["w"]) * float(yolo_target["h"]) /
            (frame_width * frame_height)
        )
    timing_text = "NONE"
    if stage_timing:
        timing_text = ",".join(
            "%s:%.1f" % (name, float(stage_timing.get(name, 0.0)))
            for name in ("cam", "yolo", "cv", "trad", "tof", "disp")
        )
    return (
        "BOX_FACE|valid=%d|reason=%s|cx=%s|cy=%s|dx=%s|"
        "area_px=%s|area_ratio_frame=%s|face_score=%s|"
        "method=%s|blob_count=%d|blob_pixels=%d|blob_fill=%.4f|"
        "blob_cx=%.2f|blob_cy=%.2f|"
        "center_source=%s|center_final=%d|horizontal_valid=%d|"
        "midline_source=%s|edge_valid=%d|edge_reason=%s|"
        "edge_candidates=%d/%d/%d|edge_tilt=%.3f/%.3f|"
        "edge_score=%.3f|axis_raw_cx=%.2f|axis_error=%.2f|"
        "axis_alpha=%.2f|corner_mode=%d|area_peak_stable=%d|"
        "area_peak=%.1f|guide_area=%.1f|threshold_pixels=%d|"
        "color_mask_pixels=%d|"
        "incomplete=%d|horizontal_border=%d|refine_reason=%s|"
        "yolo_score=%s|quad=%d|contour=%d|rejects=%s|"
        "source=%s|scale=%.3f|fallback=%d|fast_miss=%d|"
        "yolo_ran=%d|yolo_age=%d|yolo_area=%.4f|near=%d|"
        "track_valid=%d|track_source=%s|track_cx=%.2f|"
        "track_dx=%.2f|track_age=%d|"
        "timing_ms=%s|fps=%.2f|frame=%d" % (
            1 if result.get("valid") else 0,
            result.get("reason", "UNKNOWN"),
            cx,
            cy,
            dx,
            area_px,
            area_ratio,
            face_score,
            method,
            blob_count,
            blob_pixels,
            blob_fill,
            blob_cx,
            blob_cy,
            center_source,
            center_final,
            horizontal_valid,
            midline_source,
            edge_valid,
            edge_reason,
            edge_candidates,
            edge_left_candidates,
            edge_right_candidates,
            edge_left_tilt,
            edge_right_tilt,
            edge_score,
            axis_raw_cx,
            axis_filter_error,
            axis_filter_alpha,
            corner_mode,
            area_peak_stable,
            area_peak,
            guide_area,
            threshold_pixels_total,
            color_mask_pixels,
            incomplete,
            horizontal_border_touch,
            refine_reason,
            yolo_score,
            result.get("quad_candidate_count", 0),
            result.get("contour_count", 0),
            reject_summary,
            source,
            process_scale,
            fallback_used,
            fast_miss_count,
            1 if yolo_ran else 0,
            yolo_age,
            yolo_area_ratio,
            near_target,
            track_valid,
            track_source,
            track_cx,
            track_dx,
            track_age,
            timing_text,
            fps,
            frame_index,
        )
    )


def _failed_rule_text(candidate):
    """把首个失败原因转换成可直接与实测值比较的门限表达式。"""
    reason = candidate.get("reason", "UNKNOWN")
    if reason == "AREA_PX":
        return "area>=%.0f" % config.MIN_QUAD_AREA_PX
    if reason == "AREA_SMALL":
        return "ratio>=%.3f" % config.MIN_QUAD_AREA_RATIO
    if reason == "AREA_LARGE":
        return "ratio<=%.3f" % config.MAX_QUAD_AREA_RATIO
    if reason == "ASPECT":
        return "%.3f<=aspect<=%.3f" % (
            config.MIN_ASPECT_RATIO, config.MAX_ASPECT_RATIO)
    if reason == "RECTANGULARITY":
        return "rect>=%.3f" % config.MIN_RECTANGULARITY
    if reason == "ORTHOGONALITY":
        return "ortho>=%.3f" % config.MIN_ORTHOGONALITY
    if reason == "VERTICALITY":
        return "vert>=%.3f" % config.MIN_VERTICALITY
    if reason == "EDGE_LENGTH":
        return "edge>=%.1f" % config.MIN_EDGE_LENGTH_PX
    if reason == "BORDER_TOUCH":
        return "border=0"
    return "UNKNOWN"


def _format_rejected_candidate_logs(result, frame_index):
    """生成被拒候选明细；只由定时报告调用，不会逐帧刷终端。"""
    candidates = result.get("rejected_candidates", [])
    limit = max(0, int(config.MAX_REJECTED_CANDIDATES_TO_REPORT))
    lines = []
    for rank, candidate in enumerate(candidates[:limit], start=1):
        lines.append(
            "BOX_REJECT|frame=%d|rank=%d|reason=%s|rule=%s|"
            "area_px=%.1f|area_ratio_roi=%.5f|aspect=%.3f|"
            "rect=%.3f|ortho=%.3f|vert=%.3f|edge=%.1f|"
            "border=%d|score=%.3f" % (
                frame_index,
                rank,
                candidate.get("reason", "UNKNOWN"),
                _failed_rule_text(candidate),
                candidate.get("area_px", 0.0),
                candidate.get("area_ratio", 0.0),
                candidate.get("aspect_ratio", 0.0),
                candidate.get("rectangularity", 0.0),
                candidate.get("orthogonality", 0.0),
                candidate.get("verticality", 0.0),
                candidate.get("min_edge_length", 0.0),
                1 if candidate.get("touches_border") else 0,
                candidate.get("score", 0.0),
            )
        )
    return lines


def _create_tof_sensor():
    """按正式配置创建TOF对象；硬件在main()的try块中打开。"""
    if not bool(getattr(config, "TOF_ENABLED", True)):
        return None
    return ToF100Device(
        resolution=int(getattr(config, "TOF_RESOLUTION", 50)),
        spi_bus_num=int(getattr(config, "TOF_SPI_BUS_NUM", 2)),
        spi_cs_num=int(getattr(config, "TOF_SPI_CS_NUM", -1)),
        valid_min_mm=int(getattr(config, "TOF_VALID_MIN_MM", 200)),
        valid_max_mm=int(getattr(config, "TOF_VALID_MAX_MM", 1500)),
        display_min_mm=int(getattr(config, "TOF_DISPLAY_MIN_MM", 200)),
        display_max_mm=int(getattr(config, "TOF_DISPLAY_MAX_MM", 1500)),
        rotate_180=bool(getattr(config, "TOF_ROTATE_180", False)),
        configure_pins=bool(getattr(config, "TOF_CONFIGURE_PINS", True)),
    )


def _invalid_tof_stats(reason):
    """构造字段完整的TOF无效结果，便于终端和未来串口统一编码。"""
    return {
        "valid": False,
        "reason": str(reason),
        "valid_count": 0,
        "total_count": 0,
        "median_mm": None,
        "mad_mm": None,
    }


def _build_vision_tof_packet(result, tof_frame, tof_stats, frame_index):
    """生成与输出介质无关的视觉+距离数据包字典。"""
    center_valid = bool(
        result.get("valid") and result.get("center") is not None)
    center_final = bool(
        center_valid and result.get("center_is_final", True))
    if center_valid:
        cx = float(result.get("cx", -1.0))
        cy = float(result.get("cy", -1.0))
        dx = float(result.get("dx", -1.0))
    else:
        cx = cy = dx = -1.0

    distance_valid = bool(tof_stats and tof_stats.get("valid"))
    median_mm = None if tof_stats is None else tof_stats.get("median_mm")
    distance_mm = (
        float(median_mm)
        if distance_valid and median_mm is not None else -1.0
    )
    return {
        "frame": int(frame_index),
        "frame_width": int(result.get("frame_width", 640)),
        "center_valid": center_valid,
        "center_final": center_final,
        "horizontal_valid": bool(
            center_valid and result.get("horizontal_valid", False)),
        "center_source": result.get("center_source", "NONE"),
        "cx": cx,
        "cy": cy,
        "dx": dx,
        "distance_valid": distance_valid,
        "distance_mm": distance_mm,
        "tof_reason": (
            "NO_STATS" if tof_stats is None
            else tof_stats.get("reason", "UNKNOWN")
        ),
        "tof_valid_count": (
            0 if tof_stats is None else int(tof_stats.get("valid_count", 0))
        ),
        "tof_total_count": (
            0 if tof_stats is None else int(tof_stats.get("total_count", 0))
        ),
        "tof_sequence": (
            -1 if tof_frame is None else int(tof_frame.get("sequence", -1))
        ),
        "tof_timestamp_ms": (
            -1 if tof_frame is None
            else int(tof_frame.get("timestamp_ms", -1))
        ),
        "tof_acquire_ms": (
            -1 if tof_frame is None
            else int(tof_frame.get("acquire_duration_ms", -1))
        ),
    }


def _format_vision_tof_packet(packet):
    """编码为便于人读的终端ASCII；UART另由uart_link编码成二进制帧。"""
    return (
        "VISION_TOF|frame=%d|center_valid=%d|center_final=%d|"
        "center_source=%s|cx=%.2f|cy=%.2f|dx=%.2f|"
        "horizontal_valid=%d|"
        "distance_valid=%d|distance_mm=%.1f|tof_reason=%s|"
        "tof_points=%d/%d|tof_seq=%d|tof_timestamp_ms=%d|"
        "tof_acquire_ms=%d" % (
            packet["frame"],
            1 if packet["center_valid"] else 0,
            1 if packet["center_final"] else 0,
            packet["center_source"],
            packet["cx"],
            packet["cy"],
            packet["dx"],
            1 if packet["horizontal_valid"] else 0,
            1 if packet["distance_valid"] else 0,
            packet["distance_mm"],
            packet["tof_reason"],
            packet["tof_valid_count"],
            packet["tof_total_count"],
            packet["tof_sequence"],
            packet["tof_timestamp_ms"],
            packet["tof_acquire_ms"],
        )
    )


def _create_uart_link():
    """按配置创建可选UART链路；关闭时对象仍可安全调用和关闭。"""
    return VisionTofUartLink(
        enabled=bool(getattr(config, "UART_ENABLED", False)),
        required=bool(getattr(config, "UART_REQUIRED", False)),
        device=str(getattr(config, "UART_DEVICE", "/dev/ttyS2")),
        baud=int(getattr(config, "UART_BAUD_RATE", 115200)),
        tx_pin=str(getattr(config, "UART_TX_PIN", "B0")),
        rx_pin=str(getattr(config, "UART_RX_PIN", "B1")),
        tx_function=str(getattr(
            config, "UART_TX_FUNCTION", "UART2_TX")),
        rx_function=str(getattr(
            config, "UART_RX_FUNCTION", "UART2_RX")),
    )


def main(loop_hook=None):
    center_method = _selected_center_method()
    detector, cam, mode, model_path = _create_pipeline()
    tof_sensor = _create_tof_sensor()
    uart_link = _create_uart_link()
    display_enabled = bool(getattr(config, "DISPLAY_ENABLED", False))
    disp = display.Display() if display_enabled else None
    # MaixVision 有时只刷新被改动的脚本，导致板端暂时混用新app和旧config。
    # 新增配置统一用getattr提供默认值，避免这种部分同步直接触发AttributeError。
    display_interval = max(1, int(getattr(
        config, "DISPLAY_INTERVAL_FRAMES", 2)))
    process_scale = max(0.05, min(
        1.0, float(getattr(config, "PROCESS_SCALE", 0.5))))
    full_fallback_enabled = bool(getattr(
        config, "ENABLE_FULL_RES_FALLBACK", True))
    yolo_interval = max(1, int(getattr(
        config, "YOLO_DETECT_INTERVAL_FRAMES", 1)))
    report_stage_timing = bool(getattr(
        config, "REPORT_STAGE_TIMING", True))
    tof_interval = max(1, int(getattr(
        config, "TOF_SAMPLE_INTERVAL_FRAMES", 3)))
    print("BOX_FACE_START|mode=%s|center_method=%s|model=%s|class_id=%d|"
          "process_scale=%.3f|display_every=%d|full_fallback=%d|"
          "yolo_every=%d|dual_buff=%d|conf=%.2f|tof=%d|tof_every=%d|"
          "uart=%d|display=%d" % (
        mode,
        center_method,
        "NONE" if model_path is None else model_path,
        config.YOLO_TARGET_CLASS_ID,
        process_scale,
        display_interval,
        1 if full_fallback_enabled else 0,
        yolo_interval,
        1 if config.YOLO_DUAL_BUFF else 0,
        float(config.YOLO_CONF_TH),
        1 if tof_sensor is not None else 0,
        tof_interval,
        1 if uart_link.enabled else 0,
        1 if display_enabled else 0,
    ))

    last_report_time = time.time()
    frame_index = 0
    empty_frame_count = 0
    consecutive_fast_misses = 0
    last_yolo_target = None
    previous_yolo_target = None
    last_yolo_frame = -1000000
    previous_yolo_frame = -1000000
    last_full_track = None
    previous_full_track = None
    area_peak_state = _new_area_peak_state()
    vertical_edge_state = _new_vertical_edge_state()
    stage_sums = {
        "cam": 0.0,
        "yolo": 0.0,
        "cv": 0.0,
        "trad": 0.0,
        "tof": 0.0,
        "disp": 0.0,
    }
    stage_count = 0

    # 这个窗口从一帧结束跨到下一帧结束，包含相机读取、OpenCV处理、显示和日志开销，
    # 比在绘图前调用单帧fps更接近用户实际得到的完整主循环吞吐率。
    fps = 0.0
    fps_window_start = time.time()
    fps_window_frames = 0
    fps_window_seconds = max(
        0.25, float(getattr(config, "FPS_MEASURE_WINDOW_SECONDS", 0.5)))

    try:
        if tof_sensor is not None:
            try:
                tof_sensor.open()
                print("TOF100_OPEN_OK|resolution=%d|spi=%d|every=%d" % (
                    int(getattr(config, "TOF_RESOLUTION", 50)),
                    int(getattr(config, "TOF_SPI_BUS_NUM", 2)),
                    tof_interval,
                ))
            except Exception as error:
                print("TOF100_OPEN_FAILED|%s" % str(error))
                if bool(getattr(config, "TOF_REQUIRED", True)):
                    raise
                tof_sensor = None

        uart_link.start()

        while not app.need_exit():
            stage = {"cam": 0.0, "yolo": 0.0, "cv": 0.0,
                     "trad": 0.0, "tof": 0.0, "disp": 0.0}

            stage_start = time.time()
            frame = cam.read()
            stage["cam"] = (time.time() - stage_start) * 1000.0
            if frame is None:
                empty_frame_count += 1
                if empty_frame_count == 1 or empty_frame_count % 30 == 0:
                    print("BOX_FACE_EMPTY_FRAME|count=%d" % empty_frame_count)
                if loop_hook is not None:
                    loop_hook(None)
                time.sleep(0.02)
                continue

            frame_index += 1
            image_width = int(frame.width())
            image_height = int(frame.height())
            display_due = (frame_index - 1) % display_interval == 0

            yolo_target = None
            search_roi = None
            result = None
            vision_tof_packet = None
            frame_bgr = None
            yolo_missed_with_fallback = False
            yolo_ran = False

            stage_start = time.time()
            if detector is not None:
                should_run_yolo = (
                    last_yolo_target is None or
                    (bool(getattr(
                        config, "YOLO_EVERY_FRAME_WHEN_NEAR", True)) and
                     _is_near_yolo_target(
                         last_yolo_target, image_width, image_height)) or
                    frame_index - last_yolo_frame >= yolo_interval
                )
                if should_run_yolo:
                    yolo_ran = True
                    objects = detector.detect(
                        frame,
                        conf_th=config.YOLO_CONF_TH,
                        iou_th=config.YOLO_IOU_TH,
                    )
                    detected_target = choose_yolo_target(objects)
                    if detected_target is not None:
                        detected_target["cached"] = False
                        detected_target["age"] = 0
                        previous_yolo_target = last_yolo_target
                        previous_yolo_frame = last_yolo_frame
                        last_yolo_target = dict(detected_target)
                        last_yolo_frame = frame_index
                        yolo_target = detected_target
                    else:
                        # 真正漏检后不继续沿用旧框，下一帧立即恢复YOLO搜索。
                        last_yolo_target = None
                        previous_yolo_target = None
                        last_yolo_frame = -1000000
                        previous_yolo_frame = -1000000
                else:
                    yolo_target = _predict_cached_yolo_target(
                        last_yolo_target,
                        previous_yolo_target,
                        last_yolo_frame,
                        previous_yolo_frame,
                        frame_index,
                        image_width,
                        image_height,
                    )

                if yolo_target is not None:
                    search_roi = expand_box_roi(
                        yolo_target,
                        image_width,
                        image_height,
                        config.YOLO_ROI_EXPAND_RATIO,
                    )
                elif config.FALLBACK_FULL_FRAME_WHEN_YOLO_MISSES:
                    search_roi = (0, 0, image_width, image_height)
                    yolo_missed_with_fallback = True
                else:
                    result = empty_face_result("YOLO_TARGET_MISSING")
                    result["source"] = "YOLO_MISSING"
                    result["processing_scale"] = process_scale
                    result["fallback_used"] = False
                    result["fast_miss_count"] = 0
                    consecutive_fast_misses = 0
            else:
                search_roi = clamp_roi(
                    config.MANUAL_ROI, image_width, image_height)
            stage["yolo"] = (time.time() - stage_start) * 1000.0

            near_target = _is_near_yolo_target(
                yolo_target, image_width, image_height)

            if result is None:
                if center_method == "NATIVE_FIND_BLOBS":
                    consecutive_fast_misses = 0
                    green_enabled = bool(getattr(
                        config, "USE_GREEN_EDGE_QUAD_REFINE", True))

                    if area_peak_state.get("corner_mode") and green_enabled:
                        # 进入绿框后直接运行早期灰度+Canny角点，不再让误差较大的
                        # find_blobs结果参与当前中心。连续失败才退回Blob搜索。
                        refine_start = time.time()
                        if frame_bgr is None:
                            frame_bgr = image.image2cv(
                                frame, ensure_bgr=True, copy=True)
                        guide_result = area_peak_state.get("last_guide") or {}
                        refined = _run_green_edge_quad_refinement(
                            frame, search_roi, guide_result,
                            frame_bgr=frame_bgr)
                        stage["cv"] += (
                            time.time() - refine_start) * 1000.0
                        refined.update({
                            "area_peak_stable": True,
                            "area_peak": float(area_peak_state.get(
                                "peak_area", -1.0)),
                            "area_peak_frame": int(area_peak_state.get(
                                "peak_frame", -1)),
                            "corner_mode": True,
                        })
                        if refined.get("valid"):
                            result = refined
                            area_peak_state["corner_misses"] = 0
                            area_peak_state["last_corner"] = dict(refined)
                        else:
                            area_peak_state["corner_misses"] = int(
                                area_peak_state.get("corner_misses", 0)) + 1
                            max_corner_misses = max(1, int(getattr(
                                config, "GREEN_QUAD_MAX_MISSES", 2)))
                            if (area_peak_state["corner_misses"] >=
                                    max_corner_misses):
                                # 只有绿框连续失败达到上限，才正式恢复Blob搜索。
                                # 这样Blob的错误中心不会在第一次角点漏检时插入控制量。
                                area_peak_state.update({
                                    "history": [],
                                    "ema": None,
                                    "peak_area": 0.0,
                                    "peak_frame": -1,
                                    "confirmed": False,
                                    "corner_mode": False,
                                    "corner_misses": 0,
                                    "last_guide": None,
                                    "last_corner": None,
                                })
                                blob_start = time.time()
                                result = _run_find_blobs_detection(
                                    frame, search_roi)
                                stage["trad"] += (
                                    time.time() - blob_start) * 1000.0
                                result["corner_mode"] = False
                            else:
                                # 第一次漏检沿用上一帧已确认绿框，不调用find_blobs。
                                # 仅保持一帧，下一帧仍会重新执行灰度+Canny检测。
                                result = dict(
                                    area_peak_state.get("last_corner") or
                                    refined)
                                result["corner_mode"] = True
                                result["corner_hold"] = True
                            result["refine_attempted"] = True
                            result["refine_reason"] = refined.get(
                                "reason", "EDGE_QUAD_FAILED")
                    else:
                        # 搜索阶段只用Blob做快速粗框；面积判据使用所有候选阈值像素
                        # 总和，而不是最佳Blob的中心或外接框。
                        blob_start = time.time()
                        result = _run_find_blobs_detection(frame, search_roi)
                        stage["trad"] += (
                            time.time() - blob_start) * 1000.0
                        peak_info = _update_area_peak_state(
                            area_peak_state, result, frame_index)
                        threshold_area = float(result.get(
                            "threshold_pixels_total", 0.0))
                        result.update({
                            "area_peak_stable": peak_info["stable"],
                            "area_peak": peak_info["peak_area"],
                            "area_peak_frame": peak_info["peak_frame"],
                            "area_peak_age": peak_info["peak_age"],
                            "guide_area_px": threshold_area,
                            "corner_mode": False,
                        })

                        refine_interval = max(1, int(getattr(
                            config, "GREEN_QUAD_REFINE_INTERVAL_FRAMES", 1)))
                        refine_due = (
                            frame_index - int(area_peak_state.get(
                                "last_refine_frame", -1000000)) >=
                            refine_interval)
                        if (result.get("valid") and
                                peak_info["stable"] and
                                green_enabled and refine_due):
                            refine_start = time.time()
                            if frame_bgr is None:
                                frame_bgr = image.image2cv(
                                    frame, ensure_bgr=True, copy=True)
                            refined = _run_green_edge_quad_refinement(
                                frame, search_roi, result,
                                frame_bgr=frame_bgr)
                            stage["cv"] += (
                                time.time() - refine_start) * 1000.0
                            area_peak_state["last_refine_frame"] = frame_index
                            refined.update({
                                "area_peak_stable": True,
                                "area_peak": peak_info["peak_area"],
                                "area_peak_frame": peak_info["peak_frame"],
                                "area_peak_age": peak_info["peak_age"],
                                "corner_mode": bool(refined.get("valid")),
                            })
                            if refined.get("valid"):
                                area_peak_state["corner_mode"] = True
                                area_peak_state["corner_misses"] = 0
                                area_peak_state["last_guide"] = dict(result)
                                area_peak_state["last_corner"] = dict(refined)
                                result = refined
                            else:
                                result["refine_attempted"] = True
                                result["refine_reason"] = refined.get(
                                    "reason", "EDGE_QUAD_FAILED")
                else:
                    # 只有四边形方案需要把Maix图像转换成OpenCV BGR数组。
                    stage_start = time.time()
                    frame_bgr = image.image2cv(
                        frame, ensure_bgr=True, copy=True)
                    stage["cv"] = (time.time() - stage_start) * 1000.0

                    stage_start = time.time()
                    result, consecutive_fast_misses = _run_face_detection(
                        frame_bgr,
                        search_roi,
                        consecutive_fast_misses,
                        process_scale_override=(
                            getattr(config, "PROCESS_SCALE_NEAR", 0.30)
                            if near_target else None
                        ),
                    )
                    stage["trad"] = (time.time() - stage_start) * 1000.0
                if yolo_missed_with_fallback and not result.get("valid"):
                    result["reason"] = "YOLO_MISS_%s" % result["reason"]

            # Blob橙框和颜色四边形绿框统一从左右两边生成中轴，不再使用上一版
            # 逐行密度算法，也不做三帧中值或丢失保持。
            result = _attach_midline_from_corners(
                result, image_width, image_height)

            if (bool(getattr(config, "VERTICAL_EDGE_ENABLED", True)) and
                    result.get("valid") and result.get("corners") is not None):
                if frame_bgr is None:
                    convert_start = time.time()
                    frame_bgr = image.image2cv(
                        frame, ensure_bgr=True, copy=True)
                    stage["cv"] += (
                        time.time() - convert_start) * 1000.0
                edge_start = time.time()
                prior_left_x, prior_right_x = _vertical_edge_priors(
                    vertical_edge_state)
                edge_result = detect_vertical_side_edges(
                    frame_bgr,
                    result["corners"],
                    prior_left_x=prior_left_x,
                    prior_right_x=prior_right_x,
                )
                result = _apply_vertical_edge_result(
                    result,
                    edge_result,
                    vertical_edge_state,
                    image_width,
                    image_height,
                )
                stage["trad"] += (
                    time.time() - edge_start) * 1000.0
            else:
                result = _apply_vertical_edge_result(
                    result,
                    {"valid": False, "reason": "EDGE_NOT_RUN"},
                    vertical_edge_state,
                    image_width,
                    image_height,
                )

            result = _stabilize_horizontal_axis(
                result, vertical_edge_state, image_width)

            # YOLO直接拒绝的帧也写入当前方法名，保证日志和屏幕状态可比较。
            result.setdefault("method", center_method)
            result.setdefault(
                "center_source",
                "QUAD" if center_method == "OPENCV_QUAD" else "BLOB_GUIDE")
            result.setdefault("center_is_final", center_method == "OPENCV_QUAD")
            result["frame_width"] = image_width
            result["frame_height"] = image_height
            result["near_target"] = near_target
            if result.get("valid"):
                _attach_partial_track(
                    result, yolo_target, frame_index,
                    last_full_track, previous_full_track)
                if result.get("center_is_final", True):
                    previous_full_track = last_full_track
                    last_full_track = _full_track_snapshot(
                        result, yolo_target, frame_index)
            else:
                _attach_partial_track(
                    result, yolo_target, frame_index,
                    last_full_track, previous_full_track)

            # 每N帧采集一次TOF，把本帧视觉中轴与距离放入同一字典：终端输出
            # 人读ASCII，可选UART发送带CRC的16字节二进制帧。
            if (tof_sensor is not None and
                    frame_index % tof_interval == 0):
                stage_start = time.time()
                tof_frame = None
                try:
                    tof_frame = tof_sensor.read_frame()
                    if tof_frame is None:
                        tof_stats = _invalid_tof_stats("EMPTY_FRAME")
                    else:
                        tof_roi = centered_roi(
                            tof_frame["width"],
                            tof_frame["height"],
                            int(getattr(config, "TOF_CENTER_ROI_SIZE", 1)),
                        )
                        tof_stats = tof_sensor.analyze_frame(
                            tof_frame,
                            roi=tof_roi,
                            min_valid_count=int(getattr(
                                config, "TOF_MIN_VALID_POINTS", 1)),
                        )
                except Exception as error:
                    tof_stats = _invalid_tof_stats(
                        "READ_ERROR_%s" % str(error).replace("|", "/"))
                stage["tof"] = (time.time() - stage_start) * 1000.0
                packet = _build_vision_tof_packet(
                    result, tof_frame, tof_stats, frame_index)
                print(_format_vision_tof_packet(packet))
                uart_link.send_packet(packet)
                vision_tof_packet = packet

            # 直接在相机原生帧上绘制并显示，不再把OpenCV数组转换回Maix图像。
            stage_start = time.time()
            if display_enabled and display_due:
                draw_debug_overlay_native(
                    frame, result, search_roi, yolo_target, fps)
                disp.show(frame)
            stage["disp"] = (time.time() - stage_start) * 1000.0

            if loop_hook is not None:
                loop_hook(vision_tof_packet)

            for stage_name in stage_sums:
                stage_sums[stage_name] += stage[stage_name]
            stage_count += 1

            now = time.time()
            fps_window_frames += 1
            fps_elapsed = now - fps_window_start
            if fps_elapsed >= fps_window_seconds:
                fps = fps_window_frames / max(1e-6, fps_elapsed)
                fps_window_start = now
                fps_window_frames = 0

            if now - last_report_time >= config.REPORT_INTERVAL_SECONDS:
                average_timing = None
                if report_stage_timing and stage_count > 0:
                    average_timing = {
                        name: value / stage_count
                        for name, value in stage_sums.items()
                    }
                print(_format_face_log(
                    result, yolo_target, fps, frame_index,
                    yolo_ran=yolo_ran,
                    stage_timing=average_timing))
                if (config.REPORT_REJECTED_CANDIDATES and
                        not result.get("valid")):
                    for reject_log in _format_rejected_candidate_logs(
                            result, frame_index):
                        print(reject_log)
                last_report_time = now
                for stage_name in stage_sums:
                    stage_sums[stage_name] = 0.0
                stage_count = 0

    except Exception as error:
        print("BOX_FACE_FATAL|%s" % str(error))
        raise
    finally:
        # MaixPy 的相机/显示对象没有统一的 close()；解除引用后由底层析构释放资源。
        detector = None
        cam = None
        if tof_sensor is not None:
            tof_sensor.close()
        tof_sensor = None
        uart_link.close()
        uart_link = None
        disp = None
        print("BOX_FACE_CLOSED")


if __name__ == "__main__":
    main()
