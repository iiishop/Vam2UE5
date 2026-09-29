# G0.6 实施报告

2026-09-28。工程实现、编译、安装、两个人物验证及 Cook 已完成；未进行视觉验收。

## 1. 分支与 HEAD

分支 `feature/jiggle/hip`，HEAD `74d7dcd1e974a20ff4685652c381cc192625ad9f`。G0.6 是该 HEAD 上的未提交修改，没有自动 commit/push。两个最终 output 的 80 项算法文件哈希均与当前工作区匹配，family policy identity 相同。详见 `summary.json`。

## 2. 实际来源调查

只读检查持久化 Source IR、VaM a_per/f_mb 资源以及驱动组件代码，发现独立 `SetDAZMorphFromAverageBoneAngle` 将左右 thigh X 角平均值映射到 `!Bend Fix`；原区间 0 至 -100 度映射 0 至 1 并 clamp。目标 bank 中有 266 个真实 vertex delta。Morph 自身 formulas 为空，不等于没有 pose driver。

另发现 Hip Bend Smoother（206 delta）、Glute Bend Helper（453 delta）、由 LGlute/RGlute 驱动的 LG/RG UpDown2、Side2Side，以及 TriAx/局部 bulge 来源记录。来源证明摘要见 `source-recovery-summary.json`；证据目录没有复制原始 delta bank 或反编译源码。

## 3. 复用与不可靠来源

仅在 bundle digest、对象引用、轴、区间及 target 都验证成功时恢复 `!Bend Fix`。将其局部化并适配为左右独立 pose targets，不声称精确重现 VaM 的双腿平均驱动。

Primary 有 245 个 region-supported vertex-side 候选参与复用。Secondary 的当前 bundle 未通过旧 Source IR 哈希校验，明确采用程序几何 fallback，复用数为 0。它的 provenance 前缀记录失败原因；尾部是通用构建流程描述，不代表实际复用了来源 delta。

没有可靠主骨驱动链的其它 bend helpers 不自动启用。排除基于可变 Glute 骨输入的 secondary drivers；不复用无关 genital 驱动。现有 TriAx→LBS 是单关节参考拟合，不能代表 VaM 复合姿态实时算法，也没有在 G0.6 重复叠加。

## 4. 表示与新增模块

G0.6 使用 Native Corrective Morph。新增 `UVamGluteCorrectiveProfile`、Editor corrective builder、运行时 pose blending、来源恢复脚本和工程测试。继续使用 G0.5 structural helpers；本阶段不加骨、不再改 SkinWeight。

两个人物仍是 88 个原骨 + 12 Breast helpers + 12 Glute helpers，共 112 个。每个姿态 target 的 residual 拆成三个局部轴 Morph basis，便于 Shape 各向异性缩放；零 basis 不保存。Primary 54 个，Secondary 48 个。

## 5. Pose targets

每侧采用相同 family 配置；角度顺序为 flexion、abduction、external rotation。

| Target | 角度（度） |
|---|---|
| Neutral | 0, 0, 0 |
| Flex30 / Flex60 / Flex90 | 30/60/90, 0, 0 |
| Extension | -20, 0, 0 |
| Abduction / Adduction | 0, 35/-20, 0 |
| External / Internal | 0, 0, 30/-25 |
| FlexAbduction | 60, 25, 0 |
| FlexExternal | 60, 0, 25 |

这些是工程采样角度，不是医学活动范围定义。

## 6. Pose-space blending

输入是现有 HipPoseState 的 primary pelvis↔femur 姿态。以 (60,35,30) 度缩放距离，计算 `D=|scaled(p-target)|²`。非 target 点使用 `exp(max(-80,-D/(2w²)))/D²`，再归一化；w=1.2。精确 target 返回 one-hot。

权重非负、和为 1；Neutral target 没有 Morph，所以 neutral corrective 为零。不依赖 DeltaTime、速度或历史。每实例持有自己的权重，通过公开 `FAnimationRuntime::AppendActiveMorphTargets` 同帧发布到 Native Morph buffers，不重建渲染资源。

## 7. Flexion 影响区域

现有连续 Glute region 限制影响范围。程序 residual 使用 Lower 曲率函数、Core/outer 支承和 lateral fade，叠加已验证的来源局部 delta；Medial tether 衰减保护内侧，Upper 继续以 G0.5 支承为主。结果不是整个臀部平移或自由旋转。

最终位移受人物尺寸、region mask、局部几何安全权重和连续 tanh 幅度限制。保存局部/区域统计以供诊断，不把这些统计声明为视觉质量。

## 8. Under-curve

使用 G0.5 medial anchor / middle transition / lateral fade 语义生成低频曲率 residual，Lower 横截面采用平滑正负曲率瓣，随 flexion、extension、旋转和组合 target 改变。四次拓扑平滑并处理 source vertex 的 UV seam aliases。没有高频 wrinkle、接触或褶皱模拟。

## 9. Shape adaptation

构建时按来源区域与人物 p0 的加权几何尺寸适配 source delta。Runtime 用当前 GluteRest AP/ML/SI dimensions 与构建 dimensions 比值分别缩放三个轴 basis，比值限定在 0.25–4。Shape Preview/Commit 保持 helper、Morph identity 和共享 Profile 不变。

这是尺寸驱动的近似适配，不是每次 Shape 改变后重新求解完整表面。两个来源人物及变体都验证；effective volume proxy 分别由 190.883846→300.346523（1.573452 倍）、198.088757→429.423848（2.167836 倍）。这些不是医学体积。

## 10. G0.5/G0.6 配合

G0.5 负责骨架支承。构建 G0.6 时先评价目标 G0.5 helper pose，再用混合 skin matrix 的逆把所需 posed residual 转回 pre-skin Morph delta；不复制整套 helper motion。两层都从 primary pose 取输入，G0.6 不读取自己的输出作驱动。

两层可独立开关进行 A/B。G0.6 residual 的标定基于 G0.5，因此关闭 G0.5 是诊断模式。未来 FinalRestGlute 应消费 G0.5 transforms + G0.6 Morph bases/weights；本阶段没有把最终 Morph 表面重新积分为 COM/inertia。

## 11. 自动生成与升级

原“生成 UE5 人物资产”自动增加来源验证、pose residual 生成、Native Morph/normal delta、`DA_GluteCorrective`、RuntimeConfiguration 引用、持久化 identity 和独立进程 reload 检查。旧人物通过正式 Upgrade Runtime 生成新的 committed output，不原地覆盖源资产。

用户不需要 Build Corrective 或手工挂接。普通 BP 自动加载，工程观察地图仅用于证据，不是运行依赖。

## 12. 最终资产路径

Primary：

```text
/Game/VamRuntime/R_975de5dff96a42c0243c5eb1/BP_VamCharacter
/Game/VamRuntime/R_975de5dff96a42c0243c5eb1/DA_GluteCorrective
/Game/VamRuntime/R_975de5dff96a42c0243c5eb1/DA_GluteStructure
/Game/VamRuntime/R_975de5dff96a42c0243c5eb1/RC_Runtime
/Game/VamRuntime/R_975de5dff96a42c0243c5eb1/Glute/SK_Body
```

Secondary：

```text
/Game/VamRuntime/R_cc1780198614f8100223e560/BP_VamCharacter
/Game/VamRuntime/R_cc1780198614f8100223e560/DA_GluteCorrective
/Game/VamRuntime/R_cc1780198614f8100223e560/DA_GluteStructure
/Game/VamRuntime/R_cc1780198614f8100223e560/RC_Runtime
/Game/VamRuntime/R_cc1780198614f8100223e560/Glute/SK_Body
```

可选观察地图：`/Game/GluteStructureEngineering/Empty_975de5dff96a`、`Empty_cc1780198614`。此前迭代生成的旧 output 保留；观察时使用本节最新 BP。

## 13. Debug Panel

VaM 人物面板选择实例，展开 **Glute Structural Debug - G0.5 / G0.6**。

- `Enabled`：G0.5 structural layer。
- `G0.6 Corrective Enabled`：本阶段修形，可同帧关闭清零。
- `Show Corrective Delta`：稀疏局部 residual 参考向量，不是完整网格的实时测量。
- Region/Bones/Attachments/Pose Tension/Fold：原结构诊断。
- Diagnostics：真实 primary hip 角、active targets/权重、来源、总位移上界、分区域 RMS 上界。

Fold 行仍是 G0.5 semantic state；G0.6 under-curve 随已保存 target 混合。数值标为 bound，不冒充最终渲染顶点的精确位移。

## 14. 测试、编译、安装和 Cook

| 项目 | 结果 |
|---|---|
| Editor Development | 成功 |
| Game Development / Shipping | 均成功 |
| Python | 27/27 成功 |
| Primary UE automation | 11/11 成功，在已安装插件的 SmartNPC 中复测 |
| Secondary UE automation | 11/11 成功 |
| 旧 G0.5 NativeRuntime | 1/1 成功 |
| 独立进程 save/reload | 两个 output 均成功 |
| Cook | exit 0，836 包，0 errors / 0 warnings |
| DLL 安装 | 完成，5 个文件 SHA256 一致，旧文件已备份 |

每组 11 项含 7 个 Breast 回归测试和 4 个 Glute 测试。两组各有两个测试带既有 World Cleanup/EndPlay teardown 警告，不是零警告测试。详情见 Primary/Secondary `tests.json`。

覆盖 neutral、cardinal targets、1200-step 连续组合 pose、镜像、30/60/120 FPS 权重与原生 buffer 等价、同帧开关、多实例、Shape、原 Morph delta、原骨索引与 helpers、权重和组合 corrective 的 posed triangle 几何界限。

实际修复过一项失败：组合 flexion/adduction/internal pose 的局部三角形面积比分别达到 5.068581、6.486409，超过原 5 倍阈值。构建期加入 36 个姿态 conditioning probes 与拓扑连续安全衰减；保持测试阈值不变，最终复测通过。这 36 个 probe 不是新增 runtime targets。

最大 bind delta：Primary 1.327287 cm，Secondary 0.223722 cm；不能把它们当成某姿态屏幕上的总位移。

## 15. 当前限制

只支持正式 VamFemale88 family；其它 family 需要 mapping。有限 pose basis 和几何保护不证明所有极端 Shape/pose 都安全。程序 fallback 修形幅度可能明显低于可复用 source delta 的人物；受保护的局部区域也会减弱。

没有精确体积守恒、完整肌肉力学、主动收缩、最终 COM/inertia 重积分、衣物 corrective、高频褶皱、Glute Jiggle、Chaos 或 Contact。本轮未做视觉验收，也未继续 G1。

## 16. 普通 Empty Level 人工观察

1. 重开 UE，创建 Empty Level，拖入本报告最新 BP；启动 Play/Simulate，面板选择此人物。
2. 用已有衣物可见性控制隐藏衣物，关闭蓝色 region 点和其它遮挡轮廓的 debug 标记，固定侧后方相机。
3. 保持 G0.5 `Enabled` 开启，只切换 `G0.6 Corrective Enabled`，隔离本阶段差异。
4. Target 先选 Left 或 Right。依次 Neutral、Flexion 30、60、90、Hip extension、Abduction、External/Internal rotation；再看 Both。按钮请求经过原有 Joint Constraints，实际角度以 diagnostics 为准。
5. Neutral 的 G0.6 应为零。其它姿态重点比较下臀到大腿的曲率、内侧固定、外侧过渡和局部轮廓，不用整个臀部移动幅度衡量。
6. 如需三组对比：两层均关 → 仅 G0.5 → G0.5+G0.6；保持相同 Shape、相同动画/pose 和相机。结束按 Reset。

Pose Debug 不提供完整平衡弯腰动画；已有动画仍参与 primary pose。最终外观由用户人工判断。
