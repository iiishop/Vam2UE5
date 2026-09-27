> 后续 Breast Jiggle 实现与验证见 [BREAST_JIGGLE.md](BREAST_JIGGLE.md)。下文保留物理清理提交当时的基线记录。

# Native Skeletal Runtime — 2026-09-27

本次仅清理旧物理实现。没有新的 Jiggle、Contact Flesh、辅助骨或 Flesh asset；没有视觉效果通过判定。STAGE07、STAGE071_RUNTIME、STAGE07_TESTING 和 Evidence/Stage07、Evidence/Stage071 是 superseded 历史资料，不能作为当前源码通过证据。

## 当前能力与数据流

`AVamCharacterActor` 默认仅包含 Character、Motion、Interaction、PhysicsHandle、PhysicalAnimation、ActivePose、PhysicsOutput。BeginPlay 后 Character 异步加载 RuntimeConfiguration 和经验证的原生 Definition/网格/材质/动画/物理资产，创建原生 USkeletalMeshComponent Body，附加人物网格跟随其姿态。

Shape 参数由 Character 统一写 Morph；Expression 层与形状合并。每实例参考绑定进入 VamShapeAnimInstance：基础 AnimationSequence → ActivePose → 用户摆姿/Debug offset → 足接触适配 → PBIK → 关节裁剪 → 原生 SkeletalMesh（可选原有刚体混合）。没有隐藏 Body、LOD0 CPU 重蒙皮、ProceduralMesh 或软组织碰撞代理。

Motion 保留连续运动、显式 Teleport 和见证时钟。见证不是组织求解器。Interaction 保留物理抓取与局部刚体响应。Shape-aware PhysicsShapeProfile 保留私有 PhysicsAsset、胶囊尺寸和约束 frame 更新，避免共享资产污染；重绑定保留抓取和刚体速度。PhysicsOutput 保留真实 Chaos 完成时刻、最终姿态、warm-up 和版本化刚性胶囊输出；它不是软表面输出。

## 配置与旧资产

- 新 RuntimeConfiguration 为 schema 3；只保存通用人物依赖。旧四个软组织配置字段及 CharacterState 中的旧软表面字段已移除。
- schema 2 仍接受原有发布标志，加载时告警并忽略已删除的属性；不加载旧 Profile，也不自动把 schema 2 标为 3。未知 schema 继续拒绝。
- 内容浏览器选中旧 BP_VamCharacter 或 CD_Character，在资源浏览器点击“升级已导入人物 Runtime”。使用原生定义及持久化来源映射；有旧 RC 时保留其回执中的动画、材质、家族设置，显式移除旧 soft_tissue 配方。
- 生成新的不可变 Runtime BP/RC，经过 build、独立 reload/publish、第三进程 verify 后才 committed。原资产和已放置实例不自动覆盖。替换实例时自行保留场景特有编辑。
- 手写配方含 soft_tissue 时直接报退役错误，避免静默忽略用户输入。通过升级入口迁移，或明确删除该字段后重新构建。
- 不删除用户已有 DA_SoftTissue、测试地图或其他 .uasset。它们是旧内容，正式 Cook 应以新 schema 3 BP/关卡闭包为根，不 CookAll 旧实验目录。含旧 API 的用户自定义 Blueprint 图需要移除旧节点；原插件生成的 BP 不依赖这些图节点。

## 退役范围

移除 SoftTissue Component/Profile/Builder/RuntimeProbe、TissueBackend、FleshCapability Actor/Asset/Builder；移除旧 capability/map 构建脚本、生命周期测试入口、历史包 replay 入口及 Stage071 示例配方。移除插件对 ChaosFlesh、ProceduralMeshComponent、OptimusCore、ComputeFramework 的依赖，旧调试面板按钮退役。

来源解析、Morph 名称中的 Flesh、原生资产和 GeometryBinding 的来源区域语义不属于旧求解器，保留。Git 历史不改写。

## 下一阶段接口

后续 Bone Jiggle 的明确接入位置是 `FVamShapeProxy::Evaluate` 中 `SolveIK` 之后、`ClampJointRotations` 和返回原生 pose 之前，仍须明确与现有刚体混合的写入职责。读取 `GetShapeReferencePose()`、`OnShapeChanged`、Motion 样本及 TeleportRevision 来更新每实例状态；不得借用 ActivePose/Debug offset 做隐式动态叠加。本次没有创建未来 Profile 或求解框架。

## 工程验证

当前入口：Build.ps1、BuildRuntimeConfiguration.ps1、RunStage07Regression.ps1、RunStage07Composition.ps1、RunStage07Cooked.ps1。旧名字仅表示保留的通用基线测试。新的构建使用唯一空输出目录；测试宿主复制 Content，避免构建工具清理目录联接所指向的用户资产。

本次基于 master `1b639e3`；独立 BuildPlugin 的 Editor、Game Development、Game Shipping 均编译成功。46 项 Python 测试通过。已安装的新 Runtime DLL SHA256 为 `6f3f19d3d702f4913dbfc32883f219680ca9720cc479ed527428939e3d9e08e4`。

| 工程项 | 本次结果 |
|---|---|
| 两个原生来源的 Runtime 构建及三进程发布 | 通过；b41 为 60 参数，c6 为旧 6 参数人物，分别记录，未混为同一版本证据 |
| 旧 schema 2 BP 加载 | 通过；缺失 SoftTissue 子对象产生加载器告警并跳过，Body 正常加载 |
| 旧 BP 升级任务及原项目重载 | 通过；schema 3、7 个标准组件、依赖闭包无旧后端/测试地图 |
| 旧/新 BP 的 Shape/Expression/IK/动画事务 | 通过；双实例隔离、三形状、动画对象/时间保留 |
| 原生形状内核 | 通过；60 参数，最大参考骨位置误差约 4.27e-14 cm |
| Stage06 游戏运行回归 | 通过；移动、抓取/释放、刚体模式、Teleport、双侧眨眼 |
| 完整 Stage07 组合回归 | **未通过**；最终代码 b41 抓取位移 0.348 cm，门槛 1 cm；此门槛之前的动画、呼吸、眨眼、最终 IK 通过 |
| 同输入、清理前 HEAD 对照 | 重新编译 HEAD 并构建独立 schema 2 配置，关闭旧后端；同一门槛失败，抓取位移 0.486 cm。没有降低阈值，也没有改动 Grab/Motion/PBIK 实现 |
| 完整组合后半段 | 前置失败导致其 Shape+Grab、时钟、停稳检查未执行，不能据独立测试补记为组合通过 |
| Cooked | Win64 Development 编译、Cook、打包、启动及 Stage06 运行链通过，退出码 0；NullRHI，未作视觉验收。包内资产闭包审计结果见 Evidence/PhysicsReset |
| 完整 VaM 源文件重新导入 | 本轮未运行；现有解码缓存缺失。已验证持久化原生定义到 Runtime 的生成及旧 BP 升级，不将其称为从来源重新导入 |

原项目可使用 `/Game/VamRuntime/R_8d3611b11cc1575cab011ec1/BP_VamCharacter`。该升级保留旧 b41 配方（它未配置 BaseAnimation），没有擅自给人物绑定测试动画。组合验证使用独立的动画测试配方 `R_f03f68ad4da2cff8ccacad43`；第二来源构建为 `R_bdd0ed230b5b2d8a438ca301`，均留在隔离宿主，未发布到用户 Content。

原有文件保持不变，只新增上述正式 Runtime 文件夹。运行日志和失败过程保留在 `Saved/PhysicsReset01`，原 DLL 备份在其 `InstalledBefore`。当前完整工程门槛不是全绿；这里没有组织动态或视觉效果通过声明。

旧 Stage06 测试以未操作人物的手保持静止作为隔离对照。本轮给人物配置测试动画后，Cooked 首跑因对照手随动画移动 0.642 cm 而失败。已在该测试自己的初始化中显式停止两人的 BaseAnimation，再测静态 IK/Grab/Motion；没有改 Runtime、阈值或组合测试。持续动画的组合检查仍由 Stage07 probe 承担。原生形状内核测试中对已退役 `SimulatedSurface` 字段的访问也已移除。
