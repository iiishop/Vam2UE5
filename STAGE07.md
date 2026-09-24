# Stage07 — 07.0 基线回归通过，软组织纵切待交付

本文件记录实现与证据边界，不是整个 Stage07 完成声明。起点为 `21bd244`。本轮没有重做 Stage05，没有加入 MetaHuman。

操作与预期见 [STAGE07_TESTING.md](STAGE07_TESTING.md)。`ReplayStage07Evidence.ps1` 可直接回放已保存的 Baseline/Flesh Cooked 检查点，输出独立的新报告；它不把旧包当作最新工作区源码验证。

## 当前检查点（2026-09-24，后文历史记录不替代本节）

**07.0 当前通过检查点：`Evidence/Stage07/BaselineSourceShading`。** 配置 `R_a5e7cbde4e87969a3d1e8636` / `R_2d4b4313350807542f149830` 使用显式 `skin_shading: source`，保留原生着色模型；各自经过三个独立发布进程、新 Stage05 内核/事务回归，三个实例通过 30/60/120 联合数值回归和新 Cooked 包的真实渲染回归。首次启动截图及后三档截图已检查，皮肤正常。Cooked 包为 `Saved/Stage07CookSourceShading01`（工程 Saved 目录），对应 Composition run `2714b2d9bbc541f5aae27953df3e2803`；二进制和资产身份见回执。39 项 Python 测试通过。另一骨架家族、精确解剖单向关节标定不在本次证据覆盖内。历史 SSS 首次截图异常未宣称根因已修复，显式 SSS 模式不属于这次通过的材质配方。

07.0 的运行链已扩展到：两个独立原生来源、三个实例（其中两个共享配置），默认宿主持续播放基础动画、主动动作/摆姿/PBIK、局部刚体响应、物理抓取、三种合法参数的 Preview/Commit/Cancel、碰撞尺寸/约束 frame 更新与足接触。私有 PhysicsAsset 避免跨实例写入，抓取中的重建保留速度和约束目标。动画生产、Chaos 完成、外部物理结果与最终姿态分开发布；刚性胶囊输出经过版本及四个真实 Chaos 步的 warm-up 检查，仍不代表皮肤表面。

RuntimeConfiguration 使用 schema 2。构建、独立重载并发布、第三进程重验发布状态分为三个进程；只有最后一步通过才形成 committed 回执。未发布的配置不能被宿主加载。原始资产、派生算法和运行 DLL 均有独立指纹，不把旧六参数人物当作当前八参数人物的证据。

暂停回归区分 witness 与整个测试世界：P/O/R 保留旧含义并显示“仅见证”；世界暂停另行验证 Chaos 和动画停止。Teleport 明确释放抓取、清除足锚、清零速度并重新 warm-up。连续移动、急停、纯旋转、短卡顿及静置漂移已纳入运行探针。

最新数值检查点 `Evidence/Stage07/BaselineCurrent` 对应 `Saved/Stage07/Composition-17d44bc6d1734fb687d03ed88f9f03ee`，在 30/60/120 FPS 通过预设阈值。它要求真实 Chaos 子步：最大 1/120 s、最多 16 步，配置在 `Config/Stage07CompositionQuality.json`；单独的 Motion 120 Hz 见证不足以达标。未启用该配置的 30 FPS 静置速度曾达到 3.064 cm/s，超过 2 cm/s 阈值，失败记录保留，阈值未放宽。配置只写入隔离测试宿主，不修改用户工程设置。

该数值检查点已补 Cooked 图形运行，仍不是整个 Stage07 验收。`BaselineCurrent/Cooked` 保留同一包的 30/60/120 图形、数值和实际帧时间：实测中位帧率 30.072 / 60.113 / 120.137，三进程退出码均为 0。原生与派生材质图中的贴图和连接结构已确认保留。当前两个样本同属经父链/局部轴核对的 88 骨来源家族，另一骨架家族未验证。`proportions.json` 从实际绑定父链测得腿链/臂链比 1.71987 / 1.91682，证明不是统一缩放。两个人物各自的新 Stage05 形状内核回归、事务回归也位于该证据目录。

图形诊断边界：第一次 Cooked 截图曾出现皮肤发黑；同包关闭 SSS 后恢复，但随后保留原 SSS 的静态对照、动画、完整抓取/形状事务及三个帧率均正常，不能把单次 A/B 当成已证明的根因。旧 Stage06 无条件切换皮肤到 SSS 的策略已改为配方 `skin_shading`（`source` 默认保留来源、`subsurface` 显式选择），该字段、算法和保存后的着色模型参与新身份与重载验证；新配方的发布/验收结果必须另列，不能沿用上述旧资产通过记录。

当前 Cooked 入口为 `RunStage07Cooked.ps1`，显式选择已通过的 CompositionRun 和新的 OutputRoot。它验证源码、Editor DLL 与关卡身份，复用独立 Editor 插件并编译游戏目标，随后 Cook/打包/运行。UE 5.8 的 `-NoUBA` 在本机用于禁用构建加速器 detour，绕过已复现的 `mt.exe` 启动错误；不会关闭用户 Live Coding 编辑器。该工具入口存在不等于 Cooked 已通过，须查看实际报告。

性能采集另有可复现的 UE 5.8 退出异常：异步 CSV 处理返回 777003；引擎自带 `-csvNoProcessingThread` 后静态对照及三档完整回归正常退出。入口使用同步 CSV、直接检查实际游戏进程，按预先保存的 `Stage07GraphicsQuality.json` 校验真实 FrameTime；保留启动和卡顿全量样本，不以 `t.MaxFPS` 数字冒充测量。此项不修改引擎，也不是软组织收敛证据。

| 范围 | 当前交付边界 |
|---|---|
| 通用构建、原生身份与独立发布 | 已实现并运行；两个独立来源 |
| 动画/形状/IK/局部物理事务 | 两独立人物、三实例、三形状联合数值及真实图形/Cooked 回归通过 |
| 体型碰撞 | 私有刚性胶囊与约束 frame；不是脂肪体积 |
| 真实时钟/足接触/最终 IK | 已实现，包含暂停、warm-up、Teleport 与停稳回归 |
| 07.1 可见体积软组织 | 07.0 门槛已关闭；开始后端最小链路验证，尚未交付 |
| 07.2 全身区域及真实 BodySurface | 未实现，不可供 Stage08/09 当作软组织输出 |

## 07.1 后端能力实验（尚未合格）

Cooked 最小表面链路现已运行：`Evidence/Stage07/FleshCapability03` 对应独立资产 `/Game/VamRuntimeTests/FleshCapability03` 与工程 `Saved/Stage07FleshCook04`，游戏目标新编译、Cook/打包和真实图形进程退出码均为 0。检查了接触前、压入中、解除后的三张原始截图：原皮肤材质保留，局部皮肤可见变化并恢复。球体隐藏仅为避免遮挡受压区域，仍参与 Flesh 接触。关联皮肤探针峰值 1.724 cm，最大相对代理体积误差 3.088%，1182 个采样帧。它是无重力、静态支承的能力实验，**不代表高质量软组织纵切通过**，不覆盖两种比例、其它姿态、动画叠加、形状更新或区域接触阈值。

这条链路修复了三个实际失败：原生 CDO 的软变形图路径没有被 Cook 收集，改为关卡显式引用独立图资产；绑定名称原先直接使用可能无效的 PrimaryAssetId，改为与引擎运行器一致的 `ChaosFlesh::GetMeshId(Body,false)`；材质缺少 MeshDeformer 用途导致默认灰材质，改为实验拥有的材质副本并保存相应编译用途。未原地修改已发布的 07.0 配置。失败包与日志留在工程 Saved 的 FleshCook01/02/03 中，不能用新包通过覆盖其失败状态。

后续 `FleshCapability04` 编辑器构建增加了全部 LOD0 导入位置与实际渲染位置的逐点顺序验证，并独立重载检查所有保存材质的 MeshDeformer 用途，均通过；此资产没有另行宣称 Cooked 通过。导出的实际变形图节点、连线与内核保存在 `Evidence/Stage07/FleshCapability04/graph-links.txt`：位置输入来自 `OptimusSkinnedMeshDataInterface`（引擎实现绑定静态位置缓冲），没有 MorphTarget 接口。该示例图不能直接用于正式宿主的形状/表情共存。正式链路还需形状 rest、动态附着与 Morph 合成的共同设计和运行回归；不能仅添加显示偏移而继续使用旧碰撞。图审阅是实现层证据，未冒充新的运行状态丢失复现。

在上述 07.0 检查点后加入显式实验 Actor/资产/构建器，普通宿主不启用该组件。`FleshCapability01` 从原生 `lPectoral` 及子骨骼权重选中 370 个渲染顶点，用骨骼局部包围盒生成 125 点、384 个正体积四面体的封闭工程笼，并预计算到原 SkeletalMesh 渲染顶点的重心绑定。它没有修改渲染拓扑、UV 或原始 Mesh 资产，也没有把开放人体表面直接四面体化。该盒状代理仅用于后端实验，不能当作高质量人体区域模型。

保存并独立重载后的 NullRHI 运行测得关联表面探针峰值 1.719 cm、最大相对体积误差 3.043%，是实际 Chaos Flesh 粒子输出经皮肤绑定计算的结果，不是刚体 witness。重力及主动动作关闭，近父关节四分之一代理点为固定支承，球体经 `UDeformableCollisionsComponent` 参与接触。尚未完成 GPU 可见结果、重力 rest 校准、骨骼动态附着、ShapeRevision 事务、碰撞消费者协议和 Cooked 验收，报告中的 `backend_qualified` 与 `stage07_passed` 均保持 false。

接口核查依据为本机 UE 5.8 的 FleshCollection、DeformableSolver/Collisions、TetrahedralBindings、DIFleshDeformer 实现，及 Epic [Chaos Flesh Overview](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview)、[Chaos Flesh Quickstart](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart)。文档描述的能力没有直接计入通过结果。

编译环境另复现 Windows SDK 所在 E: 卷的设备访问异常，系统 Ntfs/Volsnap 日志有扫描/离线检查告警。没有执行磁盘修复；将实际所需 `rc.exe`、依赖 DLL 与头文件复制至工程 Saved 下的 I: 工具缓存，重放相同资源编译命令后 Editor 编译/链接成功。此处不声称 E: 已恢复，也不把 SDK 失败误报为 Flesh 能力缺失。

## 初始 HEAD 审阅（后续修复见各检查点）

已读 Stage05/06 文档、Character/ShapeAnim/ActivePose/Motion/Interaction/AcceptanceActor、Definition/ShapeData/RigProfile、Stage06AssetEditor、Stage06 构建/材质/限位升级脚本，以及原生人物构建、独立重载、形状和运动验收脚本。

- Stage05 正式 Mesh/Morph/来源合同/每实例绑定已经存在，保留。
- `DA_SourceMapping.NativeContractJson` 已持久化来源合同，可作为后续通用升级器输入，不需要猜测 Saved 文件。
- Stage06 构建和材质脚本仍硬编码两个不同的样例配方；限位升级仍会读取旧 adapter 报告。07.0A 尚未完成。
- 默认动画代理仍从 RefPose 开始；单动画探针不等于默认宿主中的分层动画。
- 28 个刚体、肢体 LocalResponse 和 witness 没有体积软组织能力。
- 工作区未找到用户提及的 `Evidence_and_Sources.md`；未把它算作已阅读资料。

## 初始事务修复实现

1. `ApplyShape` 不再强制 `InitAnim(true)`。本机 UE 5.8 的 `SetRefPoseOverride` 会使 RequiredBones 失效，在修改绑定前等待已有并行动画任务完成，再保留正常同步求值。暂时保留原 RenderState 刷新，直到同帧 Morph/GPU 恢复有独立证据后再优化。
2. PBIK 缓存比较组件空间参考绑定；参考绑定变化会重新初始化骨长/rest。初始化使用参考姿态而不是已叠加主动动作/摆姿的输入。
3. Character 统一写入 body 与 follower Morph。ActivePose 提交 transient Expression 层，使用作者值与主动闭合值的最大值，最后减去导入基线。Preview/Commit/Cancel 不再覆盖该层。清空层恢复作者值；错误批次原子拒绝。带 BoneCenter 的 Expression 暂不接收顶点覆盖，避免仅皮肤变化而关节未变。
4. LocalResponse 在释放抓取、改变碰撞/重力前校验目标物理 body。
5. `GetCharacterState` 不再用查询时刻冒充动画生产时刻，也不再用 witness 时钟冒充 Chaos 完成时刻。没有生产者发布结果时，时间与 SimulationShapeRevision 返回 -1；`bHasSimulation` 只反映实际组件是否有模拟 body。这是移除错误证据，不是已完成时序桥接。

## 可复现探针

`Scripts/ue_stage07_transaction_probe.py` 在独立 UE Editor 进程执行，读取显式环境变量：

- `VAM_STAGE07_PROBE_CONFIG`：JSON，包括 `blueprint`、同一 Skeleton 的 `animation`，可设置 `require_fixed: true` 执行修复断言。
- `VAM_STAGE07_PROBE_REPORT`：独立输出文件。

样例路径仅在 `Config/Examples/Stage07TransactionProbe.json`。入口为 `UnrealEditor.exe <project> -ExecutePythonScript=<script> -unattended -nosplash -NullRHI`。脚本新建临时空世界，不保存人物资产；请使用独立进程，不在用户正在编辑的世界执行。输出包含所加载插件的源码/DLL 指纹、资产指纹、Engine 版本。

断言在运行前固定：同值 Commit 的 IK 位置变化 < 0.0001 cm，另一实例变化 < 0.0001 cm，单动画播放位置变化 < 0.00001 s，Expression 权重误差 < 0.00001。三种参数值为定义中的 minimum/区间中点/maximum。这里只验证事务，不把这些阈值解释为物理精度。

修复前使用已安装的插件实测：默认代理同值 Commit 保留同一 AnimInstance，手部 IK 误差前后均为 1.065188 cm，手部变化与另一实例变化均为 0；已有 A_VamIdle 单节点播放时间从 0.370000 s 重置到 0。没有据此宣称默认代理 IK 状态丢失。旧已安装 DLL 的构建来源未重新证明，修复后的独立构建证据需单独阅读。

## 初始事务修复时的能力矩阵（历史快照）

| 范围 | 当前状态 | 尚需证据/实现 |
|---|---|---|
| 07.0A 通用配置构建 | 未完成 | 显式配方、骨架家族/父链/局部轴校验、完整派生身份、独立重载后提交、限位迁移 |
| 07.0B 事务与表情所有权 | 局部实现 | 本轮探针仅覆盖事务；默认宿主基础动画输入、物理中 shape 事务仍缺失 |
| 07.0B 体型物理适配 | 未完成 | 每实例碰撞尺寸/约束 frame 更新；PBIK rest 更新不能代替它 |
| 07.0C 时序 | 仅移除错误时间 | 动画/Chaos/局部求解完成标记及有界桥接；版本化表面/碰撞输出 |
| 07.0C 交互 | 局部防错 | 接触查询足锁、最终物理混合后 IK 误差、抓取释放速度与停稳验证 |
| Lab 暂停/单步 | 仍仅 witness | 不能称为暂停 Chaos；P/O/R 旧含义保持 |
| 07.0D 联合回归 | 未通过 | 两种独立构建比例 × 三合法体型 × 动画/IK/抓取/形状事务/足锁、漂移与跨实例检查 |
| 07.1 胸/臀腿体积纵切 | 未开始 | Chaos Flesh 本机插件存在不等于能力通过；须先通过 07.0 |
| 07.2 全身区域配置 | 未实现 | AnatomyRegion/SoftTissueProfile、权重可视化、cook 映射与形状范围 |
| 真实皮肤/衣发碰撞输出 | 未实现 | SimulatedSurface 仍为空；没有可交付的 BodySurface/CollisionProvider |
| 图形与 Cooked 联合验收 | 未通过 | 本轮 NullRHI/BuildPlugin 不能替代图形、Cooked 接触与软组织测量 |

脸颊/下颌/唇周/耳、颈肩/胸/腹/腰/背、臀髋/大小腿、上臂/前臂、指腹/足底及附加人体接缝，目前全部**未交付软组织等级**。眼球、牙齿、骨性支承沿用现有受控骨架，不宣称已经建立解剖物理模型。不得用骨骼数或 witness 数填充区域覆盖率。

07.0 未通过，因此禁止将本轮结果标为 Stage07 完成，也不得用于 Stage08/09 的真实体型碰撞能力声明。

## 隔离回归入口

先运行 `Build.ps1 -Engine <UE目录> -NoInstall`，再运行 `RunStage07Regression.ps1 -Engine <UE目录> -Project <uproject绝对路径> -Distribution <BuildPlugin输出目录> -ProbeConfig <显式JSON>`。

入口创建独立宿主、复制编译后的插件、运行事务与原有 Stage05 形状探针，并验证分发源码与当前 checkout 一致。每次结果保留在独立 `Saved/Stage07/Run-<ID>`，不覆盖历史报告，不安装用户编辑器 DLL。资产指纹覆盖探针列出的目录，不等于完整 Cook 依赖闭包。

## 初始事务修复证据（2026-09-24，非后续代码的最终验收）

`Evidence/Stage07/summary.json` 索引最终证据，最终运行 `Run-218bf20c2c994fa4944b1a322196522e`；完整独立宿主与日志保留在同 ID 的 Saved 目录。最终编译包含并行动画等待修复，Editor/Development/Shipping 通过；28 项既有 Python 测试通过。

新 DLL 在八参数人物的两个共享资产实例上通过事务探针：同值 Commit 前后 IK 位置变化 0，另一实例变化 0；动画时间始终保持 0.370000 s；参数值 0/0.5/1 下，Expression 权重均保持 0.800000。无效 Expression 批次和无效 LocalResponse 请求没有部分写入。

原 Stage05 形状内核探针在同一八参数资产上重新运行通过，最大真实求值关节误差约 3.18e-14 cm。这不是沿用旧六参数证据。验收范围仅为文中事务修复：没有把单节点动画时间保留说成默认宿主的动画/IK/物理组合，也没有宣称两个不同构建比例或抓取/足接触门槛通过。新 DLL 尚未安装到用户正在运行的编辑器。

## 07.0A 继续实现：通用原生配置事务

新增 `BuildRuntimeConfiguration.ps1` 与 `ue_runtime_build.py`。入口必须显式提供 Engine、Project、Distribution、Recipe、Report；旧 Stage06 build/material/pose-rig-upgrade 脚本转发到同一个配方事务，不再选择固定人物或最新 Saved 文件。旧 Stage06 资产路径保留，不自动替换旧关卡。新配方示例位于 Config/Examples，家族映射与明确的有限控制角位于 Config/RigFamilies。

构建器验证持久化 SourceMapping 合同摘要、Definition 来源/绑定/MorphSet 一致性，并从实际 SkeletalMesh 读取骨名、父链、局部绑定。家族模板仅保存源中性父链和局部轴，不复制人物坐标与骨长；关节优选方向不再按名字猜正负。配置、家族策略、依赖资产闭包指纹、代码与 Engine 版本共同参与派生身份。

新原生 RC_Runtime 保存 Definition/Rig/Anim/Physics/Materials 引用、来源身份与不可编辑的构建回执。宿主通过 RuntimeConfiguration 装配；加载时再检查来源、绑定与 MorphSet。派生包的保存/独立重载分为两个进程；只有指纹、序列化限位和宿主引用均一致才提交。缺失 Saved 报告时，可以从 RC_Runtime 恢复并重验，不能猜测来源或覆盖用户修改。

物理胶囊从目标人物实际网格生成，约束 frame 从该人物绑定计算，与父局部摆姿轴对齐。未映射的受控部位以及物理父子跨越多个源关节的链明确锁定并写入回执，不能把复合链伪称为已校准的单关节。当前有限对称控制角仍未完成解剖单向铰链校准。

本次两人物构建与独立重载证据在 Evidence/Stage07/RuntimeConfiguration。主配方源绑定头部高度约 157.10 cm，第二个独立来源约 154.42 cm；不是同一实例缩放。二者共用经过父链/局部轴验证的 88 骨家族策略，分别生成自己的 Rig/Anim/Physics/Material/宿主。其它骨架家族未验证。

阶段能力矩阵中旧的“07.0A 未完成”描述是前一修复批次的快照；本节更新其构建入口、身份与保存重载状态。07.0 联合门槛仍未通过，尤其是动态体型碰撞适配、基础动画组合、足接触和求解时间协议仍需继续实现。


## 默认宿主基础动画组合检查点

`Evidence/Stage07/AnimationComposition` 保存独立重载后的两个配置、版本化测试地图及真实 Play 世界的 PostUpdateWork 测量。默认宿主从配置加载已有的 A_VamIdle，在同一个代理中按“基础 clip → 形状绑定修正 → 主动动作/摆姿 → PBIK → 限位 → 引擎物理混合”执行；RefPose 输入仍可选。当前输入明确限定为同 Skeleton 的非 additive、无 root-motion clip，尚不是玩法动画状态机，不执行 gameplay notifies。

两独立人物的实际动画行程均约 3.00 s，组件空间骨位移峰值 3.64/3.59 cm。最终物理混合后的左手 IK 最大误差 0.367/0.334 cm（预设阈值 3 cm）；呼吸/眨眼峰值约 1/0.992，右手物理抓取位移 2.26/2.28 cm（预设最低 1 cm），释放瞬间速度差 0。该检查点发生在动态碰撞形状适配之前，不能用于证明后续版本已通过。

后续碰撞拟合使用原生 LOD0 位置、实际蒙皮与 Morph 的预计算映射，保存 cookable PhysicsShapeProfile。胶囊是骨架刚性碰撞代理，不是体积软组织。形状事务预检局部包围盒比例域 [0.2, 5]；无有效表面点的骨体显式禁用碰撞，仅作刚性锚点。运行时用私有 PhysicsAsset 更新胶囊与约束 frame，不修改共享源包。动态重建、速度/抓取保留尚在独立 Play 回归中验证，不因此宣称 07.0 通过。


## 碰撞事务检查点

`Evidence/Stage07/CollisionTransactions` 是碰撞拟合版本的独立证据。三个实例（两个独立人物，其中主配置重复一次共享源资产）各完成一次 minimum/中点/maximum 的抓取中形状事务。每个事务包含 Preview、Commit、临时 Preview、Cancel 和再次抓取；动画实例/播放位置、抓取、线速度、已提交的碰撞几何均保持预期，共享源 PhysicsAsset 和其它实例未变化。三实例各 3 次，线速度瞬时差均为 0。后续更严格版本另增加“实际碰撞必须发生尺寸/位置变化”、地面接触误差、Chaos 版本与延迟的断言，不能把旧检查点替代这些新增要求。

## 时钟、碰撞输出与足接触（实现中）

新增 PhysicsOutput 组件通过 Chaos SimCallback 的 PostSolve 阶段发布真实步结束时刻；游戏线程读取到达外部结果时刻的队列，不访问异步线程内部时钟，也不手工 Tick 世界。发布中区分 solver 完成时刻、solver 结果插值时刻与 world 发布时刻，携带实例装配代次、ShapeRevision、动画生产版本和 TeleportRevision。延迟超过预设 0.1 s、版本不符或无生产结果时标无效。CPU capsule 输出仅代表实际刚性碰撞体；没有假装提供皮肤表面，SimulatedSurface 保持空。

足锁通过地面射线、坡度限制及本实例当前足部胶囊计算支承目标，保存支承物体局部接触点。每帧更新支承位置/当前体型的接触高度；失去支承则取消该帧 IK 目标，显式 Teleport 清理旧世界锚点。最终误差由 PostUpdateWork 探针测量，尚在验证中。

首次时钟 Play 测试触发 UE 5.8 SimCallback 基类 PreSimulate 断言（`Composition-0000904c2d14430e93b9f5cb661b9dec`）。已补齐空 PreSimulate 实现，继续验证；没有用编译通过代替运行通过。

原 DebugPanel 新增“加载内容浏览器所选 RuntimeConfiguration”入口，从持久化回执定位并验证该配置的宿主 Blueprint。原“最新导入人物”保留并明确标为旧样例。P/O/R 及面板按钮明确只操作见证时钟，Chaos 和动画继续运行。原 root 拖动的 LocalResponse 切换和摆姿点用途已补充说明。


地面回归进一步发现：仅用脚部目标不足以适配该来源的整体高度。主样本脚踝在动画中约 21.4 cm，地面几何要求约 7.5 cm；固定躯干根部后最终误差约 10.7 cm，超过事先设定的 3 cm。`Composition-750bc26e3ee84ea8bb9040aa2821c2bb` 保留此失败，不降低阈值。后续修复从未经地面修正的基础姿态计算支承高度差，在动画层对躯干根部做有界平滑适配，再解腿部 IK；不拉长骨骼、不修改共享绑定。待重新验证。
