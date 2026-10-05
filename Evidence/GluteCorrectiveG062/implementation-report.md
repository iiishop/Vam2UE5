# G0.6.2 实施报告

## 状态

- 分支：`feature/jiggle/hip`。
- HEAD：`6c52542bfb2634d36e157510b4fa8c6319724016`。本轮与此前 G0.6.1 修改保留在工作区，未自动 commit / push。
- 完成编译、安装、两个不同来源人物的自动生成、独立进程 reload / verify、正式 Upgrade Runtime、工程测试与 Windows Cook。
- 本轮没有新增 Jiggle / Chaos / contact，没有修改 Breast 模型。

## 实际变化

1. 新增构建期 `VamGluteRotationBlend` 几何参考，使用同一 G0.5 最终 pose 和同一量化 skin weights 测量旋转混合与 LBS 的差。
2. 新增 `VamGluteSkinningResidual`，将现有 glute region 内的收缩差烘焙为 pose corrective。保留 affine scaffold；经连续 region mask、人物尺寸限幅、逐 target / 混合 pose / 独立 AP、ML、SI basis 的安全约束。
3. 已有来源 Morph 与 procedural contribution 作为固定项，不被新增残差的 conditioning 衰减。与 G0.6.1 全 target RMS 对比：Primary 最大差 `8.85e-13 cm`，Secondary 为 `0`。
4. Profile schema 3，算法 `glute-corrective-g062-v1`；旧 schema 1/2 继续加载。Runtime identity 为 `runtime-bundle-v11-glute-skinning-residual`。
5. Runtime 继续使用确定性 pose weights → native Morph → GPU skeletal skinning。没有运行时 DQS / CPU 全身蒙皮；没有新增 helper、改变 source bone index 或 donor weights。
6. 新增 Editor 按需当前姿态 snapshot，读取当前实例最终 pose、Shape/Morph buffer 与原生 LOD0，显示同姿态 OFF / ON 叠加，并保存 JSON。两组调试按钮的说明区分了“离线诊断目标”和“人物姿态”。

模型公式、研究来源与详细限制见 [GLUTE_CORRECTIVE_G062.md](../../docs/runtime/GLUTE_CORRECTIVE_G062.md)。

## 位移实测

最终原生 LOD/Morph buffer 重建，在区域加权表面域测量。下表为单侧屈髋 90°，单位 cm；P95 是区域分布分位数，不是所有顶点都移动该距离，也不是视觉评分。

| 人物 / 侧 | G0.6.1 P95 | G0.6.2 P50 | G0.6.2 P95 | G0.6.2 Max | G0.6.2 RMS |
|---|---:|---:|---:|---:|---:|
| Primary L | 0.410 | 0.434 | 2.306 | 2.497 | 1.121 |
| Primary R | 0.408 | 0.512 | 2.032 | 2.251 | 1.143 |
| Secondary L | 0.424 | 1.533 | 2.403 | 2.510 | 1.547 |
| Secondary R | 0.410 | 1.491 | 2.377 | 2.460 | 1.501 |

Primary 原始旋转混合参考与 LBS 的差，在同姿态 P95 约 L 3.713 / R 3.296 cm。发布结果没有照搬全部参考位移；受 region、尺寸和几何安全约束。两个人物新增残差均经 6 次外层安全迭代收敛。

## 实际资产

Primary 从上一版 committed BP 经正式 Upgrade Runtime 创建：

- BP：`/Game/VamRuntime/R_284a9f4a49f5555fa11e5796/BP_VamCharacter`
- 配置：`/Game/VamRuntime/R_284a9f4a49f5555fa11e5796/RC_Runtime`
- Corrective：`/Game/VamRuntime/R_284a9f4a49f5555fa11e5796/DA_GluteCorrective`
- Structure：`/Game/VamRuntime/R_284a9f4a49f5555fa11e5796/DA_GluteStructure`
- 原生网格：`/Game/VamRuntime/R_284a9f4a49f5555fa11e5796/Glute/SK_Body`

Secondary 经普通自动生成链创建：

- BP：`/Game/VamRuntime/R_89c63bf279fa1ac8baf43b0c/BP_VamCharacter`
- 配置：`/Game/VamRuntime/R_89c63bf279fa1ac8baf43b0c/RC_Runtime`
- Corrective：`/Game/VamRuntime/R_89c63bf279fa1ac8baf43b0c/DA_GluteCorrective`

两个人物原 88 个 source bones 与索引保留，总计 112 bones（包含已有 Breast / Glute helpers）。本轮没有新增骨骼。生成的 Blueprint 无需工程测试地图或 Level Blueprint 初始化。

## 如何观察

1. 打开普通 Empty Level，拖入上述新 BP；或选中旧场景人物，在 Content Browser 选中上述新 `RC_Runtime`，点击人物面板的“加载内容浏览器所选 RuntimeConfiguration”。旧资产不会因 DLL 更新自动获得新 Morph。
2. 如衣服遮挡，使用已有“显示衣服 / 配饰”切换显示。
3. 展开 **Glute Structural Debug - G0.5 / G0.6.2**，保持最上方 `Enabled`（G0.5 structural helpers）开启。
4. 在“设置人物姿态”一组中，选 `Target both`，点击 `Flexion 90`。也可分别观察 Neutral standing、Hip extension、Abduction、External rotation；Reset 恢复调试姿态。
5. 比较 **G0.6 Corrective Enabled** 开关。新补偿属于这个开关，最上方 `Enabled` 控制的是另一层 G0.5 helpers。
6. 点击 **对比当前姿态：Corrective OFF / ON（15 秒）**：青色 OFF，品红 ON，1:1 大小。结果保持捕获时的位置 15 秒；移动人物后需重新点击。面板显示 RMS / P95 / Max 和 JSON 保存路径。
7. 离线诊断一组的 Flex90 只选构建 target，不会抬腿。`Skinning Residual` 显示新增分支的构建期向量，`Final` 显示全部 corrective。

## 测试与安装

- Unreal Editor Development：编译成功。
- UnrealGame Development / Shipping：编译成功。
- Python：29 / 29。
- Primary 正式升级输出：13 / 13，失败 0。
- Secondary 自动生成输出：13 / 13，失败 0。
- 安装后的 SmartNPC 项目 Primary：13 / 13，失败 0。
- 每组包含 2 个已存在的测试 world teardown 警告（Breast.NativeRuntime / Glute.NativeRuntime 的 EndPlay 提示），不是零警告测试运行。
- 新数学单测验证 neutral、单 influence affine、混合旋转半径、刚体等变、antipode。
- 实际 LOD surface 检查 neutral、单侧 / 双侧、72 个 target / 混合 / extreme pose probes；保留翻转、面积、边差、接缝、有限性、来源保留率和 fallback calibration 约束。
- 保留 Shape Preview / Commit、Morph、helper identity、两实例独立和 30 / 60 / 120 FPS 的确定性验证。
- 当前实例 snapshot 在普通 NativeRuntime BP 上生成；测试检查捕获不改变 pose，关闭 Corrective 时仍能计算假定 ON 对比。
- 两个普通 Empty cook fixture：836 个包处理完毕，829 实际 Cook，7 平台跳过，0 error / 0 warning。
- 安装 DLL / PDB / modules 共 5 个文件，逐文件 SHA-256 核对。原安装备份路径见 [install.json](install.json)。

一次候选版本的完整叠加 surface 已通过，但独立轴 basis 有边差超限。最终版本补充独立 basis conditioning 后重建并通过原阈值，未放宽测试标准。

## 限制与证据

旋转混合参考不能保证肌肉形态正确，仍可能鼓胀；新增层是受限几何补偿。源 `!Bend Fix`、现有 structural model 和局部 residual 各自承担不同部分。本轮没有宣称解决全部原生蒙皮或解剖误差，也没有宣称精确还原 VaM。

当前 snapshot 是 native LOD0 buffers 的 CPU 重建，未做 GPU readback，不含材质 WPO / cloth。极端 Shape 仍使用原各轴尺寸适配；稀疏 target 与有限 pose probes 不等于全姿态安全证明。用户需人工判断臀部主体、下缘和臀腿连接在真实动画中的结果。

详细数值在 [summary.json](summary.json)、两个角色目录的 `surface-truth.json` / `attenuation.json` / `CurrentInstance`、测试 JSON 与 Cook 日志中。清理仅针对本轮 3 个临时候选输出，先备份、检查外部引用，再用 Editor 删除；原有 3 个 committed runtime root 和两个新正式输出保留。
