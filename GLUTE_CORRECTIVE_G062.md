# G0.6.2 — 局部蒙皮收缩补偿与当前姿态对比

本阶段延续 G0.5 / G0.6.1。只处理确定性 pose corrective；没有 Glute Jiggle、惯性、阻尼、接触或 Chaos。Breast 不变。

## 问题与量化方法

G0.6.1 的来源 `!Bend Fix` 恢复解决了来源 Morph 被过度衰减的问题，但没有修正 Native LBS 在混合 pelvis / femur 大角度旋转时的收缩。不能通过给该来源 Morph 乘更大系数来声称解决整个蒙皮基线。

新增构建期旋转混合参考，使用**同一人物、同一最终 G0.5 pose、同一量化后的蒙皮权重**，对比 LBS 与 normalized dual-quaternion blending。参考模型来自 [Kavan 等，Geometric Skinning with Approximate Dual Quaternion Blending，2008](https://users.cs.utah.edu/~ladislav/kavan08geometric/kavan08geometric.html)，算法 1 与 antipodality 处理。它是几何参考，不是 VaM 运行时重现，也不是人体解剖真值。

## 模型

每骨 `M = posed × bind⁻¹`，旋转 `q = q_pose q_bind⁻¹`，平移 `t = M.origin`，dual part `d = (t,0)q/2`。以最大权重骨的 quaternion 为符号参考，同半球后加权 q / d，除以加权 q 的长度，得到点的刚性混合变换。

G0.5 helper 有非刚性 scale，不能直接丢弃。额外保留 `Σ w [M p − (q p + t)]`，即原 LBS 的加权 affine 残差。因此单 influence 保留完整 affine，neutral 等价，统一刚体变换下等变。这个 affine 扩展是项目工程选择，并非声称实现论文的完整 two-phase scale/shear 算法。

目标差值 `R = Reference − LBS` 只进入现有连续 glute region。左右按 region 占比分配；边缘 SmoothStep 衰减。逆当前 target 的 skin matrix 烘焙为 pre-skin delta，以人物最小区域尺寸的 30% 减去既有 source/procedural 位移为预算，tanh 连续限幅。目标不是精确复现全部 DQS，边界、安全和既有结构优先。

新的 skinning residual 独立进行 target / blended-pose safety。已通过 G0.6.1 检查的 source / procedural delta 作为 fixed contribution，conditioning 只衰减新增 residual；保留原三角形翻转、5 倍面积、3 倍边差与矩阵 determinant 限制。不能通过压低来源 Morph 腾出预算。无法收敛则构建失败，不发布资产。

最终 `Source + Procedural + SkinningResidual` 烘焙为同一套 AP/ML/SI native Morph bases，继续使用现有 pose weights 与 Shape 各轴适配。Runtime 不计算 DQS，不执行 CPU 蒙皮，不增加骨骼或修改共享 Skeleton。该系统仍是稀疏 targets 的确定性 pose 函数，不是连续肌肉求解器。

## 版本与自动构建

新 Profile 显式写入 schema 3 / `glute-corrective-g062-v1`，新增独立 residual 的 RMS / sparse vectors。旧 schema 1/2 可加载；类默认值保持 schema 1，避免破坏省略默认属性的旧资产。Runtime bundle identity 更新为 `runtime-bundle-v11-glute-skinning-residual`。

“生成 UE5 人物资产”以及正式 Upgrade Runtime 自动构建新 profile / Morphs，生成新的 committed runtime root。旧场景中的 Actor 不会被自动替换；需加载新 RuntimeConfiguration 或使用新生成 BP。

## 当前姿态对比

人物调试面板 → Glute Structural Debug → **对比当前姿态：Corrective OFF / ON（15 秒）**。

- 固定当前实例最终骨骼姿态与当前已应用 Shape / Morph buffer，计算同姿态 OFF 和假定 ON 的 native LOD0 表面。
- 青色为 OFF，品红为 ON，均为 1:1；显示 15 秒，移动人物后需重新捕获。
- 面板显示每侧 RMS / P95 / Max（cm），JSON 保存到项目 `Saved/VamDiagnostics`。
- 无论开关当前状态如何，按钮都能比较两种结果；另外保存当前实际 applied corrective 的统计。不会切换开关或修改 pose / Morph。
- 仅按需在 Editor 计算，不进入 Runtime 主路径。它是最终 native buffer 的 CPU 重建，**不包含材质 WPO / cloth / GPU readback**。仅支持 LOD0，其它 LOD 明确提示移近后重试。
- 原两组按钮分别标明“查看构建时诊断目标”和“设置人物姿态”。前者仅选择离线 target 向量；后者才改变人物姿态。离线 stage 新增 `Skinning Residual`。

## 验证与限制

新增数学单测覆盖 neutral、单 influence affine、混合旋转半径、刚体坐标变换等变、quaternion antipode。SurfaceTruth 在实际 LOD / Morph buffer 域检查完整输出；来源保留率与无来源 family calibration 继续单独检查各自分支，不把新增几何残差混进来源幅度指标。

当前姿态 snapshot 在普通动态生成 BP 的 NativeRuntime 测试内调用，检查不修改姿态；保留 Morph / Shape / 两实例 / 30、60、120 FPS 测试。两个人物的构建、reload、测试及 Cook 结果、资产路径与实测位移记录在本阶段 evidence 报告。

限制：DQS 也可能引入鼓胀，不能保证人体姿态正确；本方案只将其用作受限局部补偿。稀疏姿态 targets、极端 Shape 的轴向尺寸适配、现有 region 和权重仍会限制结果。安全采样不构成全姿态解析证明；不会修复原 baseline 已有的全部问题。视觉轮廓和臀腿连接仍由用户在实际动画中判断。
