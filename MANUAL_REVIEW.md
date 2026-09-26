# MetaHuman 人工观察指南

本文件不设自然度、相似度或视觉通过阈值。自动验证只报告实际测试范围。

旧 `启梦MH00/AssemblyV3` 已发现并记录形状缺陷，不再推荐作为转换结果。查看最新修复记录中的实际资产路径。

手脸核对需要局部视图：双手分别检查手背、手心、手腕和每个指尖；脸部检查正面和侧面的眼角、眼睑、鼻翼、嘴角、唇缘及下颌。源 T pose 与 MH A pose 必须区分；比较拟合误差时先核对求解姿态是否确实匹配源姿态，不能把不同姿态的图标成“同姿态”。保存几何方向、相机、关键点及求解参数，不用一张全身缩略图判断局部完成。

本轮按用户要求仅继续脸部核对：正面、左右斜侧、左右纯侧、俯视、仰视。区分求解器 combined 头部与实际官方 Face 组件；后者用 MH 自身 A pose 和保存姿态之间的刚体变换还原，不允许再拟合到源脸来掩盖差异。灰模图未应用睫毛透明材质，睫毛卡片的实心显示不等同于皮肤折叠；鼻翼、唇缘的连续皮肤曲面必须另行检查。稀疏锚点误差不是全脸表面误差，更不是相似度分数。

拿到 `VerifiedEditorAssembly` 后，在 Content Browser 同时核对可编辑 Character 源资产及 Assembly BP。打开 Character 检查还能用官方工具编辑；不要把 SM_ConformTarget 或 Save Pose DNA 当作最终产物。

将 Assembly BP 放入任意新普通关卡，或动态 Spawn。观察头身接缝、眼齿与嘴部中性状态、不同 LOD、表情与 correctives。以相同官方光照条件比较，再记录具体问题；截图的存在不是通过证据。

创建两个共享 BP 的实例，观察各自通知/后续扩展状态是否隔离。当前 MH00 的 revision 通知不会实现捏人或布料/软组织仿真；不要以按钮不报错推断这些效果已存在。

记录输入人物名、recipe revision、Engine/插件版本、最终 BP 路径、观察动作及现象。人工校准点/轮廓/相机保存至配方，避免将鼠标坐标操作视为可重复流程。

本轮局部复核使用 `face_transfer_component_skin_face_views.png`：左侧源 p0，右侧保形表面校准 Draft，七组相同相机。只绘制实际 `head_shader_shader` 皮肤组及官方逐角法线，避免把灰模中隐藏的睫毛、眼壳和眼缘辅助网格错误画为实心。此图不用于验证正式睫毛材质。重点观察内眼角下方的皮肤线、鼻梁至鼻尖的侧面曲线、鼻翼宽度和鼻唇沟过渡；这些区域仍有残差，不以无三角形翻转或重载一致代替人工观察。

2026-09-26 的 topology-family calibration 是另一项前置标注工作，不能用脸部相似度验收替代。打开 `Saved/MetaHuman/FaceFidelity/CanonicalG2F-r3/canonical-face.obj`，以未施加人物 Morph 的基础拓扑确认边身份；OBJ 序号减一为原始 source vertex ID。与同目录校准 JSON 的 material_boundary_chains、morph_support_evidence 交叉检查。特别区分眼睑 rim 与周边受影响区域、鼻孔边缘与鼻翼体积区、唇红材质边与口腔开口边。无独立 Morph 的法令纹必须记录实际选取的连续 edge chain 及依据。未确认项保持 unverified；不能通过填充 confidence 或改全局状态跳过逐项确认。完整步骤见 `FACE_FIDELITY.md`。

也可打开上述两个 canonical 目录中的 calibration-workspace-v3.html，直接查看 Morph 影响区、原始顶点和材质边链。确认依据属于一次性拓扑标注记录，不是人物视觉通过记录。工程测试资产 FaceFidelityTemplateRoundTrip 仅验证 Template 写回和重载；不要将其当作新的还原结果。

最新通用候选检查目录为 `Saved/MetaHuman/FaceFidelity/AdaptiveExperimentV9`。`post-template-metrics-seven-views.png` 来自官方写回后重载；`post-rig-metrics-seven-views.png` 来自完整绑定后的两次独立重载和复评。V9 的对应几何检查已通过；V7 保留为未通过的历史样本。高清版本为 `official-high-resolution-seven-views.png`，每侧每视角 768×768。图中左侧为 SOURCE p0，右侧为对应阶段的实际官方头部。所有图保留 0°、±45°、±90°、pitch ±20°；只显示头部皮肤，不能用它们判断睫毛透明材质或眼球、牙齿的最终表现。

分别检查下眼睑是否挤出新线、内外眼角位置、鼻梁至鼻尖的侧面曲率、鼻翼宽度、鼻孔和鼻小柱、唇缘及嘴角、法令纹过渡、下巴和耳轮。再在最终 BP 上检查眨眼、转眼、张嘴和多个 LOD；当前几何分数不会代替这些观察。Morph-support 区域报告可能左右共用或互相重叠，不能将它读成独立确认的左右语义曲线误差。

## V20 眼角修复人工观察

后续跨预设检查另见 [COHORT_STRUCTURE_REVIEW.md](COHORT_STRUCTURE_REVIEW.md)，不要只用启梦判断通用性。

打开 `Saved/MetaHuman/FaceFidelity/EyeCornerV20/eye-comparison.png`：左列 SOURCE p0，中列旧 V9 实际绑定后皮肤，右列新 V20 实际绑定后皮肤。上下两组三视角分别为两眼；只比较相同行、相同相机。观察眼角下方斜沟、上下眼睑间距、内眼角转折和上眼睑波纹。新图仍有相对 SOURCE 的轮廓差异；0 自交不等于这条沟已经消失。

完整头部七视角见同目录 `post-rig-metrics-seven-views.png`。这两类图均为皮肤几何灰模，不能判断睫毛、眼壳、眼缘材质。进入 `/Game/MetaHumans/启梦MH眼周稳定修复` 打开 Character，以中性表情检查皮肤；再在普通关卡放入其 Assembly BP，用最终材质分别观察静止、眨眼、转眼和各 LOD。需要隔离辅助部分时仅操作临时副本的可见性，不删除正式资产中的辅助网格。区分皮肤本身折线与辅助面片/透明材质造成的线，再记录角度、表情与 LOD。这里不设人工视觉通过阈值，最终判断由用户作出。

## fei / cat29 跨预设结构检查

证据位于 `Saved/MetaHuman/FaceFidelity/CohortStructureV1/<人物>/`。`structure-eyes.png` 与 `structure-seven-views.png` 使用同一来源确定相机；依列标签区分 SOURCE、Coarse（首次官方拟合）、Tracked（追加统一图像追踪）、OfficialBaseline（官方完整绑定后）、OfficialTemplate（残差经官方 Template 写回并重载），以及存在时的 Final（通用残差正式写回并绑定后）。未生成的阶段不会伪造一列。

先判断内眼角斜沟、眼睑厚度、鼻翼和鼻唇过渡在哪一阶段首次改变，再看后续步骤有没有加重。fei 与 cat29 应同时观察，不把某一人物的改善用作另一人物的结论。灰模只显示皮肤；泪阜、眼球、眼壳和睫毛的材质关系需在各自实际 BP 中另看。来源中被排除的 Lacrimals/Tear 有独立诊断证据，但其 MH 解剖对应尚未确认。

标为 DraftStructuralBaseline 的 Character/BP 可以用于观察完整官方流程，不能视为还原缺陷已经修复。工程重载、DNA/LOD、Cooked 生命周期与人工视觉判断分开记录；不设置本轮人工通过阈值。
