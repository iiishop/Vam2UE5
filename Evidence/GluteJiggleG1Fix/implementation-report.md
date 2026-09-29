# G1 臀部空间标定修复

> 后续已完成蒙皮参与和合成阻尼优化。最新人物路径和 Breast 对比结果请看 `../GluteJiggleG1Surface/implementation-report.md`。本报告保留为空间标定修复的历史记录。

日期：2026-09-29。分支 `feature/jiggle/hip`。HEAD：
`119c23b8aa7f9ca96673f1c2ca611a15304b320a`。
G1 和本次修复保留为工作区改动，没有自动提交或推送。

## 原因

调试点绘制的是实际 G1 节点，问题出在继承自 G0 的几何标定。
原先按顶点数量计算 region 的均值、方差、语义中心和 donor 支承统计。
会阴附近的高密度拓扑获得过大统计权重，此外保留在 native 数据中的 253 个
无表面三角形顶点也参与了统计。Primary 左侧约 20% 的旧 region 权重落在这些顶点上。
这将 helper 拉向中线，并影响下游 attachment、尺寸、质量和动态标定。

Primary 左 Core 的旧 rest 为 `(6.612, 1.650, -16.013)` cm，实际蒙皮作用的
面积中心为 `(10.306, 7.411, -15.639)` cm，距离 6.853 cm。
这些是人体支承局部坐标，不是固定世界坐标。旧测试覆盖了数值响应，但缺少
helper 与实际作用表面的空间一致性检查，因此没发现这个错误。

## 修复

- G0 algorithm 升为 `glute-structure-g05-surface-v2`。
- 三角形面积按每顶点三分之一累积，用于原始证据、region 空间矩、语义中心、
  donor 支承和 Shape 重测。无表面积顶点的 region seed/gate 强制为零。
- 重新生成现有十二个 helper 的位置、局部支承、部分转移蒙皮权重、Shape 响应及
  下游 G1 Profile。保留原骨骼索引、helper 名称/层级、Morph 和原有 donor compression。
- 没有提高默认 Jiggle 强度，没有修改 Breast 的物理参数，没有添加接触或 G2。
- 增加 `VamGluteStructureBuilder::SpatialAudit`，用最终 native mesh 的实际 helper
  influences × 表面积独立计算作用中心。新算法发布时拒绝无表面 region 权重、
  无实际 skin support 或超出归一化空间误差 0.25 的 helper。
- 增加 `Vam.Glute.G1.SpatialCalibration`，同时验证左右语义排列，并故意移动一根
  helper，确认错误标定不能发布。这是工程一致性检查，不是解剖精度或视觉评分。
- 旧标定人物在 G1 Diagnostics 中显示升级提示；编辑视口显示需要 Play/Simulate。

Primary 左 Core 新 rest 为 `(11.587, 12.759, -11.382)` cm，重新分配权重后的
作用中心为 `(11.663, 12.502, -10.813)` cm，距离 **0.629 cm**。
上/下/内/外节点均与各自作用区域一致，具体误差及第二人物结果见 `summary.json`。
节点不强制等于表面中心；分区核与 donor compression 不同，允许有限差异。

## 新资产

| 人物 | 新 committed runtime 根目录 |
|---|---|
| Primary | `/Game/VamRuntime/R_9584ad8ea42c00ed6e45eef9` |
| Secondary | `/Game/VamRuntime/R_cc46a2c81d851cdb45dca5b4` |

每个目录包含 `BP_VamCharacter`、`RC_Runtime`、`DA_GluteStructure`、
`DA_GluteJiggle` 和 `DA_GluteCorrective`。新的可见 body 在 `Glute/SK_Body`。
两份资产经过独立进程 build/reload/verify，源文件指纹校验通过。
原始人物和旧 committed runtime 输出均保留；仅更新 DLL 不会修改旧 BP 引用的网格。
正式“生成 UE5 人物资产”以及 Runtime 升级链自动采用新 Builder，不增加额外按钮。

## 如何观察

1. 打开 SmartNPC，在 Content Browser 进入上述 **Primary 新目录**，将
   `BP_VamCharacter` 拖入普通 Empty Level。不要继续使用旧目录里的同名 BP。
2. Play 或 Simulate，选中运行中的人物，在“VaM 人物调试”点“使用选中人物 / 刷新”。
3. 必要时用“显示衣服 / 配饰”隐藏遮挡；不删除任何服装资产。
4. 展开 **Glute Jiggle - G1**，保持 Enabled 开启，先使用默认参数。
5. 开 **Show Dynamic Nodes** 和 **Show Rest vs Dynamic**：橙点为动态节点，
   青点为结构 rest，品红线是当前位移。Pelvis/Thigh Attachment 的支承端本来就
   位于体内，判断区域时应看动态节点，而不是要求每条线的两端都在臀部表面。
6. 点击 **Walk Cycle / Alternating Thigh Swing** 或 **Jump**，也可用
   **Smooth Forward Accelerate → Smooth Stop**。仅打开 Show 不会触发运动；
   稳定站立或余振结束后 rest 与 dynamic 重合是预期行为。
7. 需要固定屈髋观察时先 Reset 停止 Walk，再在 Structural Debug 选择 Flexion 90，
   回 G1 进行加速/Jump。对比用 G1 Enabled，保持结构层与 Corrective 状态不变。
8. “对比 G1 OFF / 当前动态表面”保存同一姿态和 Morph 下的 native LOD0 表面差值。

## 验证与范围

最终编译、两人物测试、Cook、表面位移和安装 hash 记录见本目录日志与 `summary.json`。
原始 G1 报告保留为历史证据；其旧资产路径不适用于本次修复。

| 检查 | 结果 |
|---|---|
| Editor Development / Game Development / Game Shipping | 全部编译成功 |
| 主项目已安装插件，Primary | 18 项通过，0 失败 |
| 主项目已安装插件，Secondary | 18 项通过，0 失败 |
| Python | 52 项通过 |
| Windows Cook，两个普通空地图 BP fixture | 831 cooked，7 platform skipped，总计 838；0 error / 0 warning |
| Breast 数值回归 | 两人物各 7 个 Breast 测试的数值 Info 与首次 G1 完全相同 |
| 253 个未使用顶点的 region 权重 | 两人物左右侧全部为 0 |
| 最大归一化 helper/support 误差 | 两人物全部低于 0.10；发布上限 0.25 |
| 深屈髋 + 200 cm/s² 加速的真实 native 表面差值 | Primary 0.159701 cm，Secondary 0.217743 cm |
| 安装 | 两个 DLL、两个 PDB、modules 文件全部与 host SHA256 一致，旧文件已备份 |

每个人物的 18 项测试中有两项既有 NativeRuntime 测试记录 WorldCleanup 缺少 EndPlay
的清理警告，故报告为 16 succeeded + 2 succeededWithWarnings；未隐去警告。
加速测试中的 helper 峰值分别为 0.504364 / 0.945111 cm；表面只接受部分 helper
权重，因此表面位移小于节点位移。这个测试证明有效传递，不代表观感或幅度验收。

本修复会改变 G0 几何标定及其派生权重，不能声称旧 G0.6.2 的逐点表面输出完全不变；
Corrective 算法本身没有在本次改写。静态零偏移 bind reconstruction、Morph、Shape、
动态开关等价、实例隔离和 Breast 回归由自动测试检查。

工程表面证据是 native LOD0 的按需 CPU 重建，不是 GPU readback，也不包括 cloth/WPO。
本次没有录像或人工视觉验收，不宣称摆动幅度或观感已符合用户要求。
最终视觉效果仍需用户用新 BP 观察。
