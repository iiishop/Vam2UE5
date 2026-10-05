# 文档索引

从 [根目录 README](../README.md) 开始。正文按导入指南、人物与物理、研究、历史归档分类；`Evidence/` 保留原始测试回执、日志及实施报告，路径不变。

这些文档包含阶段交付快照。文中的 HEAD、资产目录、编译结果与参数默认值属于记录时的版本，不能自动视为当前提交的验收结果。下列入口按当前默认分支 `master` 的源码组织。

## 导入与预览

| 文档 | 内容 |
| --- | --- |
| [STAGE02](guides/STAGE02.md) | 预设、依赖解析与锁定导入计划 |
| [STAGE03](guides/STAGE03.md) | 数据解码、格式边界与临时几何预览 |
| [STAGE04](guides/STAGE04.md) | 来源材质与 UE 外观参考 |
| [STAGE05](guides/STAGE05.md) | 正式人物资产、体型内核、重导入与动作重定向 |

根目录 `STAGE02–05.md` 仅保留跳转，兼容现有分发检查脚本。指南中的 `Config/`、`Scripts/`、`Tests/`、`Saved/` 等裸路径均以插件根目录为起点。

## 人物与物理

| 入口 | 阅读顺序与范围 |
| --- | --- |
| [原生 Runtime 与旧资产迁移](runtime/PHYSICS_RESET.md) | 退役 SoftTissue 的基线记录；其中“没有新的 Jiggle”限定在清理阶段 |
| [Breast v3](runtime/BREAST_JIGGLE_V3.md) | 移动参考系、三模态、调试操作与后续幅度控制；[v2 报告](../Evidence/BreastCalibrationV2/implementation-report.md)保留在 Evidence |
| [Glute G1.1](runtime/GLUTE_JIGGLE_G11.md) | 参考重力平衡；[G1](runtime/GLUTE_JIGGLE_G1.md)保留基础模型和空间校准修订 |
| [腰臀衔接](runtime/HIP_WAIST_TRANSITION.md) → [表面保护](runtime/GLUTE_SURFACE_GUARD.md) | 按后续修订阅读共同运动、区域残差及边界权重；旧输出规则已被正文中的后续修订替代 |
| [Leg T1](runtime/LEG_JIGGLE_T1.md) | 大腿/小腿动态、余振及臀腿过渡 |
| [G0](runtime/GLUTE_STRUCTURE_G0.md) → [G0.5](runtime/GLUTE_STRUCTURE_G05.md) | 臀部姿态结构的基础模型与细化，属于当前管线的结构层 |
| [G0.6](runtime/GLUTE_CORRECTIVE_G06.md) → [G0.6.1](runtime/GLUTE_CORRECTIVE_G061.md) → [G0.6.2](runtime/GLUTE_CORRECTIVE_G062.md) | 姿态修形、来源幅度审计与局部蒙皮收缩补偿的增量记录 |

当前 `VamGluteSkeletalMeshComponent.h` 的 `GluteAmplitude` 默认值为 **1**；`VamBreastSkeletalMeshComponent.h` 的 `BreastAmplitude` 默认值为 **2**。G1.1 与表面保护记录中的臀部默认 **3** 是此前交付值，后续已经调整。历史报告保留原始数值，当前行为以源码及实际资产/实例设置为准。

## 研究资料

| 文档 | 内容 |
| --- | --- |
| [胸部自动校准](research/BREAST_CALIBRATION_RESEARCH.md) | v2 校准模型、参考来源与近似边界 |
| [臀部表面传递](research/HIP_SURFACE_RESEARCH.md) | 拓扑过渡、双侧校准与后续修订 |

旧 Chaos 方案的可行性分析见[历史归档](archive/README.md)，其中已删除的类与实验不代表现有架构。

## 维护规则

- 新指南放入 `guides/`，人物/物理合同及增量说明放入 `runtime/`，研究依据放入 `research/`。
- 完整退役的方案与固定阶段验收快照放入 `archive/`，并在[归档索引](archive/README.md)说明原因。
- 已有模型仍参与当前管线时，保留其基础说明，不仅因日期旧就归档或删除。
- 根目录保留 README、许可证（若已有）、工具与必要兼容入口；移动文档时同步相对链接、打包文档清单和索引。
