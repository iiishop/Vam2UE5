# Face Fidelity：拓扑校准与实验求解器

最新眼周约束、官方结果与限制见 [EYE_CORNER_RESULT.md](EYE_CORNER_RESULT.md)，算法细节见 [EYE_CORNER_FIX.md](EYE_CORNER_FIX.md)。新保存请求默认启用有序眼睑、内外表面归属、测地邻域自交/间距保护和采样密度正则化；已有缓存保持锁定版本。

2026-09-26。**V9 已通过官方 Template、AutoRig、独立重载、post-rig 复评、实际 Assembly 网格/依赖检查以及隔离 Game 编译/Cook/Package/运行生命周期。结果与路径见 [FACE_FIDELITY_RESULT.md](FACE_FIDELITY_RESULT.md)。V7 因绑定后重三角化产生方向反转，保留 Partial，不用于交付。来源精细语义仍有未确认项，不能称为所有人物的完整语义还原或视觉通过。** Native 入口不变，现有生成按钮尚未切换到实验管线。

## 本轮实际证据

- 分支 `MetaHuman`，HEAD `1b639e382d556b0423aa49f465c63b09388219bb`；历史 MH 接入、拟合修复仍在未提交工作区，未重置或覆盖。
- 本机 UE 5.8.3：读取 `MetaHumanConformTargetParams.h`、`MetaHumanConformSolverSettings.h`、`MetaHumanCharacterEditorSubsystem.h`，并运行独立 UE Python 反射检查。
- G2F 基础网格 21556 顶点，拓扑摘要 `c68eb53638242445e6c1706afc583759be46cd95b467b1abc2f19a3d9640437a`。坐标来自 SourceIR 未施加外观 Morph 的 unity_mesh，而非当前人物截图。
- 有界选择 32 个 builtin Morph，通过既有 `vam_editable_morphs.extend` 解码和锁定；加上已有 IR 的局部候选，得到 46 份真实 delta support。重复 delta 先合并再判断非零，UV 重复/非基础索引另记，graft 域不参与。
- 8 组材质交界边及其可排序的拓扑边链。材质边界是可靠边身份，不等于已确认的所有解剖曲线。
- 31 个语义槽均为 `unverified`。上下眼睑、眼角、鼻、人中、唇、下颌、颧骨、耳部等有候选 support；左右法令纹没有独立 Morph 候选。没有把 support 或 tracker 自动提升为语义真值。
- G2M 已通过既有 builtin 索引、锁定计划和解码器提取 canonical 输入，得到 32 份真实 Morph support、8 组材质边界。两份样本的连接摘要相同，因此新增 topology_family 独立校验；不会将 G2F 确认自动转给 G2M。G2M 也尚无已确认语义表。

## 一次性 topology-family calibration

`Config/FaceTopology/G2F_<digest>.json` 保存拓扑/UV 摘要、材质边、真实 Morph support、原始来源 ID、锁与哈希、候选发现条件，以及每个 semantic 的 evidence/provenance/confidence/verification_state。文件不含 canonical 顶点坐标、贴图或 Morph delta 数组；这些本机输入仍留在 Saved 和原只读来源中。

`vam_face_topology_calibrate.py --ir <锁定IR> --plan <锁定计划> --data <Saved> --catalog <BuiltinCatalog.json> --out <新目录>` 生成候选，不执行 Morph，不接触人物资产。Morph displayName/region/group 只用于发现资源；实际候选顶点来自 delta support，不来自这些元数据的字符串含义。

本机最新目录：`Saved/MetaHuman/FaceFidelity/CanonicalG2F-r3`。包含 canonical-face.json/obj、选择清单和拓扑校准 JSON。OBJ 的第 n 个顶点对应原始 source vertex n-1；保留未引用顶点以保持编号。不要经过焊接、减面或重排后再取编号。

人工在 canonical 基础拓扑上确认一次有序边链，保存注释 JSON：

```json
{
  "topology_family": "G2F 或 G2M，与校准文件一致",
  "topology_digest": "与校准文件一致",
  "reviewer": "实际确认者",
  "reviewed_at": "实际确认时间",
  "semantics": {
    "某个semantic_id": {
      "vertex_ids": [],
      "edge_chain": [],
      "verification_state": "verified",
      "evidence": ["实际参考的 material boundary / morph evidence ID"],
      "provenance": ["canonical 基础网格确认记录"]
    }
  }
}
```

空数组不是有效确认。使用 `vam_face_topology_review.py review --calibration <候选表> --annotations <标注> --ir <IR> --out <新修订>` 校验真实边邻接、拓扑一致及记录完整性。允许分批确认；未确认项不会自动通过。边链顺序是解剖对应的一部分，不能只填写无序 support 集合。

MH 拓扑还需一次性确认对应；目标注释为 `target_topology_sha256`、`provenance`、`face_triangles` 加 `curves[semantic_id]`，每项是 `{"source_vertex": 原始顶点ID, "target": {"vertices": [MH顶点ID], "weights": [1]}}`。`face_triangles` 必须与源材质脸区对应，不能把 MH 头网格中的整段脖子加入脸部评分。target 也接受真实 edge 或 triangle barycentric identity。`bridge` 模式要求所有 source 语义已确认，使用 source.json 的 input_to_source_vertex 将原始顶点 ID 转到皮肤子集；不会按空间位置或弧长猜测缺失配对。MH 三角形顺序/顶点数哈希独立验证。

## 已确认语义路线（尚待完整 canonical 标注）

`vam_face_fidelity.py` 使用同一组 Conservative / Balanced / SemanticStrong / SurfaceStrong 配置。这些是**项目实验 profiles**，不是 Epic 预设；目前控制离线 residual 求解，不代表已经为四种 profiles 实测四次官方 Conform。

每个候选从同一个官方拓扑基线开始，依次做：

1. coarse：下颌、下巴、脸颊、轮廓、耳部的已确认拓扑对应；
2. semantic：加入全部已确认曲线/关键点；已吻合点保持约束；
3. surface：法线与距离门限过滤的三角形近邻、切平面法线约束、七视角投影轮廓约束、位移 Laplacian 和边向量正则。

语义曲线及其两圈拓扑邻域排除普通 ICP。非脸区域固定。步长回退检查三角形方向/面积与边长比，防止过度拉伸。这里使用边向量正则，**不是完整旋转不变 ARAP，也没有证明所有自交均被排除**。

评分包括双向采样表面距离、法线差、七视角投影轮廓，以及每一条语义的 RMS 误差。轮廓使用投影遮挡边/开放边的采样距离，尚不是完整可见性光栅轮廓。表面搜索是 32 个近邻三角形候选，不能当作严格全局最短距离。所有距离以源脸范围归一化，保留 cm 统计。

保留 Baseline；平均分下降不足以晋升。任一关键语义超过固定退化阈值或发生翻转，候选不得胜出。评分只用于选择/回归，不产生视觉通过结论。

## 任务入口与恢复

1. `vam_face_fidelity_cli.py prepare --job <新目录> --source-ir <IR> --plan <plan> --head <已导出的官方head.json>`：重新验证锁，提取 imported p0，排除带 expression/pose 标记的 Morph 与 graft，不读取运行时动画、物理或动态软组织。source.json 保存原始 source vertex 对应与材质身份。Morph 本身烘焙的表情仍需要来源分类，不能仅凭元数据保证中性。
2. `vam_face_fidelity_cli.py run --job <目录> --semantic-map <已确认bridge.json>`：核对文件/拓扑哈希，运行候选，保存分项指标、诊断和七视角图。缺少/未验证映射时停在 AwaitingSemanticAnnotation。`cancel` 文件可取消；移除后重新运行。当前恢复会从锁定输入重新计算离线候选，保留已有检查点，不声称逐迭代续算。
3. 通过后生成 `template.json`，使用同一 MH 的 posed/A-pose 刚体关系回到官方 A pose，状态为 AwaitingOfficialTemplateImport。交给已有 `ue_metahuman_surface_template.py` 的显式请求；不写最终 assembled SkeletalMesh。
4. 官方 Template 导入后的实际 Face 必须再独立导出、重载、重新评分，并完成 AutoRig/Assembly 与 DNA/LOD/eyes/jaw/teeth 验证。这段自动串联已实现于 `vam_face_fidelity_finish.py`，实际输出重新评分由 `vam_face_fidelity_verify.py` 完成；未拿真实已确认语义表跑通全链，不能宣称端到端通过。 既有 `ue_metahuman_job.py` 的授权、Draft、独立验证机制保留；未新增云请求。

## 实际官方接口与验证范围

实际调用包括 `get_editor_subsystem`、反射属性读取，以及本轮工程 Draft 上的 `fit_state_to_target_vertices`、`commit_face_state`、`commit_body_state`、`save_loaded_asset` 和跨进程预览几何导出。反射确认 `ConformToTargetMeshes`、`FitStateToTargetVertices`、`CommitFaceState`、`CommitBodyState`、`RequestAutoRigging`、`BuildMetaHuman` 可调用。FaceIcpWeight、FaceIcpSearchTolerance、FaceNormalCompatibility、FaceKeypointWeight、FaceLandmark2DWeight、ModelRegularization、PatchSmoothness 在本机均存在；头文件还确认 refinement 的 Laplacian/Bending/Strain 等。

上述接口已用于 V7 的真实候选写回与 AutoRig。Assembly、绑定后复评与 Cook 是独立阶段，不能由 Template 成功推断其通过。

早期任务 `Saved/MetaHuman/FaceFidelity/TopologyCalibrationV1/task.json` 仍为 AwaitingSemanticAnnotation；它不代表后续 provisional 实验的状态。最新 V9 位于 `Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9`，每个阶段均保存自己的七视角图和回执。baseline 图始终代表旧 MH，candidate 是离线网格，post-template/post-rig 才来自官方实际资产。

工程检查：新增拓扑校准/语义/候选测试及既有回归；独立 UE API 反射检查；原 native 资产哈希核对。合成测试的误差改善只证明该数值测试，不证明真实面孔已提高。未改 C++，未重复宣称此前编译或 Cook 回执适用于本实验。

## 继续实现：本地标注工具与官方生命周期

本机可直接打开两个独立 HTML，无需安装、登录或上传：

- `Saved/MetaHuman/FaceFidelity/CanonicalG2F-r3/calibration-workspace-v3.html`
- `Saved/MetaHuman/FaceFidelity/CanonicalG2M/calibration-workspace-v3.html`

左侧是未施加人物 Morph 的 canonical 基础网格，右侧是已知官方头拓扑参考。选择语义后检查真实 Morph support 和材质边界，选取来源原始顶点与对应 MH 顶点；来源链必须沿原始多边形边连接。确认需要实际确认者与依据。载入已有边界仅产生候选，不确认其解剖含义。保存完整草稿可恢复；载入时清除确认状态，防止沿用已变更证据。

“导出确认记录”下载一个 `vam-topology-review-bundle/1` 文件，包含 source_review 与 target_review。review/bridge 两个命令均接受该 bundle。目标脸区通过“框选添加/移除 MH 评分区域”保存三角形 ID，需从侧面核对是否包含不应参与评分的颈部。男女拓扑族与 MH 目标摘要均在载入/编译时检查。

官方后半程入口：`python Scripts/vam_face_fidelity_finish.py --request <保存的请求.json>`。请求字段为：

```json
{
  "job": "已生成 template.json 的 Fidelity 任务目录",
  "name": "用户选择的新资产名称",
  "destination": "/Game/MetaHumans",
  "source_character": "/Game/原官方Character/原官方Character",
  "source_mh_job": "同一锁定 SourceIR 的原 MH 任务目录",
  "engine": "本机 UE 安装目录",
  "project": "项目 uproject 绝对路径"
}
```

`--local-only` 只运行 Template、独立重载与实际输出复评，不请求云端。完整运行复用既有 AutoRig 授权、依赖和 Assembly 机制。顺序是 Template → 独立重载 → 几何复评 → rig-only → 两次跨进程导出 → post-rig 复评 → Assembly。跨阶段回执绑定 Character 路径和资产哈希，刚体姿态变换也锁定哈希。失败保留 Draft/检查点；不会修改最终 assembled mesh。

实测证据：`Saved/MetaHuman/FaceFidelity/OfficialTemplateRoundTrip/Reload/reload.json`。工程 Character 为 `/Game/MetaHumanEngineering/FaceFidelityTemplateRoundTrip/FaceFidelityTemplateRoundTrip`，头 24049 顶点、完整 Face 33845 顶点，跨进程重载最大坐标差 0 cm。这只是官方写回/保存/重载工程测试，full_rig=false、assembly=null，不是新还原人物或交付 BP。

独立工程 roundtrip 是早期检查，不能替代后续人物实测。当前测试数量、实际人物路径及阶段以最新工程记录为准。未修改 C++，未将 Python 测试等同 Cooked 生命周期或视觉验收。不确定的 31 个来源语义槽仍未被程序伪造为 verified；缺失标注不会阻止下面明确标记的 provisional 实验路线继续生成可检查的实际资产。


## 通用实验入口（继续实现）

最新入口为 `python Scripts/vam_face_pipeline.py --request <pipeline-request.json>`，可追加 `--local-only` 停在官方模板和重载验证。输入仍复用已有 SourceIR、SourceMaterialIR、锁定计划和原 MetaHuman 任务；不复制导入器或创建另一套 NativeRuntime。

离线求解依赖 NumPy、SciPy、Pillow；本机实测 Python 3.11.0、NumPy 2.2.3、SciPy 1.14.1、Pillow 10.4.0。官方编辑器阶段由 UE 自带 Python 执行。它们均不作为游戏运行依赖。新实验会锁定求解代码哈希；算法改变或旧候选没有代码锁时要求使用新输出目录，不默默复用。

请求字段：

```json
{
  "output": "本次新的输出目录",
  "source_mh_job": "同一人物的已锁定 MH 任务目录",
  "source_character": "/Game/已有官方初始拟合源资产/资产名",
  "head": "该官方头部的源姿态及 A-pose 导出 JSON",
  "name": "用户选择的新人物名称",
  "destination": "/Game/MetaHumans",
  "engine": "本机 UE 安装目录",
  "project": "项目 uproject 绝对路径"
}
```

`head` 包含 vertices、apose_vertices、triangles，必须来自同一 MetaHuman 的两种已知状态。脚本核验两者之间是刚体变换；不能拿源脸重新对齐来掩盖拟合误差。目前这个初始官方导出仍是入口前置条件，没有把任意旧 SkeletalMesh 当成合法模板。

默认实验策略：原始 polygon 边环候选、唇/鼻孔材质边界候选、可选 Phong 位置/法线参考面、增量平滑和 ARAP、局部三角形及边长守卫。四个固定候选及 Baseline 共同评分；未知语义保持 unverified。RMS 归一化和确定性三角形并列处理避免姿态/单位改变产生选择漂移。图像追踪未被提升为来源真值。

V9 起还要求本机官方 mean.obj 的原始四边面拓扑与头部一致，逐面证明覆盖后同时保护两种对角线。否则返回可恢复的拓扑不匹配错误。AutoRig 若重三角化，验证器仅接受已证明等价的有向四边面翻边，要求实际顶点身份保持稳定，并按实际输出三角形重新评分；未知重网格、翻绕向或顶点重编号仍失败。

任务可恢复：请求和项目算法哈希锁定，取消文件保留当前证据；有同名资产时报 RenameRequired。仅在独立重载和实际几何复评通过后才继续既有 AutoRig 与 Assembly。几何验证状态不等于完整语义验证或视觉验收；配方另存 source_semantics_reviewed=false。

原始三角面误差与平滑参考面误差分开记录，不能混用不同表面模型的分数。official_curve_surface_distances 衡量官方曲线到来源面的距离，不能当成精确来源语义曲线误差。vam_face_region_report.py 补充实际 Morph delta support 区域的误差，未具备独立证据的项明确为空。

标准灰模图现绘制完整头部/来源皮肤，在相同的来源脸部相机框架下观察；评分脸区与显示几何区分，避免裁掉评分外三角形而制造假孔洞。视角保持 0、±45、±90、pitch ±20 度。详细研究及失败记录见 FACE_FIDELITY_RESEARCH.md。
