# MetaPipe 与 VaM → MetaHuman 最终路线调查

调查日期：2026-09-26。范围：公开一手文档、本机 UE 5.8.3 头文件、现有三人物诊断。没有购买、安装、运行 MetaPipe，没有上传素材。本次只新增研究记录，不改变转换代码或人物资产。MetaHuman 分支 HEAD 仍为 `1b639e382d556b0423aa49f465c63b09388219bb`；现有未提交改动保留。

## 决策

MetaPipe 可以列为候选的 Maya 作者工具；公开资料支持其方法值得参考，但不足以认定它已经支持本项目 VaM/G2F/G2M 输入和 UE 5.8 Character 往返。不要购买旧版 2.0 后假设即可一键解决。项目主线应收敛为：**拓扑族一次校准 → 身份形变转移 → 分部件约束 → 官方 Template 或工作 DNA 返回 Character → Assembly**。这是项目设计，不是对 MetaPipe 私有算法的描述。

对当前三例而言，继续在既有错误对应上微调眼周表面权重不是优先方向。商业工具能缩短作者工作量，但不能替代正确的 canonical 对应，也不能保证未知预设无损转换。

## 已核实的外部能力与边界

- [厂商产品页](https://caliskanuzay.artstation.com/store/PmpWr/custom-metahuman-dna-calibration-tutorial)：当前标注 3.3.2，声明 Maya 2022–2026、MetaHuman for Maya、UE 5.8 支持。属于厂商兼容性声明，本机未实测。页面同时保留 wrapping 不包含的介绍和新增头部 wrapping beta 的更新说明，不能据此承诺成熟的自动全身 wrapping。
- [Gene Transfer](https://www.artsandspells.com/gene-transfer)：需先准备对应文件，再复用于相同拓扑；输出可选 MH 或自定义拓扑。本项目只评估标准 MH 输出。未找到官方明确列出的 VaM/Genesis 2 即用模板，不把其他 Genesis 世代演示当成 G2 支持证明。
- [厂商 UE Head Setup](https://www.artsandspells.com/ue-head-setup)：展示 FBX、骨架、后处理和 DNA 的网格导入路径；这本身不能证明已创建可供 Creator 作者编辑的 Character。当前版本是否支持所需往返仍须实测。
- [Epic DNA Calibration 仓库](https://github.com/EpicGames/MetaHuman-DNA-Calibration)：明确旧仓库未支持 UE 5.6 Creator 新人物，推荐新版 MetaHuman for Maya。因此不套用旧 2.0 教程的工具链。
- [Faceform Wrapping](https://docs.faceform.com/Wrap/Nodes/Wrapping/Wrapping.html)：公开方法包含粗到细拟合、对应点与排除面；排除区域通过 ARAP 随周边变形。它提供了比永久固定初始错误顶点更合适的参考。该节点要求连通基础网格，眼球、牙齿等不能当作一个连通皮肤问题处理。

## 与本项目失败证据的对应

依据 [跨预设检查](COHORT_STRUCTURE_REVIEW.md) 及 `Evidence/FaceFidelity/CohortStructureV1-summary.json`：

| 已观察事实 | 应改变的通用机制 |
|---|---|
| 两个新增人物首次拟合就出现眼周结构偏差；后续绑定几乎不改顶点 | 首先修正输入和对应；不能仅怪 AutoRig 或渲染 |
| G2F Lacrimals/Tear 与皮肤分离，MH 眼角部分结构属于连接的头部皮肤 | 明确各部件的语义所有权；建立跨部件对应，不能用一圈眼睑代表全部眼角 |
| 当前只给 Template 传 head，没有传标准 MH 拓扑的眼球 | 在中性身份中求眼球中心、大小、方向并生成合法 MH 眼球输入；禁止直接塞 G2F 眼球顶点 |
| cat29 两侧鼻孔对应缺失，每侧 33 个曲线点有 19 个永久固定 | 必需语义缺失应报失败；非匹配区应受结构正则约束随动，而非保持错误初始位置 |
| 平均表面评分下降但鼻部仍塌陷 | 固定评估域，报告缺失语义、被排除区域、局部轮廓与最坏误差 |

这些事实支持改结构转换方向，但没有证明唯一病因。眼球中性变换尚未完整求值，来源精细语义仍未人工确认；需保留 unverified。已完成的两例属于同一 G2F 拓扑，不代表 G2M 获支持。

## 建议收敛的实现

1. **一次 canonical 校准**：分别锁定 G2F/G2M 顶点顺序、边、UV 与摘要；用真实 Morph delta support 和材质边界提供候选，确认眼角、内外睑缘、泪阜、鼻孔、鼻翼、唇缘等。结果保存为版本化拓扑对应，而不是人物坐标阈值。
2. **身份形变转移**：从 canonical G2 与人物中性 p0 的差异得到变形场。用已确认的三角形重心对应及局部坐标系，将其转移到标准 MH canonical mesh。眼球、皮肤、眼角辅助结构和牙齿分开处理；不让全脸最近点搜索跨部件吸附。该算法需独立实现，未声称 MetaPipe 内部如此实现。
3. **受约束修正**：在语义正确的初始化上拟合表面、法线和多视角轮廓，以 Laplacian/ARAP、局部应变与翻面保护限制变形。缺少可靠目标的区域通过周边结构随动。不能为了消沟槽而把全部凹陷抹平。
4. **完整输入和评分**：必需曲线无对应时保存 Draft，不能以整体低均值获胜。使用固定脸部评估域，分别统计眼、鼻、唇、下颌、排除区域与自交；同一设置同时回归启梦、fei、cat29。
5. **官方写回**：优先标准 MH topology 的 Template 路线；若官方再拟合损失仍不可接受，再评估官方工作 DNA 校准路线。两条路线共用前面的拓扑对应，不另建人物专用修补器。

## 两条官方写回路径及真实取舍

### A：标准模板 → Character → AutoRig → Assembly

本机 `MetaHumanCharacterEditorSubsystem.h` 已核实：`ImportFromTemplate` 接受头、左右眼与牙齿；`FMetaHumanCharacterFitToVerticesParams` 同样有四组顶点数组。已有代码只设置 head，需要补全部件输入，保持标准拓扑及顶点身份。

此路线保留 Creator 作者工作流。拟合后的每个阶段必须导出并比较，检查官方拟合是否重新改变目标。不能以 API 返回成功证明保真。[Epic 初始 DNA 指南](https://dev.epicgames.com/documentation/metahuman/obtaining-an-initial-metahuman-head-dna) 同时强调顶点语义，并允许用标准拓扑模板重新拟合或进行 Neutral Pose 校准。

### B：完整绑定的 DCC Export → DNA 校准 → Whole Rig 导入 → Assembly

[Epic Neutral Pose Editing](https://dev.epicgames.com/documentation/metahuman/neutral-pose-editing) 支持中性网格及关节修改，并可把 LOD0 修改传播到低 LOD；这比直接改最终 SkeletalMesh 合法且完整。仍需检查闭眼、眼球转动、张嘴及校正形状，不能只验证静态脸。

[Epic 保存与导出说明](https://dev.epicgames.com/documentation/metahuman/saving-and-exporting-data) 明确区分工作 DNA 与运行导出 DNA：Creator 往返需保存的工作 DNA，不能拿运行导出的 DNA 代替。`Import Whole Rig` 保留校准，但头部捏脸工具会锁定；移除或重新生成绑定会丢失校准。保留可重新编辑的 Maya/DNA 作者源与 Character，不声称可无限任意捏脸且无损保留绑定。

本机确认有 `ImportFromFaceDna` 和 `FImportFromDNAParams.bImportWholeRig`，本轮没有运行新增 DNA 往返。本机头文件个别头部选项注释含 body 字样，不能仅靠该注释推断行为，以上限制以官方流程文档和后续实测为准。

## 商业工具采用条件与停止条件

如采用 MetaPipe：在合法授权的 Maya 环境中，以 canonical G2 对应准备为第一步，保留 MH 输出，随后完成工作 DNA → Character → Assembly 往返。不得仅用 FBX 导入后的动画成功作为交付。正式依赖前，必须用同一对应文件转换三例，记录各阶段几何、LOD、DNA 与动画检查、独立重载和普通关卡动态 Spawn；视觉由用户比较固定七视图。

如果标准输出仍须每人物手工重做对应，或校准无法通过 Character 保存重载，则不将其作为通用转换器生产依赖。可保留为作者修正工具。这是工程决策边界，不是人工视觉验收阈值。

没有核实已购 MetaPipe/Maya 许可证或可用安装。本次未运行其试用，也没有性能或转换时长实测。厂商视频不能替代本项目三例结果。照片/图像追踪可作辅助检查，但已有确定三维源拓扑时，改走照片推断不会补回缺失的可靠拓扑对应；这是本项目的工程判断。

## 许可与数据

只参考公开工作流，独立实现算法。厂商页面的许可限制不支持复制其源码、库或受限模板到公开仓库；生成资产的发布范围需按所购许可确认。工具使用许可不等于来源资产许可。[DAZ 官方许可说明](https://www.daz3d.com/blog/daz-3d-interactive-license) 区分渲染与交互产品内的三维数据使用；具体 VaM/DAZ 资源须逐项核对，不能因改拓扑就假定取得再分发权。任何新增服务授权或素材传输另行明确，既有 Epic 授权不扩大到新服务。

## 本轮交付状态

完成公开方案调查、与真实三人物失败证据对照、本机 API 核实及路线收敛。没有生成新的 MetaPipe 人物，没有声称眼角已经修复或达到完美还原。后续应执行一次 canonical 对应与官方往返的有限验证，而不是再扩大工具搜索或对当前人物继续无界微调。
