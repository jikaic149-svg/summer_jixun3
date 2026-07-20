# MaixCAM2 视觉与 IMU 调试过程记录

> 只记录关键事实、代码变更、测试方法、实测数据和结论。未经实测的想法必须标记为“假设”，不能直接写成已确认方案。

## 记录规则

- 发生代码或参数修改时，记录修改项、旧值、新值和原因。
- 发生 IMU 测试时，记录板体安装方向、静置时间、采样频率、零偏、噪声和漂移。
- 发生坐标补偿时，记录 IMU 坐标、相机坐标、图像坐标和云台电机坐标之间的映射。
- 未完成轴向、单位、符号和时序验证前，不把 IMU 输出接入云台电流、舵机命令或视觉靶心坐标。
- MaixCAM2 视觉/IMU 内容记录在本文件；STM32 平衡车控制内容继续记录在 `jixun3/process.md` 或 `process_second.md`。

## 2026-07-10 MaixCAM2 板载 IMU 姿态解算基线

### 用户提出的思路（待验证）

- 对 yaw 和 pitch 都做解算。
- yaw 用于 yaw 云台增稳。
- pitch 用于检测平衡车前后震荡，并对画面坐标做一定修正。
- 该思路当前只是方向，不视为已确认控制方案。

### 官方资料确认

- MaixCAM2 板载 IMU 是 `LSM6DSOWTR`，不是 BMI088，也不是 MaixCAM-Pro 使用的 QMI8658。
- MaixCAM2 板载 IMU 包含加速度计和陀螺仪，没有磁力计。
- 官方硬件范围：加速度计约 `+/-2g ~ +/-16g`，陀螺仪约 `+/-125dps ~ +/-2000dps`。
- MaixPy 使用 `from maix.ext_dev import imu` 和 `imu.IMU("default")` 自动选择板载 IMU。
- `read_all(calib_gryo=True, radian=True)` 可读取校准后的加速度、角速度、温度和空磁力计向量。
- MaixPy 内置 `maix.ahrs.MahonyAHRS`，本项目优先使用官方实现，不自行复制一份未验证的 Mahony 算法。
- 官方坐标说明为右手系：`x` 向右、`y` 向前、`z` 向上；官方 AHRS 输出 `angle.x=pitch`、`angle.y=roll`、`angle.z=yaw`。
- 陀螺仪需要静止校准。`calib_gyro(10000)` 采集约 10 秒并保存零偏，后续使用 `load_calib_gyro()` 加载。

官方参考：

- https://wiki.sipeed.com/maixpy/doc/zh/modules/ahrs.html
- https://wiki.sipeed.com/maixpy/api/maix/ahrs.html
- https://wiki.sipeed.com/maixpy/api/maix/ext_dev/imu.html

### 物理边界

- 无磁力计时，roll/pitch 可由重力长期约束；yaw 没有绝对参考，会随陀螺零偏逐渐漂移。
- yaw 云台高频增稳优先使用 `gyro_z` 角速度，而不是只使用积分后的 yaw 角。
- yaw 绝对角保持仍需要云台电机编码器、外部航向参考或视觉参考；MaixCAM2 的积分 yaw 不能单独替代这些反馈。
- MaixCAM2 板载 IMU 测量的是 MaixCAM2 板体姿态。如果 MaixCAM2 固定在底盘，而 USB 相机安装在独立 yaw/pitch 云台上，它不能直接测出相机相对底盘的云台角，只能提供底盘运动前馈。
- pitch 画面补偿只有在 IMU 与相机光轴的安装变换、pitch 舵机角度和图像焦距已知时才有确定几何意义。

### 第一阶段决定

- 只建立独立 IMU 采样和 Mahony 姿态诊断链路。
- 输出 `pitch_deg`、`roll_deg`、相对 `yaw_deg`、`pitch_rate_dps`、`yaw_rate_dps`、`dt`、温度。
- 姿态量只显示和记录，不写入 yaw 云台控制量，不修改 YOLO 靶心和环数计算。
- 第一阶段随视觉主循环更新，实际解算频率等于视觉循环频率；这足够验证轴向和漂移，但不作为最终高频云台内环。
- 若实测证明需要高频增稳，再把 IMU 采样拆到独立高频线程，并通过 UART 向 ESP32-S3 发送角速度/姿态。

### 第一阶段验证步骤

1. 将设备静置，完成 10 秒陀螺零偏校准。
2. 静置 60 秒，记录三轴角速度均值、标准差、pitch 波动和 yaw 漂移。
3. 单独绕板体 x/y/z 轴正向旋转，确认 pitch/roll/yaw 及三轴角速度的对应关系和符号。
4. 记录开启 YOLO 后的 IMU 更新 `dt`，确认视觉推理是否把解算频率压得过低。
5. 手动让车体前后摆动，比较 pitch 与实际前后倾方向；确认相机画面上下移动与 pitch 符号的关系。
6. 手动转动车体和 yaw 云台，区分“底盘 yaw”“云台相对角”和“相机视线 yaw”，确认板载 IMU 实际能观测哪一项。

### 进入控制/补偿前的通过条件

- 三轴映射和正负号经过单轴实验确认并记录。
- 静置零偏校准能正常保存和再次加载。
- 视觉运行期间 IMU 的实际更新率和最大 `dt` 已测得，不再依赖假定频率。
- yaw 增稳明确采用角速度前馈、角速度闭环或角度闭环中的哪一种，并确定 ESP32-S3/云台编码器的反馈边界。
- pitch 像素补偿完成相机内参或至少 `pixels_per_degree` 实测标定，并设置限幅；不得直接凭符号猜测写入 `cy`。

### 当前未解决项

- MaixCAM2 板体与 USB 相机、yaw 轴、pitch 轴的实际安装方向尚未记录。
- 当前没有 MaixCAM2 到 ESP32-S3 的 IMU 数据协议。
- 当前没有磁力计，yaw 长期漂移无法只靠六轴 IMU 消除。
- 尚无实测 IMU 日志，因此不确定板载 IMU 是否满足最终云台增稳带宽。

## 2026-07-10 第一阶段 IMU 诊断代码接入

### 新增文件

- `maixcam2_imu.py`：封装板载 IMU 初始化、陀螺零偏加载/校准、官方 Mahony AHRS、yaw 解包和诊断输出。

### 参数与单位

| 参数 | 当前值 | 说明 |
|---|---:|---|
| `IMU_MAHONY_KP` | `2.0` | 官方示例初值，实测前不调 |
| `IMU_MAHONY_KI` | `0.01` | 官方示例初值，实测前不调 |
| `IMU_GYRO_CALIBRATION_MS` | `10000` | 首次静止校准时长 |
| `IMU_FORCE_GYRO_CALIBRATION` | `False` | 有持久化零偏时直接加载 |
| `max_dt_sec` | `0.25s` | 视觉循环长时间阻塞时丢弃本次积分并重置 AHRS |
| `IMU_PITCH_BASELINE_TAU_SEC` | `1.5s` | pitch 慢基线诊断初值，尚未按实车频率整定 |

- 本实现统一使用 `read_all(calib_gryo=True, radian=False)`，陀螺单位为 `degree/s`。
- AHRS 同步使用 `get_angle(..., radian=False)`，避免输入单位标志不一致。
- `yaw_relative_deg` 从本次 AHRS 启动时归零，并对 `+/-180deg` 跳变做连续解包；它仍会长期漂移。
- `pitch_delta_deg` 从第一帧有效姿态归零，只作为诊断量，不代表已完成机械零位或光轴标定。

### 主程序修改

- `main.py` 在调参界面按下 START 后初始化 IMU；首次无零偏文件时会要求设备静止并阻塞校准约 10 秒。
- 每帧在 YOLO 推理前读取一次 IMU，解算频率因此等于视觉主循环频率。
- 画面只读显示：
  - `P`：融合 pitch 角。
  - `Pshake`：融合 pitch 减去 1.5 秒慢基线后的前后震荡分量。
  - `Yrel`：相对 yaw 积分角。
  - `Pdot`：x 轴陀螺角速度，暂按官方坐标视为 pitch rate。
  - `Ydot`：z 轴陀螺角速度，暂按官方坐标视为 yaw rate。
  - `dt`：两次实际 AHRS 更新间隔。

### 明确未接入

- 未修改 YOLO 检测框、红心中心、环数估计或屏幕中心。
- 未计算 pitch 像素补偿量。
- 未向 ESP32-S3 发送姿态或角速度。
- 未把 `yaw_relative_deg` 或 `yaw_rate_dps` 接入 yaw 云台控制。

### 首次上机记录要求

运行 MaixVision 项目后，按以下顺序把结果补到本文件：

1. 终端是否显示 `IMU gyro calibration loaded` 或完成 10 秒校准。
2. 静置时 `Pdot/Ydot` 的典型波动范围。
3. 静置 60 秒前后 `Yrel` 的变化量。
4. 前倾车体时 `P`、`dP`、`Pdot` 的正负号。
5. 车体绕竖直轴左转时 `Yrel`、`Ydot` 的正负号。
6. 正常 YOLO 运行时 `dt` 的平均值和最大值。

在以上数据拿到前，不进入 yaw 控制和 pitch 画面补偿阶段。

## 2026-07-10 机械约束确认：只有 yaw 电机，pitch 只做电子消抖

### 已确认硬件边界

- 当前机构只有 yaw 电机轴，没有 pitch 舵机或 pitch 电机。
- yaw 方向后续可以通过电机执行增稳控制。
- pitch 方向无法机械调节，只能通过图像坐标补偿或画面裁剪/变换做电子消抖。
- 在没有 pitch 执行器的情况下，pitch 解算仍有价值，但它是扰动观测量，不是 pitch 闭环反馈量。

### yaw 路径

- 最终增稳优先使用 `yaw_rate_dps` 抑制车体快速转动，积分 `yaw_relative_deg` 只提供短期相对角参考。
- 仅靠 MaixCAM2 六轴 IMU 不能长期保持绝对 yaw；最终仍需 yaw 电机编码器闭环，并明确 MaixCAM2 数据是前馈还是外环参考。
- 当前视觉主循环内的 IMU 更新频率只用于验证，不足以直接作为最终高带宽 yaw 速度环。

### pitch 电子消抖分层

1. 第一优先：只补偿目标/激光控制坐标的 `cy`，保持 YOLO 在原始帧上运行。这样计算量小，也不会因图像重采样降低检测精度。
2. 第二优先：若确实需要人眼看到稳定画面，再对显示帧做垂直裁剪/平移或透视变换。该路径会损失边缘视场并增加计算量，不应先于坐标补偿实现。
3. 不直接用“相对开机 pitch”长期补偿。平衡车可能存在慢速倾角、坡度或安装偏置，应先提取高于慢基线的震荡分量 `pitch_shake`。

### 第一版震荡量定义（待数据验证）

- 用一阶慢速低通得到 `pitch_baseline`，初始时间常数暂取 `1.5s`。
- `pitch_shake = pitch - pitch_baseline`。
- 该参数只是诊断初值：时间常数必须结合平衡车实际震荡频率调整，不能直接视为最终滤波器。
- 后续小角度像素补偿可从 `dy ~= fy * tan(pitch_shake)` 开始验证；`fy`、符号和限幅必须实测。
- 在相机内参未知时，可用固定角度垫高/压低设备，测量靶心像素位移，先标定 `pixels_per_degree`。

### 当前决定

- 新增并显示 `pitch_shake_deg`，但不修改图像和靶心坐标。
- 保留 `pitch_rate_dps`，用于判断是否需要角速度前馈或相位超前。
- 拿到 `pitch_shake` 与靶心 `cy` 的同步数据后，再决定补偿公式、符号、滤波时间常数和像素限幅。

## 2026-07-10 MaixCAM2 全硬件 6DoF 电子防抖能力调研

### 调研问题

- 确认 MaixCAM2 是否能把板载六轴 IMU 和 AX630C 图像处理硬件直接组成“全硬件 6DoF 电子防抖”。
- 区分“六轴 IMU”“三轴旋转姿态”和“六自由度画面运动补偿”，避免把三个概念等同。

### 官方资料确认

- Sipeed MaixCAM2 硬件页确认主控为 `AX630C`，板载六轴 IMU 为三轴加速度计加三轴陀螺仪。
- Sipeed MaixPy AHRS 文档确认 MaixCAM2 的器件为 `LSM6DSOWTR`，没有磁力计；公开使用方式是通过 `IIC1` 读取 IMU，再由软件调用 `MahonyAHRS` 解算姿态。
- 爱芯元智 AX630C 官方产品页和产品规格书在 ISP 能力中明确列出“支持 DIS，EIS”。因此 AX630C 芯片内部确实存在数字/电子防抖相关硬件能力，不能简单写成“AX630C 不支持 EIS”。
- 爱芯元智公开产品页只写 `DIS/EIS`，没有写明 `6DoF EIS`，也没有公开说明 MaixCAM2 板载 LSM6DSOWTR 可直接接入该硬件管线。
- 检索 2026-07-10 的 MaixPy 官方文档、API 和 `sipeed/MaixPy` 主分支，没有找到向 Python 应用开放的 `EIS`、`DIS`、IMU 到 EIS 同步、硬件网格变换或相机帧时间戳绑定接口/示例。
- Sipeed ISP 调校页只说明可使用爱芯原厂 ISP 工具修改 ISP 配置，没有提供 EIS 开启方法或 MaixPy 接口。

官方参考：

- https://wiki.sipeed.com/hardware/zh/maixcam/maixcam2.html
- https://wiki.sipeed.com/maixpy/doc/zh/modules/ahrs.html
- https://wiki.sipeed.com/maixpy/api/maix/ext_dev/imu.html
- https://wiki.sipeed.com/hardware/zh/maixcam/maixcam2_isp.html
- https://axera-tech.com/zh-hans/product/2931.html
- https://axera-tech.com/sites/default/files/2026-06/20260616170738_911.pdf
- https://dl.sipeed.com/shareURL/MaixCAM/MaixCAM2/Hardware/board_maixcam2

### 板级连接核对

- 官方 `SCH_MaixCam2_379C.pdf` 第 7 页显示 `LSM6DSOWTR` 的 `SDA/SCL` 接到 AX630C 的 `GPIO0_A27/A26`，即 `I2C1`。
- 原理图中 `INT1/INT2` 只看到 `R259/R260` 的 10k 电阻终端，没有看到连接到 AX630C GPIO、相机帧同步或传感器同步网络的网络名。
- 因此当前公开原理图能确认的是“AX630C 通过 I2C 读取 IMU”，不能确认存在相机曝光时刻与 IMU 样本的硬件同步链路。
- 没有精确帧时间戳、曝光行时序和 IMU 时间戳对齐时，即使能做整帧旋转/平移，也无法把滚动快门逐行畸变修正称为完整的全硬件 EIS。

### 6 轴不等于 6DoF 电子防抖

- 六轴 IMU 的“六轴”是三轴加速度和三轴角速度，不代表能够直接得到无漂移的三轴位置和三轴姿态。
- 陀螺仪可较可靠地提供短时间的 roll/pitch/yaw 旋转增量；加速度二次积分得到平移会快速累积零偏和重力分离误差。
- 三轴旋转造成的远景画面运动可近似用单应/网格变换加裁剪补偿，通常是 IMU-EIS 的主要能力。
- 三轴平移会产生与物体深度相关的视差。单目相机和六轴 IMU 没有场景深度时，无法用一个全局二维变换同时稳定近景和远景，所以不能仅凭板载六轴 IMU 实现严格意义的完整 6DoF 画面补偿。

### 当前结论

- **芯片能力：部分支持。** AX630C 官方规格明确支持 `DIS/EIS`。
- **MaixCAM2 开箱即用能力：尚不支持/至少未公开。** 当前 Sipeed 固件、MaixPy API 和示例没有公开将板载 IMU 接入 AX630C EIS 的应用接口。
- **“全硬件 6DoF EIS”：不能确认，且按现有公开硬件和接口不能实现。** 现有证据只支持“六轴 IMU + 具备 EIS 能力的 SoC”，不支持“二者已完成硬件同步并提供完整 6DoF 防抖管线”。
- 本结论不排除爱芯闭源 SDK 内部存在可用的 EIS 模块；若要调用，必须向 Sipeed/爱芯确认 AX630C SDK 的 EIS API、授权、输入数据格式、帧/IMU 时间同步要求，以及 MaixCAM2 OS 是否包含对应运行库。

### 对当前项目的决定

- 不在 `main.py` 中假设存在未公开的硬件 EIS 开关，也不写未经验证的底层 AX630C 调用。
- yaw 仍采用“yaw 电机机械增稳 + 编码器闭环 + IMU 角速度前馈”的路线。
- pitch 无执行轴，先做目标 `cy` 的小范围电子补偿；需要人眼画面稳定时，再评估带安全裁剪边界的旋转/垂直网格变换。
- 当前方案应称为“IMU 辅助的 pitch 电子消抖/坐标补偿”，不能称为“全硬件 6DoF EIS”。
- 若后续拿到爱芯 EIS SDK，先做独立最小验证程序，测量相机帧时间戳、IMU 采样时间戳、端到端延迟、滚动快门残差和 YOLO 输入是否使用防抖前/后的图像，再决定是否接入主程序。

## 2026-07-10 自研实时电子防抖总体思路

### 目标边界

- 当前只实现 IMU 辅助的三轴旋转防抖，不把三轴平移称为已经解决的 6DoF 防抖。
- yaw 由电机承担大角度、低中频机械增稳；电子防抖只处理电机剩余的小幅高频残差。
- pitch 没有执行轴，只能靠图像裁剪/重采样消抖，补偿幅度受预留边缘视场限制。
- 电子防抖不能消除运动模糊。必须同时控制曝光时间，否则画面位置稳定后目标仍会拖影。

### 必须分开的两条输出链路

1. **识别与物理控制链路**：原始图像 -> YOLO -> 红心 -> 原始靶心坐标 -> 环数/激光关系 -> 控制器。
2. **人眼显示链路**：原始图像 -> IMU 姿态匹配 -> 裁剪/单应变换 -> 稳定显示图像。

当前 `estimate_ring_score()` 把原始图像中心当作激光命中点。因此第一版不得把电子防抖后的 `cy` 直接送进环数估计，否则会把“稳定坐标中的靶心”和“原始坐标中的激光点”混在一起。后续若确实要稳定控制坐标，必须同时维护：

- `raw_target_x/raw_target_y`：原始帧坐标，用于原始图像、激光点和环数关系。
- `stable_target_x/stable_target_y`：虚拟稳定相机坐标，只用于稳定显示或明确设计过的控制外环。
- `H_raw_to_stable`：本帧原始坐标到稳定坐标的变换；检测框、靶心、屏幕准星和所有叠加元素必须用同一变换后才能在稳定画面中比较。

### 实时处理时序

```text
高频 IMU 线程（建议先验证 200~500Hz）
    读取 gyro/acc -> 单调时钟时间戳 -> 姿态解算 -> 环形缓冲区

相机/视觉线程（当前 YOLO 帧率）
    cam.read() -> 确定该帧曝光时刻 -> 从 IMU 缓冲区插值对应姿态
               -> 原始帧运行 YOLO/红心/环数
               -> 计算该帧防抖变换
               -> 只对显示副本做裁剪/仿射/透视变换
               -> 显示并记录耗时
```

- 当前代码每次 `cam.read()` 后只读一次 IMU，IMU 频率等于 YOLO 帧率，不能作为最终实时防抖采样链路。
- 第一版可用 Python 独立线程轮询 IMU 并保存带 `ticks_us` 时间戳的样本；若抖动、丢样或占用过高，再下沉到 MaixCDK/C++，并使用 LSM6DSOWTR FIFO。
- 对显示帧必须使用该帧曝光时刻的历史姿态，不能直接使用 YOLO 推理完成后的“最新姿态”。后者比图像内容晚一个采集和推理延迟，会形成反向拖尾或越补越抖。

### 标定项

- 陀螺仪零偏、噪声和温漂。
- IMU 坐标到相机坐标的固定旋转 `R_cam_imu`，至少先标定 pitch/yaw/roll 的轴向和正负号。
- 相机内参 `fx/fy/cx/cy` 和畸变；简化版本至少要测 `pitch_pixels_per_degree`。
- 相机帧时间戳相对 IMU 时间戳的固定偏移 `time_offset`。
- 若要处理滚动快门，还需要一帧从第一行到最后一行的读出时间 `readout_time`；当前公开 MaixPy 接口尚未提供这些精确时序。

### 第一版：只做 pitch 垂直裁剪消抖

- 使用当前已有的慢基线：`pitch_shake = pitch - pitch_baseline`。
- 更合理的稳定参考不是永久固定开机角，而是缓慢跟随的虚拟相机角度；这样车辆上坡、下坡或长期倾斜时不会一直顶在裁剪边界。
- 小角度下先使用：

```text
dy = sign * fy * tan(radians(pitch_shake))
```

- 如果还没有 `fy`，先用实测的 `pitch_pixels_per_degree`：

```text
dy = sign * pitch_pixels_per_degree * pitch_shake_deg
```

- `sign` 必须通过实机前倾/后倾试验确定，不能凭坐标名称猜测。
- 预留上、下裁剪边界 `margin_y`，并限制 `dy` 在 `[-margin_y, +margin_y]`。理论最大可补偿角约为：

```text
max_pitch_deg = degrees(atan(margin_y / fy))
```

- 简化显示实现可从动态裁剪开始：从原图取 `[margin_x, margin_y + dy, width - 2*margin_x, height - 2*margin_y]`，再缩放到显示尺寸。这样会损失部分视场，但不会出现黑边。
- MaixPy 已公开 `crop/resize/affine/perspective`；`rotation_corr` 文档仍标记为 `TODO: support in the future`，当前不能依赖它。

### 第二版：三轴旋转单应防抖

- 由相机内参矩阵 `K`、当前相机姿态和缓慢跟随的目标姿态计算旋转补偿矩阵 `R_corr`。
- 原始像素到稳定像素的理想旋转模型为：

```text
H_raw_to_stable = K * R_corr * inverse(K)
```

- 用 `H` 映射四个角点，再调用 `Image.perspective()` 生成稳定显示图像。
- roll 表现为画面旋转，pitch/yaw 表现为透视和平移；单应模型适合以相机旋转为主、目标距离较远的场景。
- 平衡车前后平移会引入与距离有关的视差，近景和远景不能由同一个 `H` 同时完全稳定。第一版不处理该部分。

### 裁剪边界与参考姿态回中

- 固定参考姿态会在车辆持续转弯、上下坡时很快耗尽裁剪余量。
- 目标姿态应低通跟随真实姿态：高频抖动被 `R_corr` 消除，低频运动允许画面缓慢跟随。
- 当补偿接近裁剪边界时，应提高参考姿态跟随速度，使画面平滑回中；不能只做硬限幅，否则会在边界突然停住。
- 初始参数只能作为待测值：pitch 参考时间常数可从当前 `1.5s` 开始，裁剪边界先取图像高度的 `10%~15%`，然后根据实测震荡频率和视场损失调整。

### 延迟处理

- 显示防抖：用“帧曝光时刻”的姿态变换该历史帧，重点是正确回查/插值 IMU，而不是用当前姿态预测旧画面。
- 控制输出：若稳定坐标用于 yaw 控制，还要从曝光时刻预测到电机实际执行时刻，可先用 `angle_predict = angle_frame + gyro_rate * control_latency`，并对预测量限幅。
- 必须分别测量 `cam.read`、IMU 匹配、YOLO、图像变换和 `disp.show` 的耗时；总延迟不能只用循环 `dt` 代替。

### 滚动快门边界

- 整帧只使用一个 `H` 只能修正全局旋转，无法完全修正车辆震荡时产生的果冻形变。
- 严格滚动快门修正需要按图像行时间查询不同姿态并做网格变换。MaixPy 当前没有公开硬件 EIS/逐行网格接口，Python 中逐行处理也可能无法满足实时性。
- 因此先通过短曝光降低果冻效应；只有整帧防抖验证成功且滚动快门残差仍明显时，再调查 AX630C GDC/EIS 或在 MaixCDK 中实现网格变换。

### 推荐实施顺序

1. 记录静态噪声、实际 IMU 采样率、YOLO 帧率和各阶段耗时。
2. 将 IMU 采样拆为高频线程，建立带时间戳的姿态环形缓冲区。
3. 只输出 `dy` 数值和裁剪余量，不改图像，验证 pitch 符号、幅度和时间对齐。
4. 对显示副本实现 pitch 动态裁剪/缩放，YOLO、红心、环数保持原始帧。
5. 用固定墙面和重复前后摆动测稳定前后特征点 RMS 位移，比较防抖收益、FPS 和延迟。
6. pitch 版本稳定后，再加入 roll/yaw 单应变换；yaw 电子防抖只清理电机剩余残差。
7. 最后才评估控制坐标补偿和滚动快门网格修正。

### 验收指标

- 静置时防抖输出不能因 IMU 噪声造成可见游动。
- 同一组人工前后摆动中，稳定画面的特征点 RMS 位移应明显小于原始画面，并记录改善比例。
- 不出现黑边；达到裁剪限幅时画面平滑回中，不发生跳变。
- 开启显示防抖后 YOLO 检测率、红心定位和环数结果与原始链路一致。
- 记录平均/最小 FPS、IMU 实际采样率、最大样本间隔、每阶段耗时和内存增长。
- 暂定通过条件不是“看起来更稳”，而是残余位移、延迟和丢帧都有可重复测量结果。

### 本次决定

- 本次只完成方案设计和记录，不直接修改运行代码。
- 下一次代码实现从“高频 IMU 时间戳缓冲 + 只计算/显示 `dy`”开始，不直接上整帧透视变换。

## 2026-07-10 实时电子防抖对帧率的影响评估

### 当前结论

- 只进行 IMU 读取、姿态插值和 `dy` 计算，计算量很小，通常不会成为 YOLO 主循环的主要瓶颈。
- 真正影响帧率的是对整张图执行 `crop + resize`、`affine` 或 `perspective`，因为它们需要读取、插值并写入整帧像素。
- MaixPy 文档明确说明 `crop()`、`resize()`、`affine()`、`perspective()` 都会创建新的 `Image`；每帧临时分配和复制还会增加内存带宽、内存抖动和延迟。
- 当前程序使用 `dual_buff=False`，并按 `cam.read -> IMU -> YOLO -> 绘制 -> disp.show` 串行执行。若在 `disp.show` 前增加防抖变换，其耗时会直接加到单帧总耗时中。
- 在 MaixCAM2 实机测量前不能给出可靠的固定降帧百分比；分辨率、图像格式、插值方式和 MaixPy 底层是否使用硬件加速都会改变结果。

### 计算方法

```text
fps_before = 1000 / frame_time_before_ms
fps_after  = 1000 / (frame_time_before_ms + eis_time_ms)
```

示例：原程序为 `20 FPS`，即每帧约 `50ms`：

- 防抖增加 `3ms`，理论约为 `18.9 FPS`。
- 防抖增加 `5ms`，理论约为 `18.2 FPS`。
- 防抖增加 `10ms`，理论约为 `16.7 FPS`。

以上只是时间预算示例，不是 MaixCAM2 实测结果。

### 降低影响的方法

- YOLO、红心和环数继续使用原始模型输入图，防抖只处理显示图，不让重采样降低检测精度。
- 第一版先测一次 `crop + resize` 和一次 `perspective` 的实际耗时，选择更快的单次变换；避免一帧连续生成多个中间图像。
- 先用最近邻插值测性能和时序，再比较双线性插值的画质与耗时。
- 只为显示防抖时，可降低显示防抖分辨率或隔帧更新稳定画面，识别仍保持原帧率。
- 后续 C++ 实现应复用输出缓冲区，避免 Python 每帧创建和销毁多个大图对象。
- 如果底层 EIS/GDC 可以调用，再用硬件网格变换替代 Python 图像重采样。

### 必测数据

- `cam.read()`、IMU 姿态匹配、`detector.detect()`、红心/环数、EIS 变换和 `disp.show()` 的独立耗时。
- 开启/关闭 EIS 时的平均 FPS、最低 FPS、最大帧间隔和端到端显示延迟。
- 连续运行至少 5 分钟后的内存占用，确认每帧新建图像没有导致持续增长。

## 2026-07-10 是否需要裁剪画面的决定

### 结论

- 只做靶心/控制坐标补偿时不需要裁剪，因为没有生成新的显示画面。
- 做无黑边的整幅显示防抖时需要裁剪或使用更高分辨率传感器画面作为超扫区域。图像被反向平移/旋转后，边缘会产生原始帧中不存在的空白像素；不裁剪只能接受黑边、历史帧填充或图像生成。
- 对当前实时嵌入式项目，历史帧拼接在动态场景中会产生重影，图像生成不适合低延迟控制，因此选择“预留视场 + 动态裁剪/缩放”。

### 当前项目策略

- 第一版只补偿 pitch，因此只需要重点预留上、下边缘，不必一开始就大幅裁掉左右视场。
- yaw 已有机械电机，后续若只补偿 yaw 电机剩余误差，再增加少量左右余量。
- YOLO、红心和环数仍使用未裁剪原图；裁剪只作用于显示副本，避免减少模型看到的目标范围。
- 如果后续相机能以高于显示/模型输入的分辨率采集，优先用高分辨率图像作为超扫区域，再输出较小的稳定窗口。这本质上仍会损失视场，但不会降低最终输出像素数。

### 裁剪量计算

- 不固定使用一个未经测量的百分比，先测车辆实际最大高频 `pitch_shake_max`。
- 垂直安全边缘按下式估算：

```text
margin_y >= ceil(fy * tan(radians(pitch_shake_max))) + safety_pixels
```

- 没有相机内参时使用实测比例：

```text
margin_y >= ceil(pitch_pixels_per_degree * pitch_shake_max) + safety_pixels
```

- 可先用高度的 `10%` 作为性能和观感测试初值，但最终值必须由实测震荡幅度决定。
- 补偿量接近 `margin_y` 时让虚拟参考姿态加快跟随并平滑回中；不允许继续移动产生黑边，也不应在边界突然硬切。

### 代价

- 裁剪越多，可消除的最大 pitch 震荡越大，但视场越窄，近距离目标越容易离开画面。
- 裁剪后放大到原显示尺寸会降低一些细节清晰度；使用更高分辨率超扫源可以缓解这一问题。
- 对本项目而言，应优先保证原始识别视场和控制正确，再优化稳定显示观感。

## 2026-07-10 固定裁剪与动态裁剪窗口的区别

### 结论

- 固定裁剪尺寸是推荐方案：输出窗口宽高在运行期间保持不变，画面放大倍率不会随抖动变化，可避免呼吸感和视场忽大忽小。
- 裁剪窗口位置必须根据 IMU 测得的抖动反向移动；如果尺寸和位置都固定在图像中心，只会永久损失视场，不会产生任何防抖效果。
- 因此正确描述是“固定大小的稳定窗口，在更大的原始图像内动态移动”，而不是每帧动态改变裁剪大小。

### pitch 第一版计算

设原始图像高度为 `H`，上下各预留 `M` 像素：

```text
crop_h = H - 2 * M
shift_y = clamp(sign * pitch_compensation_pixels, -M, +M)
crop_y = M + shift_y
stable_img = raw_img.crop(0, crop_y, W, crop_h).resize(W, H)
```

- `crop_h` 始终不变，只有 `crop_y` 随 pitch 抖动变化。
- `sign` 通过实机前倾/后倾试验确定。
- 静止时 `shift_y` 应接近 0，窗口位于中央。
- 达到 `+/-M` 后不能继续移动，应让虚拟参考姿态平滑跟随并回中。
- 该变换只用于显示副本；YOLO、红心、激光点和环数仍然使用完整原始帧。

### 不采用的方案

- 固定裁掉一圈后始终显示同一个中心区域：没有防抖作用。
- 根据瞬时抖动不断改变裁剪宽高或 zoom：会产生明显的画面缩放呼吸。
- 把裁剪后的显示坐标直接与原始激光中心比较：坐标系不一致。

## 2026-07-10 对点防抖还是对画面防抖

### 术语区分

- 对整幅图像做反向裁剪、平移、旋转或透视变换，才是电子画面防抖。
- 只对靶心坐标做低通、alpha-beta 或保持上一帧，是目标点滤波；它能让控制量平滑，但不会让背景和画面稳定。
- 把 IMU 姿态用于把原始目标点转换到虚拟稳定相机坐标，是坐标补偿；它必须与画面使用完全相同的变换。

### 当前项目决定

- YOLO、红心、环数和激光命中关系全部保留在原始坐标系，继续使用 `raw_target` 和原始图像中心。
- 显示防抖计算本帧 `H_raw_to_stable` 或 pitch 裁剪窗口，对显示图像做变换。
- 显示在稳定画面上的靶心、检测框和准星必须通过同一个 `H_raw_to_stable` 映射，不能只移动画面而把标记留在原坐标。
- yaw 电机控制第一版使用原始水平视线误差、现有目标滤波和 IMU yaw 角速度前馈，不直接使用稳定显示坐标。
- pitch 没有执行器，稳定后的 `stable_target_y` 只用于显示/观测，不得被描述为已经修正了真实激光俯仰误差。

### 数据关系

```text
raw_img -----------------------> YOLO/红心/环数/原始控制误差
   |
   +-- H_raw_to_stable --------> stable_img（显示）

raw_target -- H_raw_to_stable -> stable_target（稳定画面叠加标记）
```

- 对 pitch 动态裁剪，点的垂直映射必须包含裁剪偏移和缩放：

```text
stable_y = (raw_y - crop_y) * output_h / crop_h
```

- 检测框至少要映射左上角和右下角；三轴透视变换时应映射四个角后重新求显示包围框。

### 优先级

1. 比赛控制正确性优先：先保持原始点、原始激光中心和环数坐标一致。
2. 需要人眼看到稳定画面时，再变换显示图像和全部显示标记。
3. 只有明确建立稳定坐标控制模型后，才允许把 `stable_target` 接入控制器；不得在第一版中替换 `raw_target`。

## 2026-07-10 靶纸角点与对角线交点检测接入

### 需求

- 在 YOLO 检出的靶纸区域内寻找四个外部角点。
- 连接四边形两条对角线，并计算解析交点作为靶纸几何中心候选。
- 角点和交点先用于显示、标定和与红心中心比较，不立即替换控制中心。

### 实现

- `main.py` 新增 `ENABLE_CORNER_GEOMETRY` 及角点检测参数。
- 在任何检测框、文字或辅助线写入图像前，对 YOLO 靶纸框扩展 `6%` 得到角点搜索 ROI。
- 使用 MaixPy 官方 `Image.find_rects(roi=..., threshold=...)`，当前初始阈值为 `2000`。
- 将候选四角按 `TL/TR/BR/BL` 排序，使用鞋带公式计算四边形面积。
- 候选四边形面积必须不小于 YOLO 框面积的 `20%`，对角交点相对 YOLO 框中心的偏移不得超过框对角线的 `45%`。
- 多个候选同时存在时，优先选择面积更大、中心更接近 YOLO 中心且 `magnitude` 更高的候选。
- 两条对角线使用直线解析式求交，分母接近零时安全拒绝，避免除零。
- 青色绘制四边形边界和四个角点，洋红色绘制两条对角线，白色十字绘制对角交点。
- 若本帧同时找到红心，显示 `d`：对角交点到红心中心的像素距离。

### 当前数据边界

- `quad_geometry` 只传给 `draw_target_quad_geometry()`。
- `CenterTracker.update()` 仍只接收 `red_center`。
- `estimate_ring_score()` 仍使用原始红心/滤波中心和原始图像中心。
- 因此本次接入不会让角点误检直接改变瞄准点、环数或后续控制命令。
- 后续显示画面启用防抖时，四角、两条对角线和交点必须与图像一起通过 `H_raw_to_stable` 映射。

### 参数初值（待实机调节）

| 参数 | 初值 | 作用 |
|---|---:|---|
| `CORNER_RECT_THRESHOLD` | `2000` | `find_rects` 矩形响应阈值 |
| `CORNER_ROI_EXPAND_RATIO` | `0.06` | 避免 YOLO 框过紧截断靶纸边缘 |
| `CORNER_MIN_QUAD_AREA_RATIO` | `0.20` | 排除靶纸内部小矩形和噪声 |
| `CORNER_MAX_CENTER_OFFSET_RATIO` | `0.45` | 排除中心明显偏离靶纸的候选 |

这些参数不是实测最终值。若外框检测不到，先降低 `CORNER_RECT_THRESHOLD`；若内部图案误当外框，先提高面积比例并检查 ROI，而不是直接让交点替换红心。

### 静态验证

- `python -m py_compile main.py maixcam2_imu.py` 通过。
- 纯几何测试通过：无序矩形四角正确排序，`80x80` 矩形对角交点为 `(60, 60)`。
- 透视四边形对角线得到有限交点；平行直线返回 `None`。
- 静态调用检查确认 `quad_geometry` 只进入绘制路径。
- 尚未在 MaixCAM2 实机图像上运行 `find_rects`，因此检测率、阈值和新增耗时仍需上机测量。

### 首次上机记录

1. 正视、左右斜视、上下倾斜和部分遮挡时是否稳定得到四个外角。
2. 外框漏检率、内部图案误检率和 `magnitude/area_ratio` 典型范围。
3. 红心中心与对角交点距离 `d` 的平均值、最大值及随视角的变化。
4. 开启/关闭 `ENABLE_CORNER_GEOMETRY` 时的 FPS 和角点检测单独耗时。
5. 只有当 `d` 和误检数据证明几何中心可靠后，再决定它是红心丢失时的回退中心还是仅保留诊断。

## 2026-07-10 当前代码结构快照

本节描述 2026-07-10 当前磁盘中的实际代码，不包含前文尚未实现的规划。后续结构发生变化时应新增快照或明确更新本节。

### 项目文件

| 文件 | 当前职责 |
|---|---|
| `main.py` | 模型加载、相机/屏幕、触摸调参、YOLO、红心、目标滤波、环数、四角/对角交点、IMU 信息显示和主循环 |
| `maixcam2_imu.py` | MaixCAM2 板载 LSM6DSOWTR 初始化、陀螺校准、Mahony AHRS、相对 yaw、pitch 慢基线和姿态样本 |
| `2026LaserTrack_yolo11n_cam2.mud` | MaixCAM2 YOLO11 模型描述，类型为 `axmodel`，标签为 `target_paper` |
| `2026LaserTrack_yolo11n_cam2_npu.axmodel` | NPU 模型文件 |
| `2026LaserTrack_yolo11n_cam2_vnpu.axmodel` | VNPU 模型文件 |
| `process.md` | 需求、调查、实现、参数、验证和未解决问题的持续记录 |

### `main.py` 顶层依赖与配置

- MaixPy：`app`、`camera`、`display`、`image`、`nn`、`time`、`touchscreen`。
- Python 标准模块：`configparser`、`math`、`os`。
- 项目模块：`MaixCam2Attitude`。
- 模型配置：`MODEL_FILENAME` 和设备候选目录 `MODEL_SEARCH_ROOTS`。
- YOLO 配置：`CONF_TH=0.77`、`IOU_TH=0.45`。
- 红心配置：LAB 阈值、搜索 ROI 比例、最小像素/面积和短时保持帧数。
- IMU 配置：使能、校准时长、Mahony `kp/ki` 和 pitch 慢基线时间常数。
- 角点配置：`find_rects` 阈值、ROI 扩展、最小面积比例和最大中心偏差。
- 环数配置：径向扫描、灰环判定、射线采样、环宽范围和环宽滤波参数。
- 颜色配置：目标、红心、激光、IMU 面板、四角和对角线的显示颜色。

### 目标状态与滤波类

- `LowPassFilter`：一阶低通；当前 `update_filter()` 实际创建它并使用 `alpha=0.75`。
- `AlphaBetaFilter`：位置/速度预测滤波实现已保留，但当前未启用。
- `CenterTracker`：
  - 找到红心时输出红心并更新历史。
  - 红心短时丢失不超过 `HOLD_MAX_MISSES=4` 帧时输出上一可靠红心，来源标记为 `hold`。
  - 超过保持帧数后输出 `reject`，不会退回 YOLO 外接框中心。
- `filter_state={"filter": None}`：保存当前目标低通滤波器；目标无效或丢失时重置。
- `RING_WIDTH_STATE`：保存环宽滤波值和连续异常次数；目标无效或丢失时重置。

### 通用几何与角点模块

- `clamp()`：数值限幅。
- `polygon_area()`：鞋带公式计算四边形面积。
- `order_quad_corners()`：把任意顺序四角规范为 `TL/TR/BR/BL`。
- `line_intersection()`：解析计算两条无限直线交点，近平行时返回 `None`。
- `make_expanded_target_roi()`：对 YOLO 框扩展并限制到图像边界。
- `find_target_quad_geometry()`：
  - 在扩展后的 YOLO ROI 中调用 `img.find_rects()`。
  - 按面积比例、对角交点偏移和 `magnitude` 筛选外部四边形。
  - 返回 `corners/center/area_ratio/magnitude/roi` 字典。
- `draw_target_quad_geometry()`：绘制青色四边形、洋红色对角线、白色交点，以及交点到红心的距离 `d`。
- 当前角点模块是纯诊断旁路，几何交点不进入 `CenterTracker`、环数或控制。

### 模型发现与校验模块

- `resolve_model_path()`：先查 `main.py` 同目录，再查 `/root/models`、`/root`、`/root/mymodel`；找不到时提示必须用 MaixVision“运行项目”。
- `validate_maixcam2_model()`：
  - 解析 `.mud`。
  - 要求 `[basic] type=axmodel`。
  - 检查 `model_npu/model_vnpu/model` 引用。
  - 要求引用文件后缀为 `.axmodel` 且实际存在。
- `nn.YOLO11(..., dual_buff=False)`：保证检测框与当前用于红心、角点和环数分析的图像是同一帧。

### 靶纸与红心检测模块

- `box_center()`：计算 YOLO 外接框中心，只用于对比/几何筛选，不作为红心丢失后的控制回退。
- `make_inner_roi()`：按 `RED_SEARCH_RATIO` 在 YOLO 框内生成红心搜索区域。
- `find_red_center_in_box()`：
  - 在内部 ROI 调用 LAB `find_blobs()`。
  - 最小像素和面积会随 YOLO 框面积动态提高。
  - 多个红色色块时选择面积最大的色块。
  - 返回全图坐标中的红心中心、blob 和搜索 ROI。
- `get_best_target()`：多个 YOLO 对象中选择置信度最高者；当前不做跨帧目标 ID 关联。
- `update_filter()`：当前使用 `LowPassFilter(alpha=0.75)` 输出 `filtered_center`。

### 环数估计模块

- `pixel_luma()`、`pixel_rgb_luma_chroma()`：读取像素亮度/色度。
- `percentile_value()`：计算排序样本的百分位数。
- `point_in_blob()`：判断激光点是否落在红心 blob 内。
- `filter_ring_width()`、`reset_ring_width_filter()`：抑制环宽跳变并处理连续异常。
- `estimate_outer_radius_by_radial_scan()`：从靶心沿多个方向扫描，估计靶纸外边界半径。
- `collect_gray_segments_on_ray()`：沿“靶心到屏幕中心”射线收集灰环区段。
- `estimate_ring_width_on_hit_ray()`：根据灰环段中心间距估计单环宽度。
- `is_gray_at_hit()`：判断屏幕中心/激光点当前落在灰环还是白环。
- `estimate_ring_score()`：
  - 把原始图像中心作为激光命中点。
  - 红心命中直接为 10 环。
  - 否则根据目标中心到激光点距离、估计环宽和灰白环修正计算 `1~10` 环或靶外 `0` 环。
- `draw_ring_score()`：绘制屏幕中心、射线、环边界刻度、灰环中心和环数文字。
- 当前环数只显示/调试，没有发送给外部控制器。

### 绘制与诊断模块

- `draw_target()`：绘制 YOLO 框、框中心、红心 blob、本帧测量中心、低通中心，并调用 `draw_ring_score()`。
- `draw_rejected_target()`：红心长期无效时绘制拒绝框，不输出控制中心。
- `draw_imu_attitude()`：显示：
  - `P`：融合 pitch。
  - `Pshake`：pitch 相对慢基线的震荡量。
  - `Yrel`：相对 yaw。
  - `Pdot/Ydot`：pitch/yaw 角速度。
  - `dt`：AHRS 实际更新间隔。
- `DEBUG_PRINT=False` 时关闭逐帧检测打印，避免影响实时性。

### 触摸调参模块

- `read_touch()`：将 `FIT_CONTAIN` 显示坐标反向映射到原始图像坐标。
- `draw_button()`、`handle_slider()`：绘制按钮/滑条并更新参数。
- `safe_set_exposure()`：切换自动/手动曝光并设置曝光值，异常时不中断主程序。
- `build_tune_config()`：从顶层常量生成临时调参字典。
- `apply_tune_config()`：按 START 后把调参值写回 YOLO/红心全局参数。
- `draw_lab_preview()`：显示全图 LAB 红色色块预览。
- `draw_exposure_preview()`：灰度曝光预览。
- `tune_ui()` 包含三页：
  - `LAB`：`Amin/Amax/Bmin`。
  - `YOLO`：`CONF/ROI/AREA`。
  - `EXP`：曝光时间。
  - `PREV` 开关预览，`AUTO/MAN` 切换曝光模式，`START` 应用参数并进入正式主循环。

### `maixcam2_imu.py` 结构

- `AttitudeSample`：使用 `__slots__` 保存 pitch、roll、包裹 yaw、相对 yaw、相对 pitch、`pitch_shake`、三轴角速度、`dt` 和温度。
- `MaixCam2Attitude.__init__()`：保存 Mahony、校准、最大 `dt` 和 pitch 慢基线参数，并初始化内部状态。
- `start()`：
  - `imu.IMU("default")` 打开板载 LSM6DSOWTR。
  - 创建 `ahrs.MahonyAHRS`。
  - 首次或强制时静止校准陀螺；否则加载持久化零偏。
- `reset()`：重置 AHRS、时间、yaw 解包、pitch 零位和慢基线。
- `zero_reference()`：用最近一次有效样本重置相对 pitch/yaw 参考；当前主界面没有调用入口。
- `_unwrap_delta()`：消除 yaw 在 `+/-180deg` 处的跳变。
- `update()`：
  - `read_all(calib_gryo=True, radian=False)` 读取校准后的六轴数据。
  - `dt<=0` 或 `dt>0.25s` 时重置积分并丢弃本次样本。
  - 调用 Mahony 得到 pitch/roll/yaw。
  - 连续解包相对 yaw。
  - 一阶低通更新 pitch 慢基线并计算 `pitch_shake`。
  - 返回 `AttitudeSample`；读取异常只按首次和每 100 次打印，避免刷屏。

### 启动流程

```text
resolve_model_path
  -> validate_maixcam2_model
  -> nn.YOLO11(dual_buff=False)
  -> Camera(模型输入宽高和格式)
  -> Display
  -> tune_ui
  -> MaixCam2Attitude.start
  -> 创建目标滤波和 CenterTracker
  -> 进入正式主循环
```

### 正式主循环

```text
计算视觉循环 dt
  -> cam.read
  -> attitude.update（当前仍为每视觉帧一次）
  -> detector.detect
  -> 选择最高置信度目标
     -> 原始帧角点/对角交点检测
     -> 原始帧红心检测
     -> CenterTracker 红心/保持/拒绝
     -> 有效：低通中心 -> 目标绘制 -> 环数估计
     -> 无效：重置目标/环宽滤波 -> 拒绝绘制
     -> 绘制角点、对角线和几何交点诊断
  -> 无目标：重置全部目标状态
  -> 绘制 IMU 诊断和 FPS
  -> disp.show(FIT_CONTAIN)
```

角点检测和红心检测必须发生在所有辅助图形写入当前图像之前，否则绘制的线条可能被视觉算法当成真实边缘或色块。

### 当前实际数据边界

- 有效目标中心来源只有“本帧红心”或“最多 4 帧历史红心”。
- 低通后的目标中心进入显示和环数估计，但当前没有 UART/网络/电机控制输出。
- 四角对角交点只显示，不替代红心。
- IMU 只显示，不修改目标坐标、不修改图像、不发送 yaw 电机命令。
- 环数只显示，不发送外部命令。

### 已设计但尚未进入代码

- 高频独立 IMU 线程、微秒时间戳环形缓冲和相机帧姿态插值。
- pitch `dy` 计算、固定大小动态裁剪窗口和稳定显示图像。
- `H_raw_to_stable` 三轴单应变换及检测点/框同步映射。
- yaw 电机编码器闭环、IMU 角速度前馈和 MaixCAM2 到控制器的数据协议。
- AX630C 硬件 EIS/GDC 或滚动快门逐行网格修正。
- USB 摄像头采集替换；当前仍使用 MaixCAM2 自带摄像头。

### 当前维护约束

- 必须使用 MaixVision“运行项目”，保证 `.mud`、两个 `.axmodel`、`main.py` 和 `maixcam2_imu.py` 一起上传。
- 保持 `dual_buff=False`，除非重新设计检测结果与图像帧的配对机制。
- 新增任何画面防抖前，保留原始图像/原始坐标链路，不能破坏激光点和环数坐标一致性。
- 几何交点只有经过实机误检率和与红心距离统计后，才能成为中心回退来源。
- 每次结构、参数、控制边界或验证结果变化都继续记录到本文件。

## 2026-07-11 复刻 jixun-2 打靶通信并改为单 yaw 轴

### 参考实现

- 参考仓库为私有仓库 `jikaic149-svg/jixun-2` 的 `vision` 分支，提交 `cc0cf0c2c1d189f96b1591c0dbf354d7f07ad1e5`。
- MaixCAM2 通信使用 `B0=UART2_TX`、`B1=UART2_RX`、`/dev/ttyS2`、`115200 8N1`。
- 视觉到控制器使用 ASCII 行协议：`dx,dy,valid\n`。
- `dx/dy` 是目标中心相对瞄准参考点的像素误差；`valid=0` 要求控制器退出视觉闭环。
- 电机控制、安全限幅和激光状态仍归下位控制器所有，MaixCAM2 只发送像素误差。

### 当前实现

- 新增 `vision_target_link.py`：
  - 初始化 UART2 并收发 ASCII 行。
  - 发送周期下限为 `15ms`。
  - 输出死区为 `2px`，比例为 `1.0`，正常 416x320 画面误差不做缩小。
  - `TARGET_OUTPUT_POLARITY_X/Y` 可独立校准视觉坐标到执行轴的正负方向。
  - 单次变化和绝对保护均放宽到 `dx 320px`、`dy 240px`；正常图像坐标不会触发，只拦截异常值。
  - 无目标时输出向零回落的坐标和 `valid=0`。
  - 保留接收下位机 `M,STAT,...` 等文本状态行的能力。
- `main.py` 只在本帧实际检测到红色色块时发送有效目标：
  - 原始中心来自 `find_red_center_in_box()` 选择的最大红色 blob。
  - 低通后的红色中心用于计算 `dx`，不允许 YOLO 框中心、四边形交点或历史保持点产生有效串口控制包。
  - 瞄准参考点默认为图像中心，可用 `TARGET_AIM_OFFSET_X/Y` 校准相机与激光的固定安装偏差。
- 当前机构只有 yaw 轴，`TARGET_YAW_ONLY=True` 会强制串口 `dy=0`；本地仍保留红心 `cy` 用于显示和诊断。
- MaixCAM2 IMU 默认关闭；复刻的 `jixun-2` 边界是控制器持有 yaw 编码器/IMU 反馈，MaixCAM2 不参与电机内环。
- 修正备用 IMU 路径的单位：`read_all(..., radian=True)` 给 Mahony 提供 rad/s，`get_angle(..., radian=False)` 输出角度制。

### 验证

- `python -m py_compile main.py maixcam2_imu.py vision_target_link.py` 通过。
- 假串口协议测试通过：有效目标输出 `dx,0,1\n`，周期门控生效，丢失目标输出 `0,0,0\n`，状态行可完整接收。
- 尚需 MaixCAM2 与下位控制器联机确认 UART2 接线、yaw 极性和 `TARGET_AIM_OFFSET_X`；极性不正确应在下位机 yaw 控制极性或视觉输出极性中只改一处。

## 2026-07-11 MaixCAM2 到 VOFA+ 的纯 UDP 试验链路

- 默认目标输出由 UART2 改为 UDP，UART 实现保留为备用但运行时不初始化。
- MaixCAM2 向 `255.255.255.255:5005` 广播 `dx,dy,valid\n`；数据格式本身兼容 VOFA+ FireWater。
- 新增电脑端 `udp_vofa_bridge.py`：
  - 监听所有网卡的 UDP `5005`。
  - 严格校验三个整数字段和 `valid=0/1`。
  - 转发到本机 `127.0.0.1:5006`，供 VOFA+ UDP 数据源读取。
  - 同时在终端打印实时目标数据，替代 MaixVision 不具备的任意 UDP 转发能力。
- MaixVision继续用于上传、运行和查看设备日志，不作为 UDP 到 VOFA+ 的数据中转节点。
- 实机网络检查确认 MaixCAM2 为 `10.81.37.1`、电脑 RNDIS 端为 `10.81.37.100`；目标地址已由多网卡环境下出口不确定的 `255.255.255.255` 改为 `10.81.37.100` 单播。
- MaixVision 终端每 `500ms` 输出 `FPS/det/UDP_TX/UDP_ERR`，用于实时确认视觉循环和 UDP 发送状态；避免逐帧打印反过来降低推理帧率。
- 修复 Windows UDP 桥接在 VOFA+ 尚未监听 `5006` 时因 `WinError 10054` 退出的问题；现在忽略端口不可达并持续等待，启动顺序不再影响桥接存活。
- 为正式识别循环增加生命周期日志：进入前打印 `tracking loop ready`，收到 `app.need_exit()` 后打印 `tracking loop stopped: app exit requested`；顶层未处理异常会先打印 `fatal error` 再重新抛出，便于区分 MaixVision 主动停止与程序故障。
- 实机出现进入正式循环后无 Python 异常直接退出。比赛主链路默认关闭仅用于显示诊断的四角检测与环数径向扫描，并为第一帧增加相机、推理、目标处理、UDP 和显示检查点；核心 YOLO、红色色块中心与 UDP 输出保持启用。
- 后续运行时 MaixVision 实际改为通过 WLAN `192.168.237.147 -> 192.168.237.66:7899` 连接板卡，旧 USB/RNDIS 目标 `10.81.37.100` 已不可达；UDP 目的地址同步改为电脑 WLAN 地址 `192.168.237.147:5005`。WLAN 为 Public，现有 Python 入站防火墙规则已覆盖该配置。
- 通信链路改为双向串口加单向 UDP 遥测：MaixCAM2 UART2/B0 TX 向 STM32 USART6/PG9 RX 下发 `dx,dy,valid\n`，STM32 USART6/PG14 TX 向 MaixCAM2 UART2/B1 RX 回传原 VOFA FireWater CSV，统一使用 `115200 8N1` 和共地。MaixCAM2 对完整数值行校验后转成 UDP 发往电脑 `192.168.237.147:5005`；电脑桥接支持最多 32 个有限数值通道并转发到 VOFA+ `5006`。文本表头和非法行不进入 VOFA+。
- UART/UDP 链路在进入触摸调参界面前立即启动。调参阶段持续轮询 STM32 遥测并转发 UDP，同时按 `15ms` 门控发送 `0,0,0`；按下 `START` 后才允许真实红心误差产生 `valid=1`，但通信本身不再等待 `START`。
- 增加电脑到 MaixCAM2 的 UDP `5007` 命令通道。VOFA+ 从本地 `5006` 发给桥接 `5005` 的内容会被识别为调试命令并转发到 `192.168.237.66:5007`，MaixCAM2 再追加换行后写入 UART2。支持 STM32 现有的 `0..5`、英文模式词、bias/PID/速度命令；电脑桥接额外把“停止/站立/前进/后退/左转/右转/循迹”映射为 `0..5`。

## 2026-07-11 红色色块偏差回传与视觉锚点校准

- 按 `jixun2` 的视觉锚点逻辑实现：未校准时参考点为图像中心 `x=width//2`，回传 `dx=当前帧红色色块中心x-图像中心x`。
- 串口发送只使用当前帧 `find_red_center_in_box()` 得到的红色色块中心，不再使用低通后的中心、历史保持点、YOLO 框中心或几何中心；yaw-only 保持 `dy=0`。
- 输出层死区改为 `0px`，帧间步进和绝对限幅放宽到 `2048px`，因此正常 `416x320` 图像范围内的像素偏差不会被改写。
- 正式识别界面新增 `CAL` 和 `RST`。按 `CAL` 后连续收集 15 帧红心横坐标，最近 15 帧极差不超过 `4px` 时取平均值作为本次运行的 `reference_x`；按 `RST` 恢复图像中心参考点。
- 校准仅在内存中生效，程序重启后自动恢复图像中心；校准采样期间或当前帧没有红色色块时发送 `0,0,0`，校准完成后发送 `red_x-reference_x,0,1`。
- 屏幕和 MaixVision 日志同时显示图像中心、红心坐标、相对图像中心的 `raw_dx`、实际发送的 `tx_dx`、`reference_x` 和校准状态。
- 新增纯 Python 校准单元测试和输出层回归测试；本轮没有修改 STM32 协议、`115200 8N1` 波特率、像素到角度换算或电机控制。

## 2026-07-12 UDP 自动发现与 UART 解耦

- MaixCAM2 遥测默认改为广播到 `255.255.255.255:5005`，发送 socket 显式启用 `SO_BROADCAST`；不再写死电脑 DHCP 地址。
- `VisionTargetLink.start()` 分别初始化 UART2 和 UDP。UART2 初始化失败会保留错误状态，但不会再阻止 UDP `5007` 命令监听和遥测转发启动。
- 电脑 bridge 只从通过有限数值 CSV 校验的遥测包学习 MaixCAM2 源 IP，并将 VOFA+ 命令回发到该 IP 的 `5007`；非法包不能覆盖已发现地址。
- `--command-host` 仅作为广播不可用时的显式覆盖，默认不再包含任何 `192.168.*` 地址。独立发送工具同样要求调用者显式提供 `--host`。
- 当前实机自动发现为 `192.168.1.222:5007`，电脑监听 `0.0.0.0:5005` 并转发到 VOFA+ `127.0.0.1:5006`。更换 Wi-Fi 或 DHCP 地址后无需修改代码。
- 全部 Python 文件通过 `py_compile`，UDP/UART/视觉校准单元测试共 `17/17` 通过。

## 2026-07-13 三页面、五任务与视觉控制重构

### 当前版本边界

- `main.py` 只负责硬件初始化、逐帧调度和 `MAIN / CONTROL / RESULT` 三页面切换。
- `target_recognition.py` 单文件封装靶纸 ROI、四边形/单应校正、红色椭圆降级锁定、移动检测和 0 至 10 环计算。
- `visual_calibration.py` 使用连续 8 个稳定 X/Y 样本确定激光参考点。
- `vision_target_link.py` 统一 UART 与 UDP：视觉侧发送 `V,...`，接收任务侧 `T,...`，并处理 `5005/5007/5008`。
- 已删除 `maixcam2_imu.py` 和运行代码中的 IMU、电子防抖及裁剪路径。本文前面的 IMU/EIS 内容仅是历史调研记录，不再表示当前代码行为。
- STM32 任务代码位于 `jixun3/task`，任务 1 至 5 通过统一任务控制器调用；任务 2 至 5 均在瞄准完成后才允许打开激光。

## 2026-07-13 UDP、SET 预览与环数语音增量

### UDP 当前链路

- 电脑当前 WLAN IPv4 为 `192.168.237.147`，MaixCAM2 默认将 STM32 数值遥测发到 `192.168.237.147:5005`，将 `VISION,...` 状态发到 `192.168.237.147:5008`。
- `udp_vofa_bridge.py` 必须在电脑运行并监听 `0.0.0.0:5005` 和 `0.0.0.0:5008`；它把数值遥测转发到 VOFA+ `127.0.0.1:5006`。
- PC 命令先进入桥接程序，再发到已发现的 MaixCAM2 `5007`；MaixCAM2 校验命令后写入 UART。只打开 VOFA+ 的 `5006` 不会自动监听 `5005/5008`，因此此前会出现“UDP 没数据”。
- SET 中的 UDP 项只在固定电脑地址和广播诊断地址之间切换，不提供自由文本输入。

### SET 实时画面

- SET 仍是 MAIN 页任务按钮区域的临时状态，不增加第四个页面。
- 曝光和 UDP 项显示当前彩色相机帧；红阈值项显示按当前 LAB 阈值生成的二值预览。
- 二值图像从识别完成后的原始帧副本生成，不能覆盖送入 YOLO、`find_rects`、`find_blobs` 和环数计算的当前帧。
- 只有 SET 打开且选中红阈值时才复制并二值化图像；CONTROL 和 RESULT 不承担这项额外开销。

### 环数 WAV 播报

- `wav/ring_1.wav` 至 `wav/ring_10.wav` 作为离线播报资源，不使用在线 TTS。
- 每次 3 秒射击结束、`ShotScoreSession` 锁存最终环数时只触发一次播报；0 环和未知值 `-1` 不播放，因为没有对应资源。
- 播放器采用非阻塞方式，播放期间视觉主循环继续取帧、推理和通信。
- 任务重新进入准备态时重置本任务的播报记录，因此新任务可以再次播报相同环数。

### 帧率重点说明

以下操作会明显消耗单帧时间，本版本按题目要求全部保留，暂不通过隔帧、降分辨率或关闭功能来提速：

- 每帧 YOLO11 推理通常是最大耗时项。
- 靶纸 ROI 内的 `find_rects` 边缘/矩形搜索。
- LAB `find_blobs` 红色色块搜索和椭圆参数计算。
- 四点单应矩阵求解、坐标投影，以及角点不足时的椭圆轴归一化。
- 红色椭圆轮廓叠加绘制中的三角函数和多段线绘制。
- 环数采样/统计、等待第二枪时的目标移动检测。
- 图像显示、UART 解析和 UDP 收发；逐帧打印日志也会显著拖慢循环，因此不应增加逐帧终端输出。
- SET 红阈值项的整帧复制与 LAB 二值转换。这是新增的主要图像开销，但只在该设置项可见时运行。
- 首次启动某个 WAV 的文件读取、解码和播放器启动可能造成短时开销；已使用非阻塞播放并且只在每枪结算时启动，不在每帧重建播放器。

当前保持 `dual_buff=False`，保证检测结果、红色目标和显示帧严格配对。上述高耗时路径不能在没有实机对照数据时删除；后续需要在 MaixCAM2 上分别记录 `cam.read`、YOLO、矩形/色块、几何校正、SET 二值化、绘制、音频启动和 `disp.show` 的耗时，再决定优化顺序。

### 本轮验证结果

- 纯 Python 单元与集成测试共 62 项通过；全部 Python 文件通过 `py_compile`。
- 真实本机 UDP socket 测试已覆盖：`5005` 遥测转发到 `5006`、`5008` 视觉状态接收、发现 MaixCAM 地址后向 `5007` 回发 `task5`。
- 电脑端桥已经以后台进程运行，实测 `0.0.0.0:5005` 和 `0.0.0.0:5008` 由 Python 监听，`0.0.0.0:5006` 由 VOFA+ 监听。
- MaixCAM2 `192.168.237.66` 可达，MaixVision 到设备 `7899` 的 TCP 连接正常，Windows Public 防火墙已有 Python 入站允许规则。
- 在 2026-07-13 本轮修改完成后的 3 秒实包观察窗口内，电脑没有收到 MaixCAM2 的 `5005/5008` 包。这说明当前板端尚未重新运行/上传本轮视觉项目；本地文件修改不会自动替换板端进程。必须在 MaixVision 中重新执行“运行项目”，然后再以 `5008` 的周期 `VISION,...` 包和 `5005` 的 STM32 数值遥测确认实机链路。
- 随后重新运行板端项目，用户已确认电脑端收到 UDP 数据；`MaixCAM2 -> 5005/5008 -> PC bridge -> VOFA+ 5006` 实机链路最终连通。前一条 3 秒无包仅表示当时板端尚未运行新代码，不是桥接故障。

## 2026-07-13 MaixCAM2 无 traceback 退出与卡死优化

### 实机现象和判断

- 实机日志完成模型、相机、显示、触摸、UART 和 UDP 初始化，打印旧版 `vision ready: three pages, five tasks` 后，在首个媒体图像对象出现时直接显示 `program exited`。
- 没有 Python traceback，也没有进入 `main.py` 的 `finally` 打印 `vision stopped`，因此不是普通 Python 异常，更符合 `detector.detect` 或原生图像 ROI 算子在 native 层终止进程。
- 旧过程记录曾明确把 `find_rects` 默认关闭，原因就是该原生扫描可能直接中止核心循环；三页面重构时重新改成每帧执行，成为本次首要风险。

### 当前优化

- 比赛默认 `enable_quad_scan=False`，不再调用高风险 native `find_rects`。四边形/单应代码没有删除，实车确认固件稳定后可以显式开启；开启时改为每 8 帧扫描一次、最多缓存 20 帧。
- 默认锁定继续使用红色椭圆主轴归一化，斜拍下不要求图像中的红色区域保持正圆。
- 所有 YOLO 框先与图像边界求交；完全出框、负尺寸或无面积的框不进入任何 native ROI 算子。
- `find_blobs` ROI 限制为最多 `320x320`；可选四边形 ROI 限制为最多 `360x360`。检测框、blob 和 rect 的 Python 候选处理分别限制为 8、12 和 8 个。
- MAIN、SET、RESULT 以及任务1不再运行 YOLO/靶面识别。任务2至5的 CONTROL 才启用识别。
- CONTROL 中 YOLO 每 2 帧执行一次，结果立即复制成纯 Python 坐标框，只复用一个间隔帧；红色 blob 仍逐帧在当前图像中验证，因此当前帧丢红色目标会立即令 `locked/control_valid` 失效。
- 相机连续无帧达到 `180ms` 时立即发送无效视觉帧并向 STM32 发送 STOP；无帧轮询增加 `5ms` 退避，避免空转。
- 识别、控制、绘图和显示增加帧级异常隔离；异常先发送无效视觉帧，重复错误每 2 秒最多打印一次，避免终端日志反过来拖死主循环。
- 每 30 帧执行一次 `gc.collect()`；SET 二值显示副本在 `disp.show` 后立即解除引用，退出时显式释放音频、UART、UDP、触摸、相机、显示和模型资源。
- 椭圆轮廓的 25 个单位圆点改为模块加载时预计算，每帧只计算一次椭圆旋转角的正余弦。

### 通信和音频资源

- UDP `5007` 命令改为先于 UART 遥测处理，STOP 不再等待串口积压。
- UART 每帧最多读取 512 字节，不再一次追赶完整积压；超长行进入丢弃状态直到下一个换行，避免尾段被误解析成新包。
- UART 写入检查短写/负返回，IDE 重跑前显式关闭 UART 和共享 UDP socket。
- 播放第二枪前先停止并释放上一枪的播放器；播放失败和程序退出也执行资源清理。

### 验证与实机检查点

- 纯 Python 测试共 79 项通过，覆盖越界 ROI、扫描分时/缓存、逐帧丢靶失效、YOLO 结果快照、日志节流、UART 积压上限、STOP 优先、短写、音频释放以及原有任务/UI/UDP/环数逻辑。
- 新版启动日志为 `vision ready: optimized, yolo period=2, quad scan=0`。
- MAIN 第一次显示成功后打印一次 `first display frame ok`；进入任务2至5后，首次 YOLO 成功打印 `first YOLO frame ok`，首次红色 ROI 处理成功打印 `first target frame ok`。若仍发生 native 退出，最后一条检查点可直接确定失败阶段。

## 2026-07-13 点击重启、显示生命周期与SET校准稳定版

### 两次实机定位

- 第一轮优化后日志到达 `first display frame ok` 随即退出。原因是 `disp.show()` 为异步提交，代码在提交后立即 `del frame`，显示驱动仍使用该 native Image，形成释放后使用。当前保留已提交图像引用，直到下一帧成功提交后才允许旧引用释放。
- 修复显示生命周期后 MAIN 稳定；点击任务2至5或 SET 后重启，且没有出现 `first YOLO frame ok`。这把 native 终止点确定在自定义 AX 模型的首次 `detector.detect()`，不是触摸驱动。

### 默认禁用不稳定NPU模型

- `enable_yolo=False` 为当前比赛稳定默认值。关闭时不导入 `maix.nn`、不创建 `YOLO11`、不初始化 NPU，也不会因点击按钮调用 `detect()`。
- 相机固定为 `416x320 RGB888`。任务2至5和 SET 使用当前彩色帧中央最多 `320x320` 的 LAB 红色 blob 搜索，锁定最大合格红色椭圆。
- 红圆椭圆主轴归一化、斜拍锁定、移动检测、环数统计、瞄准门控和逐帧丢靶失效继续保留。
- 靶纸类别过滤暂时停用，这是避免整进程 native 重启的明确取舍。YOLO代码和模型仍保留，只有自定义模型通过独立NPU冒烟测试后才能重新设置 `enable_yolo=True`。
- 稳定版启动标志为 `vision ready: stable detector=red, quad scan=0`；首次进入 SET 或任务2至5应打印 `first detector frame ok: red` 和 `first target frame ok`，不应再出现 NPU ENGINE/model load 日志。

### SET激光和红圆中心校准

- SET 增加独立 `CAL/RST` 行，仍属于 MAIN 页面，不增加页面数量，也不覆盖 `ITEM/-/+/BACK`。
- 进入 SET 先发送无效视觉坐标，再发送 `cal_laser_on`；BACK、相机失败、识别/显示异常和程序退出发送 `cal_laser_off` 或 STOP。
- STM32仅在无竞赛任务、STAND、急停解除、无故障且底盘真实停稳时允许物理激光点亮。
- SET持续发送 `V` 心跳续租；STM32超过 `180ms` 没有新V帧会撤销请求并直接 `BSP_LaserOff()`，因此Maix原生崩溃或UART断线不会让激光永久保持。
- CAL只接受已经 `LOCK` 的红圆中心，连续8个X/Y样本的双轴极差都不超过4像素时，将平均中心锁存为 `reference_x/reference_y`。RST恢复画面中心并清空采样。
- 曝光和UDP设置项显示彩色帧及红椭圆/参考十字；红阈值项显示二值图，识别始终使用未修改的彩色原帧。

### 最终验证

- 视觉端85项测试通过，全部Python文件通过 `py_compile` 和 `git diff --check`。
- 嵌入式 `cal_laser_on/off`、租约、静止/故障门控已通过ARM完整编译链接，固件为 `jixun3/build/spi_bmi088.hex`，生成时间 `2026-07-13 10:08:14`。
- 嵌入式主机测试源码已编译；本机Application Control策略阻止新生成测试EXE执行，因此本轮以ARM构建、语法检查和已有主机测试覆盖作为静态验证，仍需烧录后确认物理激光极性。

## 2026-07-13 UART/UDP native稳定性加固

- MAIN空闲时本来就会周期发送无效V帧并轮询UART/UDP，因此旧版“点击后才重启”与首次NPU推理的时间关系更直接，不能把旧重启全部归因于通信。
- 仍然移除一个明确的驱动风险：原实现用同一个已绑定UDP socket同时执行 `recvfrom(5007)` 和 `sendto(5005/5008)`。稳定版改为两个独立native socket：未绑定发送socket只负责遥测/状态，绑定 `0.0.0.0:5007` 的接收socket只负责PC命令。
- UART视觉帧周期由 `15ms` 降为 `40ms`（25Hz），仍明显快于STM32的 `180ms` 视觉/校准激光超时。
- VISION UDP状态周期由 `100ms` 降为 `200ms`，减少无线栈和Python socket调用峰值。
- UART每帧读取上限由512字节降为256字节，命令接收每帧最多处理2包；读取改为Maix示例一致的单参数 `read(256)`，不再组合调用 `available()+read(size, timeout)`。
- UDP命令仍先于UART遥测处理，STOP不会被串口积压长期阻塞。发送和接收socket在程序退出时分别关闭。
- 本轮视觉85项测试再次全部通过，UDP电脑桥继续监听 `5005/5008`，VOFA+继续监听 `5006`。

## 2026-07-13 `80000`回显误触发运动命令

### 根因

- 实机在红色检测与显示检查点全部成功后重启，日志末尾单独出现 `80000`。该文本不存在于视觉代码、目标识别或STM32日志打印中，因此不是程序大小或Python内存数字。
- 旧电脑桥把所有来自VOFA+ `127.0.0.1:5006` 的ASCII内容视为控制命令；旧Maix白名单又允许任意有限浮点数；STM32最终把裸数字解析为 `WheelSpeed_SetTargetRpm()`。
- 因此VOFA回送的 `80000` 会沿 `VOFA 5006 -> bridge 5005 -> Maix 5007 -> UART -> STM32` 变成轮速输入。STM32虽会把它夹到200 RPM，但仍会触发非预期运动和电流变化，足以造成失稳或共享供电跌落，看起来像Maix程序重启。

### 三层封堵

- PC桥 `parse_command_payload()` 现在只接受明确白名单；`80000`、`120.0` 等裸数字回显直接归类为 `invalid_command`，不会发往5007。
- Maix `VisionTargetLink` 使用同一白名单再次校验。允许 `task1..5`、`stop`、`cal_laser_on/off`、命名模式、`0..5`安全模式和带明确前缀的PID/限幅参数；拒绝裸 `6/7` 和其他数值。`mode 6/mode 7` 仍保留。
- 独立PC命令工具同样使用该白名单，不能手工发送裸数字。
- STM32最后增加防御：即使绕过PC和Maix直接从UART收到裸数，绝对值超过200 RPM也直接拒绝，不再静默夹紧后执行；`+350 RPM`、`-350 RPM` 和 `80000` 均为明确拒绝用例。
- 新增回归测试明确断言 `80000` 不能通过PC桥、Maix命令解析和STM32轮速入口。

## 2026-07-13 STM32直接轮速边界调整

- STM32串口直接轮速命令的允许范围由 `-200..+200 RPM` 放宽到 `-300..+300 RPM`，正负300边界均可执行。
- `+350 RPM`、`-350 RPM`、`80000` 等超界裸数字继续直接拒绝，不夹紧、不执行。
- PC桥和Maix视觉端仍拒绝全部远程裸数字，避免VOFA遥测回流成为运动命令；该300 RPM边界仅适用于直接连接STM32的本地串口调试入口。

### 验证

- 视觉端测试由85项增加到87项，全部通过；全部Python文件通过 `py_compile`。
- PC桥已重启为新白名单版本，当前监听 `5005/5008`，VOFA+监听 `5006`。
- STM32 ARM构建通过：text `71460`、data `184`、bss `28232`；新固件已生成到 `jixun3/build/spi_bmi088.hex`。

## 2026-07-13 快速画面变化时的显示链路稳定性

- 现场表现为静态或慢变化画面能够运行，快速移动相机/快速改变画面内容时MaixCAM进程或整机重启。当前Python日志没有异常回溯，因此优先按异步显示native资源积压处理，而不是按普通Python异常处理。
- `disp.show()`是异步提交。原循环按相机最高帧率持续提交，快速变化画面会放大显示转换和驱动消费压力；仅保留一张Python引用不能限制native提交速率。
- 新增 `display_period_ms=50`，将LCD提交限制为20Hz。相机读取、红圆识别、UART视觉帧、UDP状态和任务控制仍按原循环运行，不把控制频率一起降低。
- 显示提交失败后进入 `250ms` 冷却，不再同一帧立即执行第二次fallback `disp.show()`，避免驱动异常状态下重复压入native请求。
- 成功提交的native Image继续保留到下一次成功提交；20Hz间隔给显示硬件完整消费时间，减少上一帧仍在使用时Python释放对象的风险。
- 当前高耗时操作仍明确保留：识别期间每帧 `find_blobs()`、SET红阈值页的 `frame.copy()+binary()`、红椭圆多线段绘制、周期性 `gc.collect()`。本轮只限制显示提交峰值，没有删除这些功能；其中 `find_blobs()` 和SET二值副本仍是主要帧率消耗点。

## 2026-07-13 发车条件放宽

- 任务2至5的START不再要求A点当前存在红圆LOCK，已有CAL且UART可用即可先循迹发车。
- CAL仍保留为任务2至5的发车条件，避免未校准时把默认画面中心误当成激光参考点；任务1仍只要求UART。
- 到中央标记后的射击条件没有放宽：视觉输出必须同时满足CAL有效、当前帧红圆锁定、几何质量有效、`dx/dy`进入死区并连续稳定300ms。任何条件丢失均不能进入激光驻留。

## 2026-07-13 MaixCAM2单独复位与最新VOFA核对

- 最新 `jixun3/vofa+.csv` 为2824帧、约56.48秒。STM32遥测连续，未出现MCU时间轴重启或异常裸数字命令。
- 最后约7.68秒为 `mode=5`；最后约4秒视觉 `valid=1`，`age_ms`按0/20/40ms正常刷新，证明Maix至少持续完成识别并向STM32发送视觉帧。
- 记录末尾 `v_ref=0.10m/s`、实际速度约0.1m/s、激光为0；没有 `350/80000` 或激光突变。现场进一步确认是MaixCAM2单独复位，因此不按STM32固件重启处理。

### native图像链路进一步隔离

- LCD提交由20Hz进一步降为10Hz，但识别结果发送、UART轮询、UDP和任务状态机仍按主循环运行。
- 每次显示先执行一次独立 `frame.copy()`，异步 `disp.show()`只持有显示副本，绝不再直接持有相机采集池的native buffer。SET二值页在该显示副本上原地二值化，不进行第二次复制。
- `find_blobs()`由每个相机帧降为每80ms一次（约12.5Hz）。两次识别之间只复用最多160ms的观测；超过租约立即令视觉无效，不能继续瞄准或开激光。
- CAL只消费真正执行过识别的新帧，不能用同一缓存观测重复凑满8个校准样本；环数也只累计新识别帧，避免缓存样本重复加权。
- 新版启动标志为 `vision ready: stable detector=red, quad scan=0, display=10Hz, recognition=12Hz`。若设备日志没有后两个频率字段，表示Maix仍在运行旧版代码。

### 锁定标记

- 按现场操作习惯继续使用十字，但不恢复白色：检测未锁使用黄色十字，LOCK使用绿色十字。
- 激光校准参考点使用蓝色十字，避免与红圆锁定中心混淆。

## 2026-07-13 MaixCAM2官网复位排查

### 新VOFA证据

- 11:03保存的最新 `vofa+.csv` 共1987帧、约39.74秒。前38.16秒为stand，最后1.58秒进入mode 5；CSV结束时STM32仍收到 `valid=1、age_ms=20ms`，激光全程为0。
- MaixVision在11:00:48连接，11:02:40报 `Device timeout`；结合STM32时间轴连续，结论仍是MaixCAM2本机/网络侧突然消失，不是STM32复位或异常运动命令。

### Sipeed官方资料结论

- 官方相机文档：低分辨率相机需要显式设置 `fps`；帧率通过 `camera.Camera(..., fps=30)` 设置。`buff_num`过低可能丢图，不应为了延迟盲目改成1。
- 官方FAQ：`camera read timeout`常见原因包括读取过快或多个相机通道同时读取；处理方式是捕获异常、等待10ms后重试。
- 官方显示文档：代码由MaixVision运行时，`disp.show()`会自动压缩图像并发送给MaixVision。快速变化画面因此不仅更新LCD，还会增加远程预览编码、网络和多媒体内存压力。
- 官方FAQ：系统只显示约128MB普通内存，其余保留给相机、显示、编码器和NPU的ION/驱动内存。检查命令为 `cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary` 和 `cat /proc/meminfo`。
- 官方FAQ：异常退出日志位于 `/maixapp/tmp/last_run.log`；可用官方 `examples/tools/show_last_run_log.py` 或SSH读取。
- 官方FAQ：MaixCAM典型功耗约250mA屏幕待机、400到500mA满负载，供电不足会造成启动慢、闪屏和异常；快速画面编码带来的负载上升可能暴露供电裕量问题。
- 官方升级文档：Runtime/MaixPy依赖系统驱动，版本不匹配可能导致相机/显示异常。MaixCAM2必须使用对应型号系统镜像，不能刷MaixCAM或MaixCAM Pro镜像。

官方来源：

- `https://wiki.sipeed.com/maixpy/doc/en/vision/camera.html`
- `https://wiki.sipeed.com/maixpy/doc/en/vision/display.html`
- `https://wiki.sipeed.com/maixpy/doc/en/faq.html`
- `https://wiki.sipeed.com/maixpy/doc/en/basic/upgrade.html`
- `https://github.com/sipeed/MaixPy/releases/tag/v4.12.5`

### 按官网建议落地

- 相机由未指定fps改为显式 `camera_fps=30`；相机异常重试由5ms改为10ms。
- LCD仍为10Hz，但每次只提交独立 `frame.copy()`，不把相机池原始buffer交给异步显示/远程压缩。
- 新版启动标志必须包含 `camera=30Hz, display=10Hz`。缺少该字段说明设备仍运行被同步回来的旧 `main.py`。
- 稳定性对照测试必须关闭MaixVision图像预览或把程序安装为本机APP后断开MaixVision运行；仅保留日志连接仍可能触发自动图像压缩，不能作为纯板载LCD测试。

## 2026-07-13 与参考MaixCAM代码对比后的稳定版

- 可核对的参考实现为 `jixun3` 提交 `a2164c7` 中的 `25e.py`：相机使用320x240灰度、中心点 `alpha=0.25` EMA，并在相机异常后等待50ms。当前功能必须识别红色，不能退回灰度，但可恢复其较小采集尺寸和中心滤波。
- 当前模块化版本此前为416x320 RGB888，单帧约399KB；改为320x240 RGB888后约230KB，采集帧和显示副本分别降低约42%。相比320x240灰度仍多约3倍，因此继续限制显示和识别频率。
- `recognition_period_ms=80` 原本只存在于配置，设备同步回来的 `main.py` 没有使用它，实际仍每个30fps相机帧执行native `find_blobs()`。现已真正接入主循环，约12.5Hz识别，缓存观测最多租用160ms；CAL和环数只消费新识别帧。
- `find_blobs()`增加 `x_stride=2/y_stride=2`，降低LAB全ROI扫描量；候选数量上限只约束Python遍历，不能替代native扫描节流。
- 红圆控制中心恢复参考实现的 `alpha=0.25` EMA；候选关联重置或丢失时清空EMA，避免跨目标继承坐标。
- LCD保持10Hz。复核Sipeed官方示例后，常态路径恢复官方 `cam.read() -> disp.show(frame)`，不再额外复制并保留3张RGB帧；此前的UAF判断缺少官方API证据，只能作为排查假设，不能当作已确认根因。仅SET二值预览复制画面。显式GC由每30帧降低为每120帧。
- 11:09生成的 `dist/maix-jixun3-v1.0.0.zip` 包含旧 `main.py`：未指定fps、直接显示相机buffer、无新版启动标志。完成验证后必须重新打包，不能继续安装该旧zip。
- APP版本提升为 `1.0.1`并生成 `dist/maix-jixun3-v1.0.1.zip`，避免MaixVision按相同版本号复用旧安装缓存。

## 2026-07-13 红色目标锁定增强

- LAB 红色阈值保持保守的 `A_min=25`，只把 `A_max` 从80扩展到127，避免高饱和红色被阈值上限截断；未降低像素数、轴比或填充率门槛。
- 红色候选首次获取仍偏向画面中心。建立连续轨迹后，候选排序改为优先靠近上一帧红心，抑制第二个居中红点或红色物体抢锁。
- 当前帧没有合格红色 blob 时仍立即清零锁定计数、EMA和 `control_valid`；下一帧重新按全局候选获取，不让历史目标继续产生控制坐标。
- 保持连续3帧确认、`lock_max_jump=0.20`、`geometry_min_quality=0.45`和 `alpha=0.25`，没有通过缩短确认时间换取表面上的快速 LOCK。
- 新增多红块抗干扰、丢帧后解除候选关联和高饱和红阈值回归测试；视觉端共95项测试通过。
