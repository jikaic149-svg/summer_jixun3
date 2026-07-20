# 当前控制与视觉闭环流程图

本文描述当前工作区实际采用的闭环，不描述旧文档中的 ESP32 未来架构。当前由 **DJI C 型开发板 STM32F407** 直接接收 UART 视觉与命令，运行路线和任务状态机，控制 yaw 电机 CAN，并完成激光 PWM 的最终安全门控。MaixCAM2 上的 `25e.py` 负责图像识别、标定、目标状态滤波和视觉结果发布。

## 1. 当前系统边界

```mermaid
flowchart LR
    CAM["MaixCAM2 相机<br/>320 x 240 灰度图"]
    ALG["25e.py<br/>识别、滤波、标定、状态发布"]
    LINE["灰度巡线传感器"]

    subgraph CBOARD["DJI C 板 STM32F407：当前控制主体"]
        U6["USART6 接收<br/>视觉帧、任务命令、模式命令"]
        U1["USART1 接收<br/>巡线 error、black_count、black_mask"]
        VP["Vision_ParseLine<br/>校验并发布一致视觉快照"]
        CQ["命令队列<br/>task1 至 task5、Stop、模式切换"]
        TASK["Task 状态机<br/>射击、等待移动、返程、完成"]
        ROUTE["Mode_Tracking 状态机<br/>A、中央靶位、B 路线"]
        YAWCTL["视觉 yaw 控制<br/>dx 转角度目标与电流控制"]
        SAFE["激光最终门控<br/>任务阶段、反馈、运动、急停、故障"]
        BAL["平衡与底盘控制"]
    end

    YAW["yaw 电机<br/>C 板直接发送 CAN"]
    WHEEL["左右轮电机<br/>C 板直接发送 CAN"]
    LASER["激光<br/>C 板直接控制 PWM"]
    STATUS["任务状态 T 帧<br/>变化时或每 100 ms 回传"]

    CAM --> ALG
    ALG -->|"V 视觉帧和任务命令"| U6
    LINE --> U1
    U6 --> VP
    VP -->|"非视觉行"| CQ
    VP --> TASK
    VP --> YAWCTL
    U1 --> ROUTE
    CQ --> TASK
    TASK <-->|"route phase、release、drive、laser"| ROUTE
    ROUTE --> BAL
    TASK --> SAFE
    YAWCTL --> YAW
    BAL --> WHEEL
    SAFE --> LASER
    TASK --> STATUS
    STATUS --> U6

    classDef source fill:#f4f6f8,stroke:#59636e,color:#17202a;
    classDef cboard fill:#e9f4ed,stroke:#39724a,color:#183822;
    classDef safety fill:#fff2d8,stroke:#ad691b,color:#4d2a05;
    classDef output fill:#f6eaf0,stroke:#934864,color:#461d2b;
    class CAM,ALG,LINE source;
    class U6,U1,VP,CQ,TASK,ROUTE,YAWCTL,BAL cboard;
    class SAFE safety;
    class YAW,WHEEL,LASER,STATUS output;
```

当前链路中没有 ESP32 中间控制层。ESP32 若将来加入，应视为另一版架构，不能用于解释本实现中的 UART、yaw 或激光归属。

## 2. 25e.py 视觉算法链

```mermaid
flowchart TD
    F["相机帧<br/>320 x 240"] --> GRAY["转灰度图"]
    GRAY --> BIN["全图二值化<br/>灰度阈值 0 至 115"]
    BIN --> OUTER["外框候选<br/>白区面积和宽高比筛选"]
    OUTER --> CORE["框内黑芯候选<br/>面积、宽高比、位置、相对占比筛选"]
    CORE --> GEO["四边形几何筛选<br/>边长、对边比、凸性、角余弦、对角线比、填充比"]
    GEO --> SCORE["候选评分并取最低分<br/>标定中心距离、上次中心距离、几何质量"]
    SCORE --> JUMP{"相对 EMA 中心<br/>跳变是否大于 80 px"}
    JUMP -->|"是"| REJECT["拒绝本帧目标<br/>保持失锁状态"]
    JUMP -->|"否"| EMA["中心点 EMA<br/>alpha 等于 0.55"]
    EMA --> CAL{"是否已标定"}
    CAL -->|"否"| CALS["自动标定<br/>8 个稳定样本且散布不超过 4 px"]
    CALS -->|"完成"| REF["保存 aim_ref_x 和 aim_ref_y"]
    CAL -->|"是"| ERR["计算 dx 和 dy<br/>平滑靶心减标定参考点"]
    REF --> ERR
    ERR --> LOCK["状态滤波<br/>locked、valid、aim_ready"]
    LOCK --> MOVE["目标移动滤波<br/>WAIT_MOVE 中位移至少 18 px<br/>连续 5 帧后 move_epoch 仅加一次"]
    MOVE --> TX["每 15 ms 发布扩展视觉帧<br/>V,dx,dy,valid,locked,aim_ready,move_epoch"]
    REJECT --> TXOFF["发布 valid 等于 0 的撤销帧"]

    ROI["跟踪时优先 ROI 和上帧角点<br/>失配后扩大 ROI，连续丢失再全图重捕获"]
    ROI -.-> OUTER

    classDef image fill:#eaf2f7,stroke:#2e7188,color:#153842;
    classDef filter fill:#edf5ea,stroke:#407548,color:#183a1e;
    classDef decision fill:#fff2d8,stroke:#ad691b,color:#4d2a05;
    classDef reject fill:#f8e7e7,stroke:#a44747,color:#4d1c1c;
    class F,GRAY,BIN,ROI image;
    class OUTER,CORE,GEO,SCORE,EMA,CALS,REF,ERR,LOCK,MOVE,TX filter;
    class JUMP,CAL decision;
    class REJECT,TXOFF reject;
```

候选评分是“越低越优”：优先接近标定参考点，并以更高权重保持接近上一帧中心，同时惩罚非直角和对角线比例异常。外框角点用于排除背景矩形，最终 yaw 与标定使用黑芯矩形中心。

视觉输出状态含义：

| 字段 | 当前生成条件 |
|---|---|
| `locked` | 本帧通过外框、黑芯、几何、评分和跳变拒绝后仍存在目标 |
| `valid` | 页面处于运行状态、当前阶段允许输出、已经标定且目标锁定 |
| `aim_ready` | `valid` 成立，且 `abs(dx) <= 2 px`、`abs(dy) <= 2 px` |
| `move_epoch` | 仅在 C 板回报 `WAIT_TARGET_MOVE` 时检测；目标相对锚点移动至少 18 px 并连续确认 5 帧后递增一次 |

## 3. 竞赛任务状态机

```mermaid
stateDiagram-v2
    [*] --> IDLE: Task_Init
    IDLE --> TRACK_CENTER: task1 至 task5
    STOPPED --> TRACK_CENTER: 再次启动任务

    TRACK_CENTER --> TRACK_FINISH: task1 路线已离开中央靶位
    TRACK_CENTER --> AIM: task2 至 task5 到达中央并停稳

    AIM --> AIM: 条件失效则清零 300 ms 计时
    AIM --> LASER: 全部精瞄条件连续成立 300 ms
    LASER --> AIM: 任一精瞄条件失效则立即关激光
    LASER --> WAIT_MOVE: 3000 ms 照射完成且仍需下一枪
    LASER --> COMPLETE: 最后一枪完成且任务停在中央
    LASER --> TRACK_FINISH: 最后一枪完成且任务需要到 B

    WAIT_MOVE --> WAIT_MOVE: move_epoch 未改变
    WAIT_MOVE --> AIM: move_epoch 改变

    TRACK_FINISH --> TRACK_FINISH: task4 或 task5 丢失宽锁定则停车关激光
    TRACK_FINISH --> COMPLETE: 路线 FINISHED 且底盘停稳
    COMPLETE --> COMPLETE: active 保持，等待外部收尾

    IDLE --> STOPPED: Stop
    TRACK_CENTER --> STOPPED: Stop、切模式、急停或故障
    AIM --> STOPPED: Stop、切模式、急停或故障
    LASER --> STOPPED: Stop、切模式、急停或故障
    WAIT_MOVE --> STOPPED: Stop、切模式、急停或故障
    TRACK_FINISH --> STOPPED: Stop、切模式、急停或故障
    COMPLETE --> STOPPED: 外部 Stop 或切换模式
```

中央静止精瞄进入 `LASER_DWELL` 的完整条件为：

```text
motion_settled
&& laser_motion_safe
&& vision_fresh
&& vision_valid
&& target_locked
&& vision.aim_ready
&& abs(dx) <= 2 px
&& gimbal_ready
```

这些条件必须连续保持 300 ms。3000 ms 照射期间任一条件失效，C 板在下一个控制周期关激光并返回 `AIM_ACQUIRE`，稳定计时和照射计时均重新开始。

`AIM_ACQUIRE`、`WAIT_TARGET_MOVE` 以及 task4/task5 的返程等待均为 **fail-closed 且无超时**：条件不满足时保持激光关闭，或同时停止底盘，不会因等待过久自动跳到下一阶段。

### task4/task5 返程区别

返程不是中央靶位的静止精瞄。它使用更宽的锁定条件：

```text
vision_fresh
&& vision_valid
&& target_locked
&& gimbal_ready
```

返程不要求 `aim_ready`、`abs(dx) <= 2 px`、`motion_settled` 或 `laser_motion_safe`。宽锁定成立时允许底盘继续巡线，同时允许激光开启；丢锁则立即停止底盘并关闭激光，重新锁定后继续。刚完成第二枪进入返程的第一个控制周期会抑制一次返程激光，避免阶段切换瞬间误亮。

到达 B 后，路线进入 `FINISHED` 且底盘停稳即可进入 `COMPLETE`；终点视野丢锁不会再阻止 task4/task5 完成。`COMPLETE` 中任务输出仍保持 `active=1`，不会自行退回 `IDLE`，必须由外部 `Stop`、切换模式或启动新任务完成收尾。

## 4. A 到中央再到 B 的路线时序

```mermaid
flowchart LR
    A0["寻找 A 标志<br/>WAIT_START_MARKER"]
    A1["等待离开 A<br/>WAIT_START_EXIT"]
    C0["巡线到中央<br/>FOLLOW_TO_TARGET"]
    C1["中央越界<br/>TARGET_RUNOUT"]
    C2["中央停车等待<br/>TARGET_WAIT_SETTLE"]
    C3["离开中央<br/>TARGET_DEPART"]
    C4["等待中央标志退出<br/>WAIT_TARGET_EXIT"]
    B0["巡线到 B<br/>FOLLOW_TO_FINISH"]
    B1["B 点越界<br/>FINISH_RUNOUT"]
    B2["终点停车等待<br/>FINISH_WAIT_SETTLE"]
    DONE["路线完成并保持<br/>FINISHED"]

    A0 -->|"A 标志连续确认 40 ms"| A1
    A1 -->|"离开 A 连续确认 300 ms"| C0
    C0 -->|"中央标志连续确认 80 ms"| C1
    C1 -->|"前行 runout 500 ms"| C2
    C2 -->|"Task 发布 target_release"| C3
    C3 -->|"离开中央累计前行 1200 ms"| C4
    C4 -->|"中央标志退出连续确认 300 ms"| B0
    B0 -->|"B 标志连续确认 40 ms"| B1
    B1 -->|"前行 runout 500 ms"| B2
    B2 -->|"motion_settled"| DONE

    LEGACY["未启用 Task 的兼容路径<br/>中央静止照射 3000 ms"]
    C2 -.-> LEGACY
    LEGACY -.-> C3

    classDef route fill:#eaf2f7,stroke:#2e7188,color:#153842;
    classDef hold fill:#fff2d8,stroke:#ad691b,color:#4d2a05;
    classDef done fill:#edf5ea,stroke:#407548,color:#183a1e;
    class A0,A1,C0,C1,C3,C4,B0,B1 route;
    class C2,B2,LEGACY hold;
    class DONE done;
```

| 路线门槛 | 当前值 | 判定要点 |
|---|---:|---|
| A 标志确认 | 40 ms | 宽标志且保持前进 |
| 离开 A | 300 ms | 宽标志消失且保持前进 |
| 中央标志确认 | 80 ms | 中央标志且保持前进 |
| 中央 runout | 500 ms | 按实际前进时间累计 |
| 离开中央 | 1200 ms | `target_release` 后按实际前进时间累计 |
| 中央标志退出 | 300 ms | 中央标志消失且保持前进 |
| B 标志确认 | 40 ms | 宽标志且保持前进 |
| B 点 runout | 500 ms | 按实际前进时间累计 |

## 5. 视觉超时与 yaw 电流停止

```mermaid
flowchart TD
    VF["收到新的有效视觉帧"] --> TRACK["更新 yaw 目标<br/>vision tracking valid"]
    TRACK --> T150{"距最后有效帧<br/>是否超过 150 ms"}
    T150 -->|"否"| TRACK
    T150 -->|"是"| INVALID["视觉跟踪失效<br/>撤销 ready 并复位积分<br/>任务精瞄失败且激光关闭"]
    INVALID --> T1000{"距最后有效帧<br/>是否超过 1000 ms"}
    T1000 -->|"否"| HOLD["不再更新视觉目标<br/>尚未执行 yaw 电流停止"]
    T1000 -->|"是"| RELAX["yaw 切到 RELAX<br/>停止电流控制"]
    INVALID -->|"重新收到有效帧"| TRACK
    HOLD -->|"重新收到有效帧"| TRACK
    RELAX -->|"重新收到有效帧且反馈正常"| TRACK

    classDef active fill:#edf5ea,stroke:#407548,color:#183a1e;
    classDef wait fill:#fff2d8,stroke:#ad691b,color:#4d2a05;
    classDef stop fill:#f8e7e7,stroke:#a44747,color:#4d1c1c;
    class VF,TRACK active;
    class T150,T1000,HOLD wait;
    class INVALID,RELAX stop;
```

两个时间门槛含义不同：

| 门槛 | 作用 |
|---:|---|
| 150 ms | 视觉新鲜度或 yaw 跟踪有效性失效，任务瞄准条件立即失败，激光关闭；这不是 yaw 电流停止时刻 |
| 1000 ms | 自最后有效视觉帧起持续超时后，yaw 模式切到 `RELAX`，停止电流控制 |

任务侧的 `vision_fresh` 按“是否收到新视觉帧”计时；新帧即使声明 `valid=0` 也会刷新帧时间，但 `vision_valid` 会独立使瞄准或返程锁定失败。yaw 侧则按“最后一个有效视觉帧”计时。

## 6. 兼容旁路

```mermaid
flowchart LR
    CMD["USART6 模式或任务命令"]
    TASKS["task1 至 task5<br/>当前竞赛闭环"]
    MANUAL["vision<br/>STAND 下的手动激光请求"]
    OLD["aim、target、shoot<br/>旧 TARGET_AIM 模式"]
    TSTATE["Task 加 Mode_Tracking<br/>完整任务状态机"]
    BYPASS1["绕过 Task<br/>仅保留 STAND 手动兼容门"]
    BYPASS2["绕过 Task<br/>沿用 Mode_TargetAim 旧流程"]
    FINAL["C 板激光最终物理门控"]

    CMD --> TASKS --> TSTATE --> FINAL
    CMD --> MANUAL --> BYPASS1 --> FINAL
    CMD --> OLD --> BYPASS2 --> FINAL

    classDef main fill:#edf5ea,stroke:#407548,color:#183a1e;
    classDef compat fill:#fff2d8,stroke:#ad691b,color:#4d2a05;
    class TASKS,TSTATE main;
    class MANUAL,OLD,BYPASS1,BYPASS2 compat;
```

`vision` 和旧 `TARGET_AIM` 是为调试与旧操作流程保留的旁路，不参与 `task1~task5` 的 `Task` 阶段、射击计数、目标移动确认或任务完成判定，也不能用来说明竞赛闭环的安全时序。

## 7. task1 至 task5 行为

| 命令 | 中央射击 | 目标移动 | 最终位置 | 锁定返程 | 里程碑上报 |
|---|---:|---|---|---|---|
| `task1` | 0 次 | 无 | B | 否 | 否 |
| `task2` | 1 次 | 无 | B | 否 | 否 |
| `task3` | 2 次 | 第一枪后等待 `move_epoch` 改变 | 中央 | 否 | 否 |
| `task4` | 2 次 | 第一枪后等待 `move_epoch` 改变 | B | 是，宽锁定 | 否 |
| `task5` | 2 次 | 第一枪后等待 `move_epoch` 改变 | B | 是，宽锁定 | 是 |

task5 里程碑位：`0x01` 到达中央、`0x02` 第一枪完成、`0x04` 第二枪完成、`0x08` 锁定返程完成。

## 8. 协议与状态编号

视觉扩展帧：

```text
V,dx,dy,valid,locked,aim_ready,move_epoch\n
```

兼容旧视觉帧：

```text
dx,dy,valid\n
```

旧帧只将 `locked` 映射为 `valid`，并令 `aim_ready=0`、`move_epoch=0`，所以可供普通视觉跟踪兼容使用，不能直接通过竞赛任务的自动射击条件。

任务状态帧：

```text
T,task_id,phase,shot_index,aim_ready,laser,milestones,done\n
```

| phase | 状态 |
|---:|---|
| 0 | `IDLE` |
| 1 | `TRACK_TO_CENTER` |
| 2 | `AIM_ACQUIRE` |
| 3 | `LASER_DWELL` |
| 4 | `WAIT_TARGET_MOVE` |
| 5 | `TRACK_TO_FINISH` |
| 6 | `COMPLETE` |
| 7 | `STOPPED` |

## 9. 代码索引

- `25e.py`：灰度识别、外框黑芯几何筛选、评分、EMA、标定、目标状态和扩展视觉帧。
- `Device/vision/vision.c`：新旧视觉协议解析和一致快照发布。
- `Application/ins/ins_task.c`：USART6 分流、命令队列和任务状态回传。
- `Application/gimbal/gimbal_task.c`：任务输入汇总、视觉新鲜度、yaw CAN 控制和激光最终门控。
- `task/task_control.c`：竞赛任务状态机、射击计时、返程和完成判定。
- `task/task1.c` 至 `task/task5.c`：五个任务的行为参数。
- `mode/mode_tracking.c`：A、中央和 B 的巡线路线状态机。
- `mode/mode_control.c`：任务输出与底盘运动命令的汇合点。
- `mode/mode_target_aim.c`：旧 `TARGET_AIM` 兼容旁路。
