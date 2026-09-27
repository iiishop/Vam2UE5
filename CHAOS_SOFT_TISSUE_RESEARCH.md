# 原生人物软组织与 Chaos 可行性报告

日期：2026-09-27。研究范围：当前源码、UE 5.8 本机实现、官方资料、论文，以及用户提供的 Better Jiggle Mod 2.6.3。此次不实现功能、不执行 Mod、不修改人物资产。

## 结论

推荐保留当前原生 SkeletalMesh 人物路线，将物理拆成两层：常态的辅助骨骼二级运动，以及近距离交互区域的 Chaos Flesh 体积变形。前者负责跑动、转身、停止后的惯性和回弹；后者负责指定区域受碰撞体挤压后的局部形变。

目前的性能和观感问题不能直接归结为“Chaos 不行”。当前实现同时存在全 LOD0 CPU 表面更新、同步求解、规则盒状组织代理和简单固定边界。这套实现证明了运行链路，但还没有形成适合游戏的人体组织表示和更新预算。

如果需求只是类似所附 Mod 的 jiggle，应先采用骨骼方案；如果还要求手部按压、局部凹陷和体积保持，才需要额外的软体求解。完整的双向接触、全身自碰撞、多层组织和任意体型实时变化仍属于高成本研发范围。本报告不承诺帧率或视觉验收结果。

## 1. 检查基线与证据范围

- 插件仓库：`I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser`。
- 分支：`master`；HEAD：`1b639e382d556b0423aa49f465c63b09388219bb`。研究开始时工作区干净。
- 本机 UE：5.8.3，安装目录 `I:/Program/Epic Games/UE_5.8`。
- 当前路线为原生人物，不引入 MetaHuman 依赖。
- 本次没有运行人物性能基准，没有进行新一轮视觉模拟验收。下面明确区分源码事实、机制推断和待测方案。

主要源码证据：

| 文件 | 检查内容 |
|---|---|
| `Source/VamResourceBrowser/Private/VamSoftTissueBuilder.cpp` | 区域选取、代理网格、固定点、表面绑定 |
| `Source/VamCharacterRuntime/Private/VamSoftTissueComponent.cpp` | 求解生命周期、CPU 蒙皮、碰撞收集、表面更新 |
| `Source/VamCharacterRuntime/Private/VamTissueBackend.cpp` | Chaos 接入、碰撞对象增删更新 |
| `Source/VamCharacterRuntime/Public/VamSoftTissueProfile.h` | 组织参数、绑定信息和质量配置 |
| `Config/RuntimeImportPolicy.json` | 默认生成策略 |
| `STAGE071_RUNTIME.md`、`STAGE07.md` | 现有能力与历史实验限制 |

本机 Chaos Flesh 插件描述仍标记 `IsExperimentalVersion=true`。这不意味着不可用于游戏，但意味着版本兼容、缺失能力和性能应由项目自行验证。

## 2. 当前系统实际如何工作

### 2.1 构建组织代理

当前默认区域为左右胸、左右大腿，依据对应骨骼权重选择源表面。生成器在各区域的骨骼局部空间建立包围盒，扩张约 2%，再划分规则四面体笼。

按当前默认 `Cells=4` 计算，每个区域有 125 个粒子、384 个四面体；四区域共 500 个粒子、1536 个四面体。这是策略与生成器推算值，不代表逐一审计过所有已保存 Profile。

代理点从最近的候选表面顶点取得蒙皮权重和形状来源。靠支承方向约 25% 的区域固定，其余点参与模拟。高精度表面顶点保存四面体重心绑定。区域重叠时选择最大权重的绑定，而不是建立区域间的力学耦合。

因此当前代理并不是贴合脂肪组织的实体，也没有显式表示皮肤、脂肪、筋膜和肌肉的不同性质。

### 2.2 运行与显示

运行组件为每个人物创建独立的 Flesh 数据、Solver、碰撞组件和 ProceduralMesh，不依赖预放的关卡 Solver。它检查 Body、BindSignature、MorphSet 等绑定身份，并将身份 Morph 应用于代理静止形状。

求解器使用质量、刚度、阻尼、不可压缩性、运动学约束和碰撞。项目明确设置 `bDoThreadedAdvance=false`，自行同步调用写入、求解和读回。Balanced 为 2 子步、5 次迭代；High 为 4 子步、10 次迭代。后者的子步×迭代数为前者四倍，但这不是实测总帧耗时四倍。

可见人物走以下路径：

1. 拷贝完整 LOD0 静止顶点，应用 Morph。
2. CPU 对完整表面逐顶点蒙皮。
3. 求出代理相对骨骼驱动基线的模拟位移。
4. 通过四面体绑定将位移传给表面。
5. 对各表面的三角形重算法线和切线。
6. 分切 Section 数据并更新 ProceduralMesh；原 SkeletalMesh 被隐藏，但仍提供动画姿态。

低分辨率软体代理并没有消除高分辨率显示网格的 CPU 成本。

### 2.3 碰撞与生命周期

每帧在人物范围外加 100 cm 查询附近碰撞组件，处理球、盒、胶囊和凸体，并包含人物自身的 PhysicsAsset 与显式注册碰撞体。已有物理几何会在可复用时复用，不能说所有 Chaos 碰撞对象每帧都被重建；但查询、列表构建、变换和部分数据复制仍有成本。

当前没有实现软体对刚体的双向作用力反馈，也不是任意三角网格都可直接作为等价碰撞体。当前同一区域的对应骨骼碰撞会被跳过，其他邻近碰撞体仍需要排查是否过度约束组织。

`ShapeChanged` 会触发软体重置，下一轮重新创建相关状态。单次推进时间使用 `min(DeltaTime, 1/30)`，未见将超出部分累计到后续模拟的机制。因此长帧时可能出现模拟时间落后；这是源码风险，不是已经测出的视觉故障原因。

## 3. 为什么可能慢、为什么可能不好看

### 已确认的性能负担

| 来源 | 对当前系统的影响 | 优先处理方向 |
|---|---|---|
| 全 LOD0 CPU Morph、蒙皮、法线与切线 | 成本随整个显示网格增长 | 保留 SkeletalMesh 渲染，研究 GPU 局部变形 |
| Section 数组构建与上传 | 产生 CPU 工作和数据传输 | 传递低分辨率代理位移，避免全网格重复上传 |
| 同步 Flesh 推进 | 进入人物更新的等待链路 | 独立计时后评估调度和线程化 |
| 每帧大范围碰撞收集 | 多人物、多碰撞体时扩大开销 | 缓存候选、缩小区域、明确更新频率 |
| 形状事件重建状态 | 捏人过程可能发生尖峰和运动中断 | 区分身份、表面、装备、物理状态 |
| 缺少自动距离/重要性预算 | 远处人物也可能付出近景成本 | 加入 LOD、休眠、数量上限与滞回 |

没有 Unreal Insights 或等价计时数据，不能给出“其中某一项占比多少”，也不能保证改一项即可达到目标帧率。

### 已确认的模型简化与待验证的视觉影响

- 规则盒状体积可能包含人体表面之外的空间，质量与受力分布不符合真实组织。
- 固定四分之一截面的硬边界可能形成不自然的弯折与牵拉。
- 同一套刚度、阻尼和不可压缩参数难以代表不同部位、体型与皮肤约束。
- 最近表面顶点驱动代理形状，在较大 Morph 下可能不连续，不能保证四面体质量。
- 四个独立区域缺少连续连接关系，可能出现区域之间的响应不一致。
- 四帧预热不能证明已经达到重力平衡；需要区分作者期望的静止外形与重力加载前的参考形状。
- 时间截断和重建可能影响运动相位与连续性。

这些机制需要分别做控制实验。盲目提高刚度、迭代数或不可压缩性，既可能增加耗时，也可能放大僵硬或锁死感。

## 4. Better Jiggle Mod 到底做了什么

分析对象为用户提供的 ZIP，大小 11983 字节，SHA256：

`0e66c9e6cf3f14978bb86b39c6c1f8fa5f7f97495fd8b469c7ecb6e9ff306d28`

只读检查了 13 个文件，主要是 `main.lua`、9 份 `SpringBoneTweaks*.lua`、配置和 UE4SS 设置。包内没有人物网格、四面体数据、DLL 或 UE4SS 主程序。本次没有执行这些文件。

### 实现机制

脚本通过 UE4SS 查找 Eve 的指定动画蓝图类及实例，枚举可能存在的 `AnimGraphNode_SpringBone` 属性，通过 `SpringBone.BoneName` 匹配 Breast、Thigh、Hip，然后修改弹簧节点参数。它还处理新角色对象创建、配置切换、重载和参数保存。

调整项包括最大位移、弹簧刚度、阻尼、错误重置阈值，以及平移/旋转轴和位移限制开关。脚本同时处理类默认对象和已存在实例。

默认第一组配置举例：

| 区域 | MaxDisplacement | SpringStiffness | SpringDamping |
|---|---:|---:|---:|
| Breast | 1.5 | 120 | 7.5 |
| Thigh | 1.55 | 369 | 6.1 |
| Hip | 2.45 | 250 | 5.5 |

这些是该游戏节点的配置值，不是可以直接移植给本项目的组织材料参数。脚本枚举了 25 个可能的节点字段名，也不能据此认定角色有 25 个实际抖动骨骼。

### 对我们有什么价值

它证明的是：游戏已有合适的骨骼和蒙皮时，调整少量二级运动参数就能明显改变 jiggle，而不必重建每帧完整表面。

它没有在包内实现 FEM、体积组织、手指压痕或新的全身碰撞系统。不能据此认定原游戏没有其他物理系统，也不能把整个游戏的表现全归功于这几段脚本。其观感还依赖原游戏的模型、权重和动画。

该包 `UE4SS-settings.ini` 的 EngineVersionOverride 为 4.26，至少说明这个 Mod 按 UE4.26 配置；它不能作为“UE5 Chaos Flesh 已经这样实现”的证据。本机 UE5.8 的 `FAnimNode_SpringBone` 确有刚度、阻尼和位移限制等对应概念，但没有发现包中所有额外字段，例如 `bUseLocalSpace`、`AverageVelocityFrameCount`，不能机械复制配置。

自己的 UE 项目可以直接编辑动画蓝图和运行组件，无需引入 UE4SS。应借鉴参数编辑和区域控制思路，使用自身的辅助骨骼与蒙皮，不复制 Eve 的资产路径、游戏专用字段或第三方源码。包内未确认再分发许可。

来源页面：[Better Jiggle Mod](https://www.nexusmods.com/stellarblade/mods/1570)。本次网页正文未能完整读取，上述机制结论以实际 ZIP 为证据，不以页面评论或视频印象为证据。

## 5. Blender 教程能否应用到 UE

可以迁移资产和建模思想，不能把 Blender 模拟器或参数直接视为 UE 运行时能力。用户尚未指定具体教程，以下是按常见技术分类的判断。

| Blender 中的做法 | 在 UE 中的落点 | 边界 |
|---|---|---|
| 辅助骨骼、权重、形态键 | 导出骨骼/蒙皮/Morph，UE 动画图驱动 | 模拟约束通常需要重新实现或配置 |
| Soft Body Goal、顶点组约束 | Flesh 运动学或柔性附着区域 | 参数含义、单位和求解器不同 |
| 低模代理驱动高模 | 组织代理加渲染表面绑定 | 需解决 Morph、蒙皮、坐标空间和 LOD |
| Cloth 压力形成柔软外壳 | 可探索壳模型近似 | 不等价于体积脂肪，接触和体积响应不同 |
| 烘焙 Alembic/Geometry Cache | 固定动作播放、离线训练参考 | 不自动适应新的接触、动作或体型 |
| 付费插件 | 可能作为本地作者工具 | 购买不等于可以复制源码或再分发资产 |

可将 Blender 用于一次性的 canonical 拓扑区域标注、代理制作、权重和参考模拟。对相同拓扑家族，通过来源顶点身份和 Morph 传播复用结果，避免逐人物手工特例。Cooked 运行不依赖 Blender。

参考：[Blender Soft Body Goal](https://docs.blender.org/manual/id/4.4/physics/soft_body/settings/goal.html)、[Surface Deform](https://docs.blender.org/manual/ru/4.5/modeling/modifiers/deform/surface_deform.html)、[UE Alembic 导入](https://dev.epicgames.com/documentation/unreal-engine/alembic-file-importer-in-unreal-engine)。前两项为官方文档的本地化页面；烘焙播放与交互式模拟应明确区分。

## 6. 推荐的游戏级架构

### 第一层：默认辅助骨骼二级运动

胸、臀、大腿软组织使用专用辅助骨骼和经过校准的蒙皮，不直接让负责行走的主大腿骨产生不受控抖动。根据需求选择：

- Spring Controller：惯性、回弹、阻尼和最大位移，适合首先对齐 Mod 类效果。
- AnimDynamics：轻量约束式二级运动，但官方明确其不计算碰撞，不能当作通用按压系统。
- RigidBody 动画节点：结合 PhysicsAsset 处理辅助刚体与碰撞；表面仍由骨骼权重控制，不是连续体组织。

这些方案中，Spring Controller 并不是 Chaos Flesh；不要将所有 UE 二级运动都称为 Chaos 软体。骨骼数量少通常有利于控制预算，但最终成本仍须在本项目测量。

参数界面应暴露区域、软硬程度、回弹速度、阻尼和最大变形等用户能理解的控制。物理上频率与阻尼比可以帮助校准，但不能未经检查就将连续弹簧公式直接套到所有 UE 节点的数值单位。

### 第二层：近景局部 Chaos Flesh

只有确实需要局部接触变形的区域启用低分辨率体积代理：

1. 在基础拓扑上定义可复用的组织边界、支承和过渡区。
2. 使用贴合人体的体积笼，区分硬支承、柔性连接和自由组织。
3. 验证四面体质量、质量分布、体积与重力平衡，建立通用参数范围。
4. 为交互手部、躯干和环境使用数量受限、尺寸合理的碰撞代理。
5. 将模拟位移以局部残差传给 SkeletalMesh 渲染，避免完整 LOD0 CPU 更新。
6. 根据距离、屏幕占比、接触状态和人物数量安排预算，加入休眠和切换滞回。

同一区域不能简单叠加全幅弹簧骨骼和全幅 Flesh，否则会重复运动。首先采用互斥的质量层级；若后续需要叠加，必须明确 Flesh 的参考姿态和残差定义，并验证切换时形状和速度连续。

GPU 变形器只解决显示表面更新，并不意味着 Flesh 求解已经转到 GPU。历史记录中的 `DG_FleshDeformer` 路径曾遇到 Morph 组合问题，因此还必须验证 Morph→蒙皮→软体残差的顺序、坐标空间、法线、运动矢量和各 LOD 绑定，不能仅替换一个图就宣布完成。

### 第三层：暂不作为首期承诺

完整双向作用力、复杂软体自碰撞、细粒度手指接触、分层皮肤/脂肪/肌肉、多人物同时高质量交互，都需要额外研发与性能取舍。当前官方 Flesh 碰撞能力和本项目接入不能自动满足这些目标。

离线高质量模拟加 ML Deformer 可作为动作相关形变的后续方案，但模型不会自然理解训练范围外的任意按压或体型。参考：[ML Deformer](https://dev.epicgames.com/documentation/unreal-engine/ml-deformer-framework-in-unreal-engine)。

## 7. 查阅资料与学习顺序

以下资料提供不同层面的证据，不将早期视频的节点名称当作 UE5.8 现成接口。视频条目核对了标题/简介或官方入口，未完整观看并复现实验。

| 类型 | 资料 | 对本项目的价值与限制 |
|---|---|---|
| 官方概览 | [Chaos Flesh Overview](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview) | 理解体积代理、运行与离线高精度模拟的定位 |
| 官方教程 | [Chaos Flesh Quickstart](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart) | 约束、表面绑定和碰撞；文档说明刚体对 Flesh 的单向碰撞，不能据此承诺双向反馈 |
| 官方教程入口 | [Chaos Flesh Muscle Simulation 5.6](https://dev.epicgames.com/community/learning/tutorials/RZwB/unreal-engine-chaos-flesh-muscle-simulation-tutorial-5-6) | 肌肉/组织作者流程参考；5.8 需核对本机节点和接口 |
| 实作博客 | [Historia：Chaos Flesh 入门，UE5.3](https://historia.co.jp/archives/39713/) | 代理、Solver、绑定的教学示例，含演示；不是现成人体生产方案 |
| 视频 | [renderBucket：UE5.2 Introduction to Chaos Flesh / Softbody Physics](https://www.youtube.com/watch?v=LwnDh8u5sGU) | 直观入门；版本较早，不能照搬节点图 |
| 官方骨骼运动 | [Spring Controller](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-spring-controller-in-unreal-engine) | 对应 Mod 的主要机制 |
| 官方骨骼碰撞 | [RigidBody](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-rigid-body-in-unreal-engine) | PhysicsAsset 驱动的辅助骨骼方案 |
| 官方轻量动态 | [AnimDynamics](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-animdynamics-in-unreal-engine) | 轻量二级运动；不提供通用碰撞 |
| 论文 | [XPBD，2016](https://matthias-research.github.io/pages/publications/XPBD.pdf) | 理解柔顺性、时间步与迭代的关系，避免把迭代数当材料属性 |
| 论文 | [Small Steps in Physics Simulation，2019](https://matthias-research.github.io/pages/publications/smallsteps.pdf) | 研究小时间步与多迭代的取舍；结论需结合具体模型验证 |
| 论文 | [Position-Based Nonlinear Gauss-Seidel for Quasistatic Hyperelasticity](https://arxiv.org/abs/2306.09021) | 非线性材料与收敛研究，不是免费的性能开关 |
| 论文 | [Volume Preserving Simulation of Soft Tissue with Skin](https://arxiv.org/abs/2109.01170) | 解释近不可压缩组织、体积约束和皮肤层建模，帮助避免均匀“果冻”模型 |

本机 `ChaosDeformableSolverGroups.h` 包含 Gauss-Seidel/Neo-Hookean 相关选项，但默认 `bUseGaussSeidelConstraints=false`，项目也未启用。因此不能声称当前正在使用 PBNG 路线；根据构造配置，它仍选择现有 XPBD/corotated 相关路径。这是源码配置判断，不是运行时求解跟踪。

## 8. 下一轮实施前的验证计划

先做一组受控对照：原始 SkeletalMesh 无软体、仅辅助骨骼、当前 Flesh、局部 Flesh 新代理。每组保持动画、镜头、渲染设置和人物数量一致。

性能测试记录 Game Thread、求解、碰撞收集、表面更新和 GPU 时间，分别观察 1/4/16 个人物。人数是测试梯度，不是性能保证。记录中位数和尖峰，确认瓶颈后再决定线程或渲染改造顺序。

行为测试包括：重力静止、运动停止后的衰减、跑动转向、局部按压释放、高速接触、30/60/120 Hz 与长帧、体型 Morph 极值、多实例隔离、普通关卡动态 Spawn/销毁。至少使用多个人物和体型，不能只为一个例子调参。

身份变化应只更新相关绑定与参考形状；材质、口红和肤色不触发物理重建。传送可以有意重置速度，不能为保持连续性而取消必要的传送处理。

验收分开记录：工程稳定性、性能预算、碰撞边界与人工观感。测试通过只证明测试覆盖的范围。此次报告没有实施这些改造，也没有声称已经得到更快或更自然的新结果。

## 最终建议

先以辅助骨骼方案建立可调、可比较的游戏级 jiggle 基线；随后只对真实交互需要的部位重做 Flesh 组织代理与 GPU 表面传递。当前全 LOD0 CPU ProceduralMesh 路径不宜继续作为高性能成品的默认架构。Chaos Flesh 保留为局部接触工具，不必承担所有人物所有距离下的全部二级运动。
