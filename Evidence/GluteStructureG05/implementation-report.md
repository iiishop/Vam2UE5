# G0.5 实施报告

## 1. HEAD 与交付状态

分支 `feature/jiggle/hip`。开始及结束 HEAD：
`6f159ffed33029f623db8ed9a4141af71a471c21`。
本轮修改保留在工作区，未自动 commit/push。已编译并安装 Editor 插件；安装前文件备份和 SHA-256 对比见 `install.json`。

## 2. 新增模块与数据

- `VamHipPoseState.h`：可序列化、可读取的主骨架姿态快照。
- `VamGlutePoseRefinement.cpp`：确定性姿态细化函数与区域系数校准。
- Profile schema 2 / refinement 1：股骨轴、参考 pelvis、区域响应与 FoldSemanticMap。
- Runtime 输出：区域位置、方向、双支承、被动张力、分轴支撑基线和 FinalRestCOM。
- Glute Structural Debug、左右独立姿态选择，以及新增工程测试。

保留 G0 region、helper、权重构建机制；Breast V3 求解器未修改。

## 3. HipPoseState

在任何 Glute helper 写入之前，读取最终 primary pelvis、左/右 femur component pose。
保存 femur 相对 pelvis/anchor 的变换、相对四元数、髋屈伸/内外展/内外旋、pelvis tilt/yaw/roll 和 Shape revision。
沿来源 thigh→shin 轴分离 twist，再用 swing 的解剖坐标分量表示屈伸/外展。
正值分别表示屈曲、外展、外旋；显示使用度，数据使用弧度。
Pelvis 角度以导入解剖参考 frame 为基准，不是 Actor 世界朝向。

## 4. Structural refinement 数学模型

保留 G0 双支承 fiber scaffold 和 determinant=1 的局部轴向/横向缩放。
从髋姿态生成连续、有界的 flexion/extension/abduction/rotation 特征，结合每个区域保存的系数，更新位置、方向和支承份额。
位移按人物 AP/ML/SI 尺寸缩放，用连续 tanh 限幅；后方投影使用 pelvis 局部 frame 中的平滑下界；方向用旋转向量平滑限幅。
函数不接收 DeltaTime，也不读取前一帧结构状态。

## 5. 各区域职责

| 区域 | 主要响应 |
|---|---|
| Core | 保留后方主体投影，屈髋提高区域支撑，有限跟随股骨。 |
| Upper | 更强 pelvis tether，较小向下位移和旋转范围。 |
| Lower | 更明显的股骨跟随，屈伸改变臀腿连接，但有界限和持续支承。 |
| Medial | 最强的骶侧约束，较小横向位移和旋转。 |
| Lateral | 外展与内外旋的区域响应、股骨/筋膜方向代理。 |

左右使用各自几何与姿态，不强制相同尺寸或相同响应。系数是通用语义工程策略，按来源 attachment/geometry 校准，没有人物 ID 或 preset 特例。

## 6. PassivePoseTension

组合正向 fiber 对数拉伸的平方与姿态路径的被动应变代理。
每个区域分别调整 pelvis/thigh 权重并归一化，输出 AP/ML/SI 三轴支撑基线。
不推断主动肌肉收缩，不使用全臀一个 scalar，也没有动态弹簧求解。

## 7. GluteFoldSemantic

从 Lower/Medial/Lateral 区域位置建立 medial infragluteal anchor、middle transition、lateral fade。
根据区域 attachment 校准增益；输出三处 activation 和 stretch。
伸髋、外展、旋转可改变 activation，屈髋改变 stretch。
本轮不通过这些输出修改 mesh、Morph 或骨骼褶皱。

## 8. 弯腰时结构职责

Upper/Core 的 pelvis 支承和后方投影下界、Medial tether、Lower 股骨连接、局部体积缩放与有限方向变化共同定义 FinalRestGlute。
未来 G1 应读取该姿态下的 rest/support，而不是站立时的固定 rest。
这些数学约束不等于精确全网格体积守恒或视觉形状保证，最终轮廓仍需人工观察。

## 9. 自动生成与资产

“生成 UE5 人物资产”仍然是原流程，Glute builder 自动额外生成股骨轴、姿态区域系数和 Fold 数据。
版本 identity 更新为 `runtime-bundle-v8-glute-pose-refinement`。
旧人物用正式 Upgrade Runtime 生成新 committed output，不原地覆盖。

| 项目 | 第一个人物 | 第二个人物（正式升级） |
|---|---|---|
| 根目录 | `/Game/VamRuntime/R_396242bf2a0053ff47f744ed` | `/Game/VamRuntime/R_186a115985471a688e407bc5` |
| Blueprint | 根目录下 `BP_VamCharacter` | 根目录下 `BP_VamCharacter` |
| Profile | 根目录下 `DA_GluteStructure` | 根目录下 `DA_GluteStructure` |
| 最终身体 | 根目录下 `Glute/SK_Body` | 根目录下 `Glute/SK_Body` |

每个 Skeleton 仍是 88 个来源骨 + 12 个 Breast helper + 12 个 Glute helper，共 112 个；G0.5 未增加骨骼。
每侧 `GluteAnchor` 下仍是 Core/Upper/Lower/Medial/Lateral。

## 10. 普通 Empty Level 观察方法

1. 打开一个新的 Empty Level，将上表任意新 BP 拖入，Play/Simulate。
2. 在现有 VaM 人物调试面板选择运行中的人物，展开 Breast 控件下的 **Glute Structural Debug - G0.5**。
3. 用 Target both/left/right 选择作用侧；Neutral standing 清除选中侧调试 offset，Reset 清除两侧。
4. Hip flexion 请求 70°；Hip extension −20°；Abduction 30°；Adduction −20°；External/Internal rotation ±30°。实际姿态受正常 Joint Constraints 约束。
5. Enabled 对比结构层开关；Show Glute Region 看来源区域；Show Structural Bones 看 helper；Show Pelvis/Thigh Attachments 看双支承；Show Pose Tension 看区域张力；Show Fold Semantics 看预留的三个紫色标记点。
6. Diagnostics 显示髋角度、pelvis 姿态、Shape revision、体积/COM/尺寸、区域偏移和方向、双支承权重、张力、分轴支撑与 Fold 状态。
7. 弯腰可使用现有 Pose 控件或 pelvis/femur 动画。调试按钮只是髋姿态入口，不生成完整平衡弯腰动作；已有 Base Animation/Pose 仍会参与。
8. 衣服遮挡时使用已有衣物显示开关。无须删除衣服或使用工程测试地图。

## 11. 工程验证

- Editor Development、Game Development、Game Shipping 编译通过。
- 两个人物分别 10/10 测试成功：3 个 Glute 测试和 7 个 Breast 回归测试。
- 安装到实际 SmartNPC 项目后再次 10/10 成功。
- 旧 G0 profile 的 NativeRuntime 兼容测试成功。
- 新资产独立进程 save/reload/verify 全部完成。
- 原索引、helper hierarchy、8-influence、归一化、zero bind、Morph、实际网格有限性检查通过。
- primary pose 输入隔离、helper 反馈隔离、单侧独立、两实例隔离、Shape 连续扫掠与 helper identity 检查通过。
- 新 pose 测试覆盖 neutral、三轴和组合姿态 sweep、左右镜像、后方下界、局部 determinant、连续性、HipPoseState 序列化及 30/60/120 求值一致性。
- 第一人物有效体积代理：190.883846 → 300.346523 cm³（1.573452 倍）。第二人物：198.088757 → 429.423848 cm³（2.167836 倍）。这些是受支持 Shape 参数生成的结构测试变体，不是医学体积。
- 两个 NativeRuntime 测试各有一次既有 World teardown 的缺少 EndPlay 警告；测试没有失败。未将警告描述为零。
- Windows Cook 通过：834 个 package，0 error、0 warning；结果记录在同目录 `cook-result.json` 和 `cook.log`。

机器可读证据见 `summary.json`，人物 profile/attachment/系数明细见各人物 `audit.json`；测试原始结果见 `tests.json`。

## 12. 当前限制

只支持已有 VamFemale88 family。姿态角是工程 swing/twist 坐标，不是临床角度标准；180° swing 奇点不属于支持范围。
Shape 使用既有一阶几何响应，区域 determinant=1 不保证混合蒙皮后的精确全局体积。
区域系数不是实验测量的肌肉材料参数。没有主动收缩、最终 fold/wrinkle、衣物形变、碰撞、坐压或 G1/G2。
姿态轮廓、区域边缘和人体结构表现由用户人工验收。本轮没有进行视觉验收，也没有继续实现 Glute Jiggle。
