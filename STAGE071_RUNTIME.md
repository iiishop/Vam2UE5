# Stage07.1 人物自身的软组织能力

本页描述新的正式 Runtime 接入。旧 `AVamFleshCapabilityActor` 和其 Cooked 包只保留为后端实验，不能证明本页功能。视觉验收由用户进行；编译、生命周期测试和资源依赖检查不代表外观质量通过。

## 使用与依赖

正式依赖为 `BP_VamCharacter → RC_Runtime → DA_SoftTissue`。`AVamCharacterActor` 默认拥有 `UVamSoftTissueComponent`。人物 BeginPlay 异步加载配置、原生人物及软组织配置，自动建立私有 Solver、Flesh、环境碰撞适配器及可见表面。关卡无需额外 Actor、Level Blueprint 或 Python 初始化。

无 SoftTissueProfile 的旧人物继续正常加载，软组织为 Disabled。配置不匹配或代理非法时只禁用软组织并报告 LastError，恢复原 SkeletalMesh 可见性。

实例组件提供 `SetSoftTissueEnabled`、`SetSoftTissueQuality(Off/Balanced/High)`、`ResetSoftTissue`。原 Character Debug Panel 提供对应按钮及状态。Shape Preview/Commit/Cancel、人物卸载和 Motion.TeleportTo 由正式组件处理；测试 Actor 不提供 rest、绑定或逐帧更新。

## 实现范围

- 后端：Chaos Flesh，人物私有实例。Balanced 为 2 子步、5 次迭代；High 为 4 子步、10 次迭代。一次 Runtime Tick 只推进该私有求解器，不手动 Tick 世界。长帧步长受 Profile.MaximumStepSeconds 限制，输出分别记录实际求解累计时间与发布世界时间。
- Profile 持久化原生 LOD0 拓扑、UV、颜色、蒙皮、Morph 和四面体表面映射。示例覆盖左右胸及左右大腿的骨权重区域，使用闭合工程笼；这不是已校准的解剖模型或全身覆盖。
- 可见表面：CPU 组合当前 Morph、最终骨骼姿态和 Flesh 相对蒙皮基线的位移，写入 ProceduralMesh。保留 Body 材质，原 SkeletalMesh 继续驱动动画/IK/刚体。没有逐帧 GPU readback；当前会逐帧更新全 LOD0，性能需单独验收。
- 环境：自动查询普通 WorldStatic、WorldDynamic、PhysicsBody、Pawn，并读取自身当前 PhysicsAsset。支持简单球、盒、胶囊、凸包；显式交互物可注册 PrimitiveComponent。仅三角网格碰撞、Landscape 尚未接入，UnsupportedColliders 会报告未支持项。测试物体名称不参与匹配。
- 形状：预计算几何映射更新私有 rest、质量、骨附着和碰撞输入；不运行时重新四面体化，不修改共享 Profile/PhysicsAsset。超出有效四面体体积比范围时软组织进入 Error，不能冒充形状支持成功。
- 附加人体网格仍走原人物链，目前未烘焙其软组织接缝映射。近景接缝、重力平衡、接触深度、体积误差及组织参数尚需后续交付。

## 输出协议

`GetBodySurfaceOutput()` 只在 Ready 且人物/Shape/Teleport 版本有效时返回 Valid。加载、重新绑定、关闭、卸载和重置先使输出失效；完成实际求解输出的 warm-up 后才恢复。

输出含唯一 Instance、CharacterGeneration、ShapeRevision、TeleportRevision、PoseRevision、SolverRevision、实际 SolverTimeSeconds、PublishedWorldTimeSeconds、局部空间与 ToWorld。可见表面通过 SurfaceResource 提供，粗碰撞通过闭合笼顶点/三角形提供。后续衣发必须检查 Valid 和版本，不能缓存旧 SurfaceResource 后无条件继续消费。旧 SimulatedSurface 数组不复制全高模；CharacterState 暴露资源与版本。

## 人工测试方式

完成下方运行证据后，以已提交构建回执中的 blueprint 路径为准，在 Content Browser 找到该 BP_VamCharacter，拖入新建空白关卡并 Play。不要添加 Flesh/solver/test Actor。预期人物自行进入 Ready，动画继续播放，软组织输出具有该实例自己的版本。复制第二个实例，分别切换 Off/High 和改变体型：另一个实例的形状与资源身份不应变化。

原 Debug Panel 的 Off 应立即恢复原人物表面，动画、IK、Morph 和刚体继续；重新开启经过 warm-up 后回到 Ready。Preview/Cancel/Commit 和 Teleport 会短暂使输出无效，随后使用新版本恢复。可在 Simulate 中移动普通带简单碰撞的球/盒，观察接触；接触外观是否合理由人工记录，不由此文宣称通过。

测试地图仅提供人物摆放、普通场景物和相机；自动审计图才包含生命周期探针。删去测试地图不会移除正式人物所需对象。

## 本机已构建入口（2026-09-25）

正式人物位于以下 Content Browser 文件夹，各自包含 BP_VamCharacter、RC_Runtime 和 DA_SoftTissue：

| 人物 | Content Browser 路径 |
|---|---|
| 主人物 | `/Game/VamRuntime/R_0acac32bac22c6821acfa124` |
| 第二种独立比例 | `/Game/VamRuntime/R_2cfdaea2278408f942ae8571` |

将任一文件夹中的 BP_VamCharacter 拖进自己的普通关卡即可 Play。不是把旧 Stage06 BP 改名；旧配置无 Profile，保持 Off。原项目插件 DLL 已更新，更新前的版本保留在 `Saved/Stage071/InstalledBackup01`。

也可以直接打开只负责摆放人物的人工测试地图：

```powershell
& 'I:\Program\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'I:\Document\UE5\SmartNPC\SmartNPC.uproject' `
  '/Game/VamRuntimeTests/SoftTissueRuntime02/L_Manual'
```

选择 **Simulate**，在 World Outliner 选中正在运行的人物，通过 **Window → VaM 人物调试** 打开原面板并点击“使用选中人物 / 刷新”，再使用软组织 Off/Balanced/High、形状和 IK 控件。观察状态由 WarmingUp 到 Ready。若使用 Play，先 Shift+F1 释放鼠标并选中 PIE 世界实例；不要在编辑器副本上操作。

此图没有测试探针、Flesh Actor、预放 Solver 或软组织初始化 Level Blueprint。图中的普通 WorldDynamic 球可以在 Simulate 时移动；其初始位置不保证接触任何区域，需要移到胸/大腿表面。人物移动仍走原连续 Move 控件；显式瞬移使用 Motion.TeleportTo。

Cooked 人工观察入口（无自动退出；用 Alt+F4 关闭）：

```powershell
& 'I:\Document\UE5\SmartNPC\Saved\Stage071RuntimeCook01\Package\Windows\HostProject\Binaries\Win64\HostProject.exe' `
  '/Game/VamRuntimeTests/SoftTissueRuntime02/L_Manual' -windowed -ResX=1280 -ResY=720 -csvNoProcessingThread
```

Cooked 人工图仅供观察自动加载和表面，编辑器的形状/IK/软组织操作入口仍在原 DebugPanel。自动回归入口 `TestSoftTissueRuntime.ps1` 显式接收 Executable、可选 Project 和 MapsReport；`Saved/Stage071/Build02/maps.json` 是本次地图回执。它只执行生命周期验证，不做视觉通过判定。

## 当前验证与未完成项

原生 Editor/Game 编译、41 项 Python 测试、两个独立配置的三进程发布与依赖方向检查已通过。两个 Editor 游戏模式测试均完成无配置人物正常加载、同资产双实例隔离、Off 后动画继续、High 重开、Preview/Cancel/Commit、Teleport、每人物三轮动态 Spawn/Destroy 及强制 GC 后组件释放。

同一 Cooked 包在 30/60/120 **帧率上限**下均运行真实图形生命周期测试并正常退出；这不是实际帧率或性能达标证据。每个运行均保留 `visual_acceptance_passed:false`。新增代码另通过旧 07.0 组合场景的 60 FPS 上限 NullRHI 回归，包含最终 IK、足接触、抓取/形状事务与停稳。

可审阅证据位于 [Evidence/Stage071/CharacterRuntime](Evidence/Stage071/CharacterRuntime/summary.json)，包括构建闭包、DLL/包指纹、地图依赖方向、隔离编辑器/安装后的原项目及三档 Cooked 生命周期报告。首次运行的抽象类分配崩溃保留在 `Saved/Stage071/Build01/audit1.log`，新版本使用具体的碰撞标识类修复；没有删除失败或将其计为通过。

还不能宣称完成：全身区域覆盖、近景挤压质量、重力平衡校准、附加人体网格软组织接缝、三角网格/地形碰撞、Cloth/Hair 的实际消费者以及长期 GPU/CPU 内存压力测试。当前三轮 GC 检查证明所测组件可回收，不能推导出长时间运行绝无资源泄漏。无需用户替开发者补做编译/Cook/生命周期测试；人工需要评价的是形状、接触观感、接缝与可接受的响应幅度。

## 正式导入与旧人物升级入口（2026-09-25）

资源浏览器的“生成 UE 人物资产”现在在原生资产保存、独立重载验证后，自动继续构建 Runtime 配置。只有 Runtime 的 build → reload → verify 三个独立进程全部成功，进度窗口才显示完成与最终 Runtime BP 路径。原生阶段成功但 Runtime 失败时，保留原生资产，报告失败和日志目录，不把原生 BP 当成软组织人物交付。

旧人物：先在 Content Browser 选中已保存的 BP_VamCharacter 或 CD_Character，再点击资源浏览器的“升级已导入人物软组织”，检查窗口中的资产路径并点击“升级并生成 Runtime BP”。也可直接输入 /Game 资产路径。此入口不依赖解码预览缓存，不要求重新导入；使用该人物持久化的 CharacterDefinition 和 DA_SourceMapping。升级有进度提示，成功后显示新 BP 的完整路径。请将新 Runtime BP 拖入关卡；原人物 BP 和已放置实例不会被自动替换。

默认区域/质量/骨架家族策略位于 Config/RuntimeImportPolicy.json；通用入口没有样例 Character ID 或 Quinn 路径。当前策略仅覆盖 VamFemale88，构建器会验证实际父链与局部轴，其他骨架家族不静默套用。若原 BP 已有 RuntimeConfiguration，则保留其回执中的动画、材质、家族及软组织策略；未配置软组织时才补默认策略。家族不支持、来源映射缺失、保存失败或版本不匹配均明确失败。

此更新只接通构建和升级流程，不改变既有胸/腿代理、求解成本和视觉质量，也不宣称修复 GPU 崩溃。

本次旧人物实测：`/Game/VamCharacters/C_b41dfaaabfc8427b0ef4e065` 升级输出为 `/Game/VamRuntime/R_d0d35bf99b4cfc9265acbf08/BP_VamCharacter`，原目录 119 个文件哈希不变。新导入入口用原始锁定请求重放（复用已验证原生资产），完成自动 Runtime 三阶段提交并复用同一 Runtime 结果。NullRHI 生命周期检查含两个共享资产实例 Ready、无配置原人物 Disabled、Off/On、Shape、Teleport、三轮 Spawn/Destroy；并非图形/性能或 GPU 崩溃修复验收。17 项相关 Python 测试及 Editor/Game 编译通过，插件 DLL 已安装，需重新打开 UE 才显示新按钮。证据见 Evidence/Stage071/ImportUpgrade/summary.json。
