# 跨预设结构转换检查

2026-09-26。新增 `fei` 和 `cat29` 两个独立 VaM 预设，旧启梦 V20 只作为历史参照。本轮不修改眼睑残差权重。两份新输入均来自原索引和锁定 SourceIR，重新校验源文件/VAR 内容哈希，使用中性 p0，隔离衣发、附件和 graft；没有复制索引或解码器。

## 方法与检查边界

两个人物都从空白 MetaHuman Character 开始，使用同一套官方 combined AutoSolve、按来源材质包围盒确定的相机、本地官方图像追踪和再次 AutoSolve。没有使用启梦历史上的专用锚点。分别保存 `Coarse`、`Tracked`、残差候选、官方完整绑定与 Assembly，避免只比较最终截图。

`Coarse` 特指尚未提供显式 2D 追踪参数的 combined 求解 API 调用；日志包含 Invalid image size 相机警告。它不等同于 Creator UI 中包含自动取景/追踪前处理的完整 Auto Solve 操作。`Tracked` 才补入本轮统一相机和实际检测到的 16 条曲线。这个阶段区别必须保留，不能把我们输入准备的不足全部归因于官方求解器。

正式 Face Fidelity 管线与官方结构诊断基线分开记录。前者保持原来的自交/翻面/重载检查；后者是明确标记 `diagnostic_only`、`DraftStructuralBaseline` 的完整官方生命周期，用来观察原始转换的缺陷如何传播到 DNA/Assembly。诊断入口不是正式几何检查的替代品，不会把失败候选送入正式发布路径。

目前两例都属于相同 G2F 拓扑，不能外推为 G2M 或全部预设已经支持。对齐同一来源的 3564 个人脸顶点、仅去除平移后，fei 与 cat29 的 RMS 形状差约 3.50 mm；相对启梦分别约 3.45 mm、5.36 mm。它们不是同一个人物的换装样本。

## 已得到的结构证据

| 人物 | 首次官方拟合的眼周穿插对 | 加入同一图像追踪后 | 当前残差管线 |
|---|---:|---:|---|
| fei | 4 | 22 | 初始自交检查拒绝，保留失败 |
| cat29 | 51 | 0 | 四组候选完成，选择 BoundaryStrong；Template → AutoRig → Assembly → Cooked 全链完成，但鼻部仍失真 |

这些是同一规则下的离散皮肤三角形穿插数，不是褶皱程度或视觉分数。观察灰模可见：两个新人物在第一次官方拟合时就已有源皮肤没有的内眼角斜沟、眼睑体积差异；鼻部和鼻唇过渡也有偏差。图像追踪对两例的作用不同，因此无法仅凭启梦判断流程可靠性。

其中 cat29 是明显的鼻部失败样本：初始转换的鼻尖、鼻孔和鼻唇过渡被压平，侧面与来源的差别很大。Tracked 基线参与评分的表面平均距离约 0.723 mm，但左右 MH 鼻孔曲线到来源表面的最大距离约 2.81 / 2.91 mm；这些仍然是表面距离，不是已确认的来源鼻孔语义误差。即使候选把选定表面的平均距离降到约 0.489 mm，也不能据此认定鼻部身份已经正确。

两份初始 Character 均完成完整 AutoRig、Assembly、两个独立进程重载和 Cooked 动态生成测试。按同一官方 A-pose 坐标比较，Tracked 到完整绑定的最大顶点变化均为约 0.000153 mm，绑定到 BP 的头部变化均为 0；因此，这两份基线的明显形变已存在于上游拟合。Cooked 测试使用 NullRHI，只验证运行生命周期，不验证最终渲染、动画效果或人物相似度。

当前残差管线在初始匹配时决定固定顶点，后续不再释放。按这套规则，Tracked 输入外侧眼周会固定 fei 133/2896、cat29 74/2790 个顶点；启梦 V20 为 189/2795。fei 在求解前被自交检查拒绝，不能把它的支持区域诊断说成已经执行过残差求解。法线不一致处可能恰好就是需要展开或改变的折线，这个固定策略可能保留错误初始结构。它是需要验证的机制，不是已证明的唯一原因。

鼻部出现了更直接的反例：cat29 左右 `crv_nostril` 各 33 个顶点，其中各 19 个永久固定，表面目标各仅覆盖 13 个；fei 对应曲线固定数均为 0。cat29 官方 Template 写回后的平均表面距离约 0.491 mm，但侧面鼻部仍明显失真。已有错误初始位置加上永久固定，构成残差无法充分修正的具体限制；不能把其数值检查通过写成鼻部还原成功，也不能只解除固定而忽略翻面/结构约束。

对应清单进一步显示：fei 有 `nostril.0`、`nostril.1` 两个临时边界对应，cat29 两者都缺失，仅有两侧眼睑与外唇缘。现有求解对没有建立的鼻孔对应继续执行，也没有将鼻孔对应缺失作为整体候选评分的必需覆盖项。这比“再调鼻子权重”更早，是需要修复的通用对应覆盖问题。

当前 `surface_mean_cm` 的 MH 侧采样范围由初始距离、材质归属及法线阈值选出，不包含全部 MH 脸部顶点；反向项也使用这一筛选后的表面。上面的平均值严格说是当前选定表面的距离，不是整个面部或解剖语义覆盖的完整误差。固定且排除的错误区域可能被这一评分低估。后续回归必须单独报告语义覆盖缺失和排除区域，不能用低平均值掩盖它们。

两例自动选择的来源眼睑候选都是同一 G2F 第 2 圈拓扑环；没有观察到这两例随人物切换到不同环。但“同一环”仍不等于它与 MH 内外睑缘、泪阜、眼眶内壁解剖同源，这一对应尚未确认。

来源都有 `Lacrimals` 152 顶点/128 多边形、`Tear` 104 顶点/100 多边形，目前均从皮肤目标排除；两者与其他材质没有共享的 source edge。来源还包含眼球及眼睑骨骼的中心、朝向、旋转、缩放等 Morph 公式，当前目标只保存而未求值。这些是丢失的结构约束，不足以直接证明已有皮肤 p0 坐标错误；也不能把未求值的眼球诊断顶点直接当作 native 中性眼球的准确位置。

连通性检查进一步确认：两例的来源 Lacrimals 都是 4 个独立连通分量，Tear 是 2 个；MH 的 `eye_plica_semilunaris_l/r` 标注则属于连通的 `head_lod0_mesh` 皮肤。也就是说，不能假设“源皮肤的一条眼睑环”已经完整表达 MH 眼角的全部结构。证据见各人物 `ocular-topology-structure.json`；这仍不是已经确认的解剖一一对应表。

本机 `MetaHumanCharacterEditorSubsystem.h` 的 `FMetaHumanCharacterFitToVerticesParams` 明确包含 `HeadVertices`、`LeftEyeVertices`、`RightEyeVertices`、`TeethVertices`。当前 `ue_metahuman_surface_template.py` 仅设置 `head_vertices`，没有将来源眼球信息转为 MH 眼球拓扑传入。fei 实际官方完整绑定导出中，左右眼分别是 770 个顶点；不能直接传入来源 G2F 眼球的不同拓扑。此项是已查实的输入覆盖缺口，不代表已经实现眼球结构拟合。

Epic 的 Custom Mesh 文档说明 AutoSolve 会自动做表面 refinement，手动模式才能分开控制求解阶段；最近表面投影本身并不能保证部件对应正确。[官方 Custom Mesh 文档](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-from-custom-mesh-tool-in-unreal-engine)

Epic 的 Identity From Mesh 文档也指出空眼眶不利于追踪。这是另一条官方工作流的指导，可作为检查当前空眼眶图像输入的依据，不能据此断言它就是 UE 5.8 combined solver 的唯一失败原因。[官方 Identity From Mesh 文档](https://dev.epicgames.com/documentation/metahuman/from-mesh)

官方 From Template 支持单独的左右眼输入；这提供了后续保留 MH 拓扑、明确眼球与眼睑关系的合法路径，不能把 G2F 眼球直接当作 MH 模板。[官方 From Template 文档](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-from-template-tool-in-unreal-engine)

## 本轮代码与入口

实际运行 UE 5.8.3（CL 58210709）。官方入口为 `UMetaHumanCharacterEditorSubsystem::ConformToTargetMeshes`（combined / AutoSolve）、`TrackFaceLandmarksFromImage`、`FitStateToTargetVertices`、`RemoveFaceRig`、`RequestAutoRigging`（JointsAndBlendShapes / blocking）、`BuildMetaHuman`。`SpawnMetaHumanActor` 只用于导出检查，不作为组装完成证据。已有适配器记录 `OnAsyncMeshConformCompleted`；AutoRig 使用官方阻塞封装并在返回后检查完整 rig。Epic AutoRig 及必要的官方纹理来源请求复用既有范围内授权；没有新增外部服务，也没有上传来源衣发、私密部件或原始纹理。

- `vam_face_cohort_input.py`：复用锁定导入器，统一中性输入和相机。
- `ue_face_cohort_initial.py`：两阶段官方初始转换与原样几何导出。
- `vam_face_cohort_report.py`：阶段对照、眼周穿插、固定区域及拓扑环对应。
- `vam_face_ocular_evidence.py`：提取被排除的眼部材质与来源拓扑证据，完全本地。
- `vam_face_cohort_lifecycle.py`：显式诊断基线的 AutoRig、Assembly、独立重载。
- `vam_face_pipeline.py --solve-only`：在官方写回前暂停，保留全部候选与原检查。
- `ue_metahuman_job.py`：增加限定用途的诊断入口；不能以普通 resume 将诊断资产当正式还原结果继续。
- `vam_metahuman.py`：修复 Pose Controls 分类漏排。cat29 的两个握手控制虽标记 isPoseControl=false，仍属于 Pose Controls/Hands；已排除。它们没有顶点 delta，本次脸部几何未因此改变。旧输入配方存档在 `cat29/InputPolicyV1`。
- `ue_metahuman_surface_template.py` / `vam_face_template_contract.py`：新增从已绑定来源副本继续 Template 编辑的检查。仅在新副本调用官方 `RemoveFaceRig`；以锁定的官方四边形验证皮肤的合法三角剖分变化，辅助网格则证明变化是保持同一有向四边形边界的唯一对角线替换，不接受其他重连。独立重载仍检查全部顶点位置。cat29 本轮共 12 处对角线变化，33,845 个 Face 顶点的重载位置差全部为 0；没有通过放大几何容差处理这一差异。

证据总目录：`Saved/MetaHuman/FaceFidelity/CohortStructureV1/`。各人物的 `request.json` 是独立输入请求；`pipeline-request.json` 使用相同求解参数。`structure-seven-views.png` 与 `structure-eyes.png` 的列标签注明阶段；均为皮肤几何灰模，不是 UE 最终材质截图。`ocular-source-evidence.json` 保存原始顶点/边身份和未求值公式，`cohort-manifest.json` 保存代码与输入选择。

## 打开实际诊断人物

在 UE Content Browser 定位下列 Character 源资产并双击，即可进入 MetaHuman Character 编辑器。需要修改已绑定人物的形状时，先另存自己的副本，再移除绑定；不要直接改变本轮留作阶段对照的资产。

| 人物 | Character 源资产 | 完整 Assembly BP |
|---|---|---|
| fei | `/Game/MetaHumans/feiMH结构检查初始/Source/feiMH结构检查初始` | `/Game/MetaHumans/feiMH结构检查初始/Assembly/feiMH结构检查初始/BP_feiMH结构检查初始` |
| cat29 | `/Game/MetaHumans/cat29MH结构检查初始/Source/cat29MH结构检查初始` | `/Game/MetaHumans/cat29MH结构检查初始/Assembly/cat29MH结构检查初始/BP_cat29MH结构检查初始` |
| cat29 残差候选 | `/Game/MetaHumans/cat29MH结构检查/cat29MH结构检查` | `/Game/MetaHumans/cat29MH结构检查/Assembly/cat29MH结构检查/BP_cat29MH结构检查` |

BP 可放入普通关卡或动态 Spawn；本轮在隔离 Cooked 项目中验证了双实例、BeginPlay、Face 动画实例/后处理存在、SurfaceRevision 实例隔离、EndPlay 与重新生成。它们是保留真实转换缺陷的诊断人物，未包含来源衣发，也不等同于继承第三人称输入/移动逻辑的可操控 Pawn。Cooked 回执在工程 `Saved/MHCohort_fei/result.json`、`Saved/MHCohort_cat29/result.json`，其哈希关联在各人物 `baseline-lifecycle.json`。

cat29 残差候选的额外 Cooked 回执在工程 `Saved/MHCatFit/result.json`。其完整绑定后所选表面平均距离约 0.4914 mm，眼周穿插数 0；两次独立重载顶点差 0，BP 头部与绑定结果的 24,049 个顶点差均为 0。全部数值仅证明各自测试范围。残差候选仍缺少鼻孔对应并保留可见鼻部失真，不是还原完成的人物。

重现步骤：用独立名称填写人物 `request.json`；运行 `vam_face_cohort_input.py --request ...`；通过 `VAM_COHORT_REQUEST` 指向请求，在 UE Editor Python commandlet 运行 `ue_face_cohort_initial.py`；运行 `vam_face_pipeline.py --request .../pipeline-request.json --solve-only` 检查当前通用残差候选；正式候选恢复同一请求时省略 `--solve-only`。只为复核原始官方缺陷，才使用 `vam_face_cohort_lifecycle.py --request ...` 的显式诊断约定，最后运行 `RunMetaHumanCooked.ps1 -Job .../Initial -OutputRoot <独立目录>` 和 `vam_face_cohort_report.py --request ...`。每个人物使用自己的输出目录、锁定输入和名称；失败点保留，不覆盖已有资产。

## 后续结构修正应验证什么

应优先构建来源眼球、泪阜、内外睑缘与眼眶内壁之间的分部件对应，先验证中性身份下的眼部相对位置，再拟合皮肤；检查是否需要替换由初始法线阈值决定的永久固定区域。眼周穿插、皮肤褶皱和辅助眼网格重叠是不同问题，不能用一个自交数字替代三者。任何修改都应同时回归启梦、fei、cat29，并保留目前失败样本。

本轮跨预设检查已完成：两份新增预设、三份完整 BP 的实际 Cooked 生命周期检查通过；65 项相关单元自测、Python 语法与差异检查通过。两份来源锁定计划/原文件再次复核，原 native 参考的 114 个资产哈希检查无变化；两份新诊断基线在后续残差实验后也保持原哈希。没有修改本轮共享残差求解器的参数或算法，也没有加入人物特例。

可持久查阅的汇总为 `Evidence/FaceFidelity/CohortStructureV1-summary.json`，完整阶段证据在前述 Saved 目录。fei 残差失败与 cat29 的结构失真均保留。眼角褶皱并未在本轮被宣布修复，通用身份还原算法仍需上述结构改进；不声明脸部相似度或人工视觉通过。
