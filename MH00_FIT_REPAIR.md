# MH00 启梦拟合修复（2026-09-25）

旧 `启梦MH00/AssemblyV3` 曾通过保存、组件、DNA、依赖闭包和 Cooked 生命周期检查，但人物形状错误。该结果不能作为正确转换的交付；旧验证记录仅保留其真实工程测试范围。

## 根因与对照

参照资产为用户指定的 `/Game/VamCharacters/C_b41dfaaabfc8427b0ef4e065`。其 native 回执和 MH 原任务具有相同的 plan_id/decode_id。读取双方实际 MeshDescription 后，输入身体的三轴范围一致；源资产没有被拟合修改。

1. `vam_metahuman.neutral_target` 把 native UE 的 +X 朝前坐标直接交给 Creator。官方 `UMeshTargetContourMechanic::TrackFaceWithAutoFraming` 从 +Y 观察输入；CoreTech 的 `GetVerticesDNASpace` 将 `(X,Y,Z)` 转为 `(X,Z,Y)`。新转换为源 `(-x,z,y)*100`，相当于 native 网格绕 Z 正转 90 度。
2. 同一函数反转了原始 DAZ 多边形角点顺序。这里并未经过 WebGL 预览的反转路径，不能再套一次预览到 UE 的反转。旧 target 的有向体积约 +42,738 cm³，正常 native 约 -43,165 cm³，官方 archetype 同为负号。求解器使用法线兼容性过滤几何对应，翻面破坏了对应关系。
3. `CreateTarget` 自建显示法线也用了相反叉积，改为 UE 顺时针约定的 edge2×edge1，并按顶点累计面积权重。

本地 A/B 试验：只修方向仍有明显不对称/折叠；同时修正方向与绕序后，几何检查图不再出现原来的大面积扭曲。试验从真实求解输出读取顶点绘图，无贴图、无截图编辑。中性输入为 T pose，正式 MH 回到官方 A pose，不能用手臂轮廓直接逐点比较。

## 防回归与恢复

- target schema 更新为 `vam-metahuman-target/2`，配方记录 `target_frame` 和转换矩阵。
- 旧任务保留可读证据，但恢复入口会报 `LegacyTargetFrame`，不得把旧 `VerifiedEditorAssembly` 状态继续当成正确拟合。
- 新增方向、封闭曲面有向体积、旧任务拒绝恢复测试。
- `RunMetaHuman.ps1 -Job <任务目录> -Mode fit` 只拟合、保存和独立重载，不发送云请求、不组装，便于先检查几何。
- 用户原 native 资产和旧 MH 源资产只读；修复输出使用独立用户名称及配方。

## 剩余边界

修复针对错误朝向/绕序导致的几何异常，不代表脸部相似度、肤色、衣发和物理已经完成。现已在本地调用 `TrackFaceLandmarksFromImage`，从原始 p0 灰模和保存的透视相机取得 16 条面部曲线，配合已保存的身体姿态约束。无校准配方的正式生成任务停在可恢复的 `AwaitingCalibration`，不继续自动发布。

2026-09-25 用户要求后续只重点核对脸。`vam_metahuman_face_views.py` 从实际求解状态生成正面、左右 45°、左右 90°、俯视 25°、仰视 20°共七组对照，统一相机尺度及平滑光照；左侧始终为同一源 p0。输出位于本机 `Saved/MetaHuman/FitRepair/*_face_views.png`，不包含材质、表情或视觉验收结论。

已排除加大最近表面吸附强度的 `face_detail` 试验：鼻部出现折痕，不能作为交付。`face_solve` 单独运行官方面部求解（身体迭代为零），保留独立诊断资产。`face_anchors` 在此基础上增加从原始网格回投的唇线、眼睑和鼻尖三维对应；这些属于当前锁定启梦输入的校准，不能宣称为通用 Genesis 映射。

`vam_metahuman_face_anchors.py` 读取本机 Epic 头部轮廓顶点索引，不复制官方数据到仓库。眼睑顶点列表并非轮廓遍历顺序，按横向位置匹配；穿过眼眶落到后脑的射线被拒绝，只有在 3 像素内找到源皮肤边界时才恢复，否则报错。配方保留源三角形/顶点和图像坐标。以上诊断源资产均为 Draft，不能以求解无报错代替脸部还原完成。人工观察须参考 `MANUAL_REVIEW.md`。

## 本轮面部结果

正式推荐检查的最新 **Draft 源资产**为 `/Game/MetaHumans/启梦MH面部重拟合/启梦MH面部重拟合`。在 Content Browser 打开这个 MetaHuman Character，不打开旧 AssemblyV3 BP。本轮新增/使用的 API 均为本机 UE 5.8.3 官方编辑器/CoreTech 接口，所有拟合在本地进行，未发送新的云请求。

有效顺序是：保存的 18 个身体姿态点 + 18 个面部三维点 + 16 条相机标定曲线，从官方 `combined` 自动管线的起点参与拟合。对旧拟合继续运行局部脸部求解的 `face_solve` / `face_anchors` / `face_regularized` 试验出现了鼻唇折痕，全部拒绝晋升为 Assembly，不能只凭低锚点误差选结果。

当前重拟合的 18 个面部锚点平均残差约 1.05 mm，鼻尖约 0.99 mm；先前校准基线分别约 2.99 mm 和 12.35 mm。这只描述这 18 个点，不是全脸误差/相似度/验收。七角度几何观察已排除前述明显牵拉，但眼睑、唇形及下颌仍有差异，未宣称完全还原。灰模正式 Face 对照包含未应用透明材质的睫毛卡片。

实际 Face 组件用 `vam_metahuman_face_component.py` 从 MH 自身保存姿态计算刚体变换后比较，没有向原始脸再次配准。`ue_metahuman_face_reload.py` 在独立 UE 进程验证源 Character 和 Face 顶点/三角形保存一致性。当前 Draft 没有新的 full AutoRig、Assembly BP 或 Cooked 回执；旧工程检查不能移作此 Draft 的完成证明。

## 眼缘、鼻部、法令纹专项复核

用户指出的锯齿睫毛确实对应 `eyelashes_shader_shader`（1722 个三角形）。此前 CPU 诊断图忽略了材质，把所有辅助网格画成不透明表面；实际 Creator 灰模使用 `MID_M_Hide_Default_2` 隐藏该组。`eyeshell`、`eyeEdge` 也使用隐藏材质。该图不能代表正式睫毛效果，错误在诊断绘制，未删除/替换官方睫毛资源。新 `InspectMeshSections` 是本项目只读适配器，按 MeshDescription 材质组导出三角形和原始逐角法线。`--skin-only` 只比较 `head_shader_shader`；内眼角下方的线在皮肤组仍可见，不能归咎于睫毛。

旧鼻尖定位代码含当前样本的固定高度范围，现明确限制为历史诊断，不作为通用入口。新 `vam_face_calibration.py` 从相机、眼睑及唇线推导面部范围，自动生成鼻梁/鼻尖/鼻底和唇缘对应。眼睑仅保留 2D 轮廓；不把开口后方的表面当作眼睑深度，也不把追踪器预测的鼻唇沟/人中阴影线当作必需几何边界。默认不启用密集表面硬锚点：该试验（`启梦MH通用表面对应`）产生明显退化，已拒绝晋升，只有显式诊断参数才可重现。

随后实现 `vam_face_surface.py`：在已有官方头部拓扑上求连续最近曲面对应，使用法线/距离过滤、稀疏平滑位移求解、开口区域保护和防翻转步幅回退。它不改变顶点顺序或三角形，不建立运行时 CPU 面部替代物，也没有人物路径/固定世界坐标规则。`vam_face_surface_cli.py` 接收显式 source/head/camera 文件，head 包含同索引的 `vertices`、`apose_vertices`、`triangles`；使用 MH 自身的刚体姿态关系转换回 A pose，输出版本化配方和拓扑哈希。依赖本地 Python、NumPy、SciPy，仅用于 Editor 离线准备，Cooked 不引用这些依赖。

真实写回 API 为 UE 5.8.3 的 `UMetaHumanCharacterEditorSubsystem::FitStateToTargetVertices`，参数 `FMetaHumanCharacterFitToVerticesParams`。传入的是保留官方 MH 拓扑的头部数组，绝不是将 Genesis 原拓扑宣称为 Template。使用 `AlignmentOptions=None`、`bDisableHighFrequencyDelta=true`、`bAdaptNeck=true`。`ue_metahuman_surface_template.py` 通过 `VAM_MH_TEMPLATE_REQUEST` 接收保存的请求（`source_character`、`name`、`destination`、`template_file`、`template_sha256`、`output_dir`、`output_prefix`），创建前拒绝名称碰撞，核对官方头部拓扑哈希，不修改输入 Character。

本轮保留的新 **Draft**：`/Game/MetaHumans/启梦MH保形表面校准/启梦MH保形表面校准`。三角形翻转数为 0，最大位移约 0.815 mm；防翻转会主动限制修正，不能据此称为完整还原。官方模板写回对目标头部数组的平均/最大误差约 `3.93e-6 / 4.58e-5 cm`。这只是 API 保存几何的验证，不是相似度。内眼角下方、鼻翼/鼻底及鼻唇过渡仍有残差，旧的 `启梦MH面部重拟合` 保留为对照，未替换正式 BP。

检查包括 Editor C++ 编译、12 项 MH 回归、4 项投影/射线检查、2 项曲面检查；同一人物经过平移、旋转、缩放后生成对应保持一致，尚未完成多人物验证。所有网格/配方证据仅保存本机 `Saved/MetaHuman/FitRepair`，未发送新云请求；上述新 Draft 不继承旧 Assembly/Cooked 回执。

保存生命周期复核发现：仅 `FitStateToTargetVertices` 后保存包不会提交编辑状态。入口已补齐官方 `CommitFaceState`、`CommitBodyState`，然后保存。修复后的独立 UE 进程重载检查通过：Face 33845 个顶点及三角形一致，最大顶点重载偏差为 0 cm（`face_transfer-reload.json`）。前述模板目标误差是内存拟合保真度，独立重载回执才证明本次保存；二者均不证明视觉相似度。
