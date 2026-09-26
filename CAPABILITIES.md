# MH00 — 本机能力与真实边界

**拟合缺陷更正：旧 `启梦MH00/AssemblyV3` 虽通过工程生命周期检查，但存在已确认的方向/三角形绕序错误，不能作为正确人物转换的交付。修复与手脸校准工作见 [MH00_FIT_REPAIR.md](MH00_FIT_REPAIR.md)。**

2026-09-26 的通用脸部残差修订已另行完成真实 Character/AutoRig/Assembly 与 Cooked 生命周期验证，实际路径、对比及仍未确认的语义见 [FACE_FIDELITY_RESULT.md](FACE_FIDELITY_RESULT.md)。本页下文保留 MH00 初次接入的历史记录。

2026-09-25。仓库为 `SmartNPC/Plugins/VamResourceBrowser`，开始时分支为 `MetaHuman`，HEAD 为 `1b639e382d556b0423aa49f465c63b09388219bb`，未提交修改为空；相对该基线没有既有差异。本次保留 Stage05—07.1 和旧 native 输出，不重排 Stage05—09。

## 版本与探测

实读 `I:/Program/Epic Games/UE_5.8/Engine/Build/Build.version`：**5.8.2，CL 56702186，Compatible CL 55116800**。
安装过程中本机升级为 **5.8.3，CL 58210709**；后续 `Build.version` 与真实 UE 进程 probe 均确认这一版本。配方分别保留输入准备时版本与执行时版本，不混淆两者。
以本机头文件/实现为准，不把文档中的预想功能当成可调用 API。

`Scripts/vam_metahuman.py --engine <安装目录> --out <capabilities.json>` 检查安装版本、插件描述符及哈希、模块类型、依赖、公开符号、coredata 文件。
`unreal.VamMetaHumanEditorAdapter.probe()` 检查运行中的版本、插件启用状态、官方 coredata 检查结果及渲染 CVar。文件存在与插件启用分别记录；两者都不能证明完整功能。

开始检查时 `MetaHumanCharacter/Content/Optional` 未安装；用户随后表示正在安装 Creator 和 Chaos Cloth Creator，代码按安装后的能力编写。运行时仍重新检测，不将“正在安装”伪记为已验证。最终实测见 `Evidence/MH00/summary.json`。
后续真实进程已确认 coredata 可用，MetaHumanCharacter、RigLogic、HairStrands、ChaosClothAsset、ChaosOutfitAsset 均启用；ChaosClothAssetEditor 已安装但本项目未启用，未把它列为已测试的衣物作者工作流。SkinCache 编译/默认行为、HairStrands、VirtualTextures 的探测值均为 1。

## 本机公开入口

以下路径均相对 `Engine/Plugins/MetaHuman/MetaHumanCharacter/Source`。

| 能力 | 实际接口与生命周期 | MH00 使用范围 |
|---|---|---|
| Character 创建 | `MetaHumanCharacterEditor/Public/MetaHumanCharacterFactoryNew.h`；Factory 调用 `InitializeMetaHumanCharacter` | 创建真正可编辑的 Character 源资产，先预检名称 |
| 编辑生命周期 | `MetaHumanCharacterEditorSubsystem.h`：`TryAddObjectToEdit` / `RemoveObjectToEdit` | 成对注册/释放；失败不进入拟合 |
| 5.8 From Custom Mesh | 同头文件：`ConformTargetMeshesAsync`、`ConformToTargetMeshes`、`FConformTargetParams` | Combined 头身目标；自动管线名 `combined` 来自本机工具实现 |
| 拟合完成与取消 | `OnAsyncMeshConformCompleted(bool,bool)`、`IsAsyncConformPending`、`CancelMeshAsyncProcess` | commandlet 使用官方 blocking wrapper，检查返回值及完成回调；任务在进入/退出求解时检查取消，不粗暴杀进程 |
| 从目标姿态恢复 MH A Pose | `CommitPosedStateAsAPose`、`CommitFaceState`、`CommitBodyState` | 仅求解成功后提交；不输出 Save Pose DNA 冒充人物 |
| 完整 AutoRig | `RequestAutoRigging`，`FMetaHumanCharacterAutoRiggingRequestParams`，`JointsAndBlendShapes` | 云请求；明确授权后才调用。官方 `OnAutoRigFaceRequestCompleted` / `Failed` 处理结果；blocking 等待后检查 Face DNA、blendshapes、Body DNA |
| 完整 Assembly | `CanBuildMetaHuman` / `BuildMetaHuman` → `Subsystem/MetaHumanCharacterBuild.h::FMetaHumanCharacterEditorBuild::BuildMetaHumanCharacter` | 默认 Cinematic，独立 Common 目录；返回 void，必须检查真实生成 BP 并独立重载 |
| 预览 | `SpawnMetaHumanActor`、`AssembleForPreview` | 不用于成品判定 |
| 5.6/5.7 From Template | `ImportFromTemplate` 明确要求 MH 拓扑/UV 对应 | 本 adapter 拒绝跨版本运行；先做官方一致拓扑转换才可添加后备。Genesis2 原拓扑绝不是模板 |
| 材质 | `CommitSkinSettings`、`CommitMakeupSettings`；Default pipeline Material override | 保留官方材质体系；MH00 未迁移源纹理，不能声称材质等价 |
| Groom / Cloth | 描述符依赖 HairStrands、ChaosClothAsset、ChaosOutfitAsset；Default pipeline 有 Groom/Outfit editor pipelines | 能力发现；本阶段未建立自有衣发绑定，也未验证新安装的 Creator 作者工具 |

核心数据的官方检查在 `MetaHumanCharacterEditor/Private/MetaHumanCharacterEditorModule.cpp::IsOptionalMetaHumanContentInstalled`：TextureSynthesis 下至少一个 `.ar`、BodyTextures 下至少一个 `.uasset`，并要求相应目录存在。此检查仍不是完整数据校验，模型加载/版本失败继续返回可恢复错误。

## 自动化与手动校准

本机 From Custom Mesh 工具的自动脸部追踪依赖 Editor viewport/ContourMechanic 的 `TrackFaceWithAutoFraming`。该 UI 工具不是通用公开 subsystem 自动捕获入口。公开 `TrackFaceLandmarksFromImage` 存在，但调用者仍需提供正确图像/相机。

MH00 的无标记点本地 Combined 拟合调用是真的；尚未证明对本输入可可靠自动对齐，失败保留 Draft。需要校准时使用官方 From Custom Mesh 操作目标 `SM_ConformTarget`，保存 Character。`adopt-calibration` 将标记点、轮廓曲线、相机与图像尺寸导出为配方 `calibration`，不保存鼠标坐标；这些求解参数可重新传给 Conform。导出的求解 settings 为 MH 默认值，人工调整过的求解参数须在 calibration JSON 中显式记录。未复制 paid Blender 插件代码，也不假定离线资产可再分发。

## 网络与授权

本地输入准备、目标网格构建、Conform 不调用本项目新增的网络服务。AutoRig 由 Epic service 执行；`Subsystem/MetaHumanCharacterService.cpp::InitFaceAutoRigParams` 输入包括**拟合后的 MH** 头、牙、眼及相关面部子网格顶点、bind pose、模型系数/标识、scale 和高频变体，不上传原 SourceIR、VaM 目录、原衣发或私密扩展。

代码保存 `upload-disclosure.json`，绑定已保存 Character 文件哈希。授权可来自该 disclosure 的明确单次授权，或本地 `Saved/MetaHuman/cloud-consent.json` 中已有的同服务、同数据范围授权。2026-09-25 用户明确指示“发送吧，之后这个不需要问我”，本机已保存此范围授权；后续同范围任务不再询问。服务、操作、字段或排除项变化即不匹配，仍需新授权。授权文件留在本机 Saved，不向插件使用者分发；每个后续请求保存独立 disclosure 哈希授权凭据。Epic 账号登录仍由官方身份验证完成，与上传许可分开。

`CanBuildMetaHuman` 还要求 `HasHighResolutionTextures()`。官方 `RequestTextureSources` 是另一项云服务；同一 disclosure 另外列出纹理请求的 face high-frequency index、body tone/surface-map、纹理类型和分辨率；仅在用户明确授权该完整 disclosure 后调用。原始贴图不会上传，也可在官方 Editor 中操作后保存并采用 `adopt-calibration` 继续。认证、服务失败、纹理缺失、coredata 缺失都保留 Draft/Partial。

## Runtime / Cook

5.8.3 实测发现 TextureGraph 默认阻止 commandlet 初始化；仅移除 NullRHI 不足以完成材质烘焙。正式生成入口必须同时传入 `-AllowCommandletRendering` 和 `-ini:Engine:[ConsoleVariables]:TextureGraph.AllowCommandlets=1`。依据为本机 `TextureGraphEngine.cpp::BlockCommandlets` 及 `TG_AsyncExportTask.cpp`；首次失败产物保留为 Partial，未绕过材质检查。

生成前还必须执行 `AssetRegistry.search_all_assets(True)`。本机 `FMetaHumanCommonDataUtils::SetPostProcessAnimBP` 使用 `GetAssetsByPackageName`，找不到条目时会把 Face 后处理置空；普通 commandlet 不能假定启动时资源发现已完成。`BuildMetaHuman` 返回 void，且不会替调用者完成整组资源保存。任务会保存全部输出、保留 Control Rig 迁移诊断，并在独立进程重新编译依赖蓝图；只有结构、资源与编译检查通过才升级状态。Body 官方配置可只有后处理而没有主 AnimBP；检查实际生效的后处理类，不自行替换官方动画图。

所有生成与拟合接口只在 Editor 模块；新增 Runtime adapter 无 Editor/MH Creator/Python 依赖，不读取来源路径，不调用云服务。它附加在官方 Assembly BP 上，不派生旧 `AVamCharacterActor`；其 Ready 仅表示找到 Face/Body，并非人体、动画或物理质量通过。

最终依赖必须包括官方 Face/Body、DNA/RigLogic、动画/后处理、LOD、HairStrands/Cloth 等实际引用。独立验证器遍历 BP 的软/硬依赖并检查资源可加载、禁止 Editor Python/CreatorEditor/测试图和 source 资产方向；记录普通编辑器世界动态 Spawn 的结构。此检查不替代 Cooked 生命周期。

原项目已设置 DX12、SM6、`r.SkinCache.CompileShaders=True`。运行 probe 另记录 SkinCache 默认行为、HairStrands、VirtualTextures；不静默修改全项目渲染配置。本阶段保留旧 native 插件依赖供回退，MH BP 不依赖 native 人物/软体 profile。尚无验证过的最终 Assembly 时不得宣称已完成 Cook 或运行期生命周期。
