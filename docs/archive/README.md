# 历史归档

返回[文档索引](../README.md)。以下材料保留各阶段的实现选择、失败、限制与证据；日期旧本身不是归档理由。不要把历史资产、脚本或通过结果当作当前源码的能力或验收入口。

| 文档 | 归档原因与当前入口 |
| --- | --- |
| [STAGE06](STAGE06.md) | 固定 Stage06 资产、旧配置及当时未完成项的快照；基础能力继续存在，通用 Runtime/迁移见 [PHYSICS_RESET](../runtime/PHYSICS_RESET.md)，后续动态见[文档索引](../README.md#人物与物理) |
| [STAGE07](STAGE07.md) | 正文已标记 LEGACY / SUPERSEDED；Flesh/SoftTissue 实验链已退役，保留事务、组合回归与失败调查 |
| [STAGE071_RUNTIME](STAGE071_RUNTIME.md) | 已退役人物软组织 Runtime 的接入与生命周期报告；保留迁移背景 |
| [STAGE07_TESTING](STAGE07_TESTING.md) | 仅对应旧 Cooked 检查点及已退役回放方式，不是当前测试指南 |
| [VALIDATION](VALIDATION.md) | 阶段 01–02 的真实库计数、性能测量与浏览器验收快照，不是当前构建状态 |
| [CHAOS_SOFT_TISSUE_RESEARCH](CHAOS_SOFT_TISSUE_RESEARCH.md) | 调查对象包括已退役的 CPU 表面/SoftTissue 实现；保留架构取舍与研究来源，不作为当前实现说明 |

旧测试证据仍在 [Evidence](../../Evidence/LEGACY.md)，不搬动日志、JSON 或 UE 资产。[PhysicsReset 证据](../../Evidence/PhysicsReset/README.md)仅对应清理时的版本。

## 去重记录

删除根目录 `BREAST_JIGGLE.md`：其 v2 报告正文（自日期行起）与 [Evidence/BreastCalibrationV2/implementation-report.md](../../Evidence/BreastCalibrationV2/implementation-report.md) 逐行相同。唯一额外的 v3 导航与版本说明已合并到[文档索引](../README.md#人物与物理)，研究链接已修复。v2 原报告完整保留，删除文件也可从 Git 历史恢复。
