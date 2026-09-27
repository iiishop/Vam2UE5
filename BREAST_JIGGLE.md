# Breast Jiggle：当前版本 v3

当前实现、数值结果、资产路径及 Empty Level 操作见 [v3 完整报告](BREAST_JIGGLE_V3.md)。

v3 增加移动参考系速度事件、COM/rotation/residual 三模态和连续调试轨迹。普通四参数及其范围保持 v2。下面是 v2 历史报告；其中旧按钮、资产路径和 HEAD 不代表当前交付。

# Breast Jiggle 自动校准 v2：历史工程报告

日期：2026-09-28。分支：`feature/jiggle/breasts`。

## 1. HEAD 与交付状态

开始及结束 HEAD：`1c07fd5cb30f785dc231fccdfcebd37891246504`。远端同名分支也核对为此提交。本轮修改保存在工作区，未自动 commit/push。新版 Editor DLL 已安装到当前 SmartNPC 项目的插件目录。

旧版工程证据仍位于 `Evidence/BreastJiggle`；本轮证据位于 `Evidence/BreastCalibrationV2`。研究、参考来源与近似模型理由见 [BREAST_CALIBRATION_RESEARCH.md](BREAST_CALIBRATION_RESEARCH.md)。本文只报告工程事实。

## 2. 修改模块

- `VamBreastJiggleBuilder`：几何区域体积分配、尺寸/支承测量、Shape 校准响应、donor influence 压缩。
- `VamBreastJiggleProfile`：schema 2，算法 `breast-calibration-v2`。新增节点体积/质量中心、支承刚度、尺寸、完整对称惯量张量、转动支承/阻尼/限位、八条耦合边和权重统计。
- `VamBreastCalibration`：构建和运行时 Shape 共同使用的几何校准函数。
- `VamBreastCalibratedSolver`：隐式节点网络及独立角状态。schema 1 保留既有 Solver 兼容路径。
- `VamBreastSkeletalMeshComponent`：实例参数、Shape 重新校准、角状态应用、诊断；保持最终 pose 接入位置和 Native GPU skinning。
- `VamDebugPanel`：正式四参数、Advanced Mass Scale、恢复自动参数；保留衣服显隐和已有调试工具。
- Runtime 配方身份升级为 `runtime-bundle-v5-breast-calibration`，防止重复使用旧 committed output。

## 3. 几何与质量自动校准

保留 source pectoral support、拓扑邻接、Morph delta 证据、镜像胸廓 frame 和连续区域权重。

在 imported p0 上累加支承锥体的有效体积。每个锥体按五个连续语义核归一化分配，积累节点有效体积和质量中心。因此 `node mass fraction = node effective volume / total effective volume`，不再使用固定 40/15/20/10/15 比例。

保存 AP/ML/SI 加权尺寸、root-biased 尺寸、投影支承面积、深度、节点力臂及 COM。投影支承面积仍是几何代理，没有宣称重建真实解剖胸壁。

密度固定为版本化有效基准 **0.00102 kg/cm³**，来源依据及限定见研究文档。质量为 effective volume × density。普通 UI 不再提供 Density。

两份实际人物输出：

| 数据 | 人物 A 左 / 右 | 人物 B 左 / 右 |
|---|---|---|
| Effective volume cm³ | 585.3916 / 583.4258 | 189.5732 / 189.1602 |
| Mass kg | 0.59710 / 0.59509 | 0.19336 / 0.19294 |
| 左侧 AP/ML/SI 尺寸 cm | 17.413 / 10.442 / 11.689 | 13.124 / 6.382 / 6.766 |

人物 A 左侧节点质量占比：Core **14.03%**、Upper **26.94%**、Lower **19.71%**、Medial **26.51%**、Lateral **12.81%**。这些是本次几何积分结果，不是人物特例配置。

## 4. Support baseline

基本模型为 `k = E_effective × support-area-share / effective-length`，再由 COM 距离、支承宽度、节点位置和语义 attachment 修正。长度用 cm 时，`Pa × cm²/cm × 0.01` 转为 `kg/s²`。

`E_effective = 1200 Pa` 是版本化网络工程系数，不是从当前人物测得的组织模量。不同形状通过面积、长度和力臂得到不同刚度；质量只用于推导频率与阻尼系数，不用统一固定 Hz 表替代校准。

Upper / Medial 的 attachment 更强，Lower / Lateral 更自由；AP/ML/SI 刚度分别保存。人物 A 左侧 AP 小振幅频率约为 1.20 / 1.61 / 0.99 / 1.42 / 1.11 Hz，源自本次几何计算。该数值不代表医学真实共振频率。

## 5. 内部 coupling

八条语义边：

- Core–Upper / Lower / Medial / Lateral；
- Upper–Medial / Lateral；Lower–Medial / Lateral。

主连接比邻域连接强。每条边的刚度由两区域体积尺度和质量中心距离计算，不再使用相同 all-to-all pair。内部刚度进入对称图 Laplacian，与节点弹簧一起隐式求解。

## 6. Rotational inertia 与积分

每个语义体积用有限局部球形惯量加平行轴项，累加完整对称惯量张量：对角 XX/YY/ZZ 和非对角 XY/XZ/YZ，单位 kg·cm²，基点为校准 COM。

每侧新增真正保存的 `AngularDisplacement` 和 `RelativeAngularVelocity`，以及自动旋转支承、阻尼与角限位。转身角加速度通过 `-I α` 产生惯性力矩；恒定旋转保留参考系陀螺项。节点仍计算线性惯性、离心项和 Coriolis。

为避免同一整体旋转同时驱动节点和角状态，分离 nodal load 的刚体角分量，并将 residual deformation 保持在质量正交子空间。极端限位后重新建立该约束，修复了测试发现的残留偏移不能归零问题。

固定 **120 Hz**，最多 16 子步。平移使用带 pivot 的 15×15 隐式弹性/阻尼/Coriolis 求解；角状态使用完整惯量的 3×3 隐式求解。角状态直接驱动 helper 朝向及相对 COM 的位置变化，schema 2 不再把 `Cross(Rest, Displacement)` 作为旋转来源。

这是小角度 COM 模态近似，张量固定在胸廓局部 frame；不是任意大旋转刚体或连续软组织 FEM。

## 7. Travel 与非线性

- AP：按 effective depth；胸壁方向约束更小。
- ML：按横向尺寸与 root width 共同约束。
- SI：按纵向尺寸，并对 Lower 的悬垂长度作修正。
- 角限位：由横向/纵向尺寸相对深度的几何角推导。

正负方向采用连续位移相关增硬；胸壁方向更早进入 transition。达到边界的最后投影仍保留，并通过 `emergency limits` 计数显示，不把它当作主要恢复机制。

## 8. Helper skin weights

helper 层级与索引不变：原骨 0–87；左 Anchor 88，Core/Upper/Lower/Medial/Lateral 89–93；右 Anchor 94，五节点 95–99。保持 source pectoral 原语义。

Transfer 根据 region confidence、胸根距离、深度比例、Lower / Lateral 位置连续计算。胸根回到 source support；前部/下部/外侧获得更高 helper influence，不使用全局固定 65% 上限替代区域函数。

满 8-influence 时允许把小的获准 donor 合并到较强 donor，为 helper 腾出空间；所有非 donor influence 保持不变。保留 source 支承部分，最终归一化且不超过 8。

两份真实人物都各有 **3 个满槽顶点获得 helper**，计数中没有无法分配的满槽顶点。合成八 influence 回归覆盖非 donor 精确保留、source 支承保留、合法槽数和 normalization。

边界条件：若顶点已有七个非 donor 和一个较大 donor，保留八个来源又增加 helper 在八槽内不可能。当前显式计数保守回退，不删除无关骨权重；这两份实际人物没有遇到此情形。

绑定等价检查仍使用真实 inverse bind 重建，阈值 **0.001 cm**；原始骨骼绑定平移误差 **0 cm**。Morph 和 helper identity 延续 NativeBuilder 的既有验证。

## 9. 正式参数语义

全部默认 **1.0**，人物校准数据独立存于 Profile。

| 位置 / 参数 | 唯一直接修改的数据 | 范围 |
|---|---|---|
| 普通：Support | Anchor 平移与旋转恢复刚度 | 0.1–10 |
| 普通：Damping | 平移与旋转阻尼比 | 0.1–4 |
| 普通：Mobility | 平移和角度 soft/hard limits | 0.25–3 |
| 普通：Internal Coupling | 八条内部弹性边的刚度 | 0–4 |
| Advanced：Mass Scale | 平移质量、完整转动惯量 | 0.1–10 |

Support 不改变质量、阻尼比、travel 或内部边；Mass Scale 不改变弹性、阻尼比、travel 或 coupling。阻尼系数由 `c=2ζ√(mk)` 计算，因此质量或支承改变时系数随之变化，以保持指定阻尼比。

更大阻尼减少欠阻尼余振；超过临界阻尼后，继续加大会使恢复变慢。它不是“值越大永远越快”的宏参数。

普通 UI 已移除 Density、Softness、FrequencyScale XYZ、DampingScale XYZ、TravelScale XYZ、CouplingScale 和上一轮未完成的调节预设。只保留 schema 1 的隐藏兼容字段与路径。

## 10. Shape、Teleport、暂停、多实例

Shape 预计算响应新增尺寸、root size、节点体积 log slope 和质量中心 delta。改变形状后在实例数据上重新计算质量分布、惯量、support、coupling、travel。保持 helper 名称/索引、Mesh/Skeleton 资产身份；不修改共享 Skeleton。

Preview 清除无效运动历史，不把编辑差分当作速度；较大体积变化受控 reset。小变化保留状态。Teleport reset 同时处理节点及角状态，按 Profile 策略保留或清除位移；暂停不推进，恢复重建采样。

Profile 只读共享。两侧节点、角状态、运动历史、accumulator、sleep 与参数均为实例独立状态。

## 11. 自动生成和迁移

仍然是选择人物 → **生成 UE5 人物资产**。Native source 后自动执行区域分析、几何/质量/惯量/support/graph/travel 校准、append helpers、权重生成、Profile、Shape、RuntimeConfiguration、BP 和三进程保存/重载/发布。

v1 Profile 继续运行旧求解路径；面板会说明需要升级才能使用新四参数。旧 BP、来源资产和已放置实例没有被覆盖。

第二份输出实际执行了 `ue_runtime_upgrade.py`，从已有 committed BP 提取原生来源配方再生成新目录。第一份执行了正常 Runtime 构建三阶段。原始来源文件和本轮构建算法 SHA256 均核对一致。

本次没有重新解码 VAR；验证范围是两份已持久化 Native 来源到完整 committed Runtime 的构建/升级链。

## 12. 实际资产路径

人物 A：

- `/Game/VamRuntime/R_195d0e65b3cfff3909fd36d2/BP_VamCharacter`
- `/Game/VamRuntime/R_195d0e65b3cfff3909fd36d2/DA_BreastJiggle`

人物 B（正式升级入口）：

- `/Game/VamRuntime/R_e0c94d2ffa9b00b4cc3adc43/BP_VamCharacter`
- `/Game/VamRuntime/R_e0c94d2ffa9b00b4cc3adc43/DA_BreastJiggle`

每个目录同时包含 `RC_Runtime`、Body/Skeleton、Shape、Geometry、Parts、Rig、Physics、Materials 等正式依赖，均已位于当前 SmartNPC Content。

## 13. 工程测试与证据

- Editor Development、Win64 Game Development、Game Shipping 编译成功。
- 两份不同几何的来源使用同一算法完成保存、独立重载及第三进程 committed 验证。
- 每份新人物四组 C++ 自动测试均成功；安装到当前 SmartNPC 后重复四组，同样成功。
- `Vam.Breast.Calibration`：几何趋势、质量分配、五个控制的参数边界、欠阻尼衰减、线/角启动制动、恒定线速/角速、Coriolis、Teleport/Pause、实例隔离、100 秒极端参数有界性和卸载后的恢复。
- `Vam.Breast.Dynamics`：保持原 v1 动力学及原阈值回归。
- `Vam.Breast.NativeRuntime`：真实人物加载、helper/weight/bind、最终动画 Anchor、Shape/Morph identity、暂停/Teleport/双实例。
- `Vam.Breast.WeightCompression`：原始八 influence 顶点的 donor-only 分配。
- 19 项 Python 配方/升级/Shape 回归通过。
- v2 完整轨迹差异：30/120 FPS 最大 **0.034610 cm**，60/120 最大 **0.011445 cm**；角状态 30/120 最大 **0.000255 rad**。
- Cook 两份人物依赖：825 packages cooked、7 skipped by platform、总计 832；**0 errors、0 warnings**。
- 自动测试各轮为 3 success + 1 success-with-warning、0 failed。告警是既有 NativeRuntime 临时 World 缺少 EndPlay 的清理告警；Cook 没有该告警。

详见 `Evidence/BreastCalibrationV2/summary.json`、两份 audit、三份 automation JSON、编译/测试/Cook 日志。完整事务配方与日志位于 `Saved/CalibrationV2`。工程测试不进行外观判断。

## 14. 技术限制

- Volume/root area 和 finite-region tensor 是几何代理；没有真实内部解剖组织。
- 有效模量和语义 attachment 是工程基准，尚未用运动捕捉或实验组织数据做参数辨识。
- 角模式是局部小角度近似；极端调参可超出模型精度。节点与角模式的分解不是完整连续组织耦合。
- Shape 响应在 imported appearance 附近近似；极端 Morph 组合没有几何重积分。
- 八 influence 的单 donor 饱和特例有明确保守回退。
- 无 Chaos Flesh、Contact/按压、自碰撞、胸部间碰撞、Cloth、肌肉 corrective 或 wrinkle。衣服保留来源权重。
- Cook 通过不等于打包游戏性能或视觉验收。区域边缘、可见皮肤变形、自然程度和参数手感仍需人工判断。

## 15. Empty Level 人工体验

1. 重开 UE，创建 Empty Level，拖入第 12 节任一**新目录**中的 BP。
2. Simulate，选中运行实例；Window → VaM 人物调试 → **使用选中人物 / 刷新**。
3. 点击面板上方 **隐藏衣服 / 配饰**，再次点击可恢复。
4. 展开 **Breast Jiggle · Runtime**，先保持四参数都是 1。
5. 分别使用 Forward accelerate、Stop、Lateral accelerate、Jump impulse、Rotate continuously、Stop rotation；Reset 清除状态并停止调试运动。
6. 想改变支承恢复强度调 Support；想减少或延长余振调 Damping；想改变运动空间调 Mobility；想改变各区域相互牵连程度调 Internal Coupling。逐个调，便于比较。
7. Advanced 中才有 Mass Scale。**恢复自动校准参数（全部 1.0）** 恢复这些倍率并重置状态。
8. Show Helper Bones / Region Weights / Dynamic Nodes 与 Diagnostics 显示节点、区域、Volume、Mass、COM、Inertia、Root Area、Depth、尺寸、节点质量、自动支承/阻尼/travel、运动和求解状态。

不需要指定地图、Level Blueprint 或额外 Solver Actor。工程 Cook 的 Empty 地图只是独立测试夹具，不是这些 BP 的依赖。最终外观由用户人工评估。
