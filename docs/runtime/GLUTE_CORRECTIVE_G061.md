# G0.6.1 — Glute Corrective Fidelity / Attenuation Audit

基线为 `feature/jiggle/hip` 的 `6c52542bfb2634d36e157510b4fa8c6319724016`。保留 G0/G0.5、HipPoseState、pose targets、Native Morph/GPU skinning 和 Breast V3。本轮工程记录见 `Evidence/GluteCorrectiveG061/`；最终数字见[完整实施报告](../../Evidence/GluteCorrectiveG061/implementation-report.md)。

## 旧管线实测根因

先增加统计、保持旧几何代码不变，重新构建 Primary。原 `!Bend Fix` 266 个非零 delta 的长度统计（cm）为 P50=0.417376、P90=1.607933、P95=1.769126、Max=2.074286、RMS=0.886164。

这是原始 source bank 的全部 266 个点，不能直接当成最终可见臀部的统计。旧测量把未被三角形引用的保留顶点计入，造成 retained-input 大幅度掩盖真实 surface 小幅度。

只计算真实三角形表面、按 source ID 去除 UV alias 重复后，旧 Flex90 来源分支的逐步 RMS 为：

| 步骤 | 左 cm | 右 cm |
|---|---:|---:|
| Raw source × 0.9，当前表面区域 | 0.088773 | 0.093535 |
| Shape adaptation | 0.081072 | 0.087020 |
| Global worst-case Safety | 0.014290 | 0.017599 |
| Region / medial gate / limit | 0.001637 | 0.005456 |
| 四轮重复 mask 平滑 | 0.000089 | 0.000226 |

因此来源路径最终只保留适配后表面 RMS 的约 0.11% / 0.26%。三个阶段都有显著损失；平滑阶段的相对损失最大，global Safety 是首次大幅衰减。旧 source+procedural 合计表面 P95 为左 0.022899 cm、右 0.058188 cm。

另外确认了 topology recovery 缺口：bank delta 属于 target mesh，来源人物使用 DAZMergedMesh Boundary graft。旧 G0.6 跳过了现有 Shape 管线已经使用的 `apply_graft_boundary`，没有把 target 的变化传给 graft。现在对中性基线和加 Morph 基线分别重放同一 Boundary 算子，再相减得到 merged-domain delta。Primary 从 266 条 bank delta 得到 1,664 条合并域非零 delta。实际可见数量由 Native 三角形统计，不能用来源 polygons 的存在冒充 Native render visibility。

## 两条路径

`VerifiedSourceCorrective`：验证 driver/hash → 原始 bank delta → Boundary topology transfer → current Shape 尺寸适配 → source pose equation → 连续左右分区 → 必要的目标局部 Safety。

来源路径不再经过 procedural 的 medial fade、Region 幅度乘法、tanh limit 或 topology diffusion。左右分区使用两个已有 G0 region 的归一化支承，只有接近共同区域边界才平滑衰减；这允许 medial/under-curve 保留局部曲率。

`ProceduralResidual`：family geometry/response moments → 连续区域约束 → 尺寸限幅 → 独立 topology diffusion → 目标局部 Safety。存在可靠来源的地方，程序 residual 连续淡出，让来源贡献可单独量化。

最终相加后还要做组合姿态几何检查。G0.5 的 helper weights、ThighFollow、ProjectionRetention、structural offsets 均未放大。

## 来源 reference 的含义

来源 driver 使用两个 thigh 来源 X 角的平均值，0…−100° 映射 0…1。双侧屈髋 30/60/90/100° 分别对应 0.3/0.6/0.9/1.0。单侧 90°、另一侧 0° 的原始平均驱动是 0.45。

来源参考重放读取到的 DAZSkinV2 thigh 节点顺序、各顶点 X 权重、fully weighted rows、bulge 权重和 bulgeScale，在当前 p0 骨架坐标中执行两个孤立 thigh-X 旋转的 CPU 方程。其它局部角为零。它包括 graft 域的同名 thigh 节点，不以一个平均皮肤权重替代 TriAx 方程。

该 reference 是来源方程重放，未包含 VaM post-skin smoothing、物理、接触，也不是捕获的 VaM 渲染截图或运行时顶点真值。组合外展/旋转目标仍属于项目的姿态扩展；当前来源 benchmark 限定 bilateral flexion。

分别报告：来源 corrective 增量、UE corrective 增量、两增量之差、G0.5 baseline 与来源 baseline 之差、最终绝对表面误差，以及原 Native LBS 与来源 baseline 的差。保留 G0.5 与现有 Native LBS 时，恢复来源 Morph 幅度不会自动消除此前的骨架/蒙皮基线误差。

## Target-aware safety

不再计算一个 `SafetyGlobal[vertex]`。先在每个 target 的实际 corrective 上检查三角形法向翻转、面积放大、局部边长差和 skin matrix 条件。对失败三角形用二分求安全局部幅度，并统一 source aliases。

再在已有 targets、组合姿态和有界 extreme probes 中验证混合后的实际表面。失败只作用于该姿态中有实质权重的 targets 及失败局部顶点，按该顶点上 target 的实际位移贡献分配衰减。局部修正后，低混合权重 target 可能成为主要剩余贡献，因此不能仅用 blend weight 阈值筛选；迭代有明确收敛检查和上限，无关 target 不继承同一个 worst-case mask。

构建期加入 5% 的法向/面积余量，并按 NativeBuilder 的 16 位骨权重处理进行 conditioning。维持原 5 倍三角形面积界限、3 倍局部边差界限及 .01 skin determinant 条件，没有为通过测试放宽。来源只受最终必要几何安全修正；每个 target 都保存来源/程序 Safety 前后统计。

## Smoothing

程序场先乘一次 amplitude mask。扩散每轮只混合邻域，不再次乘 Region×Safety。边界统一到 source alias 域，结束时施加一次 boundary。

随后通过受边界约束的矩投影恢复输入的加权向量均值与 RMS：输出写作 `a × zero-mean field + boundary × mean term`，由一个二次方程解出能量系数。无可行解时保留原始场；不以重复衰减代替平滑。统计与求解都在去除 UV 重复后的真实表面域进行。Verified source 完全跳过程序平滑。

## Family fallback calibration

`VamFemale88.json` 保存由 verified source equation 提取的 family reference moments、分位数及来源哈希，没有 Character ID 或 source vertex delta。各 target、各区域分别保留 mean、axis RMS、P50/P90/P95、under-curve 和 silhouette proxy 统计。

无来源人物根据当前 AP/ML/SI dimensions、effective volume、support area、regional thigh attachment 和局部几何重建连续的区域矩场，再用平滑 semantic kernels 匹配区域 axis RMS。没有给旧 fallback 乘一个经验强度常数。Flexion 使用对应来源 target moments；没有 Bend Fix 激活的其它姿态使用 Flex60 归一化矩尺度与现有 pose-space 方向语义，明确属于程序估计。

只有通过来源验证的 profile 才能产出 source reference；无来源人物消费固定 family 数据，不会反过来更新共享参考。新人物、升级人物均经同一生成链。

## Runtime 与 Debug

Profile schema 2，算法 `glute-corrective-g061-v1`；旧 schema 1 保持可加载。C++ 类默认值保留 schema 1 / G0.6，以兼容省略默认属性的旧资产；Builder 对新资产显式写入 schema 2 / G0.6.1，经独立进程重载验证。Runtime bundle identity 为 `runtime-bundle-v10-glute-fidelity`。正式 target interpolation 仍是 0…1，精确 target 为 one-hot 1.0；没有新增 CorrectiveStrength 或超范围权重补偿。已有 Shape 各轴尺寸适配仍独立作用于 basis 权重。

人物面板 **Glute Structural Debug - G0.5 / G0.6.1** 中增加 Corrective Diagnostics，可选择 target 和 Raw Source / Source Adapted / Procedural / Final 稀疏向量。数据显示真实 primary hip 角、active target weights、来源/程序 RMS、raw/adapted/final P95、source retention、target Safety loss、smoothing RMS loss 和 affected surface count。

这些 diagnostics 是所选 target 在构建 Shape 的离线真实表面统计，不是当前任意 blended pose 或 Shape Preview 的实时测量。向量以 Anchor 局部参考位置绘制，不是当前完整网格 heatmap。没有逐帧 CPU 全身蒙皮。

## 工程验证

新增 SurfaceTruth 测试从 Native Morph LOD buffer 取实际 position delta，直接在最终 LOD 的顶点、index buffer 和 section bone-map/量化权重域重建 `X_corrected − X_base`。render-to-input 仅用于来源语义对应与 retained Morph 一致性检查；不会给被 UE 合并而没有独立 render index 的输入顶点填零。逐区域记录完整分位数、Max/RMS；对 neutral、各 target、组合姿态和 extreme probes（单侧及双侧） 检查有限性、翻转、面积、矩阵条件和 flexion 趋势。还检查完整混合 delta 的接缝一致性与局部边差、diffusion 均值/RMS、来源保留率、fallback family response 数量级。

保留已有 Morph、Shape Preview/Commit、两实例、30/60/120 deterministic、save/reload 和 Breast 回归。最终 build/test/Cook 数量与资产路径记录在实施报告中。所有结果只用于工程判断，视觉由用户人工比较。

接缝验证区分 retained 与 render 域：retained 完整 delta 的 source aliases 需在 1e-6 cm 内一致；每个 render basis 仍按 NativeBuilder 的每分量 1e-4 cm 裁剪误差界验证，再传播实际测得的裁剪误差检查最终 seam。该区分不放宽翻转、面积或尖刺阈值。

后续 G0.6.2 的独立蒙皮收缩补偿与当前姿态快照见 [GLUTE_CORRECTIVE_G062.md](GLUTE_CORRECTIVE_G062.md)。原来源幅度验证保留。
