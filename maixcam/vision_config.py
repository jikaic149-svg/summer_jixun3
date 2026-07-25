"""箱体正面识别的集中配置。

第一次运行不需要 YOLO 模型：保持 ``YOLO_MODEL_PATH`` 为空，程序会直接在
整幅原生摄像头图像中运行中心定位。训练并转换好模型后，只需要填写模型路径和
目标类别编号，就会自动切换为“YOLO 粗框 + 框内精定位”。框内方法由下面两个
互斥布尔开关选择。
"""


# ---------------------------------------------------------------------------
# 运行模式与相机
# ---------------------------------------------------------------------------

# 空字符串表示暂时不启用 YOLO；可以填写相对于 maixcam/ 的路径，例如：
# YOLO_MODEL_PATH = "models/box_face.mud"
YOLO_MODEL_PATH = ""

# 不启用 YOLO 时使用原生 MaixCAM2 摄像头的分辨率。
CAMERA_WIDTH = 640
CAMERA_HEIGHT = 480
CAMERA_FPS = 30
CAMERA_BUFFER_COUNT = 3


# ---------------------------------------------------------------------------
# PMOD_TOF100正式测距
# ---------------------------------------------------------------------------

TOF_ENABLED = True
TOF_REQUIRED = True

# 每3个RGB帧读取一次TOF；同一份VISION_TOF数据用于终端日志和可选UART发送。
TOF_SAMPLE_INTERVAL_FRAMES = 3
TOF_RESOLUTION = 50
TOF_SPI_BUS_NUM = 2
TOF_SPI_CS_NUM = -1
TOF_VALID_MIN_MM = 200
TOF_VALID_MAX_MM = 1500
TOF_DISPLAY_MIN_MM = 200
TOF_DISPLAY_MAX_MM = 1500
TOF_ROTATE_180 = False
TOF_CONFIGURE_PINS = True

# 保持此前实机使用的中心单元准心测距；若单点噪声仍大，可改为3并把最少有效点
# 改为5，此时输出是中心3x3区域的中位数。
TOF_CENTER_ROI_SIZE = 1
TOF_MIN_VALID_POINTS = 1


# ---------------------------------------------------------------------------
# UART2视觉/TOF下行接口（预留，默认关闭）
# ---------------------------------------------------------------------------

# 与当前TOF100引脚不冲突。确认接线和下位机解析完成后只需改为True；UART初始化
# 失败默认不阻止视觉运行，联调阶段若希望失败即停机可把UART_REQUIRED改为True。
UART_ENABLED = False
UART_REQUIRED = False
UART_DEVICE = "/dev/ttyS2"
UART_BAUD_RATE = 115200
UART_TX_PIN = "B0"
UART_RX_PIN = "B1"
UART_TX_FUNCTION = "UART2_TX"
UART_RX_FUNCTION = "UART2_RX"


# ---------------------------------------------------------------------------
# 发挥部分第一问：自动瞄准稳定判定
# ---------------------------------------------------------------------------

# 只对已完成精定位的水平中轴计数。TOF每3个RGB帧发布一次，因此3个连续样本
# 约对应0.3 s；任何丢靶、非最终中心或越出容差都会立即清零。
AUTO_AIM_X_TOLERANCE_PX = 8
AUTO_AIM_STABLE_SAMPLES = 3


# ---------------------------------------------------------------------------
# YOLO 粗定位
# ---------------------------------------------------------------------------

# 当前模型只有 ``box_face`` 一个类别，因此目标类别固定为0。
YOLO_TARGET_CLASS_ID = 0
# 新场景和近距离截断目标的置信度通常低于训练/验证集。此前背景弱误报只有
# 0.1205，因此先使用0.25；传统视觉仍会继续验证YOLO粗框。
YOLO_CONF_TH = 0.25
YOLO_IOU_TH = 0.45
YOLO_DUAL_BUFF = False

# 找到目标后每2帧刷新一次640x640 YOLO，中间帧用最近两次粗框线性预测；目标
# 丢失后立即恢复逐帧YOLO搜索。
YOLO_DETECT_INTERVAL_FRAMES = 2
YOLO_PREDICT_CACHED_BOX = True
# 实机表明近距离YOLO置信度仍约0.92~0.97，粗框足够稳定；继续隔帧能显著减少
# 23~45 ms 的NPU耗时。若后续高速转动时缓存框跟不上，再临时改为True对照。
YOLO_EVERY_FRAME_WHEN_NEAR = False

# YOLO 框向四周扩大的比例。保留框外背景，能让传统视觉看见箱面完整边缘。
YOLO_ROI_EXPAND_RATIO = 0.16

# YOLO框占画面过大或碰到图像边界时标记为近距离目标，日志输出 near=1。
YOLO_NEAR_AREA_RATIO = 0.55
YOLO_NEAR_BORDER_MARGIN_PX = 3

# 启用 YOLO 后，如果当前帧没有检测到目标，默认不回退到全图搜索，避免背景中的
# 矩形被误认成箱面。调试模型召回率时可以临时改为 True。
FALLBACK_FULL_FRAME_WHEN_YOLO_MISSES = False


# ---------------------------------------------------------------------------
# YOLO框内中心定位方法（二选一）
# ---------------------------------------------------------------------------

# 原方案：OpenCV灰度、高斯、Canny、闭运算和凸四边形拟合。
# 当前主线关闭它，改为“find_blobs搜索引导 + 面积稳定后灰度边缘绿框锁存”。
USE_OPENCV_QUAD_METHOD = False

# 当前正式方案：运动时用原生find_blobs橙框快速引导，停稳后用灰度边缘绿框精定位；
# 两种框都从左右边计算中轴线。
USE_NATIVE_FIND_BLOBS_METHOD = True

# MaixPy LAB格式为 (L_min,L_max,A_min,A_max,B_min,B_max)：L为0~100，A/B为
# -128~127。此初值由当前230张正样本和40张负样本换算得到，赛场仍需实测。
FIND_BLOBS_LAB_THRESHOLDS = [(20, 50, 1, 9, 6, 20)]
FIND_BLOBS_MIN_PIXELS = 40
FIND_BLOBS_MIN_AREA = 100
FIND_BLOBS_PIXELS_RATIO_ROI = 0.01
FIND_BLOBS_AREA_RATIO_ROI = 0.03
FIND_BLOBS_MIN_WIDTH_RATIO_ROI = 0.20
FIND_BLOBS_MIN_HEIGHT_RATIO_ROI = 0.20
FIND_BLOBS_MERGE = True
FIND_BLOBS_MARGIN = 3
FIND_BLOBS_MAX_CANDIDATES = 8

# ---------------------------------------------------------------------------
# 颜色面积峰值与角点精定位
# ---------------------------------------------------------------------------

# 搜索阶段把所有Blob候选的阈值像素数相加；总面积在窗口内形成稳定平台后锁存早期
# 灰度+Canny绿框模式。绿框连续失败2帧才退回Blob搜索。
AREA_PEAK_WINDOW_FRAMES = 8
AREA_PEAK_CONFIRM_FRAMES = 5
AREA_PEAK_TOLERANCE = 0.08
AREA_PEAK_RELEASE_RATIO = 0.86
USE_GREEN_EDGE_QUAD_REFINE = True
GREEN_QUAD_PROCESS_SCALE = 0.50
GREEN_QUAD_REFINE_INTERVAL_FRAMES = 1
GREEN_QUAD_MAX_MISSES = 2


# ---------------------------------------------------------------------------
# 左右竖边精定位（橙框/绿框共用）
# ---------------------------------------------------------------------------

# 当前框只提供左右窄带搜索位置；真正的中轴优先由Canny+Hough筛出的两条近竖直边
# 计算。找不齐两边时自动回退到当前橙框或绿框本身的左右边。
VERTICAL_EDGE_ENABLED = True
VERTICAL_EDGE_PROCESS_SCALE = 0.50
VERTICAL_EDGE_GAUSSIAN_KERNEL = 3
VERTICAL_EDGE_CANNY_LOW = 40
VERTICAL_EDGE_CANNY_HIGH = 120
VERTICAL_EDGE_HOUGH_THRESHOLD = 18
VERTICAL_EDGE_MIN_LENGTH_RATIO = 0.25
VERTICAL_EDGE_MAX_GAP_RATIO = 0.08

# 左右各自在当前框边附近23%箱面宽度的窄带中搜索。倾斜量定义为|dx|/|dy|；0表示
# 完全竖直，0.30约等于偏离竖直16.7度。评分中竖直度权重最高。
VERTICAL_EDGE_BAND_RATIO = 0.23
VERTICAL_EDGE_MIN_BAND_PX = 12
VERTICAL_EDGE_MAX_TILT_DX_DY = 0.30
VERTICAL_EDGE_MIN_SEPARATION_RATIO = 0.45
VERTICAL_EDGE_MAX_PAIR_CANDIDATES = 8

# 最近4帧只用于预测下一帧竖边大致横坐标、辅助排除同一窄带内的误线；输出仍是当前
# 帧原始边线，不做坐标平均。连续丢失3帧后清空历史，避免旧目标影响重新捕获。
VERTICAL_EDGE_HISTORY_FRAMES = 4
VERTICAL_EDGE_HISTORY_MAX_MISSES = 2

# 中轴只在固定参考高度计算，并强制绘制为竖线。1.5 px以内保持不动；小变化使用
# alpha=0.30抑制摇摆，大于6 px使用alpha=0.80快速跟随。青色原始边线不滤波。
AXIS_FILTER_DEADBAND_PX = 1.5
AXIS_FILTER_FAST_THRESHOLD_PX = 6.0
AXIS_FILTER_SLOW_ALPHA = 0.30
AXIS_FILTER_FAST_ALPHA = 0.80

# MaixPy Image.find_rects() 已从正式上板流程移除；正式流程只使用
# find_blobs方向引导和灰度边缘四边形精定位，避免该API在板端直接退出。

# 近距离箱面左右出画时，四边形可能仍然拟合成功，但其中心已经不再代表完整
# 箱面中心。只要可见色块或角点贴近整幅图左右边缘，就禁止进入最终角点状态。
HORIZONTAL_BORDER_TOUCH_INCOMPLETE = True
HORIZONTAL_BORDER_MARGIN_PX = 3
HORIZONTAL_BORDER_MARGIN_RATIO = 0.025


# ---------------------------------------------------------------------------
# 可选手动 ROI
# ---------------------------------------------------------------------------

# 格式为 (x, y, width, height)，None 表示不限制。它只在未启用 YOLO 时生效。
# 例如只想检查画面中央区域时可填写：MANUAL_ROI = (120, 60, 400, 360)
MANUAL_ROI = None


# ---------------------------------------------------------------------------
# 传统视觉预处理
# ---------------------------------------------------------------------------

GAUSSIAN_KERNEL_SIZE = 5
CANNY_LOW = 40
CANNY_HIGH = 120
MORPH_KERNEL_SIZE = 5
MORPH_CLOSE_ITERATIONS = 3

# approxPolyDP 的精度占轮廓周长的比例。越大越容易把轮廓简化成四边形，也越可能
# 丢失真实拐角；现场一般优先在 0.015~0.04 之间调整。
APPROX_EPSILON_RATIO = 0.035


# ---------------------------------------------------------------------------
# 四边形筛选
# ---------------------------------------------------------------------------

MIN_QUAD_AREA_PX = 1200
MIN_QUAD_AREA_RATIO = 0.02
MAX_QUAD_AREA_RATIO = 0.98
MIN_ASPECT_RATIO = 0.35
MAX_ASPECT_RATIO = 4.0
MIN_RECTANGULARITY = 0.55
MIN_ORTHOGONALITY = 0.20
MIN_VERTICALITY = 0.35
MIN_EDGE_LENGTH_PX = 18

# YOLO已经限制了语义搜索范围。允许候选贴边，以覆盖近距离箱面以及扩张ROI被
# 图像边界裁切的情况；候选仍需通过其余几何规则。
REJECT_BORDER_TOUCH = False
BORDER_MARGIN_PX = 3


# ---------------------------------------------------------------------------
# 性能配置
# ---------------------------------------------------------------------------

# 传统视觉只处理YOLO扩张ROI的0.4倍图像。若远距离出现NO_QUAD，可回调到0.5。
PROCESS_SCALE = 0.40

# 近距离ROI接近整幅640x640时使用更低比例；箱面边缘本身很长，0.30倍仍有足够
# 像素，同时预计把传统视觉面积计算量降到0.4倍路径的约56%。
PROCESS_SCALE_NEAR = 0.30

# 全分辨率兜底会造成周期性卡顿；YOLO模式先关闭。
ENABLE_FULL_RES_FALLBACK = False
FULL_RES_FALLBACK_AFTER_MISSES = 2
FULL_RES_FALLBACK_INTERVAL = 3

# 新版直接在Maix原生帧上绘图显示，不再执行OpenCV图像回拷。
DISPLAY_INTERVAL_FRAMES = 1
FPS_MEASURE_WINDOW_SECONDS = 0.5
REPORT_STAGE_TIMING = True

# 完整四边形出画后，用最近两次真实中心的速度短时外推。预测中心只写入track字段，
# 不会把 ``valid`` 改为1，也不会伪造可用于正对判断的面积。
PARTIAL_TRACK_ENABLED = True
PARTIAL_TRACK_MAX_FRAMES = 8


# ---------------------------------------------------------------------------
# 箱面颜色阈值独立测试
# ---------------------------------------------------------------------------

# 由230张正样本和40张负样本统计得到的首轮宽松范围。OpenCV LAB的a/b是
# 0~255无符号值；MaixVision直方图若显示-128~127，需要给A/B加128再填写这里。
COLOR_TEST_SPACE = "LAB"             # 可改为 "HSV" 对照
COLOR_TEST_WIDTH = 640
COLOR_TEST_HEIGHT = 640
COLOR_TEST_LAB_LOW = (50, 129, 134)
COLOR_TEST_LAB_HIGH = (125, 137, 148)
COLOR_TEST_HSV_LOW = (10, 50, 50)
COLOR_TEST_HSV_HIGH = (20, 130, 140)
COLOR_TEST_PROCESS_SCALE = 0.50
COLOR_TEST_OPEN_ITERATIONS = 1
COLOR_TEST_CLOSE_ITERATIONS = 2


# ---------------------------------------------------------------------------
# 显示与日志
# ---------------------------------------------------------------------------

# 当前联调需要MaixVision/板载屏显示相机调试画面；比赛无屏运行时再改为False。
# 调试显示只能启动一个实例，否则第二个进程会因VO设备已占用而初始化失败。
DISPLAY_ENABLED = True
REPORT_INTERVAL_SECONDS = 0.5

# 只在定时报告且本帧没有有效箱面时，输出被拒四边形的详细几何参数。
# 候选按综合分从高到低排列；限制数量可以避免复杂背景下终端刷屏。
REPORT_REJECTED_CANDIDATES = True
MAX_REJECTED_CANDIDATES_TO_REPORT = 8
