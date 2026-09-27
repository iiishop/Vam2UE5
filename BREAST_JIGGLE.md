# Breast Jiggle Runtime 实现与工程报告

基线：`5d4c54c`（Retire legacy soft tissue runtime）；分支：`feature/jiggle/breasts`。本文记录实现、工程检查和人工体验入口，不包含视觉验收结论。

## 1. 新增能力

- Runtime：`UVamBreastSkeletalMeshComponent` 仍使用原生 SkeletalMesh / GPU 蒙皮，在最终骨骼提交点运行两个独立的 `FVamBreastSolver`。
- Editor：`UVamBreastJiggleBuilder` 从持久化 Native MeshDescription / GeometryBinding 构建胸部区域、辅助骨、权重、形状响应和新的 CharacterDefinition；不覆盖来源资产。
- DataAsset：每个人物独立的 `UVamBreastJiggleProfile`（schema 1，算法 `breast-jiggle-v1`），由 `RuntimeConfiguration.BreastJiggle` 引用。保存 topology identity、family、region provenance、skin identity、几何/质量、五节点参数、Shape 响应和运行策略。
- 构建身份使用 `runtime-bundle-v4-breast-jiggle` 命名空间；RuntimeConfiguration 仍为 schema 3，保留旧配置的兼容读取。
- 调试：启停、重置、区域/骨骼/节点绘制、每实例 Density / Softness、运动命令及状态读数。

## 2. 骨架层级

正式支持 family：`vam-female-88-daz-zyx-v1`，映射位于 `Config/RigFamilies/VamFemale88.json`。映射提供 chest、superior、左右 pectoral、左右 nipple/front reference 和可转移权重的 support bones。缺少语义映射或骨架不匹配会明确报错。

当前输出保留原骨索引 **0–87**，包括原有 lPectoral/rPectoral、lNipple/rNipple。只 append 下列 12 根骨：

| 父骨 | 新骨 | 当前输出索引 |
|---|---|---|
| chest（10） | L_BreastAnchor | 88 |
| L_BreastAnchor | L_Breast_Core / Upper / Lower / Medial / Lateral | 89–93 |
| chest（10） | R_BreastAnchor | 94 |
| R_BreastAnchor | R_Breast_Core / Upper / Lower / Medial / Lateral | 95–99 |

Anchor 为运动学支承；五个语义节点独立拥有三维位移/速度。没有额外 distribution bones。小角度骨骼转动由节点相对其支承的偏移推导，并限制在 Profile 的角度范围内，没有增加独立旋转自由度。

每次构建创建新的 Skeleton、Body 和兼容的附属网格。配置中的 BaseAnimation 会复制到追加骨后的 Skeleton，保留原骨动画轨道；Runtime 不修改共享 Skeleton。

## 3. 区域与蒙皮

区域分析使用真实 pectoral 原始权重，结合 source topology 邻接和 InputToSource 接缝身份。Morph 的 delta 能量与 pectoral support 的重叠用于筛选胸部相关证据；不通过人物名称判断。

胸廓 frame 由 chest→superior、左右 pectoral 的关系建立，nipple/front reference 决定前方符号。连续前方门控、左右门控、距离衰减和 8 次拓扑扩散共同排除或衰减背部、对侧、手臂及无 pectoral support 的区域。区域权重为 `[0,1]`，不是单一 bounding box。

根部衰减乘以 region weight，最多转移相应支承权重的 65%。仅允许映射的 torso/chest/pectoral donor；保留原有非支承权重。五节点采用连续 RBF 分配，Core 有较高整体权重，Upper/Medial 的动态影响较弱。原始 influences 不截断；在 8-influence 限制内选择 helper 候选并归一化。没有可用 influence 槽的顶点保持原绑定。

NativeBuilder 会检查最终渲染域的顶点、UV、Morph 和量化权重；新增检查用真正的 inverse bind 与参考姿态重建零偏移网格。最终输出的 88 根原骨绑定平移误差为 **0 cm**，名称/索引/父级保持一致；零偏移蒙皮检查阈值为 **0.001 cm**。

## 4. Effective Volume / COM / Mass

在 imported p0 上，对区域表面三角形计算投影支承面积及到支承面的正向深度，累加 `region × projected area × depth / 3` 得到 effective volume proxy；COM 为相应支承锥体质心的加权平均。半径取横向/上下方向的加权几何尺度，语义位置来自各方向的空间核。

它是低维动力学的 **effective volume**，不是医学测量的解剖体积。

`MassKg = EffectiveVolumeCm3 × DensityKgPerCm3`。当前主人物构建数据：

| 数据 | 左 | 右 |
|---|---:|---:|
| effective volume（cm³） | 585.3916 | 583.4258 |
| density（kg/cm³） | 0.001 | 0.001 |
| effective mass（kg） | 0.58539 | 0.58343 |
| effective radius（cm） | 3.9183 | 3.9204 |
| effective depth（cm） | 17.4132 | 17.3424 |
| support area（cm²） | 100.8531 | 100.9249 |
| region weight > 0.001 的输入顶点数 | 482 | 483 |
| 预计算 Shape 响应数 | 15 | 11 |

节点质量比例为 Core 0.40、Upper 0.15、Lower 0.20、Medial 0.10、Lateral 0.15。这些数据保存在人物 Profile 中。

## 5. 实际求解模型

每侧为五节点弹性网络：node-to-anchor 弹簧、节点间对称 reduced-mass coupling、三轴阻尼、非线性正/负方向响应、重力及软/硬位移限制。

默认物理步长 **1/120 s**，每帧最多 **16 子步**。弹簧和阻尼采用 backward Euler；Coriolis 项通过隐式 3×3 线性系统处理，以避免显式旋转速度项注入数值能量。低频节点 coupling 使用前一子步的节点快照。

Profile 的 AP/ML/SI 频率描述 imported reference mass 下的弹性。Shape 或 Density 改变质量时保持这一弹性基准，不能用一个 JiggleStrength 替代质量、刚度、阻尼和 limits。Softness 为独立 compliance scale。

默认各语义 AP 频率为 Core 3.0、Upper 4.8、Lower 2.5、Medial 4.5、Lateral 2.6 Hz；另外两轴也单独保存。正/负曲线及位移范围均是 Profile 数据。软限制从范围的 65% 开始连续增强；硬限制是越界的最后约束。

## 6. 惯性与最终姿态顺序

实际顺序为：Shape/Morph → Base Animation → ActivePose → Pose/IK/Joint Constraints → 可选刚体混合 → Breast Jiggle → Native Skinning。接入点是 `FinalizeBoneTransform` 在父类交换/发布骨骼缓存之前。

从当帧最终胸廓得到左右 Anchor 世界 frame，再计算线速度/线加速度及 quaternion 差分得到角速度/角加速度。不是读取 Actor Velocity 直接施加后拖。

局部加速度包含：

`gravity - imported_gravity - a_origin - alpha × r - omega × (omega × r) - 2 omega × v_relative`

因此匀速直线运动不产生持续惯性力；加速/制动有对应滞后/延续；恒定角速度下仍有径向偏移；节点已在运动时也有 Coriolis 响应。

生成 PhysicsAsset 后移除 helper bodies/constraints，并重映射保留原刚体碰撞过滤关系。刚体只驱动原骨，Jiggle 只写 helper；PhysicsOutput 读取 Jiggle 后的最终姿态。

## 7. Shape、重力、Teleport 与实例隔离

构建时根据 Morph delta 和 BoneCenter delta 预计算 Anchor、COM、语义 rest、radius、depth、support area 的响应，以及 positive log-volume response。运行时更新每实例 rest / reference override 和质量；不重建 Skeleton 或改变 helper identity。

这是 imported shape 附近的预计算响应近似。极端、多 Morph 组合的几何/体积精度仍需人工判断，不应解释为连续组织重建。

小变化保留节点状态，只使 Anchor 差分历史失效；较大 volume 比例变化允许受控 rebase。Preview/Commit 的形状编辑不会作为高速人物运动进入加速度。

Imported p0 是其原始直立 frame 下的零偏移重力基准；求解 `g_current - g_imported`，避免再次施加完整静态下垂。仰卧/侧卧会产生不同低维平衡。

TeleportRevision、过大位移/转角和长时间间隔触发明确 reset/rebase。默认 teleport 清零位移与速度，也可由 Profile 选择保留位移。暂停冻结动态状态并清理差分历史，恢复首帧重新建立采样。

每个 Body 独立保存节点、速度、Anchor 历史、accumulator、休眠状态及 Shape rest。共享 Profile 只读。Density/Softness 调节只影响选中的运行实例；Spawn/Destroy 走普通组件生命周期。

## 8. 自动生成与升级

用户流程仍然只有选择人物 → **生成 UE5 人物资产**。原生来源解码/构建/重载完成后，已有 Runtime 自动阶段现在增加：胸部分析 → helper skeleton → skin redistribution → BreastJiggleProfile → 新 Definition/Shape/Geometry → Physics/Rig/Animation/Materials → RC/BP → 独立重载发布。

旧人物仍从资源浏览器的 **升级已导入人物 Runtime** 进入。保持来源及配方选择，按 family 身份补充缺失的 Jiggle mapping，生成新的 committed output。不会替换已放置的旧 BP 或修改来源资产。

本轮实际从两份已持久化 Native 来源生成并三进程发布；未重新执行完整 VAR 解码导入，不将本次验证称为全来源重导入。

## 9. 当前项目中的实际输出

主人物（60 参数、12 个附属网格）：

- `/Game/VamRuntime/R_fb3c4337cc55a8df4700fa75/BP_VamCharacter`
- `/Game/VamRuntime/R_fb3c4337cc55a8df4700fa75/DA_BreastJiggle`
- 同目录 `RC_Runtime`、`CD_Character`、`SK_Body`、`SK_Body_Skeleton`、`SD_Shape`、`GD_Bindings`、Rig/Physics/Animation 及材质输出。

第二来源也使用同一算法完成构建/重载/发布，输出为：

- `/Game/VamRuntime/R_97a588b8c1dd7e544f797040/BP_VamCharacter`
- `/Game/VamRuntime/R_97a588b8c1dd7e544f797040/DA_BreastJiggle`

两份新目录已复制到当前 SmartNPC 项目 Content，来源与输出文件均核对 SHA256。新 DLL 已安装；原 Binaries 备份在 `Saved/BreastJiggle/InstalledBefore`。

## 10. 全新 Empty Level 体验

1. 若 UE 在 DLL 安装前已经打开，先重启 UE。
2. 新建 **Empty Level**，将上述新 `BP_VamCharacter` 拖入。
3. 点击 **Simulate**，选中运行中的人物实例。
4. 打开 **Window → VaM 人物调试**，展开 **Breast Jiggle · Runtime**。
5. 开启 helper/node 显示，用加速、停止、转身、停止转身命令观察；可暂停后检查节点读数，再恢复。
6. 调 Shape，检查 Commit 前后状态；也可旋转人物到侧卧/仰卧观察平衡变化。

不需要特定地图、Level Blueprint、测试 Actor 或另行附加 Solver。工程 Cook 的空关卡仅为验证夹具，没有发布到用户 Content，也不是运行依赖。

## 11. 调试控件

所有下列控件都位于人物调试面板的 **Breast Jiggle · Runtime** 折叠区。

| 控件 | 作用 |
|---|---|
| Enabled | 开关当前人物 Jiggle |
| Reset | 清除动态状态并停止调试运动；不恢复 Actor 原世界位置 |
| Show helper bones | 绘制 Anchor 到 helper 的连线和节点 |
| Show region weights | 在当前 Anchor frame 中绘制构建时区域点/权重；不是实时重蒙皮表面热图 |
| Show dynamic node state | 绘制 rest 到当前节点的位移 |
| Density kg/cm3 | 改变当前实例有效质量，Profile 资产不变；输入范围 0.0001–0.1，拖动上限 0.01，更高值可直接输入 |
| Softness | 独立调节当前实例 compliance scale；输入范围 0.2–100，拖动上限 20，更高值可直接输入；Solver 同步支持该范围 |
| Forward / Lateral accelerate | 沿 Actor 前/侧方向加速一秒，随后保持速度 |
| Stop | 停止平移运动，用于观察制动 |
| Rotate continuously / Stop rotation | 持续 1.5 rad/s 旋转及停止 |
| Jump impulse | 短时预设弹道及停止，便于观察垂直惯性；不代表落地碰撞或 locomotion 系统 |
| 读数 | 双侧 Volume/Density/Mass/COM、线/角速度与加速度、各节点位移、子步/丢弃步及 sleep 状态 |

已有暂停/继续见证时钟按钮也会暂停 Jiggle。调试运动会改变所选 Actor 的位置/朝向。

面板上方的 **隐藏衣服 / 配饰** 按钮切换当前人物所有导入 Parts 的显示，再次点击 **显示衣服 / 配饰** 恢复。身体不会隐藏。此开关为实例临时状态，不修改网格、材质或人物资产；同一实例重新加载时保持选择，新实例默认显示。衣服与配饰目前没有独立分类，因此一起切换。运行/Simulate 后选中人物，点击 **使用选中人物 / 刷新** 即可操作，无需修改人物蓝图或重新生成资产。

## 12. 工程证据与未做事项

工程验证：

- Editor、Win64 Game Development、Game Shipping 全部编译成功。
- 两份来源均完成保存、独立重载发布、第三进程 committed 验证。
- `Vam.Breast.Dynamics`：匀速、线加速/制动、角加速、恒定角速度、Coriolis、Teleport、Pause、两实例、质量响应、长时间有界性/能量衰减，以及 30/60/120 FPS。
- 测试输入轨迹的最大位移差：30/120 **0.023793 cm**；60/120 **0.007892 cm**。
- `Vam.Breast.NativeRuntime`：普通生产人物的 Spawn/Destroy、异步加载、helper/weight/bind 验证、实际 Anchor 响应、仅胸廓姿态运动、Shape helper identity、冻结对照实例的状态隔离、TeleportRevision 和 Pause/Resume。
- Runtime recipe / upgrade / Shape kernel Python 回归：**19 passed**。
- 主输出的 Win64 Cook：**725 packages，0 errors，0 warnings**。这是 Cook 依赖验证，不宣称已做打包游戏视觉验收。
- 当前 SmartNPC 项目的安装后检查：两项 C++ 测试均成功（1 success、1 success-with-warning、0 failed）。NativeRuntime 的临时测试 World 销毁有一条缺少 World EndPlay 的清理告警；测试人物已执行 Destroy，不是人物资产加载失败。
- 安装后检查结果及 DLL 哈希见 `Evidence/BreastJiggle/summary.json`；几何/骨骼明细见 `audit.json`。原始日志位于 `Saved/BreastJiggle`。可用 `RunBreastJiggle.ps1` 在显式指定的项目和已 committed report 上复现检查。

本阶段未实现：Chaos Flesh、手部按压、胸部间或身体/衣物软体碰撞、Corrective/Fold/wrinkle、臀部/大腿/服装 Jiggle 或 Cloth。衣服保留原始权重，未获得胸部 Jiggle 的服装变形补偿，可能出现穿插，需要人工观察。

原有 Stage07 全组合抓取门槛在基线文档中有失败记录，本轮没有以这些新测试替代它或宣布旧组合回归全绿。构建/动力学检查不评价外观自然度；区域边缘、形变质量、极端 Shape、参数手感及衣物关系由用户人工验收。
