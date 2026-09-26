# 游戏级人物系统最终选型报告

日期：2026-09-27。依据：本机 UE 5.8.3（CL 58210709）、当前源码、Stage05—07.1 与 MH 跨人物回执、Epic 官方文档。本轮是选型研究，不是新的性能实测或功能交付；未改引擎、插件依赖、分支默认值或人物资产。

## 结论与适用前提

**推荐直接转换的 UE SkeletalMesh 人物作为生产主线；重做其运行时软组织呈现和分级策略。MetaHuman 保留为可选人物来源与研究结果，不继续作为所有 VaM 人物的强制转换目标。**

这是对先前 MH 实验目标的选型调整建议，没有擅自切换分支或默认后端。推荐依据是用户当前优先级：保留已有 VaM 人物身份与资产、游戏内连续捏脸/体型、分区物理交互、通用动画，以及可控制的性能。不是因为已有代码多而排斥 MH；主要原因是 MH 不能直接免除最核心的运行时定制和软组织成本，反而增加转换损失。

目标硬件、分辨率、同屏人数、60/120 FPS 或 VR 尚未明确。因此本报告给出架构选择，**不承诺帧率、每角色毫秒数或最大人数**。默认“捏人”包括玩家在 Cooked 游戏内连续改变脸和体型；若只需开发者在编辑器造好角色、游戏内换装，则 MH 的相对优势明显上升。

## 两个前提需要修正

1. 用户对直接转换人物的外观认可，是其重要优势。但“已支持输入的中性形状与来源材质保存”不等于所有 VaM 资源、任意姿态、TriAx/bulge、所有 Morph、物理和跨引擎着色都无损。Stage05 使用显式 Morph 集和支持边界，并非整个 VaM 运行系统的完整复制。
2. MH 的统一作者工具、面部绑定、衣发资产规范是真实优势，但 Creator 的编辑器捏脸不等于可在游戏里直接调用的完整捏脸器；默认角色也不是包含所有游戏玩法的 ACharacter。教程和资产生态能减少制作成本，不能替代项目特有物理与形状系统。

## 能力逐项对比

| 需求 | 直接转换路线 | MetaHuman 路线 | 当前决策意义 |
|---|---|---|---|
| 保留 VaM 身份、形状、贴图 | 已支持样本最直接；仍有来源功能边界 | 多人物仍有眼角/鼻部失真；源材质未等价迁移 | 直接路线优先 |
| 编辑器制作新人物 | 工具需自己完善 | Creator 头身、材料、衣发作者流程成熟度更高 | MH 优势 |
| 游戏内发色/肤色/衣物参数 | 自建材质参数与部件系统 | 已有材质与装配基础，参数仍需暴露 | 两者可做 |
| 游戏内连续身份捏脸/体型 | 已有形状参数、Morph/骨公式和事务；需扩充与约束 | 需另做受限 Morph/骨架/绑定更新系统，不能直接搬 Editor API | 直接路线更接近 |
| 步行、跑跳、交互、IK、重定向 | 已有 Stage06/07 基础 | 官方骨架、Control Rig、IK 等生态较强 | 都能做，非 MH 独占 |
| 高质量面部动画、捕捉与 correctives | 需作者工作、映射和校正 | DNA/RigLogic/表情校正是核心优势 | MH 明显优势 |
| 身体局部晃动 | 自建分区参数，可用轻量骨骼/代理 | 同样需要分区绑定与物理设置 | MH 不自动解决 |
| 任意接触的软组织挤压 | 当前 Flesh 链缺质量与成本优化 | 仍需 Flesh 或其他变形/碰撞系统 | 共同难题 |
| 衣发 | 来源形状较易保留；动态绑定仍需工程 | 标准 Groom/Outfit 作者生态较好；VaM 衣发仍需转换 | 来源不同，不能只比较教程数 |
| 人体扩展、特殊部件 | 保留原拓扑和来源绑定更直接 | 独立扩展及接缝、动画/物理对接需额外工作 | 直接路线更灵活 |
| 普通关卡/动态 Spawn | 已有自身服务与实例隔离 | 已组装 MH 可独立使用；玩法仍需宿主 | 两者均可 |
| 高性能 | 当前全 LOD0 CPU 路径必须改变 | 有优化版组装与 LOD，但高精度脸、发丝也有成本 | 无公平同场测试，不能宣称谁快多少 |

## Runtime 与 Editor 的准确边界

[Creator](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-in-unreal-engine) 是 Character 资产编辑器。本机 `MetaHumanCharacter.uplugin` 将 `MetaHumanCharacterEditor` 标为 Editor；脸部模型编辑、身体提交等接口位于其 subsystem。本项目不能把这些作为 Cooked 捏人的依赖。

UE 5.8 的 [Collections](https://dev.epicgames.com/documentation/metahuman/metahuman-collections-in-unreal-engine) / [Instances](https://dev.epicgames.com/documentation/metahuman/metahuman-instances-in-unreal-engine) 确实支持预构建内容的运行时组合、换衣发和实例参数，官方仍标为 Experimental。不要误报“MH 完全不能运行时定制”，也不要将这些功能扩大解释为实时重新拟合身份、生成 DNA 和完整 AutoRig。

本机实核 `MetaHumanCharacterPalette/Public/MetaHumanInstance.h` 有 `UMetaHumanInstance::Assemble` 和完成回调，Actor interface 有 `SetCharacterInstance`，Collection pipeline 有 `SetPostAssemblyParameters`。文档部分示例仍写旧类名 `UMetaHumanCharacterInstance`，实现必须以本机为准。本轮未将 Collections 接入或进行其 Cooked 验证；既有 MH 动态 Spawn 回执验证的是已组装 BP。

运行时形状参数应区分身份与表情。面部表情控制器不是鼻型、脸宽等身份参数的替代。即使自建 MH 身份 Morph，也要同步眼球/牙齿、关节、correctives、LOD、发型绑定与衣物/碰撞，限制合法范围。仅让静态脸动起来不构成完整捏人系统。

游戏运行中导入任意新 VaM preset 与从已 Cook 的参数范围捏人也不同。本项目当前导入/作者处理留在 Editor，两条路线都没有承诺零准备的任意运行时来源导入。

## 物理选型：将“晃动”和“软组织接触”拆开

MH 的姿态校正维持弯曲后的体积，不等于实时软组织动力学。Cloth 解决衣物，不自动成为身体体积模拟；启用 Cloth Creator 不会补齐胸、腹、臀、腿的物理系统。

建议同一人物分区应用以下层级。这是待实现的项目设计，不是已经交付的性能结论：

| 层级 | 用途 | 建议表示 | 明确限制 |
|---|---|---|---|
| 基础 | 所有人物、所有距离 | GPU 蒙皮与身份 Morph，必要姿态校正 | 无主动软组织接触 |
| 常规近中景 | 跑动惯性、局部晃动 | 少量辅助骨骼/弹簧或低自由度代理，分区幅度/阻尼 | 不能等价高质量体积挤压 |
| 重点近景交互 | 手/物体按压局部区域 | 小规模软体代理、局部绑定和碰撞集合 | 接触、稳定性和双向作用仍需验证 |
| 远景/后台 | 远处 NPC | 降频、休眠、关闭局部物理或动画近似 | 质量有意降级 |

[AnimDynamics](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-animdynamics-in-unreal-engine) 适合轻量二级运动，但没有通用碰撞求解；[RigidBody 节点](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-rigid-body-in-unreal-engine) 可使用 PhysicsAsset 做辅助结构碰撞。两者都不应冒充软组织体积接触。需要辅助骨骼时，以派生资产和明确蒙皮工作实现，不能假设标准 MH 骨架已有适用的所有独立控制骨。

[Chaos Flesh](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart) 支持低分辨率四面体驱动渲染表面，高分辨率实时求解成本则受限；文档中的世界碰撞是单向刚体驱动软体，不保证手部受反作用力。若游戏需要双向抓压，需要另外设计耦合，换 MH 不会消除此项。

[ML Deformer](https://dev.epicgames.com/documentation/unreal-engine/ml-deformer-framework-in-unreal-engine) 可学习训练数据中的变形，适合后续姿态形变优化。不能将普通姿态训练模型当作任意新体型、任意物体接触的通用物理替代；训练覆盖、身份参数及接触输入都需单独设计。本轮不引入训练作为新的阻塞条件。

## 原路线真正需要优化的部分

`Source/VamCharacterRuntime/Private/VamSoftTissueComponent.cpp` 的可见表面路径当前会组合 Morph、蒙皮与 Flesh 位移，逐三角形重算法线/切线，再分 section 调用 `UpdateMeshSection_LinearColor`。它具有全 LOD0 遍历、临时数组及上传成本。代码足以证明存在该成本，**尚不足以证明其耗时一定超过求解器**。

因此不能把当前总开销全归因于 Chaos。应拆分测量：形状求值、骨骼/IK、代理求解、碰撞准备、皮肤映射、法线切线、渲染上传、衣发及 GPU 材质。

生产方向是保留正常 SkeletalMesh GPU 形变链，把局部动态位移接入受支持的 Mesh Deformer 或等价 GPU 路径。已有 `DG_FleshDeformer` 实验图不完整支持当前 Morph 合成，不能直接替换就宣布完成。需验证 Morph → skinning → 局部位移的正确空间与顺序、各 LOD 绑定、法线/切线、阴影、上一帧位置及碰撞输出一致性；避免重复 skinning 和 GPU readback。

即使只局部模拟，物理接触代理也不等于最终高模表面，必须标明精度。改体型只更新相关 rest/附着/碰撞/衣发绑定；改肤色或口红不得重置物理。保持 ShapeRevision、SurfaceRevision、EquipmentRevision 和实例动态状态分离。

## 动画、衣发与游戏宿主

[IK Rig Retargeting](https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine) 支持不同骨骼网格间的动画重定向，因此使用 UE 动画并不要求先转 MH。直接路线仍需维护各支持骨架家族的映射；MH 减少部分标准适配工作，但也不能保证所有动作直接正确。

两条路线均需游戏自己的 Character/Pawn、CharacterMovement、输入、状态机、交互与保存。MH BP 能生成，不等于自动继承第三人称移动、Root Motion、抓取与战斗。推荐只保留一个最小玩法宿主契约；不为本次选型建立庞大双后端框架。

驱动职责：Body 主动画/IK负责骨骼运动；软组织层处理选定部位的动态偏移；Face 在 MH 路线保持官方 RigLogic/后处理，在 native 路线保留已支持表情系统；Groom 和 Cloth 各自求解，使用明确的体型绑定；扩展部件用独立资产与绑定。谁写最终顶点/碰撞要唯一，避免多个系统覆盖。

[MH 衣发资产规范](https://dev.epicgames.com/documentation/metahuman/metahumans-on-fab) 有利于制作和采购。VaM 衣服、发型不会因脸转换成功自动成为有效 Groom/Outfit；其拓扑、UV、绑定、LOD 和许可仍需处理。体型改动时的合身与碰撞，两个方案都要验证。

## 性能判断与公平测量

[UE Optimized Assembly](https://dev.epicgames.com/documentation/metahuman/assembly) 提供游戏取向的优化等级，应该与优化后的 native 比较，而非拿 MH Cine 或 Creator 预览与轻量 native 比较。[MetaHuman Component](https://dev.epicgames.com/documentation/metahuman/the-metahuman-component-for-unreal-engine) 可按 LOD 控制表情、校正与物理开销。保留完整作者源与近景质量，不意味着远处 NPC 必须全部启用。

[未烘焙 MH 材质](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-unbaked-assembly-in-unreal-engine) 官方明确提示较高 GPU 成本。为暴露所有材质参数而直接用 Creator 级材质，会与高性能目标冲突；应提供限定的游戏参数并烘焙其余内容。native 同样需要贴图/材质和 hair cards/Groom 分级优化。

已有 Stage07 非软体测试的实测帧时间，不能外推到 Stage07.1 全 LOD0 Flesh。Stage07.1 的 30/60/120 帧率上限与 MH 的 NullRHI 生命周期都不是性能合格证明。目前缺少同场、同质量、同硬件的对照数据。

后续验证矩阵（不是本次已执行）：

- 固定目标硬件、分辨率、光照、视角、相同动作和可比衣发/材质质量；记录所有质量降级。
- 1、4、16 人作为压力采样点，不作为承诺人数；近景与普通游玩距离分开。
- 依次测基础动画、轻量晃动、局部接触、衣发；native 当前 CPU 表面仅作为旧基线，新增 GPU 路线另列。
- MH 使用 UE Optimized，与 native 的相应 LOD 比较；不禁用一边表情/物理来制造优势。
- 采集 Game/Render/GPU 时间及并行任务、p50/p95/p99 帧时间、内存/显存、Spawn 峰值、换装与捏人提交卡顿；不能把各并行耗时简单相加。
- 连续变形预览使用轻量路径，昂贵绑定提交异步执行并可取消；验证双实例隔离、序列化、重载和动态销毁。

60 FPS 的整帧预算为约 16.67 ms，120 FPS 为约 8.33 ms；人物只能占其中一部分。具体人物预算在目标明确后分配，不在此捏造。

## 执行收敛与改变选择的条件

建议生产顺序：先保留现有直接转换身份和材质，分离并测量 CPU 表面与求解器；交付轻量分区晃动，再做近景局部接触与 GPU 可见表面；扩展统一捏人参数、LOD、衣发与保存。每步以可运行 Cooked 人物为结果，不再次从零重写 SourceIR、来源索引、事务或现有动画链。

MetaHuman 的研究资产、三人物对照和作者入口全部保留。当前不继续投入“所有 VaM 人物无损转 MH”的无限精修，也不购买 MetaPipe 作为物理解决方案。以后若使用 MH 作为独立人物来源，只共享最少玩法与状态接口，不要求同步维护两套完整系统。

若产品转为“标准真人外观、编辑器预制人物、有限换装、优先高质量面捕/对白、接受身份近似”，则应优先 MH。若“可编辑 MetaHuman Character 源资产”本身仍是不可变的交付条件，native 不能满足该格式要求：必须选择 MH，并接受额外拟合/运行时定制投入，或缩小捏人范围。不能把 native 成品冒称 MetaHuman。

若“任意实时体型 + 任意双向软组织接触 + 多人高精度发丝 + 高帧率”全部同时硬性要求，两条路线目前都没有合格证据；需要限定同时活跃区域与质量预算，不能以换角色格式代替约束。

本轮报告完成。未宣称物理效果、性能、相似度或人工作品验收通过。报告新增一个文件，保持所有现有用户修改与资产。
