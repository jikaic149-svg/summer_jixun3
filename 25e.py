import math
import time as _std_time

# =============================================================================
# 日志
# =============================================================================
_boot = _std_time.time()
LOG_LEVEL = 1
_LT = ["TRACE","DEBUG"," INFO","ERROR"]
def _log(lv, mod, msg):
    if lv >= LOG_LEVEL:
        ms = int((_std_time.time() - _boot) * 1000)
        print(f"[{ms:>8}ms][{_LT[lv]}][{mod:>5}] {msg}")
def log_i(m,s): _log(2,m,s)
def log_e(m,s): _log(3,m,s)

# =============================================================================
# 参数
# =============================================================================
CAM_W, CAM_H = 320, 240
VISION_BUILD_ID = "HRECT_20260712_3"
LOCK_MAX_JUMP = 80
ROI_SIZE = 30 # 缩小放大镜尺寸，降低边缘与反光干扰
CORNER_DEADZONE_SQ = 16  # 角点死区半径²（4px），小于此距离时复用上帧角点
SMOOTH_ALPHA = 0.55       # 中心点EMA平滑系数 (0=极平滑/高延迟, 1=无平滑/原始值)

# MaixVision 视觉调参区。VISION 页面直接显示 tape_bin 二值图。
BINARY_GRAY_MIN = 0
BINARY_GRAY_MAX = 115
WHITE_BLOB_AREA_THRESHOLD = 100
WHITE_BLOB_PIXELS_THRESHOLD = 20
WHITE_FRAME_RECT_AREA_MIN = 1500
WHITE_FRAME_RECT_AREA_MAX = 30000
WHITE_FRAME_ASPECT_MIN = 1.05
WHITE_FRAME_ASPECT_MAX = 5.00
BLACK_BLOB_AREA_THRESHOLD = 5
BLACK_BLOB_PIXELS_THRESHOLD = 5
BLACK_CORE_RECT_AREA_MIN = 400
BLACK_CORE_RECT_AREA_MAX = 25000
BLACK_CORE_ASPECT_MIN = 1.05
BLACK_CORE_ASPECT_MAX = 3.00
BLACK_CORE_RELATIVE_AREA_MIN = 0.30
BLACK_CORE_RELATIVE_AREA_MAX = 0.86
TARGET_INNER_AREA_MIN = 800
TARGET_INNER_AREA_MAX = 25000
TARGET_QUAD_MIN_EDGE_PX = 24
TARGET_QUAD_OPPOSITE_RATIO_MIN = 0.30
TARGET_QUAD_OPPOSITE_RATIO_MAX = 3.30
TARGET_QUAD_FILL_RATIO_MIN = 0.40
TARGET_QUAD_FILL_RATIO_MAX = 1.15
TARGET_QUAD_MAX_ABS_CORNER_COS = 0.45
TARGET_QUAD_DIAGONAL_RATIO_MIN = 0.70
# 全图重新捕获/CAL 时只接受画面中央附近的靶，防止锁到边缘支架和背景框。
TARGET_SELECTION_MAX_CENTER_DISTANCE_PX = 120
VISION_DEBUG_LOG_PERIOD_FRAMES = 10
AUTO_CAL_SAMPLE_COUNT = 8
AUTO_CAL_MAX_SPREAD_PX = 4

TARGET_SEND_PERIOD_MS = 15
VISION_LIMIT = 2048
AIM_READY_DEADZONE_X_PX = 2
AIM_READY_DEADZONE_Y_PX = 2
TARGET_LOCK_CONFIRM_FRAMES = 3
TARGET_MOVE_DISTANCE_PX = 12
TARGET_MOVE_CONFIRM_FRAMES = 5
TASK_STATUS_STALE_MS = 500
AIM_REF_X = CAM_W // 2
AIM_REF_Y = CAM_H // 2
TELEMETRY_UDP_HOST = "255.255.255.255"
TELEMETRY_UDP_PORT = 5005
COMMAND_UDP_PORT = 5007

PAGE_SELECT = "select"
PAGE_RUNNING = "running"
STAGE_VISION = "vision"
STAGE_TRACK = "track"
STAGE_TARGET = "target"
TASK_PHASE_WAIT_TARGET_MOVE = 4

DEBUG_TASK_DEFS = (
    (STAGE_VISION, "VISION", "vision"),
    (STAGE_TRACK, "TRACK", "aim"),
    (STAGE_TARGET, "TARGET", "shoot"),
)

COMPETITION_TASK_DEFS = (
    ("task1", "TASK1", "task1"),
    ("task2", "TASK2", "task2"),
    ("task3", "TASK3", "task3"),
    ("task4", "TASK4", "task4"),
    ("task5", "TASK5", "task5"),
)

TASK_DEFS = DEBUG_TASK_DEFS + COMPETITION_TASK_DEFS

TASK_PHASE_LABELS = (
    "IDLE", "TO CENTER", "AIM", "LASER",
    "WAIT MOVE", "TO FINISH", "COMPLETE", "STOPPED",
)

COLOR_TH = {
    "tape_bin":   {"gray": [[BINARY_GRAY_MIN, BINARY_GRAY_MAX]], "note": "黑色胶布灰度阈值 (全图二值化用)"},
    "blob_white": {"gray": [[200, 255]], "note": "二值图白色区域 (找白框)"},
    "blob_black": {"gray": [[0, 50]],    "note": "二值图黑色区域 (找黑芯)"},
}

DEBUG_FAIL_REASON = "Init"
DEBUG_BLOB_STATS = {
    "white_count": 0,
    "white_max_area": 0,
    "black_count": 0,
    "black_max_area": 0,
}

# =============================================================================
# 工具函数：计算对角线交点 (抵抗透视畸变)
# =============================================================================
def get_diagonal_intersection(tl, tr, br, bl):
    """计算两条对角线(tl->br 和 tr->bl)的交点，得出真实的透视物理中心"""
    x1, y1 = tl
    x2, y2 = br
    x3, y3 = tr
    x4, y4 = bl
    
    # 直线1 (tl-br): A1*x + B1*y = C1
    A1 = y2 - y1
    B1 = x1 - x2
    C1 = A1 * x1 + B1 * y1
    
    # 直线2 (tr-bl): A2*x + B2*y = C2
    A2 = y4 - y3
    B2 = x3 - x4
    C2 = A2 * x3 + B2 * y3
    
    det = A1 * B2 - A2 * B1
    if det == 0:
        return (x1 + x2 + x3 + x4) // 4, (y1 + y2 + y3 + y4) // 4
    else:
        x = (B2 * C1 - B1 * C2) / det
        y = (A1 * C2 - A2 * C1) / det
        return int(x), int(y)


def quadrilateral_area(corners):
    total = 0
    for index in range(4):
        x1, y1 = corners[index]
        x2, y2 = corners[(index + 1) % 4]
        total += x1 * y2 - x2 * y1
    return abs(total) * 0.5


def quadrilateral_geometry_metrics(corners):
    """Return topology and shape metrics for ordered tl,tr,br,bl corners."""
    if corners is None or len(corners) != 4:
        return None

    cross_values = []
    max_abs_corner_cos = 0.0
    for index in range(4):
        prev_x, prev_y = corners[(index - 1) % 4]
        curr_x, curr_y = corners[index]
        next_x, next_y = corners[(index + 1) % 4]

        in_x = prev_x - curr_x
        in_y = prev_y - curr_y
        out_x = next_x - curr_x
        out_y = next_y - curr_y
        cross_values.append(in_x * out_y - in_y * out_x)

        in_len_sq = in_x * in_x + in_y * in_y
        out_len_sq = out_x * out_x + out_y * out_y
        if in_len_sq <= 0 or out_len_sq <= 0:
            corner_cos = 1.0
        else:
            corner_cos = abs(
                (in_x * out_x + in_y * out_y) /
                math.sqrt(in_len_sq * out_len_sq)
            )
        if corner_cos > max_abs_corner_cos:
            max_abs_corner_cos = corner_cos

    has_positive_cross = any(value > 0 for value in cross_values)
    has_negative_cross = any(value < 0 for value in cross_values)
    convex = (
        not (has_positive_cross and has_negative_cross)
        and all(value != 0 for value in cross_values)
    )

    diag_1_sq = (
        (corners[0][0] - corners[2][0]) ** 2 +
        (corners[0][1] - corners[2][1]) ** 2
    )
    diag_2_sq = (
        (corners[1][0] - corners[3][0]) ** 2 +
        (corners[1][1] - corners[3][1]) ** 2
    )
    diag_max_sq = max(diag_1_sq, diag_2_sq)
    if diag_max_sq <= 0:
        diagonal_ratio = 0.0
    else:
        diagonal_ratio = math.sqrt(
            min(diag_1_sq, diag_2_sq) / diag_max_sq
        )

    return {
        "convex": convex,
        "max_abs_corner_cos": max_abs_corner_cos,
        "diagonal_ratio": diagonal_ratio,
    }


def target_candidate_score(target, preferred_center=None, previous_center=None):
    """Return a deterministic lower-is-better candidate selection score."""
    score = 0.0
    if preferred_center is not None:
        dx = target["cx"] - preferred_center[0]
        dy = target["cy"] - preferred_center[1]
        score += math.sqrt(dx * dx + dy * dy)
    if previous_center is not None:
        dx = target["cx"] - previous_center[0]
        dy = target["cy"] - previous_center[1]
        score += math.sqrt(dx * dx + dy * dy) * 4.0
    score += target["max_abs_corner_cos"] * 12.0
    score += abs(1.0 - target["diagonal_ratio"]) * 8.0
    return score

# =============================================================================
# 动态透视角点检测：旋转不变曼哈顿法 (动态基底投影)
# =============================================================================
def get_robust_corners(blob, safe_bottom_y=None):
    """利用动态向量点乘，在任意旋转角度下精准提取物理尖角"""
    try:
        pts = blob.corners() 
    except AttributeError:
        pts = blob.mini_corners()
    
    if not pts or len(pts) < 4:
        pts = blob.mini_corners()
        
    if not pts or len(pts) < 4:
        return None

    if safe_bottom_y is not None:
        filt = [p for p in pts if p[1] < safe_bottom_y]
        if len(filt) >= 4:
            pts = filt

    theta = blob.rotation()

    angles = [
        theta + math.pi/4,       
        theta + 3*math.pi/4,     
        theta + 5*math.pi/4,     
        theta + 7*math.pi/4      
    ]
    
    corners = []
    for a in angles:
        vx, vy = math.cos(a), math.sin(a)
        best_pt = max(pts, key=lambda p: p[0]*vx + p[1]*vy)
        corners.append((int(best_pt[0]), int(best_pt[1])))

    corners = list(set(corners))
    if len(corners) < 4:
        corners = [(int(p[0]), int(p[1])) for p in blob.mini_corners()]
        
    if not corners or len(corners) < 4:
        return None

    tl = min(corners, key=lambda p: p[0] + p[1])  
    br = max(corners, key=lambda p: p[0] + p[1])  
    tr = max(corners, key=lambda p: p[0] - p[1])  
    bl = min(corners, key=lambda p: p[0] - p[1])  
    
    return [tl, tr, br, bl]

# =============================================================================
# 核心识别: 漏斗式筛选 + 局部极径最大化
# =============================================================================
def find_target(
    bin_img, hint_roi=None, last_corners=None,
    preferred_center=(AIM_REF_X, AIM_REF_Y),
):
    global DEBUG_FAIL_REASON, DEBUG_BLOB_STATS
    DEBUG_FAIL_REASON = "未找到任何白框"
    DEBUG_BLOB_STATS = {
        "white_count": 0,
        "white_max_area": 0,
        "black_count": 0,
        "black_max_area": 0,
    }
    search = {}
    if hint_roi:
        search['roi'] = hint_roi
        
    white_blobs = bin_img.find_blobs(COLOR_TH["blob_white"]["gray"], 
                                      area_threshold=WHITE_BLOB_AREA_THRESHOLD,
                                      pixels_threshold=WHITE_BLOB_PIXELS_THRESHOLD,
                                      merge=True, x_stride=1, y_stride=1, **search)
    if not white_blobs:
        return None

    DEBUG_BLOB_STATS["white_count"] = len(white_blobs)
    DEBUG_BLOB_STATS["white_max_area"] = max(
        b.w() * b.h() for b in white_blobs
    )
    
    valid_tapes = []
    for b in white_blobs:
        area = b.w() * b.h()
        if area > WHITE_FRAME_RECT_AREA_MAX or area < WHITE_FRAME_RECT_AREA_MIN:
            DEBUG_FAIL_REASON = f"白框面积越界({area})"
            continue
        ratio = b.w() / max(1, b.h())
        if ratio < WHITE_FRAME_ASPECT_MIN or ratio > WHITE_FRAME_ASPECT_MAX:
            DEBUG_FAIL_REASON = "白框宽高比越界(%.2f)" % ratio
            continue
            
        valid_tapes.append(b)
        
    if not valid_tapes:
        return None
    
    accepted_targets = []
    previous_center = None
    if last_corners and len(last_corners) == 4:
        previous_center = get_diagonal_intersection(
            last_corners[0], last_corners[1],
            last_corners[2], last_corners[3],
        )

    # Evaluate every valid rectangle. Sorting by area caused large background
    # rectangles to win before the real white-frame/black-core target.
    valid_tapes.sort(key=lambda b: b.w() * b.h(), reverse=True)
    
    for tape in valid_tapes:
        roi = tape.rect()
        
        black_blobs = bin_img.find_blobs(COLOR_TH["blob_black"]["gray"], 
                                          roi=roi,
                                          area_threshold=BLACK_BLOB_AREA_THRESHOLD,
                                          pixels_threshold=BLACK_BLOB_PIXELS_THRESHOLD,
                                          merge=False,
                                          x_stride=2, y_stride=2)
        if not black_blobs:
            DEBUG_FAIL_REASON = "框内无黑芯"
            continue

        DEBUG_BLOB_STATS["black_count"] = max(
            DEBUG_BLOB_STATS["black_count"], len(black_blobs)
        )
        DEBUG_BLOB_STATS["black_max_area"] = max(
            DEBUG_BLOB_STATS["black_max_area"],
            max(bb.w() * bb.h() for bb in black_blobs),
        )
            
        candidates = []
        for bb in black_blobs:
            # 【抗纯白背景干扰：严格黑芯自身过滤】
            # 靶子核心一定是接近正方形的。如果背景全白，导致整个视野被判定为白框，
            # 这里的黑芯比例限制可以瞬间屏蔽掉“宽广的地板”或“细长的支架”等环境阴影。
            core_area = bb.w() * bb.h()
            core_aspect = bb.w() / max(1, bb.h())
            if (core_aspect < BLACK_CORE_ASPECT_MIN or
                    core_aspect > BLACK_CORE_ASPECT_MAX):
                DEBUG_FAIL_REASON = "黑芯宽高比越界(%.2f)" % core_aspect
                continue
                
            if (core_area < BLACK_CORE_RECT_AREA_MIN or
                    core_area > BLACK_CORE_RECT_AREA_MAX):
                DEBUG_FAIL_REASON = "黑芯面积越界(%d)" % core_area
                continue

            # 允许边缘溢出（兼容反光导致的胶带断裂），但限制重心必须在中央区域，拒绝四角背景
            if not (tape.x() + tape.w() * 0.2 < bb.cx() < tape.x() + tape.w() * 0.8):
                DEBUG_FAIL_REASON = "黑芯重心偏离X(疑为背景)"
                continue
            if bb.cy() < tape.y() + tape.h() * 0.15:
                DEBUG_FAIL_REASON = "黑芯重心偏离Y(疑为背景)"
                continue
            core_fill_ratio = core_area / max(1, tape.w() * tape.h())
            if (core_fill_ratio < BLACK_CORE_RELATIVE_AREA_MIN or
                    core_fill_ratio > BLACK_CORE_RELATIVE_AREA_MAX):
                DEBUG_FAIL_REASON = "黑芯占比越界(%.2f)" % core_fill_ratio
                continue
            
            candidates.append(bb)
            
        if candidates:
            valid_cores = [bb for bb in candidates if bb.cy() < tape.y() + tape.h() * 0.75]
            if not valid_cores: 
                DEBUG_FAIL_REASON = "黑芯位置全偏下(疑为干扰)"
                continue
            if sum(b.pixels() for b in valid_cores) == 0: continue

            # The target is the horizontal black rectangle itself. The screen
            # crosshair is drawn after detection and is not an image feature.
            primary_core = max(
                valid_cores, key=lambda core: core.w() * core.h(),
            )

            outer_area = tape.w() * tape.h()
            # Use one enclosed core; summing unrelated dark blobs admits
            # stands and background shadows as part of the target.
            inner_area = primary_core.w() * primary_core.h()
            inner_ratio = inner_area / max(1, outer_area)
            if (inner_area < TARGET_INNER_AREA_MIN or
                    inner_area > TARGET_INNER_AREA_MAX):
                DEBUG_FAIL_REASON = "黑芯总面积越界(%d)" % inner_area
                continue
            if (inner_ratio < BLACK_CORE_RELATIVE_AREA_MIN or
                    inner_ratio > BLACK_CORE_RELATIVE_AREA_MAX):
                DEBUG_FAIL_REASON = "黑芯总占比越界(%.2f)" % inner_ratio
                continue
            
            # 几何硬截断：通过内芯高度推算真实外框的底边极限位置，切断三脚架粘连
            core_min_y = primary_core.y()
            core_max_y = primary_core.y() + primary_core.h()
            safe_bottom_y = core_max_y + max(0, core_min_y - tape.y()) * 2.5

            # ================================================================
            # 局部放大镜钉死角点 (升级版：引入几何与物理防畸变约束)
            # ================================================================
            corners = None
            if last_corners and not any(p[1] > safe_bottom_y for p in last_corners):
                corners = []
                keys = [lambda p: p[0]+p[1], lambda p: p[0]-p[1], lambda p: p[0]+p[1], lambda p: p[0]-p[1]]
                is_max = [False, True, True, False]
                
                for i in range(4):
                    lcx, lcy = last_corners[i]
                    rx = max(0, lcx - ROI_SIZE//2)
                    ry = max(0, lcy - ROI_SIZE//2)
                    rw = min(CAM_W - rx, ROI_SIZE)
                    rh = min(CAM_H - ry, ROI_SIZE)
                    
                    if rw <= 0 or rh <= 0:
                        break
                        
                    roi_local = (rx, ry, rw, rh)
                    
                    # 【约束1：局部面积过滤】提升阈值，防止抓取极小噪点
                    blobs = bin_img.find_blobs(COLOR_TH["blob_white"]["gray"], roi=roi_local, 
                                               area_threshold=15, pixels_threshold=15, merge=True)
                    if not blobs: break
                    
                    b = max(blobs, key=lambda x: x.pixels())
                    pts = b.mini_corners() or b.corners()
                    if not pts: break
                    
                    best = max(pts, key=keys[i]) if is_max[i] else min(pts, key=keys[i])
                    corners.append((int(best[0]), int(best[1])))
                
                # 【约束2：四边形几何拓扑审查】
                if corners is not None and len(corners) == 4:
                    tl, tr, br, bl = corners
                    
                    # 计算四条边的长度平方 (省去开平方根的性能消耗)
                    dist_top_sq = (tl[0] - tr[0])**2 + (tl[1] - tr[1])**2
                    dist_bot_sq = (bl[0] - br[0])**2 + (bl[1] - br[1])**2
                    dist_lft_sq = (tl[0] - bl[0])**2 + (tl[1] - bl[1])**2
                    dist_rgt_sq = (tr[0] - br[0])**2 + (tr[1] - br[1])**2
                    
                    # 审查A：防整体萎缩 (边长必须大于阈值)
                    MIN_EDGE_SQ = TARGET_QUAD_MIN_EDGE_PX ** 2
                    if min(dist_top_sq, dist_bot_sq, dist_lft_sq, dist_rgt_sq) < MIN_EDGE_SQ:
                        corners = None 
                    else:
                        # 审查B：防单边被严重拉长 (对边比例必须在合理透视范围内)
                        ratio_tb = dist_top_sq / (dist_bot_sq + 1e-5)
                        ratio_lr = dist_lft_sq / (dist_rgt_sq + 1e-5)
                        
                        if not (TARGET_QUAD_OPPOSITE_RATIO_MIN < ratio_tb <
                                TARGET_QUAD_OPPOSITE_RATIO_MAX) or not (
                                TARGET_QUAD_OPPOSITE_RATIO_MIN < ratio_lr <
                                TARGET_QUAD_OPPOSITE_RATIO_MAX):
                            DEBUG_FAIL_REASON = "局部角点四边形比例异常"
                            corners = None 
                else:
                    DEBUG_FAIL_REASON = "局部未集齐4个角点或太小"
                    corners = None
            # ================================================================

            if not corners:
                corners = get_robust_corners(tape, safe_bottom_y)

            # 退化检测：4个角点必须两两不同，否则对角线交点无意义
            if corners and len(set(corners)) < 4:
                DEBUG_FAIL_REASON = "角点退化(含重复坐标)"
                corners = get_robust_corners(tape, safe_bottom_y)
                if corners and len(set(corners)) < 4:
                    corners = None

            if not corners:
                continue

            # 角点死区：单帧位移 < CORNER_DEADZONE_SQ 时复用上帧角点，切断局部放大镜反馈震荡
            if last_corners and len(last_corners) == 4:
                corners = [
                    old if (new[0]-old[0])**2 + (new[1]-old[1])**2 < CORNER_DEADZONE_SQ else new
                    for new, old in zip(corners, last_corners)
                ]

            geometry = quadrilateral_geometry_metrics(corners)
            if geometry is None or not geometry["convex"]:
                DEBUG_FAIL_REASON = "四边形非凸或边共线"
                continue
            if (geometry["max_abs_corner_cos"] >
                    TARGET_QUAD_MAX_ABS_CORNER_COS):
                DEBUG_FAIL_REASON = "四角非直角(max|cos|=%.2f)" % (
                    geometry["max_abs_corner_cos"],
                )
                continue
            if (geometry["diagonal_ratio"] <
                    TARGET_QUAD_DIAGONAL_RATIO_MIN):
                DEBUG_FAIL_REASON = "对角线比例越界(%.2f)" % (
                    geometry["diagonal_ratio"],
                )
                continue

            quad_area = quadrilateral_area(corners)
            quad_fill_ratio = quad_area / max(1, outer_area)
            if (quad_fill_ratio < TARGET_QUAD_FILL_RATIO_MIN or
                    quad_fill_ratio > TARGET_QUAD_FILL_RATIO_MAX):
                DEBUG_FAIL_REASON = "四边形填充比越界(%.2f)" % quad_fill_ratio
                continue

            # Use the enclosed horizontal rectangle center for calibration and
            # yaw tracking. Outer corners only reject obvious background blobs.
            tl, tr, br, bl = corners
            quad_cx, quad_cy = get_diagonal_intersection(tl, tr, br, bl)
            cx = int(round(primary_core.x() + primary_core.w() * 0.5))
            cy = int(round(primary_core.y() + primary_core.h() * 0.5))

            if preferred_center is not None and hint_roi is None:
                center_dx = cx - preferred_center[0]
                center_dy = cy - preferred_center[1]
                center_distance = math.sqrt(
                    center_dx * center_dx + center_dy * center_dy,
                )
                if center_distance > TARGET_SELECTION_MAX_CENTER_DISTANCE_PX:
                    DEBUG_FAIL_REASON = "靶心离画面中心过远(%.1fpx)" % center_distance
                    continue

            target = {
                "corners": corners,
                "outer_rect": tape.rect(),
                "outer_area": outer_area,
                "outer_aspect": tape.w() / max(1, tape.h()),
                "inner_area": inner_area,
                "inner_ratio": inner_ratio,
                "core_max_area": inner_area,
                "core_aspect": primary_core.w() / max(1, primary_core.h()),
                "quad_fill_ratio": quad_fill_ratio,
                "max_abs_corner_cos": geometry["max_abs_corner_cos"],
                "diagonal_ratio": geometry["diagonal_ratio"],
                "quad_cx": quad_cx,
                "quad_cy": quad_cy,
                "cx": cx, "cy": cy,
            }
            target["selection_score"] = target_candidate_score(
                target, preferred_center, previous_center,
            )
            accepted_targets.append(target)

    if not accepted_targets:
        return None
    accepted_targets.sort(key=lambda target: target["selection_score"])
    selected = accepted_targets[0]
    selected["candidate_count"] = len(accepted_targets)
    return selected


# =============================================================================
# 触摸、UI 与 UART 协议
# =============================================================================
class PressReleaseLatch:
    """只有在同一按钮上按下并释放，才产生一次 click。"""

    def __init__(self):
        self.was_pressed = False
        self.armed_button = None

    def update(self, pressed, button_under_touch):
        clicked = None
        if pressed and not self.was_pressed:
            self.armed_button = button_under_touch
        elif not pressed and self.was_pressed:
            if self.armed_button is not None and self.armed_button == button_under_touch:
                clicked = self.armed_button
            self.armed_button = None
        self.was_pressed = bool(pressed)
        return clicked


class AimReferenceCalibration:
    """One global visual reference shared by VISION, TRACK, and TARGET."""

    def __init__(self, default_x, default_y, width=CAM_W, height=CAM_H):
        self.width = max(1, int(width))
        self.height = max(1, int(height))
        self.default_x = self._clamp(default_x, self.width)
        self.default_y = self._clamp(default_y, self.height)
        self.aim_ref_x = self.default_x
        self.aim_ref_y = self.default_y
        self.pending = False
        self.calibrated = False
        self.samples = []

    @staticmethod
    def _clamp(value, extent):
        return max(0, min(int(extent) - 1, int(value)))

    def start(self):
        self.pending = True
        self.calibrated = False
        self.samples = []

    def set_reference(self, x, y):
        self.aim_ref_x = self._clamp(x, self.width)
        self.aim_ref_y = self._clamp(y, self.height)
        self.pending = False
        self.calibrated = True
        self.samples = []
        return True

    def update_auto(self, x, y):
        if not self.pending:
            return False
        sample = (
            self._clamp(x, self.width),
            self._clamp(y, self.height),
        )
        self.samples.append(sample)
        if len(self.samples) > AUTO_CAL_SAMPLE_COUNT:
            self.samples.pop(0)
        if len(self.samples) < AUTO_CAL_SAMPLE_COUNT:
            return False

        xs = [point[0] for point in self.samples]
        ys = [point[1] for point in self.samples]
        if ((max(xs) - min(xs) > AUTO_CAL_MAX_SPREAD_PX) or
                (max(ys) - min(ys) > AUTO_CAL_MAX_SPREAD_PX)):
            return False

        avg_x = int(round(sum(xs) / len(xs)))
        avg_y = int(round(sum(ys) / len(ys)))
        return self.set_reference(avg_x, avg_y)

    def sample_count(self):
        return len(self.samples)

    def reset(self):
        self.aim_ref_x = self.default_x
        self.aim_ref_y = self.default_y
        self.pending = False
        self.calibrated = False
        self.samples = []

    def target_error(self, target_x, target_y):
        return (
            int(target_x) - self.aim_ref_x,
            int(target_y) - self.aim_ref_y,
        )


def target_aim_ready(calibrated, locked, dx, dy):
    return bool(
        calibrated
        and locked
        and abs(int(dx)) <= AIM_READY_DEADZONE_X_PX
        and abs(int(dy)) <= AIM_READY_DEADZONE_Y_PX
    )


class TargetLockTracker:
    """Require consecutive detections to lock; one miss revokes immediately."""

    def __init__(self, confirm_frames=TARGET_LOCK_CONFIRM_FRAMES):
        self.confirm_frames = max(1, int(confirm_frames))
        self.detected_frames = 0
        self.locked = False

    def update(self, detected):
        if not detected:
            self.detected_frames = 0
            self.locked = False
            return False
        if self.detected_frames < self.confirm_frames:
            self.detected_frames += 1
        self.locked = self.detected_frames >= self.confirm_frames
        return self.locked


def parse_task_status_frame(line):
    """Parse one complete STM32 task frame without partially updating state."""
    try:
        if isinstance(line, bytes) or isinstance(line, bytearray):
            text = bytes(line).decode("ascii").strip()
        else:
            text = str(line).strip()
    except Exception:
        return None

    fields = text.split(",")
    if len(fields) != 8 or fields[0] != "T":
        return None
    try:
        values = [int(field, 10) for field in fields[1:]]
    except Exception:
        return None

    task_id, phase, shot, aim, laser, milestones, done = values
    if not (0 <= task_id <= 5 and 0 <= phase < len(TASK_PHASE_LABELS)):
        return None
    if not (0 <= shot <= 255 and 0 <= milestones <= 255):
        return None
    if aim not in (0, 1) or laser not in (0, 1) or done not in (0, 1):
        return None
    if done and phase != 6:
        return None

    return {
        "task_id": task_id,
        "phase": phase,
        "shot": shot,
        "aim": aim,
        "laser": laser,
        "milestones": milestones,
        "done": done,
    }


class TargetMoveEpochTracker:
    """Publish one epoch change for each confirmed WAIT_TARGET_MOVE phase."""

    def __init__(
        self,
        distance_px=TARGET_MOVE_DISTANCE_PX,
        confirm_frames=TARGET_MOVE_CONFIRM_FRAMES,
    ):
        self.distance_px = max(1, int(distance_px))
        self.confirm_frames = max(1, int(confirm_frames))
        self.move_epoch = 0
        self.in_wait = False
        self.anchor = None
        self.confirmed_frames = 0
        self.published_for_wait = False

    def leave_wait(self):
        self.in_wait = False
        self.anchor = None
        self.confirmed_frames = 0
        self.published_for_wait = False

    def pause_wait(self):
        self.confirmed_frames = 0

    def update(self, waiting_for_move, target_locked, center=None):
        if not waiting_for_move:
            self.leave_wait()
            return self.move_epoch

        if not self.in_wait:
            self.in_wait = True
            self.anchor = None
            self.confirmed_frames = 0
            self.published_for_wait = False

        if self.published_for_wait:
            return self.move_epoch
        if not target_locked or center is None:
            self.confirmed_frames = 0
            return self.move_epoch

        current = (int(center[0]), int(center[1]))
        if self.anchor is None:
            self.anchor = current
            return self.move_epoch

        delta_x = current[0] - self.anchor[0]
        delta_y = current[1] - self.anchor[1]
        if (delta_x * delta_x + delta_y * delta_y >=
                self.distance_px * self.distance_px):
            self.confirmed_frames += 1
        else:
            self.confirmed_frames = 0

        if self.confirmed_frames >= self.confirm_frames:
            self.move_epoch = (self.move_epoch + 1) & 0xFFFFFFFF
            self.published_for_wait = True
        return self.move_epoch


class UartUdpBridge:
    """Forward telemetry to UDP and printable UDP commands to UART."""

    def __init__(
        self,
        port,
        telemetry_host=TELEMETRY_UDP_HOST,
        telemetry_port=TELEMETRY_UDP_PORT,
        command_port=COMMAND_UDP_PORT,
    ):
        self.port = port
        self.telemetry_host = telemetry_host
        self.telemetry_port = int(telemetry_port)
        self.command_port = int(command_port)
        self.sock = None
        self.available = False
        self.last_error = ""
        self.telemetry_last_error = ""
        self.command_last_error = ""
        self.rx_line = bytearray()
        self.uart_rx_count = 0
        self.rx_line_count = 0
        self.telemetry_tx_count = 0
        self.telemetry_drop_count = 0
        self.command_rx_count = 0
        self.command_uart_tx_count = 0
        self.command_drop_count = 0
        self.last_command = ""
        self.task_state = None
        self.task_state_version = 0
        self.task_state_received_ms = None

    def start(self, socket_module=None):
        sock = None
        try:
            if socket_module is None:
                import socket as socket_module
            sock = socket_module.socket(
                socket_module.AF_INET, socket_module.SOCK_DGRAM,
            )
            sock.setsockopt(
                socket_module.SOL_SOCKET, socket_module.SO_REUSEADDR, 1,
            )
            sock.setsockopt(
                socket_module.SOL_SOCKET, socket_module.SO_BROADCAST, 1,
            )
            sock.bind(("0.0.0.0", self.command_port))
            sock.setblocking(False)
            self.sock = sock
            self.available = True
            self.last_error = ""
            log_i(
                "UDP",
                "广播 %s:%d，命令监听 0.0.0.0:%d" % (
                    self.telemetry_host,
                    self.telemetry_port,
                    self.command_port,
                ),
            )
            return True
        except Exception as e:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass
            self.sock = None
            self.available = False
            self.last_error = str(e)
            log_e("UDP", "初始化失败: %s" % e)
            return False

    @staticmethod
    def _is_numeric_csv(line):
        try:
            text = line.decode("ascii").strip()
        except Exception:
            return False
        if not text:
            return False

        fields = text.split(",")
        if not fields or len(fields) > 32:
            return False
        allowed = "+-0123456789.eE"
        for field in fields:
            token = field.strip()
            if not token or not any("0" <= ch <= "9" for ch in token):
                return False
            if any(ch not in allowed for ch in token):
                return False
            try:
                float(token)
            except Exception:
                return False
        return True

    @staticmethod
    def _normalize_command(data):
        try:
            text = data.decode("ascii").strip()
        except Exception:
            return None
        if not text or len(text) > 31:
            return None
        for ch in text:
            value = ord(ch)
            if value < 0x20 or value > 0x7E:
                return None
        return text.encode("ascii") + b"\n"

    def _forward_telemetry(self, line):
        task_state = parse_task_status_frame(line)
        if task_state is not None:
            self.task_state = task_state
            self.task_state_version += 1
            self.task_state_received_ms = int(_std_time.time() * 1000)
        elif not self._is_numeric_csv(line):
            self.telemetry_drop_count += 1
            return False
        if not self.available or self.sock is None:
            self.telemetry_drop_count += 1
            return False
        try:
            self.sock.sendto(
                bytes(line) + b"\n",
                (self.telemetry_host, self.telemetry_port),
            )
            self.telemetry_tx_count += 1
            self.telemetry_last_error = ""
            return True
        except Exception as e:
            self.telemetry_last_error = str(e)
            self.telemetry_drop_count += 1
            return False

    def reset_task_state(self):
        self.task_state = None
        self.task_state_received_ms = None

    def task_state_is_fresh(self, now_ms=None, stale_ms=TASK_STATUS_STALE_MS):
        if self.task_state is None or self.task_state_received_ms is None:
            return False
        if now_ms is None:
            now_ms = int(_std_time.time() * 1000)
        age_ms = int(now_ms) - self.task_state_received_ms
        return 0 <= age_ms <= int(stale_ms)

    def _poll_uart(self):
        if self.port is None:
            return 0
        try:
            data = self.port.read()
        except Exception as e:
            self.last_error = str(e)
            return 0
        if not data:
            return 0

        try:
            raw = bytes(data)
        except Exception as e:
            self.last_error = str(e)
            return 0

        completed = 0
        self.uart_rx_count += len(raw)
        for value in raw:
            if value == 10:
                line = bytes(self.rx_line).rstrip(b"\r")
                self.rx_line = bytearray()
                if line:
                    completed += 1
                    self.rx_line_count += 1
                    self._forward_telemetry(line)
            elif value != 13:
                if len(self.rx_line) < 511:
                    self.rx_line.append(value)
                else:
                    self.rx_line = bytearray()
                    self.telemetry_drop_count += 1
        return completed

    def _poll_commands(self):
        if not self.available or self.sock is None or self.port is None:
            return 0

        forwarded = 0
        for _ in range(8):
            try:
                data, _address = self.sock.recvfrom(128)
            except BlockingIOError:
                break
            except OSError as e:
                if getattr(e, "errno", None) in (11, 35, 10035):
                    break
                self.command_last_error = str(e)
                break
            except Exception as e:
                self.command_last_error = str(e)
                break

            self.command_rx_count += 1
            payload = self._normalize_command(data)
            if payload is None:
                self.command_drop_count += 1
                continue
            try:
                self.port.write(payload)
                self.command_uart_tx_count += 1
                self.last_command = payload.decode("ascii").strip()
                self.command_last_error = ""
                forwarded += 1
            except Exception as e:
                self.command_last_error = str(e)
                self.command_drop_count += 1
        return forwarded

    def poll(self):
        completed = self._poll_uart()
        forwarded = self._poll_commands()
        return completed, forwarded

    def close(self):
        sock = self.sock
        self.sock = None
        self.available = False
        if sock is not None:
            try:
                sock.close()
            except Exception:
                pass


def point_in_rect(x, y, rect):
    rx, ry, rw, rh = rect
    return rx <= x < rx + rw and ry <= y < ry + rh


def selection_button_rects(width, height):
    margin = 5
    rects = {}

    debug_gap = 5
    debug_top = height - 108
    debug_height = 46
    debug_width = (
        width - 2 * margin - debug_gap * (len(DEBUG_TASK_DEFS) - 1)
    ) // len(DEBUG_TASK_DEFS)
    for index, task_def in enumerate(DEBUG_TASK_DEFS):
        x = margin + index * (debug_width + debug_gap)
        width_now = (
            width - x - margin
            if index == len(DEBUG_TASK_DEFS) - 1 else debug_width
        )
        rects[task_def[0]] = (x, debug_top, width_now, debug_height)

    task_gap = 4
    task_top = height - 55
    task_height = 50
    task_width = (
        width - 2 * margin - task_gap * (len(COMPETITION_TASK_DEFS) - 1)
    ) // len(COMPETITION_TASK_DEFS)
    for index, task_def in enumerate(COMPETITION_TASK_DEFS):
        x = margin + index * (task_width + task_gap)
        width_now = (
            width - x - margin
            if index == len(COMPETITION_TASK_DEFS) - 1 else task_width
        )
        rects[task_def[0]] = (x, task_top, width_now, task_height)
    return rects


def running_button_rects(width, height):
    margin = 5
    gap = 5
    top = height - 39
    button_h = 34
    button_ids = ("cal", "rst", "home", "stop")
    button_w = (width - 2 * margin - 3 * gap) // 4
    rects = {}
    for index, button_id in enumerate(button_ids):
        x = margin + index * (button_w + gap)
        width_now = width - x - margin if index == 3 else button_w
        rects[button_id] = (x, top, width_now, button_h)
    return rects


def aim_view_rect(width, height):
    controls_top = running_button_rects(width, height)["cal"][1]
    return 0, 67, width, max(0, controls_top - 67)


def button_at(page, x, y, width, height):
    if page == PAGE_SELECT:
        for button_id, rect in selection_button_rects(width, height).items():
            if point_in_rect(x, y, rect):
                return button_id
    else:
        for button_id, rect in running_button_rects(width, height).items():
            if point_in_rect(x, y, rect):
                return button_id
    return None


def task_def_for(task_id):
    for task_def in TASK_DEFS:
        if task_def[0] == task_id:
            return task_def
    return None


def competition_task_number(task_id):
    if isinstance(task_id, str) and task_id.startswith("task"):
        try:
            number = int(task_id[4:])
        except Exception:
            return 0
        if 1 <= number <= 5:
            return number
    return 0


def format_target_frame(
    dx, dy, valid, locked=None, aim_ready=None, move_epoch=0,
):
    """Format the extended frame; the legacy three-argument call still works."""
    if not valid:
        dx = 0
        dy = 0
        locked = False
        aim_ready = False
    elif locked is None:
        locked = True
    if aim_ready is None:
        aim_ready = False
    dx = max(-VISION_LIMIT, min(VISION_LIMIT, int(dx)))
    dy = max(-VISION_LIMIT, min(VISION_LIMIT, int(dy)))
    aim_ready = bool(
        aim_ready
        and locked
        and abs(dx) <= AIM_READY_DEADZONE_X_PX
        and abs(dy) <= AIM_READY_DEADZONE_Y_PX
    )
    move_epoch = max(0, min(0xFFFFFFFF, int(move_epoch)))
    return "V,%d,%d,%d,%d,%d,%d\n" % (
        dx,
        dy,
        1 if valid else 0,
        1 if locked else 0,
        1 if aim_ready else 0,
        move_epoch,
    )


def uart_write_text(port, text):
    if port is None:
        return False
    try:
        port.write(text.encode("ascii"))
        return True
    except Exception as e:
        log_e("UART", "发送失败: %s" % e)
        return False


def send_task_command(port, task_id):
    task_def = task_def_for(task_id)
    if task_def is None or task_def[2] is None:
        return False
    # Page/task changes always revoke the old target before stopping motion.
    invalid_ok = uart_write_text(port, format_target_frame(0, 0, False))
    stand_ok = uart_write_text(port, "stand\n")
    task_ok = uart_write_text(port, task_def[2] + "\n")
    return invalid_ok and stand_ok and task_ok


def send_vision_command(port):
    return send_task_command(port, STAGE_VISION)


def send_stop_command(port):
    # 顺序是安全协议的一部分：先撤销视觉目标，再让底盘进入 stand。
    invalid_ok = uart_write_text(port, format_target_frame(0, 0, False))
    stand_ok = uart_write_text(port, "stand\n")
    return invalid_ok and stand_ok


def init_uart2(uart_module, pinmap_module=None):
    try:
        if pinmap_module is not None:
            try:
                pinmap_module.set_pin_function("B0", "UART2_TX")
                pinmap_module.set_pin_function("B1", "UART2_RX")
            except Exception as e:
                log_i("UART", "Pinmap 提示: %s" % e)
        devices = uart_module.list_devices()
        log_i("UART", "可用串口: %s" % devices)
        port = uart_module.UART("/dev/ttyS2", 115200)
        log_i("UART", "UART2 /dev/ttyS2 115200 8N1 已连接")
        return port
    except Exception as e:
        log_e("UART", "/dev/ttyS2 打开失败: %s" % e)
        return None


def elapsed_ms(time_module, now_ms, previous_ms):
    try:
        # MaixCAM2 uses ticks_diff(previous, current).
        return time_module.ticks_diff(previous_ms, now_ms)
    except AttributeError:
        return now_ms - previous_ms


def read_touch(ts, img, disp, image_module):
    try:
        screen_x, screen_y, pressed = ts.read()
        mapped_x, mapped_y = image_module.resize_map_pos_reverse(
            img.width(),
            img.height(),
            disp.width(),
            disp.height(),
            image_module.Fit.FIT_CONTAIN,
            screen_x,
            screen_y,
        )
        mapped_x = max(0, min(img.width() - 1, int(mapped_x)))
        mapped_y = max(0, min(img.height() - 1, int(mapped_y)))
        return mapped_x, mapped_y, bool(pressed)
    except Exception:
        return -1, -1, False


def configure_font(image_module):
    try:
        image_module.load_font(
            "sourcehansans",
            "/maixapp/share/font/SourceHanSansCN-Regular.otf",
        )
        image_module.set_default_font("sourcehansans")
        return True
    except Exception as e:
        log_i("UI", "中文字体不可用，使用英文字体: %s" % e)
        return False


def text_size(image_module, text, scale=1.0):
    try:
        size = image_module.string_size(text, scale=scale)
        return int(size.width()), int(size.height())
    except Exception:
        return int(len(text) * 8 * scale), int(16 * scale)


def draw_centered_text(img, image_module, rect, text, color, scale=1.0):
    x, y, w, h = rect
    text_w, text_h = text_size(image_module, text, scale)
    draw_x = max(x + 2, x + (w - text_w) // 2)
    draw_y = max(y + 2, y + (h - text_h) // 2)
    img.draw_string(draw_x, draw_y, text, color=color, scale=scale)


def link_state_label(uart_ready, udp_bridge):
    uart_state = "OK" if uart_ready else "OFF"
    udp_state = "ON" if udp_bridge.available else "OFF"
    return "UART %s UDP %s" % (uart_state, udp_state)


def draw_selection_ui(
    img, image_module, armed_button, uart_ready, udp_bridge,
    chinese_font, notice, calibration, target_locked,
):
    width = img.width()
    height = img.height()
    black = image_module.COLOR_BLACK
    white = image_module.COLOR_WHITE

    img.draw_rect(0, 0, width, 30, color=black, thickness=-1)
    cal_text = "C:OK" if calibration.calibrated else "UNCAL"
    title = ("任务 " if chinese_font else "SELECT ") + cal_text
    img.draw_string(7, 9, title, color=white)
    link_text = link_state_label(uart_ready, udp_bridge)
    link_w, _ = text_size(image_module, link_text)
    img.draw_string(max(7, width - link_w - 7), 9, link_text, color=white)

    for task_id, label, _command in TASK_DEFS:
        rect = selection_button_rects(width, height)[task_id]
        pressed = armed_button == task_id
        target_ready = (
            task_id == STAGE_TARGET
            and calibration.calibrated
            and target_locked
        )
        img.draw_rect(
            *rect, color=white if pressed else black, thickness=-1,
        )
        img.draw_rect(
            *rect,
            color=white,
            thickness=3 if pressed or target_ready else 1,
        )
        draw_centered_text(
            img, image_module, rect, label, black if pressed else white,
        )
    if notice:
        img.draw_rect(0, 30, width, 20, color=black, thickness=-1)
        draw_centered_text(
            img, image_module, (0, 31, width, 18), notice, white,
        )


def calibration_state_label(calibration, target_locked):
    if calibration.pending:
        return "CAL %d/%d" % (
            calibration.sample_count(), AUTO_CAL_SAMPLE_COUNT,
        )
    if not calibration.calibrated:
        return "UNCAL"
    return "LOCK" if target_locked else "LOST"


def mission_stage_label(mission_stage):
    task_number = competition_task_number(mission_stage)
    if task_number:
        return "TASK%d" % task_number
    if mission_stage == STAGE_TARGET:
        return "TARGET"
    if mission_stage == STAGE_TRACK:
        return "TRACK"
    return "VISION"


def stage_allows_target_output(mission_stage):
    return (
        mission_stage in (STAGE_TRACK, STAGE_TARGET)
        or competition_task_number(mission_stage) != 0
    )


def task_phase_label(phase):
    try:
        phase = int(phase)
    except Exception:
        return "?"
    if 0 <= phase < len(TASK_PHASE_LABELS):
        return TASK_PHASE_LABELS[phase]
    return "?"


def competition_status_health(mission_stage, task_state, fresh):
    active_task = competition_task_number(mission_stage)
    if not active_task:
        return None
    if task_state is None or task_state.get("task_id") != active_task:
        return "WAIT"
    if not fresh:
        return "LOST"
    return "OK"


def target_start_block_reason(calibrated, target_locked):
    if not calibrated:
        return "CAL REQUIRED"
    if not target_locked:
        return "LOCK REQUIRED"
    return ""


def draw_running_ui(
    img, image_module, target_locked, armed_button, calibration,
    mission_stage, uart_ready, udp_bridge, task_state=None,
    task_status_health=None,
):
    width = img.width()
    height = img.height()
    black = image_module.COLOR_BLACK
    white = image_module.COLOR_WHITE
    state_label = calibration_state_label(calibration, target_locked)
    left_label = "%s U:%s D:%s" % (
        mission_stage_label(mission_stage),
        "OK" if uart_ready else "OFF",
        "ON" if udp_bridge.available else "OFF",
    )

    img.draw_rect(0, 0, width, 30, color=black, thickness=-1)
    img.draw_string(7, 7, left_label, color=white)
    state_w, _ = text_size(image_module, state_label)
    img.draw_string(max(7, width - state_w - 7), 7, state_label, color=white)

    img.draw_rect(0, 30, width, 37, color=black, thickness=-1)
    if task_status_health:
        img.draw_string(
            7, 41, "STM STATUS %s" % task_status_health, color=white,
        )
    elif task_state is None:
        img.draw_string(7, 41, "STM: NO TASK STATUS", color=white)
    else:
        status_line_1 = "P:%s SHOT:%d" % (
            task_phase_label(task_state["phase"]),
            task_state["shot"],
        )
        status_line_2 = "A:%d LASER:%s M:%02X DONE:%d" % (
            task_state["aim"],
            "ON" if task_state["laser"] else "OFF",
            task_state["milestones"],
            task_state["done"],
        )
        img.draw_string(7, 32, status_line_1, color=white)
        img.draw_string(7, 49, status_line_2, color=white)

    buttons = running_button_rects(width, height)
    for button_id, label in (
        ("cal", "CAL"),
        ("rst", "RST"),
        ("home", "HOME"),
        ("stop", "STOP"),
    ):
        rect = buttons[button_id]
        active = (
            (button_id == "cal" and calibration.pending)
            or button_id == "stop"
        )
        pressed = armed_button == button_id
        filled = active and not pressed
        img.draw_rect(*rect, color=white if filled else black, thickness=-1)
        border_width = 3 if pressed or button_id == "stop" else 1
        img.draw_rect(*rect, color=white, thickness=border_width)
        draw_centered_text(
            img, image_module, rect, label, black if filled else white,
        )


def draw_target_overlay(display_img, image_module, tgt, cx, cy):
    corners = tgt["corners"]
    for corner in corners:
        display_img.draw_circle(
            corner[0], corner[1], 5,
            color=image_module.COLOR_WHITE,
            thickness=2,
        )
        display_img.draw_rect(
            max(0, corner[0] - ROI_SIZE // 2),
            max(0, corner[1] - ROI_SIZE // 2),
            ROI_SIZE,
            ROI_SIZE,
            color=image_module.COLOR_WHITE,
            thickness=1,
        )
    for index in range(4):
        p1 = corners[index]
        p2 = corners[(index + 1) % 4]
        display_img.draw_line(
            p1[0], p1[1], p2[0], p2[1],
            color=image_module.COLOR_WHITE,
            thickness=2,
        )
    display_img.draw_cross(
        cx, cy, color=image_module.COLOR_WHITE, size=15, thickness=1,
    )


def draw_aim_reference_overlay(display_img, image_module, calibration):
    # Black/white double stroke remains visible on either part of the target.
    display_img.draw_cross(
        calibration.aim_ref_x,
        calibration.aim_ref_y,
        color=image_module.COLOR_BLACK,
        size=12,
        thickness=3,
    )
    display_img.draw_cross(
        calibration.aim_ref_x,
        calibration.aim_ref_y,
        color=image_module.COLOR_WHITE,
        size=8,
        thickness=1,
    )


# =============================================================================
# 主程序
# =============================================================================
def main():
    global DEBUG_FAIL_REASON

    from maix import app, camera, display, image, time, touchscreen, uart
    try:
        from maix import pinmap
    except Exception:
        pinmap = None

    try:
        cam = camera.Camera(CAM_W, CAM_H, image.Format.FMT_GRAYSCALE)
    except Exception:
        cam = camera.Camera(CAM_W, CAM_H)
    disp = display.Display()
    ts = touchscreen.TouchScreen()
    chinese_font = configure_font(image)
    uart_port = init_uart2(uart, pinmap)
    udp_bridge = UartUdpBridge(uart_port)
    udp_bridge.start()

    frame_count = 0
    last_roi = None
    last_corners = None
    sm_cx, sm_cy = None, None
    calibration = AimReferenceCalibration(AIM_REF_X, AIM_REF_Y, CAM_W, CAM_H)
    lock_tracker = TargetLockTracker()
    move_tracker = TargetMoveEpochTracker()
    roi_miss_count = 0
    page = PAGE_SELECT
    mission_stage = STAGE_VISION
    notice = ""
    touch_latch = PressReleaseLatch()
    start_ms = time.ticks_ms()
    last_target_tx_ms = None

    def timestamp_ms():
        return int(elapsed_ms(time, time.ticks_ms(), start_ms))

    print("[T:%06d] SYS_START build=%s aim_ref=(%d,%d)" % (
        timestamp_ms(), VISION_BUILD_ID,
        calibration.aim_ref_x, calibration.aim_ref_y,
    ))
    print(
        "[T:%06d] VISION_CFG bin=%d..%d white_blob=%d/%d "
        "white_rect=%d..%d aspect=%.2f..%.2f black_blob=%d/%d "
        "black_rect=%d..%d black_aspect=%.2f..%.2f "
        "core_fill=%.2f..%.2f inner=%d..%d "
        "quad_fill=%.2f..%.2f max_cos=%.2f diag_min=%.2f "
        "center_max=%d aim=core_center" % (
            timestamp_ms(),
            BINARY_GRAY_MIN,
            BINARY_GRAY_MAX,
            WHITE_BLOB_AREA_THRESHOLD,
            WHITE_BLOB_PIXELS_THRESHOLD,
            WHITE_FRAME_RECT_AREA_MIN,
            WHITE_FRAME_RECT_AREA_MAX,
            WHITE_FRAME_ASPECT_MIN,
            WHITE_FRAME_ASPECT_MAX,
            BLACK_BLOB_AREA_THRESHOLD,
            BLACK_BLOB_PIXELS_THRESHOLD,
            BLACK_CORE_RECT_AREA_MIN,
            BLACK_CORE_RECT_AREA_MAX,
            BLACK_CORE_ASPECT_MIN,
            BLACK_CORE_ASPECT_MAX,
            BLACK_CORE_RELATIVE_AREA_MIN,
            BLACK_CORE_RELATIVE_AREA_MAX,
            TARGET_INNER_AREA_MIN,
            TARGET_INNER_AREA_MAX,
            TARGET_QUAD_FILL_RATIO_MIN,
            TARGET_QUAD_FILL_RATIO_MAX,
            TARGET_QUAD_MAX_ABS_CORNER_COS,
            TARGET_QUAD_DIAGONAL_RATIO_MIN,
            TARGET_SELECTION_MAX_CENTER_DISTANCE_PX,
        )
    )

    # 上电尚未运行任务，立即明确撤销任何旧视觉目标。
    uart_write_text(uart_port, format_target_frame(0, 0, False))
    last_target_tx_ms = time.ticks_ms()

    try:
        while not app.need_exit():
            udp_bridge.poll()
            try:
                img = cam.read()
            except Exception as e:
                print("[T:%06d] CAM_ERR %s" % (timestamp_ms(), e))
                _std_time.sleep(0.05)
                continue
            if img is None:
                continue
            frame_count += 1

            if img.format() != image.Format.FMT_GRAYSCALE:
                try:
                    img = img.to_format(image.Format.FMT_GRAYSCALE)
                except AttributeError:
                    img = img.to_grayscale()

            # VISION 是阈值调试页，直接显示检测使用的二值图；其他页面保留灰度图。
            show_binary = (
                page == PAGE_RUNNING and mission_stage == STAGE_VISION
            )
            if not show_binary:
                display_img = img.copy()
            binary_result = img.binary(COLOR_TH["tape_bin"]["gray"])
            bin_img = binary_result if binary_result is not None else img
            if show_binary:
                display_img = bin_img.copy()
            if calibration.pending:
                # CAL must reacquire globally from the screen-center prior. An
                # old ROI/EMA would otherwise keep feeding the previously
                # selected stand or background rectangle into all 8 samples.
                last_roi = None
                last_corners = None
                sm_cx = None
                sm_cy = None
                tgt = find_target(
                    bin_img,
                    preferred_center=(AIM_REF_X, AIM_REF_Y),
                )
            else:
                tgt = find_target(
                    bin_img,
                    last_roi,
                    last_corners,
                    preferred_center=(
                        calibration.aim_ref_x, calibration.aim_ref_y,
                    ),
                )
            if tgt and tgt["inner_area"] < TARGET_INNER_AREA_MIN:
                DEBUG_FAIL_REASON = "黑芯总面积过小(%d<%d)" % (
                    tgt["inner_area"], TARGET_INNER_AREA_MIN,
                )
                tgt = None

            if (tgt is None and last_roi is not None and
                    not calibration.pending):
                rx, ry, rw, rh = last_roi
                old_cx, old_cy = rx + rw // 2, ry + rh // 2
                new_w = int(rw * 1.3)
                new_h = int(rh * 1.3)
                if new_w >= CAM_W or new_h >= CAM_H:
                    last_roi = None
                    last_corners = None
                    tgt = find_target(
                        bin_img,
                        preferred_center=(
                            calibration.aim_ref_x, calibration.aim_ref_y,
                        ),
                    )
                    if tgt and tgt["inner_area"] < TARGET_INNER_AREA_MIN:
                        DEBUG_FAIL_REASON = "黑芯总面积过小(%d<%d)" % (
                            tgt["inner_area"], TARGET_INNER_AREA_MIN,
                        )
                        tgt = None
                else:
                    new_rx = max(0, old_cx - new_w // 2)
                    new_ry = max(0, old_cy - new_h // 2)
                    new_w = min(CAM_W - new_rx, new_w)
                    new_h = min(CAM_H - new_ry, new_h)
                    last_roi = (new_rx, new_ry, new_w, new_h)
                    tgt = find_target(
                        bin_img,
                        last_roi,
                        last_corners,
                        preferred_center=(
                            calibration.aim_ref_x, calibration.aim_ref_y,
                        ),
                    )
                    if tgt and tgt["inner_area"] < TARGET_INNER_AREA_MIN:
                        DEBUG_FAIL_REASON = "黑芯总面积过小(%d<%d)" % (
                            tgt["inner_area"], TARGET_INNER_AREA_MIN,
                        )
                        tgt = None

            dx = 0
            dy = 0
            cx = calibration.aim_ref_x
            cy = calibration.aim_ref_y
            if tgt:
                raw_cx, raw_cy = tgt["cx"], tgt["cy"]
                if sm_cx is not None and sm_cy is not None:
                    jump = math.sqrt((raw_cx - sm_cx) ** 2 + (raw_cy - sm_cy) ** 2)
                    if jump > LOCK_MAX_JUMP:
                        DEBUG_FAIL_REASON = "中心跳动过大(%.1f)" % jump
                        tgt = None

            if tgt:
                if sm_cx is None:
                    sm_cx, sm_cy = float(raw_cx), float(raw_cy)
                else:
                    sm_cx = sm_cx * (1 - SMOOTH_ALPHA) + raw_cx * SMOOTH_ALPHA
                    sm_cy = sm_cy * (1 - SMOOTH_ALPHA) + raw_cy * SMOOTH_ALPHA
                cx, cy = int(sm_cx), int(sm_cy)
                last_corners = tgt["corners"]
                ox, oy, ow, oh = tgt["outer_rect"]
                ex = max(60, ow // 2)
                ey = max(60, oh // 2)
                rx = max(0, ox - ex)
                ry = max(0, oy - ey)
                last_roi = (
                    rx,
                    ry,
                    min(ow + 2 * ex, CAM_W - rx),
                    min(oh + 2 * ey, CAM_H - ry),
                )
                roi_miss_count = 0
            else:
                roi_miss_count += 1
                if roi_miss_count > 15:
                    sm_cx, sm_cy = None, None
                    last_roi = None
                    last_corners = None

            if (page == PAGE_RUNNING and calibration.pending and tgt):
                if calibration.update_auto(cx, cy):
                    notice = "CAL OK"
                    print("[T:%06d] UI_CAL_AUTO ref=(%d,%d) samples=%d" % (
                        timestamp_ms(),
                        calibration.aim_ref_x,
                        calibration.aim_ref_y,
                        AUTO_CAL_SAMPLE_COUNT,
                    ))

            target_locked = lock_tracker.update(tgt is not None)

            touch_x, touch_y, pressed = read_touch(ts, display_img, disp, image)
            touched_button = button_at(
                page,
                touch_x,
                touch_y,
                display_img.width(),
                display_img.height(),
            )
            clicked_button = touch_latch.update(pressed, touched_button)

            if clicked_button == STAGE_VISION:
                vision_ok = send_vision_command(uart_port)
                page = PAGE_RUNNING
                mission_stage = STAGE_VISION
                udp_bridge.reset_task_state()
                move_tracker.leave_wait()
                if not calibration.calibrated:
                    if not calibration.pending:
                        calibration.start()
                    notice = "CAL LOCKING"
                else:
                    notice = ""
                print("[T:%06d] UI_VISION laser=%d auto_cal=%d" % (
                    timestamp_ms(),
                    1 if vision_ok else 0,
                    1 if calibration.pending else 0,
                ))
            elif clicked_button == STAGE_TRACK:
                if send_task_command(uart_port, STAGE_TRACK):
                    page = PAGE_RUNNING
                    mission_stage = STAGE_TRACK
                    udp_bridge.reset_task_state()
                    move_tracker.leave_wait()
                    notice = ""
                    print("[T:%06d] UI_TRACK aim" % timestamp_ms())
                else:
                    notice = "UART ERROR"
            elif clicked_button == STAGE_TARGET:
                block_reason = target_start_block_reason(
                    calibration.calibrated, target_locked,
                )
                if block_reason:
                    notice = block_reason
                    print("[T:%06d] UI_TARGET BLOCKED %s" % (
                        timestamp_ms(), block_reason,
                    ))
                elif send_task_command(uart_port, STAGE_TARGET):
                    page = PAGE_RUNNING
                    mission_stage = STAGE_TARGET
                    udp_bridge.reset_task_state()
                    move_tracker.leave_wait()
                    notice = ""
                    print("[T:%06d] UI_TARGET shoot" % timestamp_ms())
                else:
                    notice = "UART ERROR"
            elif competition_task_number(clicked_button):
                if send_task_command(uart_port, clicked_button):
                    page = PAGE_RUNNING
                    mission_stage = clicked_button
                    udp_bridge.reset_task_state()
                    move_tracker.leave_wait()
                    notice = ""
                    print("[T:%06d] UI_TASK start=%s" % (
                        timestamp_ms(), clicked_button,
                    ))
                else:
                    notice = "UART ERROR"
            elif clicked_button == "cal":
                cal_command_ok = True
                if mission_stage != STAGE_VISION:
                    cal_command_ok = send_task_command(uart_port, STAGE_TRACK)
                if not cal_command_ok:
                    notice = "UART ERROR"
                    print("[T:%06d] UI_CAL UART_ERROR" % timestamp_ms())
                else:
                    if mission_stage != STAGE_VISION:
                        # CAL always returns a moving TARGET task to static yaw.
                        mission_stage = STAGE_TRACK
                    calibration.start()
                    notice = "CAL LOCKING"
                    print("[T:%06d] UI_CAL_AUTO_ARM target=%d" % (
                        timestamp_ms(), 1 if tgt is not None else 0,
                    ))
            elif clicked_button == "rst":
                if mission_stage == STAGE_VISION:
                    calibration.reset()
                    calibration.start()
                    notice = "CAL LOCKING"
                    print("[T:%06d] UI_CAL_RESET_AUTO VISION ref=(%d,%d)" % (
                        timestamp_ms(),
                        calibration.aim_ref_x,
                        calibration.aim_ref_y,
                    ))
                elif send_task_command(uart_port, STAGE_TRACK):
                    mission_stage = STAGE_TRACK
                    calibration.reset()
                    calibration.start()
                    notice = "CAL LOCKING"
                    print("[T:%06d] UI_CAL_RESET_AUTO TRACK ref=(%d,%d)" % (
                        timestamp_ms(),
                        calibration.aim_ref_x,
                        calibration.aim_ref_y,
                    ))
                else:
                    print("[T:%06d] UI_RST UART_ERROR" % timestamp_ms())
            elif clicked_button == "home":
                stop_ok = send_stop_command(uart_port)
                page = PAGE_SELECT
                mission_stage = STAGE_VISION
                udp_bridge.reset_task_state()
                move_tracker.leave_wait()
                notice = "HOME" if stop_ok else "UART ERROR"
                print("[T:%06d] UI_HOME stand=%d" % (
                    timestamp_ms(), 1 if stop_ok else 0,
                ))
            elif clicked_button == "stop":
                stop_ok = send_stop_command(uart_port)
                mission_stage = STAGE_VISION
                udp_bridge.reset_task_state()
                move_tracker.leave_wait()
                print("[T:%06d] UI_STOP stand=%d VISION" % (
                    timestamp_ms(), 1 if stop_ok else 0,
                ))

            target_error_ready = calibration.calibrated and target_locked
            if target_error_ready:
                dx, dy = calibration.target_error(cx, cy)
            aim_ready = target_aim_ready(
                calibration.calibrated, target_locked, dx, dy,
            )
            task_state = udp_bridge.task_state
            active_task = competition_task_number(mission_stage)
            task_status_health = competition_status_health(
                mission_stage,
                task_state,
                udp_bridge.task_state_is_fresh(),
            )
            task_status_ready = task_status_health in (None, "OK")
            target_valid = (
                page == PAGE_RUNNING
                and stage_allows_target_output(mission_stage)
                and target_error_ready
                and task_status_ready
            )
            waiting_for_move = bool(
                page == PAGE_RUNNING
                and active_task
                and task_status_ready
                and task_state is not None
                and task_state["task_id"] == active_task
                and task_state["phase"] == TASK_PHASE_WAIT_TARGET_MOVE
            )
            if task_status_health == "LOST":
                move_tracker.pause_wait()
                move_epoch = move_tracker.move_epoch
            else:
                move_epoch = move_tracker.update(
                    waiting_for_move,
                    target_locked,
                    (cx, cy) if target_locked else None,
                )
            now_ms = time.ticks_ms()
            if (
                last_target_tx_ms is None
                or elapsed_ms(time, now_ms, last_target_tx_ms) >= TARGET_SEND_PERIOD_MS
            ):
                uart_write_text(
                    uart_port,
                    format_target_frame(
                        dx,
                        dy,
                        target_valid,
                        target_locked,
                        aim_ready,
                        move_epoch,
                    ),
                )
                last_target_tx_ms = now_ms

            fps = time.fps()
            state = calibration_state_label(calibration, target_locked)
            if tgt:
                draw_target_overlay(display_img, image, tgt, cx, cy)
                if (page == PAGE_RUNNING and
                        frame_count % VISION_DEBUG_LOG_PERIOD_FRAMES == 0):
                    print(
                        "[T:%06d] VIS %s %s fps=%.0f dx=%+d dy=%+d "
                        "center=(%d,%d) outer=%d/%.2f core=%d/%.2f "
                        "fill=%.2f quad=%.2f cos=%.2f diag=%.2f "
                        "cand=%d score=%.1f"
                        % (
                            timestamp_ms(),
                            mission_stage_label(mission_stage),
                            state,
                            fps,
                            dx,
                            dy,
                            cx,
                            cy,
                            tgt["outer_area"],
                            tgt["outer_aspect"],
                            tgt["inner_area"],
                            tgt["core_aspect"],
                            tgt["inner_ratio"],
                            tgt["quad_fill_ratio"],
                            tgt["max_abs_corner_cos"],
                            tgt["diagonal_ratio"],
                            tgt["candidate_count"],
                            tgt["selection_score"],
                        )
                    )
            elif (page == PAGE_RUNNING and
                  frame_count % VISION_DEBUG_LOG_PERIOD_FRAMES == 0):
                print(
                    "[T:%06d] BIN %d..%d fps=%.0f white=%d maxA=%d "
                    "black=%d maxA=%d reason=%s" % (
                        timestamp_ms(),
                        BINARY_GRAY_MIN,
                        BINARY_GRAY_MAX,
                        fps,
                        DEBUG_BLOB_STATS["white_count"],
                        DEBUG_BLOB_STATS["white_max_area"],
                        DEBUG_BLOB_STATS["black_count"],
                        DEBUG_BLOB_STATS["black_max_area"],
                        DEBUG_FAIL_REASON,
                    )
                )

            if page == PAGE_SELECT:
                draw_selection_ui(
                    display_img,
                    image,
                    touch_latch.armed_button,
                    uart_port is not None,
                    udp_bridge,
                    chinese_font,
                    notice,
                    calibration,
                    target_locked,
                )
            else:
                draw_aim_reference_overlay(display_img, image, calibration)
                draw_running_ui(
                    display_img,
                    image,
                    target_locked,
                    touch_latch.armed_button,
                    calibration,
                    mission_stage,
                    uart_port is not None,
                    udp_bridge,
                    task_state if task_status_ready else None,
                    None if task_status_health == "OK" else task_status_health,
                )

            disp.show(display_img, fit=image.Fit.FIT_CONTAIN)
            if frame_count % 150 == 0:
                print(
                    "[T:%06d] HEARTBEAT fps=%.0f page=%s stage=%s "
                    "UART_RX=%d UDP_TX=%d CMD=%d UDP=%s" % (
                        timestamp_ms(),
                        fps,
                        page,
                        mission_stage_label(mission_stage),
                        udp_bridge.uart_rx_count,
                        udp_bridge.telemetry_tx_count,
                        udp_bridge.command_uart_tx_count,
                        "ON" if udp_bridge.available else "OFF",
                    )
                )

    except Exception as e:
        log_e("MAIN", "异常: %s" % e)
        import traceback
        traceback.print_exc()
    finally:
        send_stop_command(uart_port)
        udp_bridge.close()
        try:
            del ts
        except Exception:
            pass
        log_i("MAIN", "系统关闭")


if __name__ == "__main__":
    main()
