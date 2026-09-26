# Face Fidelity V9：实际交付记录

后续眼角拓扑与穿插修复见 [EYE_CORNER_RESULT.md](EYE_CORNER_RESULT.md)。请使用其中的新 Character/BP 检查本轮结果；下文保留 V9 历史证据，不表示 V9 已通过后来新增的眼周自交检查。

2026-09-26，UE 5.8.3，MetaHuman 分支。**Character、完整 AutoRig、Assembly、跨进程重载、隔离 Game 编译/Cook/Package 及实际打包程序生命周期均已通过工程检查。没有声明视觉通过或“基本完美”。**

## 实际人物

- 可在 MetaHuman Character 编辑器打开的源资产：`/Game/MetaHumans/启梦MH通用还原修订/启梦MH通用还原修订`。
- 官方生成 BP：`/Game/MetaHumans/启梦MH通用还原修订/Assembly/启梦MH通用还原修订/BP_启梦MH通用还原修订`。
- 完整任务与逐阶段日志：`I:\Document\UE5\SmartNPC\Plugins\VamResourceBrowser\Saved\MetaHuman\FaceFidelity\AdaptiveExperimentV9`。
- [高清七视角对比](I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9/official-high-resolution-seven-views.png)：左 SOURCE p0，右绑定后的实际官方头部；每侧每视角 768×768。
- [几何复评](I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9/post-rig-metrics.json)、[来源 Morph 影响区诊断](I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9/semantic-region-diagnostics.json)、[实际 BP 检查](I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9/Official/Lifecycle/assembly-verified.json)。

在 Content Browser 粘贴 Character 的目录并打开 Character 资产；不要打开 BP 来寻找 Creator 捏脸面板。完整绑定后形状编辑按官方流程先移除绑定，修改后重新绑定与组装，建议保留当前版本。BP 可拖入普通关卡或动态 Spawn。当前 Body 未配置项目 locomotion 主 AnimBP，本轮没有实现可控第三人称 Pawn 替换。

## 实际算法

1. 复用锁定 SourceIR/计划，提取 imported p0、参考姿态，排除已标记的 expression/pose 与 graft。来源资产只读。
2. 使用已有官方初始拟合及标准 MH 头拓扑。按 source polygon identity 寻找眼部相邻边环，按材质边身份取得唇缘/鼻孔候选；自动配对仍标为 unverified。
3. 四个固定 profile：Conservative、Balanced、BoundaryStrong、SurfaceStrong，加 Baseline。使用法线相容的表面对应、候选边界约束、增量 Laplacian、局部旋转 ARAP 和局部步长保护。本次自动选择 **Balanced**。
4. Phong 插值仅作为明确标记的平滑参考面；原始 p0 顶点不改写，原始三角面指标另存。不能把该插值称为 VaM 原生 subdivision。
5. 读取本机官方 mean.obj 的 24002 个四边面，以顶点编号和有向三角形精确证明覆盖，拟合时保护两种对角线。官方绑定后若换对角线，只接受可证明等价的局部重三角化，并按实际输出复评。
6. 官方 Template → Commit → 保存/重载 → AutoRig → 两次独立重载 → 复评 → Assembly → 实际 BP 网格/依赖检查。没有直接修改最终 SkeletalMesh 顶点。

## 相对旧结果的数值变化

同一来源、同一已配准官方姿态关系，所有数值仅用于工程比较。

| 指标 | 旧 MH 基线 | V9 完整绑定后 |
|---|---:|---:|
| 原始三角面双向平均距离，mm | 0.4325 | 0.2797 |
| 实验平滑参考面平均距离，mm | 0.4492 | 0.2531 |
| 法线差 1−cos | 0.045216 | 0.038531 |
| 未确认候选边界平均距离，mm | 1.1974 | 0.7798 |

改进来自拓扑边环候选取代眼窝开放边界、受约束残差而非直接拉满 nearest-point 权重，以及双对角线保护。V7 虽平均距离下降，官方 AutoRig 改变 49 条对角线后仍有 5 个方向反转，已保留 Partial；V9 的 44 次官方对角线变化后，实际方向反转和全部四边面保护违规均为 0。V8 法线正则实验的综合分更差，未晋升，也未作为默认结果。

## 真实官方接口

本机实调 `UMetaHumanCharacterEditorSubsystem::FitStateToTargetVertices`、`CommitFaceState`、`CommitBodyState`、`RequestAutoRigging`（JointsAndBlendShapes、blocking）、`RequestTextureSources`、`BuildMetaHuman`、`GetMeshDataForConforming`，以及保存/加载接口。`SpawnMetaHumanActor` 只用于编辑器诊断，成品来自 BuildMetaHuman 的实际 BP。自动绑定与官方纹理获取复用已有 Epic 授权；没有新增第三方上传或照片服务。

## 主要实现文件与复用入口

- `vam_face_pipeline.py`：保存请求的通用入口；完整步骤、输入前置条件见 [FACE_FIDELITY.md](FACE_FIDELITY.md)。
- `vam_face_adaptive.py`、`vam_face_phong.py`：通用残差求解、四候选、参考面与保护。
- `vam_face_topology_equivalence.py`：官方四边面覆盖与有向重三角化证明。
- `vam_face_adaptive_cli.py`、`vam_face_adaptive_verify.py`：候选缓存锁、实际重载后复评。
- `vam_face_fidelity_finish.py`、`ue_metahuman_job.py`：官方生命周期与可恢复检查点。
- `ue_metahuman_verify.py`、`vam_face_assembly_check.py`：独立 BP 验证及实际 Face 网格一致性。
- `vam_face_region_report.py`、`vam_face_fidelity_views.py`：实际 Morph support 诊断和固定视角灰模。
- `Config/FaceTopology`、topology calibration/review/workspace 工具：一次性 G2F/G2M 语义标注工作流。

示例命令：`python Scripts/vam_face_pipeline.py --request <保存的请求.json>`。输入要求同一人物的锁定 SourceIR 任务、已有官方初始拟合 Character、该 MH 自身的源姿态/A-pose 头部导出、新用户名称和目标目录。尚未把这个入口接到旧生成按钮；初始头部导出仍是前置步骤，不声称从任意预设点击一次即完全自动。

## 工程证据与限制

- 52 项相关 Python 测试、Scripts 语法编译通过。真实 V9 输入单 profile 的旋转/缩放回归最大恢复误差约 1.51e-8 cm；它不证明跨人物还原或所有角度评分等价。
- Face 8 LOD、Body 4 LOD，双方 DNA，Face 主动画及 Face/Body 后处理存在。312 项依赖闭包、95 个纹理尺寸、依赖蓝图编译已检查。
- BP 内实际头部的 24049 个顶点与绑定后独立重载数据完全相同，最大差 0 cm。原 native 的 114 个资产及旧 MH 源资产未改变。
- 隔离打包第一次因输出路径超过 Windows 编译工具限制失败，保留日志。短路径重跑的 Game 编译、Cook、Package 和实际程序退出码均为 0；[运行回执](I:/Document/UE5/SmartNPC/Saved/MHFaceCookV9/result.json) 验证动态 Spawn、BeginPlay、动画实例、SurfaceRevision 双实例隔离、EndPlay 与 respawn。范围为 Win64 Development / NullRHI；不是渲染视觉或性能验收。新回执绑定本次 BP 哈希，没有沿用旧人物结果。
- G2F/G2M 各 31 个来源精细语义槽仍未人工确认；来源法令纹缺独立 Morph support，报告为空。自动环配对受初始 MH 位置影响，不能冒充来源真值。真实完整人物链只验证本次 G2F 输入，G2M 和更多人物仍需回归。
- 眼角、唇缘、鼻翼等仍有残差；表面距离降低不等于身份“基本完美”。目前没有完整自交证明；原 VaM 纹理、睫毛、头发、衣物与物理迁移不在本轮交付范围。
- 已确认语义的 barycentric 路线仍保留严格拓扑校验；发生官方重三角化时不能随意改写已确认的三角面引用，需要单独重绑定/验证。这条完整语义路线尚未做真实人物端到端证明。
- 人工观察步骤见 [MANUAL_REVIEW.md](MANUAL_REVIEW.md)。没有程序相似度通过阈值。
