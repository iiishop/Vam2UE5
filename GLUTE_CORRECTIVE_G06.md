# G0.6 — Pose-space Glute Corrective Geometry

## 1. 基线与范围

基于 `feature/jiggle/hip` 最新本地/远端 HEAD
`74d7dcd1e974a20ff4685652c381cc192625ad9f` 实现。保留 G0 region、双支承、G0.5 scaffold、HipPoseState、Shape/Morph 和 Breast V3。
本轮为确定性的原生 Morph 姿态修形，没有新增 Jiggle、速度/惯性、阻尼、Chaos、接触、坐压或衣物系统。

代码和证据保留在工作区，未自动提交或推送。最终编译、测试、资产路径与 Cook 结果见 `Evidence/GluteCorrectiveG06/`。

## 2. 实际来源调查

读取当前人物的持久化 Source IR / Native Contract、安装来源的 `a_per` 和 `f_mb`，并核对来源 SHA-256 与 IR 的 source_hashes。
通过对象引用解析和本地已安装程序集的方法检查，确认以下链：

`SetDAZMorphFromAverageBoneAngle`
→ `lThigh`、`rThigh` 的来源 X 轴角度
→ 两侧平均值、0 到 −100° 的有界映射
→ `!Bend Fix`
→ `DAZMorphSubBank` 中的 266 个真实 source vertex delta。

该数据的 UI 名称是 Glute Bend Fix。它自身的 formulas 为空，实际驱动来自独立组件；因此不能仅查 Morph 名称或 formulas 数组就断言没有 corrective。
来源还存在 Hip Bend Smoother（206 delta）、Glute Bend Helper（453 delta），但未确认相应可靠的 primary-pose 自动驱动链，不自动启用。
LG/RG UpDown2、Side2Side 的驱动读取 LGlute/RGlute，自身会受 secondary deformation 影响，故不作为本阶段主姿态驱动。
涉及生殖/肛周的其它平均角度驱动不属于本阶段臀部修形范围。

现有 Source IR 保留 TriAx 单轴权重、bulge 参数和 Morph formulas。现有校准是单关节 CPU 方程参考的 LBS 拟合，不是完整 VaM 复合姿态/自动修形重放；本阶段不重复叠加整套 TriAx bulge。
BoneCenter 公式原有执行能力保持，其它公式不被伪称已完整执行。

## 3. 来源复用合同

新 `vam_glute_corrective_source.py` 必须同时验证来源哈希、driver 的骨骼引用/轴/范围、唯一 target Morph 及实际 delta 数组。
新解码将该证据链保存在 immutable Source IR；已有资产可通过 Resource Browser 配置的来源目录恢复，升级也可以复用已保存且与 IR 哈希一致的证据。
不可用、哈希不匹配或链不明确时，明确记录 procedural fallback。

复用的是来源几何，不照搬整个 VaM 自动行为：delta 经连续 Glute region/medial tether 筛选、人物尺寸适配和几何保护，再进入左右独立的 pose targets。
来源的双腿平均驱动被适配为本项目 HipPoseState/pose-space 语义，因此不宣称与 VaM 完全一致。没有执行 VaM 程序代码，也没有根据 Character ID 分支。

## 4. Profile 与表示

新增 `UVamGluteCorrectiveProfile`，schema 1，算法 `glute-corrective-g06-v1`。
保存 topology identity、skeleton family、family policy identity、provenance、pose targets、blend width、构建尺寸，以及各个 native Morph basis 的侧别、目标、局部轴和几何统计。
人物共享 `VamFemale88.json` 中的同一套目标语义与算法；各自的网格和形体校准生成不可变的 Morph 数据。Profile 不含人物 ID 判定。

最终表示为 G0.5 helpers + G0.6 Native UE MorphTargets。G0.6 不增加 helper，不改变原骨索引，不增加 skin influence。
每个 pose target 的 residual 按 AP/ML/SI 分解成可独立缩放的 Morph basis；数值为零的 basis 不生成。Morph 同时构建法线 delta。

## 5. Pose targets 与插值

family policy 共 11 个 target：

| Target | 屈髋 / 外展 / 外旋，度 |
|---|---|
| Neutral | 0 / 0 / 0 |
| Flex30、Flex60、Flex90 | 30、60、90 / 0 / 0 |
| Extension | −20 / 0 / 0 |
| Abduction、Adduction | 0 / 35、−20 / 0 |
| External、Internal | 0 / 0 / 30、−25 |
| FlexAbduction | 60 / 25 / 0 |
| FlexExternal | 60 / 0 / 25 |

这些是当前 family hip 控件范围内的工程采样点，不是临床极限。
唯一运行时姿态输入是既有 `FVamHipPoseState`，不另建 Euler-angle 判断。
采用归一化的 cardinal radial interpolation：按 `[60,35,30]` 度归一化距离，径向权重为 Gaussian envelope × inverse-distance 的四次方，再归一化。
目标点处使用精确 one-hot 极限；Neutral target 没有任何 corrective geometry，因此 neutral corrective 严格为零。
权重非负、总和为一；不用易产生负值的无约束 RBF 矩阵逆解，且组合姿态直接在三维 pose space 中插值，不是三个轴的 delta 线性叠加。

## 6. 几何与 under-curve

G0.5 仍负责主体位置、双支承、支撑基线与防塌。
G0.6 在连续区域内生成局部曲率 lobe、Lower/posterior-thigh 过渡、Lateral 外展/旋转轮廓和低频 under-curve residual。
Flexion 使用不同深度 target 的非线性曲率分布，并叠加已验证、经适配的来源 Bend Fix。
Medial 使用强连续 tether；middle transition 参与 under-curve，lateral 以平滑衰减和不同激活响应过渡。
FoldSemanticMap/FoldState 用于生成这些 target 的低频形状，不是高频皮肤皱纹。

构建时先计算目标姿态的 G0.5 最终蒙皮变换，再将所需的 posed residual 通过每顶点混合蒙皮矩阵的逆映射转换成 native pre-skin Morph delta。
因此合同是 `FinalStructuralRest + CorrectiveDelta`，没有再次放大 G0.5 的 helper 权重、ThighFollow 或投影参数。
矩阵奇异时构建明确失败，不静默产生巨大位移。

单个 basis 和目标间混合均有局部尺寸限幅、源拓扑平滑、UV seam 邻接处理和连续 region 边界。
新增构建期 36 个有界姿态的 conditioning sweep：对已经被 scaffold 压缩的细小三角形，生成平滑的 residual 衰减区域。
这些是构建验证探针，不增加运行时 targets 或 Morph grid。
它修复了初版在屈髋叠加内旋时两处细小三角形放大的工程失败；测试阈值未放宽。

## 7. Shape adaptation

来源 delta 先利用原始来源几何与当前人物区域几何的加权尺寸比例适配。
程序 residual 使用 region-local、dimension-normalized 坐标与当前 attachment/fold 数据。
运行时按 Shape 后的 AP/ML/SI 尺寸，分别缩放对应轴的 native Morph 权重；helper identity 和 topology identity 不变。
Shape Preview/Commit 只更新实例数据，不修改共享 Profile、Skeleton 或 render mesh。

这是保留拓扑的低阶适配：极端 Shape 改变了局部曲率、下垂分布或三角形压缩时，不等价于重新生成所有 pose targets。尺寸比例在 0.25–4 倍内有界；此限制需要后续人工观察。

## 8. Runtime 顺序与最终 rest

Shape/Morph → Base Animation → ActivePose → Pose/IK/constraints → rigid physical blend
→ primary HipPoseState snapshot → G0.5 helper transforms → G0.6 Morph weights → Breast publication/native skinning。

`FinalizeBoneTransform` 使用当前 primary pelvis/femur，G0.6 不读取 helper 作为驱动。
使用 UE 5.8 的公开 `FAnimationRuntime::AppendActiveMorphTargets` 更新组件 native active-map/weight buffer，保留其它 Morph 曲线；显式零权重能同帧清除旧 corrective。
不重建 render resources，不调用私有 RefreshMorphTargets，不恢复 ProceduralMesh，也不逐帧 CPU 全身蒙皮。

G0.6 FinalRestGlute 的表示是当前 G0.5 transforms + immutable Morph bases + per-instance `CorrectiveWeights`。
未来 G1 必须消费这个组合后的表面，不能只用 G0.5 的 COM/静态 standing pose。
本轮没有声称已经为 Morph 后的体积重新积分质量/惯量。

## 9. 自动生成与升级

原来的“选择人物 → 生成 UE5 人物资产”自动增加 source corrective recovery、family targets、geometry calibration、Native Morph 和 `DA_GluteCorrective`。
RuntimeConfiguration 自动引用 Profile，普通 BP_VamCharacter 自动装配，不需要 Pose Driver Blueprint 节点或测试关卡。
Runtime identity 升级为 `runtime-bundle-v9-glute-pose-corrective`；旧资产使用 Upgrade Runtime 发布新的 immutable output。
源人物和旧 committed outputs 不覆盖。

## 10. Debug 与人工对比

打开普通 Empty Level，放入新的 BP，Play/Simulate。在 VaM 人物面板选择该实例，展开 **Glute Structural Debug - G0.5 / G0.6**。

- `Enabled`：G0.5 structural layer。
- `G0.6 Corrective Enabled`：独立的 Morph 修形开关。
- `Show Corrective Delta`：稀疏的 region-local residual 参考向量，不是每帧完整网格位移测量。
- 保留 helper、region、pelvis/thigh attachment、Pose Tension 与 Fold 标记。
- Diagnostics 显示真实 primary hip 角度、active pose targets/权重、来源、总位移上界和分区域 RMS 上界；这些量明确标记为 bounds，不冒充精确屏幕网格位移。
- Fold 行显示当前 G0.5 fold semantics；G0.6 under-curve 几何随已保存 target 的权重混合。

关闭衣物可见性和蓝色区域标记后，固定相机，选择 Target both/left/right。
依次使用 Neutral standing、Flexion 30/60/90、Hip extension、Abduction/Adduction、External/Internal rotation。
对比两开关均关（Base）、仅 G0.5 开、G0.5+G0.6 均开。
按钮请求的角度仍经过原有 Joint Constraints，实际角度以 primary hip diagnostics 为准。
Neutral 应没有 G0.6 差异；其它姿态观察臀腿曲率、内侧 tether、下缘连续性和外侧变化，不用整体平移幅度判断修形。
按钮不是完整平衡弯腰动画；已有 Base Animation/Pose 仍会参与，隔离比较时应保持相同输入。

## 11. 工程验证与边界

测试涵盖 neutral、target 精确权重、非负分区、连续组合 pose、镜像程序几何、30/60/120 一致性、完整 corrective 混合后的 posed triangles、原 Morph/骨索引、Shape Preview/Commit、两实例和同帧开关。
实际结果、两个人物及明显不同 Shape 的记录、save/reload、安装复测和 Cook 见 Evidence。工程测试不作视觉验收。

当前只支持正式 VamFemale88 family。没有精确体积守恒、全肌肉力学、主动收缩、接触、皱纹、衣物或 G1/G2。
混合姿态使用有限 target basis，构建期几何保护不是所有可能极端 Shape/pose 的数学保证；保护区会降低局部修形幅度。

## 参考

- [Epic Pose Driver](https://dev.epicgames.com/documentation/unreal-engine/pose-driver?application_version=4.27)：官方 RBF 驱动姿态/曲线的基本机制；本轮同帧集成另外核对了本地 UE 5.8 引擎源码。
- [Naturalis 作者更新说明](https://hub.virtamate.com/resources/naturalis.33647/updates)：区分 under-curve 的 inner/outer 角度响应。这里只参考语义分区，未复制其参数或实现。
