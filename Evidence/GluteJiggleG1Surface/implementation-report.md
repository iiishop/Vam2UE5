# Glute 表面传递与阻尼优化

2026-09-29，`feature/jiggle/hip`。
HEAD：`119c23b8aa7f9ca96673f1c2ca611a15304b320a`，工作区改动未自动提交或推送。

## 与 Breast 对比后确认的问题

1. **动态骨的位移大部分没有传到表面。** 对正式 native mesh 的所有 helper 做
   同方向 1 cm 平移，按三角形面积 × region 统计：旧 Primary Glute 平均传递
   14.66% / 15.36%，同人物 Breast 为 56.60% / 56.62%。旧 Glute 主体区域约20%，
   Breast 约65%。原 G0 将 region 证据置信度直接当作组织参与比例，再乘0.7和原
   donor 权重；无 LGlute/RGlute 原权重的有效来源尤其容易被连续衰减。
2. **双支承阻尼重复按完整节点质量标定。** 旧 CP、CT 相加后的有效阻尼比为
   `ζ(√p+√t)`，接近均分支承时是设定值的1.414倍。两条连接都必要，但总阻尼
   应先确定，再分给两路。
3. **Breast 的运动模式不同。** Breast 有 COM 平移、整体角运动、区域 residual，
   蒙皮也有 Core bias。Glute 保持 pelvis/femur 双支承和五区平移网络，没有自由
   whole-glute 角模式。本次没有将 Breast 的悬垂体运动模型复制给臀部。

## 实现

### 蒙皮参与映射

G0 algorithm：`glute-structure-g05-surface-v3`。

```text
participation = smoothstep(clamp(regionConfidence / fullConfidence, 0, 1))
transfer = maximumDonorFraction × participation × posteriorRootFade
```

VamFemale88 family 的 `maximumDonorFraction=0.7`、`fullConfidence=0.45` 保存在
`Config/RigFamilies/VamFemale88.json`，同步保存到 Profile 和 SkinWeightIdentity。
同一 family 的所有人物使用相同函数；无人物 ID、名称或世界坐标例外。

region mask、面积统计、helper 位置/层级、质量、体积、COM、attachment、Shape
响应和结构 pose 函数不变。只将有效区域置信度映射到部分蒙皮参与：主体获得更多
连续 helper 权重，边缘使用零导数淡出，无关的非 donor 骨骼权重不受影响。
每个有效 donor 至少保留30%原支承；仍使用现有八影响压缩与 normalization。

试过80% donor 上限，但 Primary Flex90 的来源 Corrective RMS 保真率降至79.54%，
未达到原有80%下限，因此未交付该候选版，也未修改这个测试门槛。
最终70%版本会重新构建派生 Corrective 数据，不能宣称所有旧姿态表面逐点不变。

### 阻尼

G1 algorithm：`glute-dual-attachment-g1-v2`。

```text
Cauto = 2 × ζ × sqrt(mAuto × Kauto)
CP = Cauto × pelvisAttachment
CT = Cauto × thighAttachment
```

两路继续使用各自真实移动参考 frame 的目标速度。参考标定下总 attachment 阻尼比
为0.32，不再随分配比例额外增大。质量、Support、Damping、Mobility 仍是独立参数；
调质量或刚度不会自动改写 C。没有提高默认质量、降低默认刚度或放大 solver 输出。
旧资产通过默认 false 的序列化标志保留旧行为，新 Builder 显式启用合成阻尼标定。

### 检测

- 新增 `VamGluteStructureBuilder::SurfaceTransferAudit`：实际 native skin weights
  对1 cm同向 helper 平移的面积加权响应，包括均值、主体均值、P10/P50/P90/最大值。
- 空间回归加入实际表面参与、边缘淡出和 donor 上限检查。
- 新增 `Vam.Glute.G1.AttachmentDamping`，检查多种支承比例下合成阻尼比及旧资产兼容。
- NativeSurface 使用调试面板实际 Walk、Jump、Smooth Turn 命令；4秒动作、60Hz
  驱动、每0.2秒采样真实 native 表面。保存 Max、区域 RMS、P95，而不只检查调试点。
- 固定姿态 G1 ON/OFF、Morph、Shape、双实例、Teleport、暂停和30/60/120Hz仍回归。
- 最低阻尼/最高质量极限测试仍执行100秒受迫运动；衰减观察窗改为20个由实际
  `C/(2m)` 计算的包络时间常数，避免将100秒误当作所有阻尼设定的同一衰减标准。

## 最终资产

| 人物 | 新 runtime 根目录 |
|---|---|
| Primary | `/Game/VamRuntime/R_5e73d4eb197a244cad0bb11a` |
| Secondary | `/Game/VamRuntime/R_282b5c70c1b647e7d8abe2b9` |

目录中使用 `BP_VamCharacter`，数据为 `DA_GluteStructure`、`DA_GluteJiggle`、
`DA_GluteCorrective`、`RC_Runtime`。原人物和旧 committed 输出保留。
正式生成/升级流程自动执行此映射，用户不需要单独创建 Glute 资产。

## 实测结果

相同输入、同样采样时刻，以下是实际 native 表面位移，单位 **mm**。
RMS 列是所有采样帧中区域 RMS 的峰值，不是时间 RMS；Max 是采样表面点的峰值。

| 人物 / 动作 | 旧 Max | 新 Max | 旧区域 RMS | 新区域 RMS |
|---|---:|---:|---:|---:|
| Primary 步行 | 2.565 | 6.413 | 1.233 | 3.053 |
| Primary 跳跃 | 3.691 | 8.648 | 1.983 | 4.740 |
| Primary 转身 | 1.760 | 3.991 | 0.778 | 1.948 |
| Secondary 步行 | 3.474 | 8.159 | 1.584 | 3.802 |
| Secondary 跳跃 | 5.283 | 9.966 | 2.319 | 4.946 |
| Secondary 转身 | 1.981 | 3.601 | 0.864 | 1.963 |

Primary 平均单位平移传递率由14.66%/15.36%变为32.58%/35.26%，主体区域由约20%
变为48.21%/49.63%；同人物 Breast 保持56.60%/56.62%，其实际 skin weights 的
单位响应没有变化。Primary 步行的节点峰值从2.741 cm变为2.702 cm，但表面峰值
增大到2.50倍，说明改善来自传递到网格的运动，而不是调试点被人为放大。

新旧 G0 的 `sides` 完整几何/attachments/pose-response 数据及全部 helper 绑定
信息逐项相同。原骨骼索引、零偏移 bind、Morph、Shape identity、空间定位测试通过。
最终 Primary Flex90 来源 Corrective RMS 保真率左80.18%、右88.05%，保留原80%
下限和三角面安全检查；这些值提示结构蒙皮的进一步增强仍须受 Corrective 约束。

两个人物在主项目安装版插件下分别 **19项通过，0失败**，其中各两项既有
NativeRuntime 清理警告（WorldCleanup 缺少 EndPlay）。未隐藏这些警告。
Editor Development、Game Development、Game Shipping 编译成功；Game Development
首次遇到 MSVC 内部编译器错误，限制并行数为2后重试成功，没有为此修改源代码。
Python 52项通过。Cook结果、二进制SHA256、原始测试及表面采样详见本目录证据。
Windows Cook：831包成功，7包按平台规则跳过，总计838包，0错误/0警告。
两人物各7个 Breast 测试的数值 Info 与旧版完全一致；Breast 实际 skin weights 的
单位平移响应也逐项一致。两人物区域 RMS 在步行/跳跃/转身中提高约2.1–2.5倍。
插件两个DLL、两个PDB及modules文件安装后SHA256与编译输出一致，旧文件有备份。

## 观察方式

1. 使用上面的新 BP 进入 Play / Simulate，选择运行中的人物并刷新调试面板。
2. Glute Structure / G0.6 Corrective 保持开启；G1 Enabled 开启，参数先全部为1。
3. 先关闭全部 Show，必要时隐藏衣服，观察 **Walk Cycle / Alternating Thigh Swing**。
   Reset 停止步行，再分别观察 **Jump**、**Smooth Turn → Smooth Turn Stop**。
4. 在动作过程中切换 G1 Enabled 做对照；不要同时关闭结构层。
5. 需要量化时再打开“对比 G1 OFF / 当前动态表面”；青色是相同当前 pose/Morph
   下的无 G1 residual 表面，品红是当前动态表面。

## 限制

保持 bone-based 模型、部分支承和受限行程；没有接触、碰撞、按压、Chaos/G2或新的
fold deformation。增加 helper 参与也会影响姿态下的结构蒙皮，因此保留原有
Corrective 源保真和三角面安全测试，最终视觉轮廓仍需人工观察。
表面测量是按需 native LOD0 CPU 重建，不是 GPU 截帧，不包括材质 WPO 或 Cloth。
本报告不做视觉自然度或真实解剖验收。
