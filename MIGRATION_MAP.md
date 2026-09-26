# MH00 — 接入映射与操作入口

**旧样例 `启梦MH00/AssemblyV3` 存在拟合形状缺陷，暂不可作为正确人物使用。工程验证仍只证明记录的范围，修复详情见 [MH00_FIT_REPAIR.md](MH00_FIT_REPAIR.md)。**

## 保留与替换

| 现有部分 | MH 分支处理 |
|---|---|
| 索引、锁定 Plan、Stage03 解码 | 原样复用，不再复制一份 importer/decoder；逐项验证 Plan 与源文件/VAR member 哈希 |
| SourceIR / SourceMaterialIR / SourceMapping | 保留锁定文件与哈希；创建独立 SourceMapping 记录，不改旧人物；重型 JSON 为 EditorOnly |
| `ue_native_job.py` + RuntimeImportPolicy | 保留为明确 Native 回退和旧人物升级，MH 不执行 native build → Flesh build |
| 原生成按钮 | 默认 MetaHuman；填写用户名称。资源碰撞返回重命名，不生成 C_hash / Name_1 |
| native ShapeAnimInstance | 仍仅驱动 native；MH Face 由官方 DNA/RigLogic、Face AnimBP 和后处理驱动 |
| Body | 官方 MH Body 骨架/动画/后处理；MH00 不将 VamFemale88 的 Motion/IK/physics profile 套到新 Body |
| Hair | 后续官方 Groom + 对应 MH mesh/LOD 的新 GroomBinding；本阶段只隔离，不移植旧绑定 |
| Clothing | 后续 Chaos Cloth/Outfit 与新 MH body 绑定；作者流程 Editor-only；MH00 不宣称完成衣物仿真 |
| 特殊解剖扩展 | 独立项目资产与实例状态；本阶段不纳入拟合或云请求，不修改 MH 官方面部拓扑 |
| 事务 | 独立 job/版本化 recipe、逐资产保存回执、独立 source/assembly reload、依赖闭包；取消保留 Draft |

## 旧路径的具体耦合

`STAGE071_RUNTIME.md` 与 `VamSoftTissueComponent.cpp`：当前每帧合成 Morph、最终骨架与 Flesh 位移，更新全 LOD0 ProceduralMesh。`ShapeChanged` 对每个 Shape 事件调用 ResetSoftTissue；`BuildRuntime` 精确核对 Profile.Body、BindSignature、MorphSetLockDigest、BackendVersion。新的 MH 顶点/骨架/LOD 不满足这些绑定，不能直接复用旧 profile。

`VamCharacterComponent.cpp` 加载时调用 `Body->SetAnimInstanceClass`（默认 VamShapeAnimInstance）。MH 完整 BP 不包含该组件，所以不会用旧动画类覆盖 MH Face。`VamMetaHumanComponent` 只保存实例身份与 ShapeRevision/SurfaceRevision/EquipmentRevision；口红/肤色的 surface 通知不触及 shape/equipment/physics。实际材质参数控制、体型改动重绑、物理扩展在后续阶段实现，这三个通知不是已经交付的捏人/物理系统。

## 操作

1. 完成 UE 5.8 Creator/Core Data 安装；本分支 uplugin 启用 MetaHumanCharacter，保留其官方依赖闭包。编译并重启 UE。
2. 仍通过原资源浏览器索引、锁定、解码与解析来源材质。点击 **生成 MetaHuman 人物**，填写目标目录与人物名称。
3. 任务在 `Saved/MetaHuman/Jobs/<任务身份>/` 保存 request、recipe、target、capabilities、status 和日志。哈希/任务身份只用于缓存；资产路径使用人物名称。`SM_ConformTarget` 是 Editor 输入中间资产，绝非最终人物。
4. 未完成的状态按 `status.json` 恢复。安装/登录/服务修复后可运行下方脚本。取消任务重试须显式加 `-ResumeCancelled`。恢复不会覆盖未经当前回执确认的源资产。

```powershell
# 插件目录内执行；Job 替换成实际任务目录
.\RunMetaHuman.ps1 -Job '.\Saved\MetaHuman\Jobs\<任务身份>'
# 仅在已阅读具体上传内容并愿意授权时使用；已有同范围授权会直接沿用，否则显示内容并等待 AUTHORIZE
.\RunMetaHuman.ps1 -Job '.\Saved\MetaHuman\Jobs\<任务身份>' -Mode rig
# 官方 Editor 中手动校准/下载纹理并保存 Character 后，明确采用其新状态
.\RunMetaHuman.ps1 -Job '.\Saved\MetaHuman\Jobs\<任务身份>' -Mode adopt-calibration
```

5. 拟合不能自动对齐时，在官方 Character Editor 的 From Custom Mesh 选中此任务 `Source/SM_ConformTarget`，保存校准后的 Character，再采用 `adopt-calibration`。其配方保存点/曲线/相机，可重复用于同一锁定目标。通用 5.6/5.7 不能绕过拓扑转换。
6. 完整 Assembly 输出位于 `<目标>/<人物名>/Assembly/<人物名>/BP_<人物名>`，公共资源位于任务自己的 Assembly/Common。源 Character 是 `<目标>/<人物名>/Source/<人物名>`。只有 `assembly-verified.json` 通过才报告 `VerifiedEditorAssembly`，且该状态不代表 Cooked/视觉验收。

## 验证与限制

本次工程实测记录在 `Evidence/MH00/summary.json`。独立 reload 脚本为 `ue_metahuman_verify.py`；运行验证不能拿“启用插件”“生成截图”“测试 Actor 成功”替代。完整 BP 可在普通关卡放置/动态 Spawn，其 adapter 无关卡服务依赖；最终实际 MH 资产可用后仍须做 Cooked Spawn/Destroy、双实例及依赖检查。

本机工程样例（2026-09-25）：任务 `Saved/MetaHuman/Jobs/MH00-local-proof`，Character 为 `/Game/MetaHumans/启梦MH00/Source/启梦MH00`。这是使用工程测试名创建的独立源资产，不改原预设名称。锁定输入为 16,425 顶点、32,734 三角形；保留 47 个 p0 Morph、排除 10 个表达式 Morph。本地官方拟合及独立 source reload 已通过，完整 AutoRig 和官方纹理请求已成功。最终 AssemblyV3 已通过独立进程验证：Face 8 LOD、Body 4 LOD、双方 DNA 与后处理、Face 主动画及依赖蓝图编译、312 项依赖闭包、95 个纹理尺寸检查。正式 BP 为 `/Game/MetaHumans/启梦MH00/AssemblyV3/启梦MH00/BP_启梦MH00`。早期 Assembly/AssemblyV2 保留为 Partial；隔离工程 Build/Cook/Package 及实际打包程序的动态生命周期检查已通过（Development、NullRHI）。`upload-disclosure.json` 与授权回执保留云请求时的 Character 哈希；早期 Partial 目录不可作为成品使用。

验证范围：新增 10 项 Python、6 项相关旧 Python 测试、1 项 UE RevisionIsolation 自动化通过，Editor/Game 插件编译通过。第一次 Python 返回值处理错误与一次 UBA SDK 文件读取失败均保留日志；修正返回值处理、采用 NoUBA 后重测通过。最终在不含 SourceIR/源 Character 的独立工程完成了 Cooked 生命周期验证；不宣称视觉或性能验收。

已发布 native 目录未改写。MH 任务不需要先构建 NativeRuntime 或全 LOD0 软体。p0 几何应用锁定 Morph 顶点位移，排除带 expression/pose 标记及 graft；形状 Morph 中烘焙的表情可能需要显式配方分类。source scale/orientation/rotation 等未执行公式作为限制保留，不声称与原 VaM 人物严格一致。

源纹理、Groom、Cloth、人体扩展与交互适配尚未完成，不能把 capability 探测当成这些功能的交付。人工观察另见 `MANUAL_REVIEW.md`。

Cooked 自测入口：`RunMetaHumanCooked.ps1 -Job <已验证任务> -OutputRoot <新的输出目录>`。它将已验证的游戏依赖复制到隔离工程，以无人物/solver 的空关卡启动；开发命令 `vam.MetaHuman.Verify <BP生成类路径>` 动态生成两个实例，检查 BeginPlay、Face/Body 动画、表面通知不重建 AnimInstance、状态隔离、EndPlay 与重新 Spawn。调用者测试代码不进入 Shipping，成品 BP 不引用它。此自测不检查视觉、性能或后续阶段的衣发/物理扩展。

本机复现产物：`SmartNPC/Saved/MetaHumanCook02/PackageLoose`。本地 Zen 输出不可用时采用本机源码支持的 `-AdditionalCookerOptions=-SkipZenStore`，并保留失败日志；完整记录见 `Evidence/MH00/cooked-lifecycle.json`。正式 BP 可在普通关卡拖入，或用 Spawn Actor from Class 动态生成。Body 未配置项目 locomotion 主 AnimBP，保留官方 Body 后处理；项目运动与交互仍属后续适配。
