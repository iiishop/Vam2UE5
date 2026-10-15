---
title: Glute · 仅保留 residual 保护
subtitle: 正式生成资产 · Editor / PIE · 同姿态同相机下方光照
lang: zh
---
源修形和程序修形的安全衰减已从正式构建链路删除。Residual 以它们的固定表面为基准，保留三角形朝向、面积、边差和幅度保护。源表面原有异常仍单独记录。

## 1 · 实现范围
- 删除逐目标、独立轴基底、混合姿态及跨目标传播到源/程序字段的衰减。
- 源 Morph、程序修形、辅助骨、Jiggle 和接触系统保留。
- B2 共 20 组目标的 source/procedural before/after 差值均为 0，安全系数均为 1。
- 仍拒绝非有限值、缺失映射和失配身份；最终实际权重重放误差门槛仍为 0.001 cm。
- 旧完整表面安全检查继续适用于旧策略资产。新资产对 residual 相对固定表面进行检查，并保存完整表面异常计数。

## 2 · 新资产与实测
配置：/Game/VamGluteResidualOnlyV2/Runtime/R_330820d9f44ce116cf1383a3/RC_Runtime

角色：/Game/VamGluteResidualOnlyV2/Runtime/R_330820d9f44ce116cf1383a3/BP_VamCharacter.BP_VamCharacter

身份：330820d9f44ce116cf1383a3b581db5faa013267531056f20adbe9d122e9f4fe

- 构建、独立重载、发布验证完成。
- 15 项 UE 自动测试通过，包含最终量化权重、修改权重使旧 corrective 失效、Breast/Glute/Leg 回归、residual 专项测试。
- 11 个 PIE 姿态、4 个 Editor 姿态；4 视角 × 8 显示模式，共 480 张真实 UE 截图。
- Editor/PIE 对应表面最大差异：0 cm。
- B2 重烘焙最大差异：6.75119903e-14 cm。
- 最终实际权重 authored replay 最大差异：0.000147395422 cm；residual 不合格三角形：0。
- 完整 corrective 仍触发旧三角形判定 256 次（跨 authored targets 累计，非去重面数）。这不等于碰撞检测结果，也不是宣称整份表面安全。

主工程 DLL 已安装，旧 DLL 留在 binaries-backup。SmartNPC 主工程另行完成坐姿 Editor/PIE 各 32 张截图，表面与构建工程差异为 0。

- [主工程验收与截图清单](main-project-validation.json)
- [安装文件 SHA256](installation.json)

## 3 · 已知形态限制
移除衰减保留了更饱满的坐姿轮廓，但外展内侧的折叠依旧存在。当前任务实施的是用户选定的保护策略，并未把该折叠标记为已修复。历史接触问题另列，未纳入本次通过结论。

新旧数据按实际骨骼矩阵和来源映射对齐，未使用最近邻匹配。折线比较沿用前次固定 source-edge 集合，避免变形后 ROI 移动漏掉坏边。

## 4 · 原始证据
- [自动测试](tests.json)
- [字段与分层对照](evaluation.json)
- [最终权重重放](authored-targets.json)
- [B2 精确重烘焙](bindings/fields/detail.json)
- [480 张截图清单及 SHA256](screenshots.json)
- [构建资产身份](bundle.json)
- [第一次构建失败证据](../residual-only/bundle.json.error.json)：旧完整表面判定不兼容本次明确移除源衰减的策略，已改为独立 residual 验收，并保留完整表面异常计数。
