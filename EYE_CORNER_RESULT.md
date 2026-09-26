# 眼角修复 V20：实际结果与边界

2026-09-26，MetaHuman 分支，UE 5.8.3。此次优先修复眼周拓扑对应与穿插，鼻部没有新增专项修补。没有使用人物名、Preset ID 或固定空间坐标决定算法行为。

## 可打开的资产

- MetaHuman Character：`/Game/MetaHumans/启梦MH眼周稳定修复/启梦MH眼周稳定修复`
- 完整 Assembly BP：`/Game/MetaHumans/启梦MH眼周稳定修复/Assembly/启梦MH眼周稳定修复/BP_启梦MH眼周稳定修复`

在内容浏览器进入第一个目录，双击 **MetaHuman Character** 资产进入 Creator；BP 用于关卡放置或动态 Spawn。已绑定的 Character 如需雕刻，应在自己的编辑副本中移除绑定后编辑，再重新绑定和 Assembly。该 BP 是官方组装 Actor，不等同于已经配置第三人称输入及移动动画的可控制 Pawn。

## 问题与通用修复

旧流程把上下眼睑合并成无序点集合，并独立寻找最近位置；眼睑边界又同时受普通表面拟合牵引。眼眶内壁与外侧皮肤没有区分，造成错误吸附、曲线局部聚集和皮肤穿插。扩大检查范围后发现旧输入中也已有穿插，因此本轮使用保存的、更早的官方 Conform 中间结果作为干净起点。

新算法按官方四边形连接恢复闭合眼睑环，再以保持顺序和弧长间距的周期曲线约束做对应。切开眼睑边界后，通过拓扑连通性区分外侧皮肤与眼眶内壁，限制各自匹配的源表面。边界退出普通最近点拟合，使用一致的曲面/曲线插值；按源与目标采样密度调节局部正则项，降低上眼睑波纹。眼周测地邻域增加自交、间距和翻面保护。所有人物共用这些规则和四组候选；本次自动选择 Balanced。

改进发生在 **官方 Template 写回之前的受约束残差拟合**，不是修改最终 SkeletalMesh。实际调用 `FitStateToTargetVertices`、`CommitFaceState`、`CommitBodyState`、`RequestAutoRigging(JointsAndBlendShapes)`、`RequestTextureSources` 和 `BuildMetaHuman`。AutoRig/纹理继续使用已授权的 Epic 服务。完整绑定后再次导出、检查，再组装。

## 实际检查

同一 3513 顶点眼周检查区，使用实际官方完整绑定后的皮肤网格：

| 指标 | 旧 V9 | 新 V20 |
|---|---:|---:|
| 不共享顶点的皮肤三角形穿插对 | 303 | 0 |
| 外侧皮肤到参考曲面的平均距离 | 0.1221 mm | 0.1777 mm |
| 外侧皮肤到参考曲面的最大距离 | 2.2035 mm | 2.5340 mm |
| 法线差异（1−cos，越低越好） | 0.1532 | 0.1356 |

这说明穿插问题得到修复，**不说明所有贴合指标更好**：平均距离增加约 0.056 mm。眼角斜沟和眼睑轮廓仍与 SOURCE 有可见差别，不能宣称已经完整还原。

47 项脸部测试及 13 项 MetaHuman 工程测试通过。两次独立重载的头部顶点差为 0；绑定结果与最终 BP 头部顶点差为 0。Assembly 验证了 DNA、Face 8 LOD、Body 4 LOD、面部动画和后处理及依赖闭包。Cooked 生命周期的独立结果另见本页末尾记录。

补充检查仅共享一个顶点的三角形对，实际绑定后也未检测到严格穿插，见 `shared-vertex-crossing-audit.json`。原 native 人物 114 个文件及用于本轮复制的 MH 源 Character 哈希未改变。旧 V9 Character 当前文件与历史绑定回执哈希不一致，缺少本轮开始时该文件的哈希，不能归因或声明其字节未变；保留当前文件，没有回滚。对照图中的“旧版”明确取自已保存的 V9 官方绑定后几何导出。

## 文件与复现入口

主要代码：`Scripts/vam_face_eye_correspondence.py`、`vam_face_eye_regions.py`、`vam_face_eye_intersections.py`、`vam_face_adaptive.py`。候选与官方写回检查在 `vam_face_adaptive_cli.py`、`vam_face_adaptive_verify.py`。报告与对照图在 `vam_face_eye_report.py`、`vam_face_eye_views.py`。

通用保存请求入口是 `Scripts/vam_face_pipeline.py --request <pipeline-request.json>`，字段和完整流程见 `FACE_FIDELITY.md`。新任务默认启用本轮眼周保护；输入必须是干净的官方拓扑头部和锁定的中性 p0。发现初始眼周自交会返回可恢复错误 `InitialEyeSkinSelfIntersection`，应选择之前验证过的官方 Conform 中间结果及新的输出目录。尚未接入原导入按钮的一键完整流程。

本次证据目录：`Saved/MetaHuman/FaceFidelity/EyeCornerV20/`。

- `eye-comparison.png`：来源／旧版／新版三列，双眼正面与 ±45°。
- `post-rig-metrics-seven-views.png`：完整绑定后，0°、±45°、±90°、pitch ±20°。
- `eye-repair-report.json`、`post-rig-metrics.json`：几何指标和检查结果。
- `Official/Lifecycle/assembly-verified.json`：正式 BP 验证。
- `AlgorithmSnapshot/`：本次实际求解代码；`orientation-fallback-hardening-replay.json` 记录后续异常分支加固对本次对应结果没有改变。

这些是同相机的皮肤几何灰模图，不是 Unreal 最终材质截图，不能用于判断睫毛透明度或辅助眼网格的最终显示。人工观察步骤见 `MANUAL_REVIEW.md`。

来源细分语义仍未人工确认；有序环不是已确认的泪阜/眼角语义真值。G2M 及其他人物尚未完成端到端回归。离散中性网格检查不能证明共面接触、动画全过程或所有辅助网格都无碰撞；眨眼、转眼、各 LOD 和最终材质仍需人工观察。原 native 资产与旧 MH 成果保留，失败/取消实验不作为交付资产。

## 本次 Cooked 结果

2026-09-26 17:07，本次新 BP 已在单独复制依赖闭包的工程中完成 Game 编译、Cook、Package 和实际打包进程运行。结果文件为 `I:/Document/UE5/SmartNPC/Saved/MHEye20/result.json`；本次任务目录另存 `cooked-lifecycle.json` 指向该回执并锁定哈希。

验证覆盖空白关卡动态生成两个实例、BeginPlay、官方面部动画与后处理实例、SurfaceRevision 隔离且不重建 AnimInstance、EndPlay、销毁后重新生成。测试工具仅是调用者，BP 不依赖预放测试 Actor。运行使用 NullRHI，因此这项检查不验证画面、眨眼形态、睫毛材质或视觉还原。汇总见 `Evidence/FaceFidelity/EyeCornerV20-summary.json`。
