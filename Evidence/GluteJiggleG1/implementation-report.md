# G1 实现与工程验证报告

> 本报告记录首次 G1 交付。后续发现并修复其继承的 G0 空间标定缺陷；本报告中的旧人物路径已被新的修复版替代。请使用 `../GluteJiggleG1Fix/implementation-report.md` 中的最终人物。此前动力学测试通过不能证明节点解剖位置正确。

日期：2026-09-29。分支：`feature/jiggle/hip`。

## 1. HEAD 与交付状态

开始时已 fetch 并检查远端；最终本地 HEAD 仍为
`119c23b8aa7f9ca96673f1c2ca611a15304b320a`。
G1 源码、测试、文档与 Evidence 为未提交工作区修改，未推送。
插件 DLL 已安装到当前 SmartNPC，已备份旧文件并核对 SHA256，见 `install.json`。

## 2–4. 架构与 Breast 复用

新增 `UVamGluteJiggleProfile`、`VamGluteJiggleBuilder`、`FVamGluteSolver`、
Glute 组件运行时接入、G1 调试面板、当前表面比较及四组 G1 自动测试。

顺序：Primary Animation/IK/rigid blend → G0.5 current rest → G0.6.2 Morph →
G1 helper 平移 residual → Breast finalization → Native Skeletal Skinning。

只提取 Breast 原有 15 变量线性求解函数到 `VamSecondaryMath`；函数体、操作顺序完全一致。
同时复用现有平滑 Actor 运动输入。没有复制 Breast modal/angular solver，没有改其标定。
源码等价证据见 `breast-extraction.json`；数值回归见每个人物 `baseline-equivalence.json`。

## 5–7. 双支承、current rest 与张力

每侧为五个世界空间动态粒子；Anchor 为运动学支承。
每个节点读取 G0.5 当前 `PelvisAttachment`、`ThighAttachment`、rest 和区域 support。

这是围绕当前承载平衡形态的切线模型：

```
eP = (x-p) - (r-p)
eT = (x-t) - (r-t)
F = -K(wP eP + wT eT) - CP(v-vP) - CT(v-vT) + coupling + gravity/preload
```

`p/t` 来自真实 pelvis/femur 支承几何；`vP/vT` 分别来自两个最终 primary frame
在粒子位置处的刚体速度场。两个弹性 rest vector 经 G0.5 平衡后，误差都等于 `x-r`；
两个运动阻尼支承仍然独立。股骨运动还会经 G0.5 改变 r。
因此，双支承是“当前结构平衡 + 两个真实运动支承”的模型，并非独立模拟两束解剖肌肉。

G0.5 的各区域 PassivePoseTension 已进入不同的 AP/ML/SI support，继而影响动态刚度与
自动阻尼系数。张力不改变质量，不改变 G0.6.2 target weights，也不暗中缩放 Mobility。

## 8–10. 自动标定与自由度

- 有效质量 = G0 effective volume × 内部 density baseline；区域质量取 G0 几何质量分数。
- Kauto = 当前区域 support × Profile 的 DynamicModulusFraction（默认 .08）。
- 每个支承的 Cauto = `2 ζ sqrt(mAuto Kauto attachment)`，ζ 默认 .32；两者相加。
- travel 来自尺寸、posterior depth、原始支承比例和 semantic tether；AP/SI 正负不同。
- 65% travel 后连续增硬，另有最终防灾边界，记录触及次数；它不是碰撞。
- Core 连接其余四区；Upper/Lower 分别连接 Medial/Lateral，共八条不同权重的边。
  权重取区域间距、支承差异和几何平均 support。

没有 whole-glute angular mode。保留 G0.5 orientation/scale，只添加五节点 translation。
Inertia candidate 存入 Profile 供后续使用，当前无角自由度消费它。
Anchor 和 helper 沿用原有 G0 骨骼；G1 不新增骨、不改权重。
两个人物均保持 source 88 / final 112 bones，已有 12 Breast + 12 Glute helpers。

## 11–12. 积分、惯性、Shape、Teleport

120 Hz backward Euler，15 变量耦合隐式 spring/damper solve，非线性刚度每步评估。
平移用 Hermite 插值，旋转用 slerp，G0.5 rest 在子步之间插值。
世界粒子天然包含转到 pelvis 系后的线加速度、Euler、离心和 Coriolis 效应，未重复施力。
恒定速度不会产生持续后拖。非线性 travel 使用同一时刻的预测位置，已修复平移速度导致
假增硬的问题；300 cm/s 匀速叠加误差约 2e-12 cm。

正常 pose/rest 连续变化保留世界位置、世界速度。
小幅 Shape 编辑保持 residual 并保留世界速度，下一帧初始化历史也不覆盖速度；大幅体积
变化受控 reset。Preview 不作为运动输入。TeleportRevision、过大姿态跳变和超子步预算的
长帧 reset 并清除无效历史。暂停不推进粒子，恢复先重建历史。
共享 Profile 不存任何可变状态，双实例及 spawn/destroy 已测。

重力作为 `m*g` 加入，同时用结构 preload `-m*g` 平衡：所有固定姿态都以 G0.5 为承载 rest，
确保 ON settled 与 OFF 等价。G1 不另加姿态相关静态下垂；跳起/落地仍由真实 pelvis 加速度
产生动态残差。此项是模型约定，不是解剖重力标定。

## 13. 开发者参数

默认全部 1：Support 只缩放 attachment K；Damping 只缩放 C；Mobility 只缩放 travel；
Internal Coupling 只缩放内部边刚度。Advanced 默认折叠，Mass Scale 只改有效质量。
保持系数正交意味着 Support/Mass 改变后实际网络阻尼比可以改变，系统不会偷偷重算 C。
没有 Density、Softness、Bounce、JiggleStrength 或 RotationStrength 控件。

## 14–15. 自动生成、升级与实际资产

正常“生成 UE5 人物资产”自动在 G0 构建后增加 G1 标定、创建 DataAsset、连接 RC/BP、独立
进程重载与发布验证。旧人物使用正式 Upgrade Runtime，生成新 committed output。

| 人物 | 流程 | Content Browser 根路径 |
|---|---|---|
| Primary | 普通生成 | `/Game/VamRuntime/R_acd9111a769d555e99335c99` |
| Secondary | 从 G0.6.2 BP 正式升级 | `/Game/VamRuntime/R_c504130ea06ee34f59aef652` |

每个根路径内：

- `BP_VamCharacter`
- `RC_Runtime`
- `DA_GluteJiggle`
- 原有 `DA_GluteStructure`、`DA_GluteCorrective` 等资产。

Primary Profile：`/Game/VamRuntime/R_acd9111a769d555e99335c99/DA_GluteJiggle`。
Secondary Profile：`/Game/VamRuntime/R_c504130ea06ee34f59aef652/DA_GluteJiggle`。
没有覆盖源人物或删除旧 committed output。早期验证输出
`R_60bffb94d9bb6e21b347ba15`、`R_b33a518a8b4404e2667c4fd1` 也保留；体验请选表中最终路径。

## 16. 两个人物数据

体积为 effective proxy，非医学体积。尺寸顺序 AP/ML/SI，单位 cm。

| 人物/侧 | volume cm³ | mass kg | dimensions cm | thigh swing Lower/Lateral peak cm |
|---|---:|---:|---|---|
| Primary L | 190.883846 | .200428 | 15.530 / 20.343 / 12.276 | .207433 / .201667 |
| Primary R | 224.320530 | .235537 | 15.036 / 18.502 / 10.707 | .197533 / .192756 |
| Secondary L | 198.088757 | .207993 | 16.134 / 19.952 / 11.796 | .248308 / .791000 |
| Secondary R | 196.491925 | .206317 | 15.899 / 19.761 / 11.476 | .235188 / .779557 |

两套不同来源人物，加上既有测试中的 ≥15% 有效体积 Shape 变体；同算法、无人物 ID 特例。
固定 pelvis 的股骨摆动，Lower/Lateral 均大于对应 Upper/Medial。

深屈髋 + 200 cm/s² Actor 加速，当前 Morph 后表面：

| 人物/侧 | P50 cm | P90 cm | P95 cm | Max cm |
|---|---:|---:|---:|---:|
| Primary L | .036190 | .086019 | .089845 | .108548 |
| Primary R | .037175 | .093810 | .103796 | .119725 |
| Secondary L | .056624 | .130709 | .158741 | .196699 |
| Secondary R | .056677 | .125702 | .152078 | .188107 |

该运行的 helper 峰值：Primary .224798 cm，Secondary .305294 cm。
Neutral/Flex60/Flex90/Extension/Abduction/FlexExternal 六种姿态 settled ON/OFF
表面最大误差分别不超过 2.91e-13 / 1.63e-14 cm。
每区及 silhouette proxy 完整数据在 `Primary/g1-surface.json`、`Secondary/g1-surface.json`。

## 17. FPS 与稳定性

同一通用平移、旋转、股骨输入轨迹：

| FPS | peak cm | spectral phase s | settling s |
|---:|---:|---:|---:|
| 30 | .702158 | .574680 | 1.066667 |
| 60 | .702383 | .574683 | 1.066667 |
| 120 | .702440 | .574689 | 1.075000 |

整条轨迹最大差：30/120 .003870354 cm；60/120 .000753921 cm。
测试还覆盖正/负线加速度、急停、三轴持续旋转、角加速度、股骨单独摆动、周期输入、
完整 jump/landing、两端极限参数各 100 秒、衰减、pause、Teleport、Shape 和多实例。
通用 jump peak 1.369695 cm，landing peak 1.367423 cm，随后衰减至测试精度零。

## 18–19. 回归、编译与 Cook

- 每个人物 17 项 UE 测试：15 项干净通过，2 项带既有警告通过，0 失败、0 未运行。
- 两条既有警告来自 Breast/Glute NativeRuntime 的世界清理缺少 EndPlay；G1 新测试无此警告。
- 52 项 Python 测试通过。
- 每个人物 288 组 G0.6.2 surface rows 与先前 Evidence 完全相等。
- Breast Calibration / MovingFrameModal / SmoothJump 的全部已记录数值字符串完全相等；
  NativeRuntime、Shape/Morph、weight compression 等现有测试也通过。
- Editor、Game Development、Game Shipping 插件模块编译通过；最终两个测试进程使用主项目
  已安装 DLL。Game 编译不等同于已经人工运行 packaged executable。
- Windows Cook：838 总包，831 cooked，7 platform skip，0 error / 0 warning。
- 两个普通 BP 的 blank-map Cook fixture 无专用求解 Actor 或 Level Blueprint 初始化。
- 所有构建算法指纹与最终源码一致；见 `summary.json` 和人物 runtime receipt。

## 20. 成本

在 i5-12600KF、Development 测试进程中，五节点一侧的 Advance 平均约 1.6–1.9 µs。
两侧同一 120 Hz 步约 3.3–3.6 µs；60 FPS 两步约 6.6–7.2 µs 为该测量的算术估算。
它不含 G0.5 求值、动画、渲染、蒙皮和调试绘制；没有据此声称多人场景完整帧耗时。
运行时仅十个粒子，不每帧 CPU skin 全身、不每顶点模拟、不创建临时 UObject。
完整 LOD0 CPU 重建仅在主动表面快照/工程测试时运行。

## 21. 限制

- 小维度预承载切线模型，不是真实内部肌肉、体积软体或独立解剖束模拟。
- 隐式积分具有数值耗散；限制区、防灾边界及超长帧 reset 是稳定性策略。
- 不产生额外静态重力 sag，不模拟主动肌肉激活。
- Actor/component 非单位或非均匀 scale 未验证，人物比例请使用 Shape。
- 表面证据是当前 native LOD0/Shape/Morph/pose 的 CPU 重建，排除 GPU readback、WPO、cloth。
- 未实现 sitting/contact/press/collision/pressure、G2 Chaos、新的 fold 或 corrective。
- 未做视觉自然度判断，也未进行目标平台多人完整场景性能验收。

## 22. 在普通 Empty Level 体验

1. 重新打开 SmartNPC，新建 Empty Level；从上面的最终根目录拖入 `BP_VamCharacter`。
2. Play 或 Simulate，选择运行中的人物，打开“VaM 人物调试”，点击“使用选中人物 / 刷新”。
3. 衣服遮挡时使用已有“显示衣服 / 配饰”按钮切换隐藏；这不删除服装资产。
4. 展开 **Glute Jiggle - G1**，Enabled 开启，四个参数先保持 1。
5. 点击 **Walk Cycle / Alternating Thigh Swing** 观察股骨交替运动输入；Reset 停止并重置。
6. **Smooth Forward Accelerate → Smooth Stop** 观察启动/减速；**Lateral Accelerate** 观察侧移。
7. **Smooth Turn / Continuous Turn → Smooth Turn Stop** 观察转动与停止。
8. **Jump** 播放完整起跳、飞行、落地输入。
9. 固定深屈髋：先 Reset，在原 **Glute Structural Debug** 点击 **Flexion 90**，再回 G1
   点击加速、转动或 Jump。Walk 会持续写股骨姿态，测试固定姿态时不要同时开启它。
10. 对比动态开关用 G1 的 **Enabled**；G0 Structural 和 G0.6 Corrective 保持原状态。
    固定姿态待衰减后，G1 ON/OFF 应回到相同结构表面。

可视化：Dynamic Nodes=动态点；Pelvis/Thigh Attachment=两类连接；Velocity=相对速度；
Rest vs Dynamic=rest 点及动态偏移。Diagnostics 显示质量、rest、位移、速度、frame
线/角速度加速度、thigh velocity、张力、K/C/travel、coupling 倍率和 solver steps/cost。
Advanced 默认折叠，仅有 Mass Scale。

“对比 G1 OFF / 当前动态表面”保持当前 pose/Morph，只去掉 G1 residual 做对照，青色 OFF、
品红当前，绘制 15 秒并保存 `Saved/VamDiagnostics/GluteSurface_*.json`。
Reset 会停止调试运动、恢复 pose debug 并清除 dynamics，不会重置四个参数。

以上为工程事实，最终视觉效果由用户人工判断。G1 到此为止，没有继续 G2。
