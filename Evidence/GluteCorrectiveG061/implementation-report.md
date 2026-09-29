# G0.6.1 实施报告

## 1. HEAD 与交付状态

分支 `feature/jiggle/hip`，HEAD `6c52542bfb2634d36e157510b4fa8c6319724016`。本轮修改留在工作区，未自动提交或推送。Editor/Game 编译、正式生成/升级、独立重载、测试、Cook、插件安装均已完成。保留 G0/G0.5、Native SkeletalMesh、Morph/GPU skinning、Breast V3。未实现 G1/G2，未做视觉验收。

## 2. 弱 corrective 的实际根因

先保持旧几何算法不变加入 audit，复现 Primary。发现四个叠加问题：来源 bank delta 未经过 Boundary graft 传递；统计包含未被真实表面引用的保留顶点；全局 worst-case Safety 抑制普通 target；region/medial gate 与每轮重复 Region×Safety 平滑继续衰减。来源 bank 中的重要位移并不等于最终可见表面的位移。

## 3. Raw !Bend Fix

266 个原始非零 source delta，单位 cm：

| P50 | P90 | P95 | Max | RMS |
|---:|---:|---:|---:|---:|
| 0.417376 | 1.607933 | 1.769126 | 2.074286 | 0.886164 |

完整 P75/P99/count、各 target 与八个区域的阶段数据见 `legacy-attenuation.json` 和 Primary/Secondary `attenuation.json`。八个区域为 whole、Core、Upper、Lower、Medial、Lateral、under_curve、silhouette_proxy；最后一项是与相机无关的后方/外侧轮廓相关区域，不是像素轮廓误差。

## 4. 旧版 attenuation ratio

以下为 Flex90、同一真实表面区域、按 source ID 去除 UV 重复后的来源分支 RMS。Raw 已使用真实 driver 0.9；不将不同顶点总体的统计直接相除。

| 侧 | Raw×0.9 | Shape 后 | Safety 后 | Gate 后 | 平滑/最终 | 最终÷Raw | 最终÷Shape 后 |
|---|---:|---:|---:|---:|---:|---:|---:|
| L | 0.088773 | 0.081072 | 0.014290 | 0.001637 | 0.000089 | 0.100% | 0.109% |
| R | 0.093535 | 0.087020 | 0.017599 | 0.005456 | 0.000226 | 0.242% | 0.260% |

旧 source+procedural 总场表面 P95：左 0.022899 cm、右 0.058188 cm。

## 5. 最大损失位置

相对损失最大的是重复幅度 mask 的平滑阶段，约再损失 95%–96% 来源 RMS；global Safety 是最早的大幅衰减。新流程先正确映射 topology，再分离两条支路，不通过运行时强度乘数补偿。Boundary 算子重放将 266 条 bank delta 转为 1,664 条 merged-domain 非零 delta；这不是 1,664 个最终渲染顶点的声明。

## 6. Target-aware safety

每个 target 对其实际 source+procedural 场计算局部 Safety：检测法向翻转、面积、局部边差和 skin determinant，用二分求安全幅度。组合姿态失败只修正该局部顶点上有实质位移贡献的 targets。筛选依据实际位移贡献，而非仅 blend weight，以免漏掉局部修正后剩余的低权重贡献。没有全局 worst-case mask。

单侧、双侧检查现有 targets、组合及极端 probe；构建按 NativeBuilder 的 16 位骨权重处理，并留 5% 法向/面积余量。原 5 倍面积、3 倍局部边差、0.01 determinant 条件未放宽。最终 Primary/Secondary 分别在 5/4 轮组合安全迭代后收敛。

## 7. Smoothing

Verified source 跳过程序平滑。Procedural 先形成 amplitude field，四轮拓扑扩散仅混合邻域；结束施加一次边界，再通过受边界约束的矩投影恢复加权向量均值和 RMS，无可行解时保留原场。UV aliases 统一处理。测试要求前后均值/RMS 差 <1e-6。

## 8. Medial/lateral gate

来源分支取消 generic medial fade、程序 tanh 和重复 region 幅度乘法。左右归一化支承只负责分区，共同区域边界连续衰减；保留 medial/under-curve 的局部曲率。G0.5 的 sacral tether、helper 权重、ThighFollow、ProjectionRetention 和 structural offsets 未放大。

## 9. Verified source 保留率

Flex90 来源分支相对映射/Shape 适配后的 source posed reference：

| 侧 | RMS 保留 | P95 保留 |
|---|---:|---:|
| L | 92.67% | 97.04% |
| R | 86.83% | 91.49% |

剩余衰减来自必要的局部几何安全修正。没有 Runtime CorrectiveStrength；target interpolation 仍为 0…1，精确 target 为 1。已有 Shape 各轴尺寸适配保持独立语义。

## 10. 无来源人物的 fallback

`VamFemale88.json` 保存来源验证后的 family 区域 mean、axis RMS、分位数及 source hashes，不包含 Primary 顶点 delta 或 Character ID。根据当前 dimensions、effective volume、support area、pelvis/thigh attachment 和区域几何构造连续场，迭代拟合区域轴向能量。Secondary 通过形状归一化 RMS 与 family reference 同量级检查。

这是区域统计标定，不能保证每个分位数或空间分布完全相同。Extension/abduction/rotation 缺少对应可信 Bend Fix，仍使用明确标记的程序估计。

## 11. 最终实际渲染表面统计

单侧 Flex90，G0.5+G0.6.1 与 G0.5 only 的差，单位 cm。直接重建最终 LOD；下表包含整个连续加权区域，不仅是非零点。

| 人物/侧 | P50 | P90 | P95 | Max | RMS |
|---|---:|---:|---:|---:|---:|
| Primary/L | 0.070443 | 0.376145 | 0.409524 | 0.580565 | 0.201679 |
| Primary/R | 0.081576 | 0.354824 | 0.407664 | 0.669996 | 0.200391 |
| Secondary/L | 0.298318 | 0.387004 | 0.423781 | 0.484931 | 0.298327 |
| Secondary/R | 0.286787 | 0.383999 | 0.409828 | 0.468678 | 0.287773 |

完整各区域结果见各人物 `surface-truth.json`，未使用 sum(weight×per-morph maximum) 作为主要强度指标。

## 12. Bilateral VaM equation reference 与 UE

双侧 30/60/90/100° 真实平均角 driver 分别为 0.3/0.6/0.9/1。来源 CPU reference 重放来源 thigh-X 权重、节点顺序和 bulge 方程；不包括 VaM post-skin smoothing、physics/contact，也不是捕获的 VaM renderer 顶点真值。独立单侧 target 是项目扩展。

双侧 Flex90 的 corrective 增量比较（RMS / P95，cm）：

| 侧 | 来源增量 | UE 增量 | 两增量差 |
|---|---:|---:|---:|
| L | 0.187333 / 0.405266 | 0.198402 / 0.408914 | 0.101180 / 0.289401 |
| R | 0.199138 / 0.413032 | 0.196816 / 0.424516 | 0.122479 / 0.344267 |

**完整表面仍未复现 VaM reference。** 下表必须与增量指标同时看（RMS / P95，cm）：

| 侧 | 原 Native LBS 基线误差 | G0.5 基线误差 | G0.5+G0.6.1 最终表面误差 |
|---|---:|---:|---:|
| L | 2.879688 / 4.913820 | 3.425677 / 6.200551 | 3.446939 / 6.113976 |
| R | 2.849217 / 4.807813 | 3.542566 / 6.408566 | 3.556429 / 6.394957 |

既有 Native LBS/G0.5 与来源 TriAx 基线的差占主要部分。恢复 Bend Fix 幅度不能消除这部分误差；本轮没有通过 corrective 抵消整个 scaffold 或改写蒙皮架构。under-curve 与 silhouette proxy 的对应残差同样保存在 bilateral 数据内。

## 13. 自动测试

- Python 29/29。来源 hash/Boundary mapping、离线 retained chain、平均角 driver、native source/recipe、TriAx、Shape kernel。
- Primary 12/12；Secondary 12/12；已安装插件的 SmartNPC Primary 复测 12/12；旧 G0.6 Runtime 1/1。每组完整回归含 7 个 Breast、5 个 Glute 测试。
- 新 SurfaceTruth：最终 LOD 的 Morph、vertices、triangles、section bone maps/weights；72 个 pose probe 分别检查左侧、右侧及双侧，双侧两区域独立输出统计。覆盖 neutral、完整混合场的有限性/翻转/面积/尖刺、seam、flexion 趋势、源保留、fallback 能量。
- retained seam 在 1e-6 cm 内一致；每个 render basis 仍按 NativeBuilder 每分量 1e-4 cm 裁剪界验证，再传播实测裁剪误差，避免把微小 UE omission 误判为真实接缝。
- 保留 Shape Preview/Commit、两实例、Morph、helper/原骨索引、30/60/120 deterministic、独立 save/reload。额外证据脚本验证 266 raw count、全部 bilateral driver、RMS/P95 保留率及记录数值有限。

测试并非零警告：存在既有 World Cleanup/EndPlay teardown 警告；Primary 及安装复测另捕获 UE 联网检测的 HTTP timeout。失败数均为 0。过程中修复/辨别的问题见 `fixes.json`，未放宽几何安全阈值。

## 14. Build / Cook / 安装

- Editor Development、Game Development、Game Shipping 均成功。
- 两个普通 BP Empty Level fixture Cook：exit 0，836 packages，0 errors / 0 warnings。地图只用于工程验证，不是 Runtime 初始化依赖。
- 已安装两个插件 DLL、PDB 与 modules，共 5 个文件，SHA256 与构建输出一致。旧 DLL 备份：`I:\Document\UE5\SmartNPC\Plugins\VamResourceBrowser\Saved\GluteG061\InstallBackup-20260928-201720`。
- Primary 通过正式 Upgrade Runtime；Secondary 通过正式生成链。均为新的 committed output，不覆盖来源资产。
- schema 2 由 Builder 显式保存；类默认值保留旧 schema 1，修复旧资产省略默认属性时的版本误读。新资产独立重载与旧资产 Runtime 均验证通过。

## 15. 实际资产和人工比较

**Primary**

- BP：`/Game/VamRuntime/R_6a3abbf3d5b9abd4960fe9fa/BP_VamCharacter.BP_VamCharacter`
- Profile：`/Game/VamRuntime/R_6a3abbf3d5b9abd4960fe9fa/DA_GluteCorrective.DA_GluteCorrective`
- RuntimeConfiguration：`/Game/VamRuntime/R_6a3abbf3d5b9abd4960fe9fa/RC_Runtime`

**Secondary**

- BP：`/Game/VamRuntime/R_4173d00aaf77658e405891f9/BP_VamCharacter.BP_VamCharacter`
- Profile：`/Game/VamRuntime/R_4173d00aaf77658e405891f9/DA_GluteCorrective.DA_GluteCorrective`
- RuntimeConfiguration：`/Game/VamRuntime/R_4173d00aaf77658e405891f9/RC_Runtime`

在普通 Empty Level 中拖入上述新 BP，启动 Play/Simulate，在 VaM 人物调试面板选择该实例。用已有衣物可见性控制隐藏衣物，关闭 region 点等遮挡，固定 Shape、动画、相机和 hip pose。

| 比较组 | Glute Structural Debug → Enabled | G0.6 Corrective Enabled |
|---|---|---|
| Base | 关 | 关 |
| G0.5 only | 开 | 关 |
| G0.5 + G0.6.1 | 开 | 开 |

使用 Target left/right/both，再选 Neutral、Flexion 30/60/90、Hip extension、Abduction、External/Internal rotation。以 diagnostics 的实际 hip 角和 active target weights 为准；已有 Joint Constraints 可能限制请求角度。结束按 Reset。

`Corrective Diagnostics` 的 Flex30/Flex60/Flex90 等按钮选择离线统计 target，不代替 Pose Debug；Raw Source / Source Adapted / Procedural / Final 切换稀疏向量。显示 source/procedural RMS、raw/adapted/final P95、source retention、Safety loss、smoothing loss、affected count。这些是构建 Shape 的 target 数据，不是当前任意混合姿态的实时全网格测量。

**当前 ThirdPerson 关卡仍引用旧 R_975… 人物，不会自动替换。请使用上述新 BP 进行本版比较。** 任意新生成或 Upgrade Runtime 输出都会自动走 G0.6.1，不需额外 Build/Attach 按钮。

## 16. 当前限制

仅支持现有 VamFemale88 semantic mapping 和验证过的单 Boundary graft source recovery。组合/极端 probe 是有限工程覆盖，不证明所有 Shape/pose 的连续表面都安全。fallback 是统计拟合；source reference 限定孤立 bilateral thigh-X 来源方程，完整 Native LBS/G0.5 基线差仍存在。silhouette 是区域 proxy，非图像轮廓对齐。

没有新增 Jiggle、spring、inertia、Chaos、contact、sitting compression、服装 corrective 或高频 wrinkle。本轮只修复/标定 pose corrective 几何与审计链，视觉效果交由用户判断。

## 附：Runtime 清理

引用审计后，将 10 个无外部引用的中间输出（4.34 GiB）移动到可恢复归档：`I:\Document\UE5\SmartNPC\Plugins\VamResourceBrowser\Saved\GluteG061\RuntimeArchive-20260928-203059`。保留最终两个输出及 ThirdPerson 仍引用的旧 R_975…；没有删除来源人物或替换关卡实例。

清理后检查最终依赖存在，180 个输出文件和 191 个来源文件 SHA256 未变，安装文件 SHA256 仍一致。详见 `cleanup-manifest.json`、`native-dependencies.json` 和 `post-cleanup-checks.json`。恢复时在 UE 关闭状态下按 manifest 将单个人物目录移回原 source 路径即可。
