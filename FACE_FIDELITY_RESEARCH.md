# 通用脸部拟合：官方资料与本机实现核对

2026-09-26，UE 5.8.3。本记录不设相似度验收阈值。

## 资料支持的结论

Epic 的 [From Template Mesh](https://dev.epicgames.com/documentation/metahuman/from-template-mesh) 要求官方拓扑及正确的语义边环；鼻唇沟和眼睑不能仅在空间上接近。保存顶点数和低表面误差不足以证明对应正确。因此保留标准头拓扑，继续记录未确认来源语义，不直接修改最终 SkeletalMesh。

Epic 的 [From Custom Mesh](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-from-custom-mesh-tool-in-unreal-engine) 提供头部单独拟合、手动阶段设置、二维曲线和 refinement。官方说明 refinement 使用近表面投射，风格化比例和输入缺陷会限制结果。项目使用已完成的官方初始拟合作为起点，在局部保护下做残差实验，再从 Template 回到官方 Character。

[From Mesh](https://dev.epicgames.com/documentation/metahuman/from-mesh) 描述中性姿态、追踪和本地 Identity Solve，再向 MetaHuman 后端提交官方模板。照片/图像追踪可帮助定位，但单张二维图像不能提供本项目已有的完整三维表面。此次未添加第三方照片重建服务或上传；实际输入仍是锁定的 imported p0。

DAZ 的 [Morph 工作流说明](https://docs.daz3d.com/public/software/dazstudio/4/userguide/creating_content/modeling/tutorials/modifying_morphs/start) 将 Morph 描述为来源顶点差值；[Genesis 2 Female Head Morphs](https://www.daz3d.com/genesis-2-female-head-morphs) 列出局部眼睑、鼻、人中等控制。这支持按基础拓扑复用身份与真实 delta support，但不能将名称分类当成精确曲线。

## 本机接口核对

- MetaHumanConformTargetParams.h：Combined、BodyOnly、HeadOnly、HeadAndBody；BodyConformSolveSettings 包含 Face 设置，bAutoSolve 与 refinement 是不同入口。
- MetaHumanCharacterEditorSubsystem.cpp 的 FitStateToTargetVertices：接收头、眼球和牙齿顶点，通过官方 FaceState->FitToTarget 并 ApplyFaceState；不是直接写最终网格。
- 本机官方 ImportFromTemplate 同样使用 bDisableHighFrequencyDelta=true。不能仅因名字就断言这个参数是旧结果丢失残差的原因。
- TrackFaceLandmarksFromImage 是本地图像曲线追踪接口，不等于“照片直接生成最终游戏角色”。

## 实验调整及边界

完整人工语义表仍是高可信路径；新增 provisional 路径允许在缺失部分语义时实测通用算法，但不会把其结果回写成 verified 语义表。来源材质身份定义脸区；来源真实开放边界产生眼睑候选，官方 MH 顶点曲线提供目标区域；自动配对明确保留 unverified。

固定四候选使用增量 Laplacian 平滑位移、法线相容表面对应、候选边界约束，以及局部三角形方向/面积和边长比保护。改为局部步长，是因为全局回退被少量眼口三角形限制，导致其他区域也无法更新。该保护不等于完整自交检测。后续加入了局部旋转 SVD 的 ARAP 边正则；刚体旋转零残差已有独立测试。

各候选保存表面、法线、投影轮廓、官方曲线邻近表面距离及候选边界距离。后两项不是已经验证的来源解剖对应误差。真实官方 Template 与 AutoRig 后重新测量；平均误差下降不能证明眼睛、鼻翼、法令纹等每处都正确。

源码入口：vam_face_adaptive.py、vam_face_adaptive_cli.py、vam_face_adaptive_verify.py。它们是项目设计，不是 Epic API。实验角色仍需保留可编辑源资产、完整绑定和 Assembly，且以实验状态展示语义局限。


## 追加研究及实测修正

- [Phong Tessellation，作者项目页](https://perso.telecom-paristech.fr/boubek/papers/PhongTessellation/)：局部位置/法线插值。项目实现受限的曲面投影作为明确标记的实验参考面，原始 p0 顶点和原始表面距离单独保留；不声称它就是 VaM 的 subdivision。
- [ARAP，作者项目页](https://igl.ethz.ch/projects/ARAP/)：独立实现局部旋转与边正则，抵抗不均匀拉伸；没有复制付费插件源码。
- 眼孔开放边界位于眼窝内部，不能直接命名为可见眼睑 rim。现在沿原始 polygon edge 的相邻环生成候选，再用双向几何距离选择，保留 unverified。唇缘/鼻孔使用来源材质交界边作候选，仍不冒充完整人工语义表。
- V3 官方 Template 写回保留了平均表面改善，但 104 项面积/方向余量检查未通过。实际方向反转数为 0；旧字段 flipped_triangles 包含保护余量违规，不能将其读成 104 个实际翻面。Draft 未晋升。
- 后续离线守卫留出更大内部余量（投影面积比 0.20，边长比 0.55—1.45），保持官方验证阈值不变。V6 已通过实际 Template 保存/重载后几何检查。
- 真实输入的旋转/缩放测试发现空间树对等距离三角形的选择不稳定。通过 RMS 尺度与以 triangle identity 打破并列修正；证据 adaptive-equivariance-stable.json，最大恢复误差约 9.7e-9 cm，保护系数与区域顶点身份一致。该测试不证明跨人物相似度。
- V8 使用同一四候选，额外投影边向量到来源切平面，加入法线场正则；该运算有独立旋转等变测试。相对 V7，Balanced 法线差从约 0.0393 降至 0.0359，但参考面距离从约 0.0249 cm 增至 0.0282 cm，固定综合分更差。因此保留实验记录，不改默认策略、不因为单张截图晋升。V8 尚未提交官方 Template/AutoRig。
- 修正新任务的配方继承：只复用锁定来源，清除旧人物的 Cook、Assembly 历史和旧 post-rig 回执。依赖闭包检查按实际 Character/Target/Mapping 包身份排除编辑器来源，允许独立 Assembly 与 Character 同处一个用户命名父目录。
- V7 的真实 AutoRig 揭示官方会重三角化：49 个四边面换用另一条对角线，24049 个头部顶点编号稳定，最大位移约 1.53e-5 cm，两次独立重载坐标差为 0。严格证明每个变化是唯一的、保持边界绕向的四边面对角线切换后，改用实际三角形复评；仍发现 5 个方向反转，故 V7 保持 Partial，禁止 Assembly。没有通过放宽门限使它通过。
- 在本机官方 `MetaHumanAnimator/Content/MeshFitting/Template/mean.obj` 找到 24002 个原始四边面。逐顶点编号和有向三角形覆盖检查证明它们恰好覆盖当前标准 MH 头的 48004 个三角形，无需空间配准。V9 开始对每个四边面的两种三角化同时施加方向、面积与边长保护；只从安装目录读取拓扑，不复制官方 OBJ 进入仓库。该约束针对所有匹配官方拓扑的输入，没有人物 ID 或位置补丁。
- V9 的实际 AutoRig 改变 44 条对角线，全部四边面保护检查通过；最终 Assembly 头部与绑定后重载数据逐点、逐三角形一致，最大位移 0 cm。隔离 Game 编译/Cook/Package 与 Development/NullRHI 动态生命周期测试通过。52 项 Python 测试通过；真实输入的双对角线保护求解旋转/缩放回归最大恢复误差约 1.51e-8 cm。完整报告见 FACE_FIDELITY_RESULT.md；这些检查不证明视觉或跨人物完美还原。
