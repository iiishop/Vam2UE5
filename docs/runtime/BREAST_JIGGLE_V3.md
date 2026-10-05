# Breast Jiggle v3：移动参考系与三模态报告

日期：2026-09-28。分支：`feature/jiggle/breasts`。

## 1. HEAD 与范围

本轮开始核对的本地和远端 HEAD：`77b3266fe04e394885390de1cf0b08f7c3843355`。结束 HEAD 相同；本轮修改保留在工作区，未自动 commit/push。本文只报告工程事实，不作视觉验收。

保留 v2 几何校准、五语义节点、稀疏 coupling、惯量张量、四参数、Native GPU skinning 和自动资产构建。旧版几何/权重细节见 [v2 历史报告](../../Evidence/BreastCalibrationV2/implementation-report.md)。没有新增 Chaos Flesh、按压、组织碰撞或 Cloth。

## 2. Stop / Jump 的实际根因

旧 Stop 和 Stop rotation 一帧清零调试速度，输入本身就是速度阶跃。旧 Jump 瞬间赋予向上速度，并在 0.5 秒后停止，没有完整下降和有限时长落地。

旧 Solver 从五节点中去除了整体旋转，却没有去除整体平移。因此整体线性/离心载荷仍进入各节点弹簧。运动输入还把渲染帧差分加速度保持到整个子步区间，没有显式的速度突变动量接口。

现在默认运动平滑，Hard Stop 独立保留。没有提高 Support/Damping 最小值，也没有更改默认四参数。

## 3. Moving Frame Kinematics

`FVamMovingFrameSample` 提供最终胸廓 Transform，以及可选的已知世界线速度/角速度。正式组件在 `FinalizeBoneTransform` 读取最终胸廓，涵盖动画、IK、刚体 blend、Actor/Root Motion 的结果。Solver 不读取调试命令或跳跃状态。

Transform 路径使用最多五个实际采样位置，因果局部多项式求端点速度；相对坐标计算减小远离原点时的精度损失。角速度取最短四元数差的世界旋转向量，再用相邻区间估计端点值。位置和端点速度用三次 Hermite 重建子步速度，旋转用 Slerp。没有加速度低通延迟。

保留 120 Hz 固定步长、accumulator、max substeps 和显式丢步统计。每个物理步从保存的世界 frame twist 计算增量，转换到该子步胸廓坐标。连续旋转的离心与 Coriolis 项仍在子步中求解。

移动参考系数学参考：[MIT Dynamics — Rotating Axes](https://ocw.mit.edu/courses/16-07-dynamics-fall-2009/resources/mit16_07f09_lec08/)。

## 4. 速度阶跃与动量连续

在相同参考姿态上，速度事件施加：

```text
v_COM_relative -= ΔV_local + Δω_local × (rest_COM + COM_displacement)
ω_relative     -= Δω_local
```

变形后力臂对应的 `-Δω × deformation` 也被分解到 COM、角状态和 residual。小角度模态基底下，重建节点世界速度在事件前后连续，已有直接测试。

生产 `AdvanceFrame` 路径施加速度差后，将本步 a/α 力输入置零，不重复注入冲量。直接 `Step` 的数值接口把指定 a/α 乘 H，交给同一个速度事件接口。后续仍正常计算重力、支承、离心和 Coriolis；恒定线速度不会作为持续后拖力。

只给离散 Transform 时无法知道帧内真实阶跃的精确时刻。高阶端点估计可能在噪声或不连续输入附近过冲。已知物理 twist 的 C++ 调用方可用 `AdvanceFrame`，即时速度事件可用 `ApplyFrameVelocityChange`；这两者不依赖调试运动。

## 5. COM Translation Mode

每侧状态包含 COM 位移/速度、整体角位移/角速度，以及五节点 residual 位移/速度。最终 helper 平移为：

```text
rest + COM displacement + θ × (rest - COM) + residual
```

这是明确的小角度位置基底，helper 朝向由角状态生成。质量分析使用校准的 node mass centers，不把蒙皮控制点直接当质量中心。

COM 质量为整侧质量；支承为各节点支承刚度之和；阻尼比和正负 travel 为质量占比加权值。这些字段保存到 schema 3 Profile，并随 Shape 和实例 tuning 派生。没有增加普通 UI 参数。

## 6. 三种模态与持续旋转

从节点外载先提取总力，再用点质量惯量矩阵提取刚体力矩，剩余差异载荷才交给 residual。整体角模态使用 v2 完整对称体积惯量，包括节点 intrinsic moment。Coriolis 的平移、旋转和 residual 交叉项分别进入对应模态。

每步投影 residual 位移和速度，使其满足：

```text
Σ m_i u_i ≈ 0
Σ m_i r_i × u_i ≈ 0
```

COM 和角状态各使用 3×3 隐式求解，residual 使用 15×15 隐式支承、阻尼、Coriolis 与八边 graph Laplacian。非线性刚度按当前状态计算并在本步冻结，是半隐式非线性、隐式线性项方法，不是完整 Newton 求解。

连续软限位增刚保留。极端越界仍有明确的 emergency limit；residual 使用共同缩放以保持两个零模态约束，修正次数显示在 Diagnostics。

持续旋转的总离心力现在驱动 COM，内部网络主要承接差异载荷。它仍允许形变，没有承诺所有胸型和极端输入都只作整体运动。

## 7. Shape、Teleport、Pause、多实例

Shape 沿用预计算响应和 Calibration，更新体积、COM、节点质量中心、支承、惯量及 COM 参数。helper 索引和共享 Skeleton 不变。小变化保留状态并重新投影 residual，大变化按既有阈值重置。编辑清除运动历史，不作为胸廓加速度。

TeleportRevision、超出 Profile 的位姿阈值或过长停顿按 reset/preserve policy 重建历史并清除速度。暂停冻结状态，恢复重新采样。COM、角状态、节点、采样历史、accumulator 和调试轨迹全部实例私有；Profile 为不可变输入。

## 8. Debug 轨迹与参数

| 命令 | 行为 |
|---|---|
| Smooth Forward Accelerate | 1 秒平滑到当前前向 150 cm/s，随后匀速 |
| Smooth Stop | 0.4 秒平滑停止线性调试运动 |
| Hard Stop | 立即清零线性调试速度，不重置组织状态 |
| Smooth Rotate Start / Continuous Rotate | 0.6 秒到世界竖直轴 1.5 rad/s，随后持续旋转 |
| Smooth Rotate Stop | 0.4 秒平滑停止旋转 |
| Hard Rotate Stop | 立即清零调试角速度，不重置组织状态 |
| Jump | 0.25 秒平滑起跳，完整 ballistic 段，0.25 秒有限时长落地 |
| Reset | 停止全部调试运动并清空 Jiggle；不将 Actor 移回原点 |

速度 ramp 是五次多项式，衔接当前 v/a/jerk，末端 a/jerk 为零。Jump 起跳加速度用 sin² 和 smoothstep 组合，与 ballistic 的 -g 连续衔接；最高点自然经过 v=0，没有特殊分支。落地用同一速度 ramp，解析计算开始高度，使人物回到起跳高度且末端 v/a/jerk 为零。落地自动包含在 Jump 中，没有独立 Landing 按钮。

Solver 内没有 takeoff/apex/landing 状态机。组织最高点晚于身体最高点是某些参数下可产生的行为，不是每次都强制出现的关键帧。

四参数默认 1；范围保持 v2：Support 0.1–10，Damping 0.1–4，Mobility 0.25–3，Internal Coupling 0–4；Advanced Mass Scale 0.1–10，仅改变质量和惯量。

## 9. 构建、资产与工程结果

自动流程继续为 family 分析 → append helpers → 连续权重 → Calibration/Shape 响应 → Profile → RuntimeConfiguration → BP → 独立进程重载验证 → committed。Profile 算法为 `breast-modal-v3`，配方身份为 `runtime-bundle-v6-breast-modal`。

原骨索引 0–87 保留；左 Anchor 88、五节点 89–93；右 Anchor 94、五节点 95–99。Core / Upper / Lower / Medial / Lateral 是各侧 Anchor 子骨。零 offset bind pose 和 Morph 的构建验证保留。

v2 Profile 可由新版 Runtime 在实例上派生 COM 参数；正式升级生成新的 schema 3 output，不覆盖旧 BP 或来源资产。v1 保留旧 Solver，应使用 Upgrade Runtime 升级。

升级第二份人物时发现并修复了通用提取问题：保留在 source domain、但没有 vertex instance 的顶点，其 UV 原先用 `SetNum` 分配而未初始化。本轮改为确定性零初始化，实际被引用顶点继续使用来源 UV，顶点对应关系保持不变。新增 `OrphanVertexExtraction` 回归会先填入 NaN，确保不会靠分配器偶然给零而通过。

最终人物 A（Native 来源自动构建）：

```text
/Game/VamRuntime/R_4c802e30dff86c7bdcc9d128/BP_VamCharacter
/Game/VamRuntime/R_4c802e30dff86c7bdcc9d128/DA_BreastJiggle
/Game/VamRuntime/R_4c802e30dff86c7bdcc9d128/RC_Runtime
```

最终人物 B（由旧 committed BP 走正式升级入口）：

```text
/Game/VamRuntime/R_14e3fa311bf786b6946b7886/BP_VamCharacter
/Game/VamRuntime/R_14e3fa311bf786b6946b7886/DA_BreastJiggle
/Game/VamRuntime/R_14e3fa311bf786b6946b7886/RC_Runtime
```

两份均为 schema 3、独立进程保存/重载/提交验证；算法文件指纹逐项与最终工作区相符。A 左/右 effective volume 为 585.3916 / 583.4258 cm³，质量 0.59710 / 0.59509 kg；B 为 189.5732 / 189.1602 cm³，质量 0.19336 / 0.19294 kg。它们是不同胸型的几何校准结果，没有人物特例。

验证结果：

- Editor Development、Game Development、Game Shipping 编译成功。
- 两份最终人物各运行 7 项 UE 测试，全部 Success；安装后的 SmartNPC 项目也运行同样 7 项，全部 Success。
- Python 配方/构建等回归 46 项通过。
- helper 层级、原骨索引、权重 normalization / 8 influences、零 offset bind reconstruction、Morph、Shape 身份、双实例、Spawn/Destroy、Teleport、Pause 均保留验证。
- 最终插件 DLL 和模块清单已安装，SHA-256 与构建产物一致。

7 项 UE 测试分别是 `Calibration`、`Dynamics`（v1 兼容）、`MovingFrameModal`、`NativeRuntime`、`OrphanVertexExtraction`、`SmoothJump`、`WeightCompression`，均在 `Vam.Breast` 下。

| 数值检查 | 最终结果 |
|---|---|
| 已知 frame twist 阶跃，重建节点世界速度误差 | 3.69×10⁻¹⁵ cm/s |
| 持续旋转 residual 质量均值 / 角模态残量最大值 | 约 1.40×10⁻¹⁷ / 2.26×10⁻¹⁷ |
| 低 Support/Damping 旋转：COM / residual RMS | 0.86375 / 0.07734 cm |
| 综合平移旋转轨迹，30/120 与 60/120 | 0.00490 / 0.00189 cm；角度 30/120 为 0.000378 rad |
| Jump 已知 twist，30/120 与 60/120 | 0.00871 / 0.00008 cm |
| Jump 实际 Transform 采样路径，30/120 与 60/120 | 0.21472 / 0.01785 cm |
| 独立相位例：身体 / 组织最高采样时刻 | 0.50833 / 0.55000 s，延后 0.04167 s |
| 上述身体最高点时组织世界速度 / 硬限位次数 | 向上 40.58 cm/s / 0 次 |

相位例使用 Support=1、Damping=0.1、Mobility=3、MassScale=1。FPS 压力例使用 Support=0.1、Damping=0.1、MassScale=3，其他默认。这些是工程测试设置，不是默认效果建议。相位测试同时要求延迟超过一个完整物理步、身体最高点时组织仍有向上世界速度、没有硬限位介入，避免把采样误差当成相位证据。

测试中的临时 UWorld 清理仍有既有的 missing EndPlay 警告；所有测试状态 Success，无失败断言。完整日志、资产审计、事务收据、安装哈希和摘要位于 [Evidence/BreastModalV3](../../Evidence/BreastModalV3/summary.json)。

最终 Windows cook 成功：832 个 package，0 errors、0 warnings。两份普通 BP 的 cook 夹具分别为 `/Game/BreastJiggleEngineering/Empty_4c802e30dff8` 和 `/Game/BreastJiggleEngineering/Empty_14e3fa311bf7`；这些地图只是工程检查，不是 Runtime 依赖。完整 cook 日志已归档到 evidence。

## 10. 在 Empty Level 中体验

1. 重开 UE，创建 Empty Level，拖入第 9 节新目录的 BP_VamCharacter，使用 Simulate。
2. 选中运行实例；Window → VaM 人物调试 → **使用选中人物 / 刷新**。
3. 如衣服遮挡，点击面板上方 **隐藏衣服 / 配饰**；再次点击恢复。
4. 展开 **Breast Jiggle · Runtime**，先保持四参数 1。
5. Smooth Forward Accelerate → 等待匀速 → Smooth Stop；Reset 后重复，改用 Hard Stop。
6. Continuous Rotate → 等待稳定 → Smooth Rotate Stop；Reset 后重复，改用 Hard Rotate Stop。
7. Reset 后点 Jump，观察完整起跳、上升、最高点、下降、落地和余振。
8. 如需压力观察，再降低 Support/Damping，同时查看 COM 和 residual。

面板开头 Enabled 负责启停，Show helper bones / region weights / dynamic node state 显示辅助骨、区域和动态偏移。下方 Diagnostics 显示 COM 位移/速度、角位移/速度、节点 residual、胸廓 v/a/ω/α、离心与模态载荷、子步/丢步/sleep/emergency limits，以及体积、质量、惯量和校准数据。力单位 kg·cm/s²，力矩 kg·cm²/s²，不能直接比较两者数值。模态载荷幅值显示连续力项，参考系速度事件的冲量另由 frame 运动量及状态变化反映。

不需要指定地图、Level Blueprint 或 Solver Actor。调试命令只移动当前实例，正式游戏继续使用其正常角色运动管线。

## 11. 主要文件

- Runtime：`VamBreastSolver.h/.cpp`、`VamBreastCalibratedSolver.cpp`、`VamBreastCalibration.h/.cpp`、`VamBreastJiggleProfile.h`。
- 最终 pose、Shape、诊断：`VamBreastSkeletalMeshComponent.h/.cpp`。
- 新增解析运动库：`VamBreastDebugTrajectory.h/.cpp`。
- Editor：`VamDebugPanel.cpp`、`VamBreastJiggleBuilder.cpp`。
- 自动测试：`VamBreastCalibrationTests.cpp`、`VamBreastNativeTests.cpp`。
- 构建身份/审计：`vam_runtime_recipe.py`、对应测试、`ue_breast_jiggle_audit.py`。

## 12. 技术限制

这是有限维、小角度、effective-volume 的 secondary dynamics。大角度、极低支承、大 Mobility 时，小角度位置基底与有限刚体转动会有差异，极端输入可能触发已显示的硬限位。

Transform 采样存在信息限制及高阶微分对噪声的敏感性；已知 twist 通常更精确。超过可接受时间跨度按 reset policy 处理，不无限追赶。

本轮验证两份已持久化 Native 来源的构建/正式升级链，没有重新解码原始 VAR。Cook 不代表独立打包游戏的性能或视觉验收。衣服仍不模拟；其显隐帮助观察皮肤。复杂动画噪声、更广泛胸型、区域边缘、参数手感和最终外观仍需人工观察。

## Breast output amplitude (2026-09-29)

`BreastAmplitude` is a per-instance final helper-output multiplier, default 2, range 0–10. A value of 1 preserves the previous output; 0 suppresses visible secondary displacement/rotation. It multiplies final helper translation offsets and small-angle rotations after solver integration, without changing mass, support, damping, mobility or shared profile data. Debug node positions use the amplified output. Solver diagnostics remain physical unamplified state and include the amplitude value. Reset tuning restores amplitude 2 and existing other controls to 1. This artistic output multiplier can exceed solver-space displacement limits, like the Hip output amplitude.

Panel: Breast Jiggle · Runtime → Amplitude · 胸部强度（1 = 原效果）. Existing runtime characters pick up default 2 when their runtime component is recreated; no mesh rebuild is needed for this control.

The Breast branch was fast-forwarded to local master `2096876` to retain completed Hip/Leg runtime classes. Cleanup retained `/Game/VamRuntime/R_9345f3209950f77911bda5eb/BP_VamCharacter` and moved 20 other generated bundles outside Content to `Saved/VamRuntimeCleanup/20260929-211105` with a manifest. Imported source characters were retained.
