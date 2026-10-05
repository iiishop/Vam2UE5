# G1.1 完成报告

> 后续变更说明：用户在本次验收后要求 Hip 幅度增强三倍。当前组件默认
> `GluteAmplitude=3.0`，放大最终 helper 的二次位移；Breast 不变。
> 本报告的测试与表面数值记录的是增强前的 G1.1，不是三倍幅度的新测试结果。

## 1. 基线与交付状态

- 分支：`feature/jiggle/hip`。
- 开始时重新获取远端；最新远端及当前 HEAD 均为 `6cfa063b687fa463775cd12e0135e113156bb90a`。
- 本次修改留在工作区，未 commit、未 push。
- Editor DLL 已安装到主项目，并保存旧 DLL 备份及 SHA-256 校验记录。
- 交付范围止于 G1.1；未实现 Thigh Jiggle、Chaos 或接触。

## 2–4. 重力问题及实际修复

旧系统逐步计算 `GravityPreload = -Mass * CurrentWorldGravity`，所以残余重力恒为零。

新系统每侧保存不可变 `ReferenceGravityLocal`，单位 cm/s²：

```
ReferenceGravityLocal = inverse(imported Anchor rotation) * (0,0,-980)
CurrentGravityLocal   = inverse(current Anchor world rotation) * WorldGravity
GravityResidualLocal  = CurrentGravityLocal - ReferenceGravityLocal
ReferencePreloadWorld = -current Anchor world rotation * ReferenceGravityLocal
```

构建参考 frame 为 `Side.AnchorLocal * G0Profile.RestPelvisComponent`，与节点 Rest 相同坐标轴。
第一人物实际参考约为 `(324.728584, -1.279124, -924.634907)`，没有假设局部重力必然沿 Z。
节点外力是自身质量乘残余加速度。保留原 world-particle integration；没有重复加入旋转参考系惯性项。
新增轻量 `VamSecondaryGravity` utility，未泛化重写 solver。

## 5–7. 默认基线、朝向和重力切换

默认 authored 朝向与 1g 下，当前局部重力等于参考值，因此没有第二次静态下垂。
整体 pitch/roll 改变当前局部重力，身体附着 preload 随之旋转，产生新的平衡。
竖直轴 yaw 不显著改变局部重力。

Gravity Zero/Half/Double 改变当前实例的 Glute 外力输入，不清空位置、速度或累计时间；
Gravity Default 重新读取场景重力。0g→1g 通过积分、余振和阻尼恢复，而非直接写回 Rest。

## 8. 自由落体等价性与数值限制

比较两个相同姿态/Shape 的实例：静止参考系 + 0g，与参考系加速度 -980 Z + 世界重力 -980 Z。

| 测试 | 最大相对节点轨迹误差，cm |
|---|---:|
| 合成 fixture，120 Hz 物理步长，30/60/120 FPS | 0.238147697 |
| 同 fixture，240 Hz 物理步长 | 0.122635702 |
| 同 fixture，480 Hz 物理步长 | 0.062342358 |
| 第一人物，30/60/120 FPS，两侧最大 | 0.359762764 |
| 第二人物，30 FPS，两侧最大 | 0.259963471 |
| 第二人物，60 FPS，两侧最大 | 0.310036749 |
| 第二人物，120 FPS，两侧最大 | 0.314248546 |

这是近似一致，不是精确一致。现有 backward-Euler 世界位置积分与解析加速参考轨迹之间存在一阶截断误差。
减半物理步长的收敛测试确认了这一点；正式默认仍为 120 Hz，没有为测试改变正式 FixedStep。

## 9–10. 当前姿态与支承

Neutral 和 Flex90 在相同 pelvis world orientation 下的残余重力一致。
Pose Tension / regional support / 双 attachment 继续决定平衡位移。
例如第一人物侧卧的最大表面位移约 2.3291 cm，Flex90 + 同侧卧约 2.1898 cm。
单独 thigh swing 可以叠加在侧向重力上。重力代码不修改 Morph weight、Shape、mass 或 Mobility。

### 经用户授权的近限位修复

首个真实人物候选中，原有限支承曲线不足以平衡仰卧负载，左侧累计发生 1,136 次硬限位修正。
用户明确同意只修正接近限位的支承曲线。

保留原内部曲线，在软过渡区最后一半增加 C1 连续屏障，使用隐式 Newton 求解和保持可行的步长。
没有增加行程，没有更改 Support 0.45 / Damping 0.65 / Mobility 2.0 / Coupling 1.0 / Mass 1.0。
最终两个人物的 standing、supine、side lying、Flex90 + side gravity、0g、0.5g、2g、恢复 1g 检查中，硬限位修正计数均为 0。
屏障方程、作用区间及积分说明见项目 `docs/runtime/GLUTE_JIGGLE_G11.md`。

## 11. Breast 与既有几何回归

- Breast 实现文件未修改；两个人物全部 Breast 测试的数值 Info 输出与修改前逐项相同。
- G0.6.2 每个人物 288 行 surface truth 数值逐项相同，仅配置资产路径不同。
- G0 的 region、结构几何、attachment、helper 层级/索引审计数据一致。
- Breast/Glute skin transfer 数据一致，未重新分配权重。
- 运行时 Shape 保持 helper identity 和不可变 ReferenceGravityLocal；两实例状态独立。

## 12. Profile 版本及旧资产

新 Profile：Schema 2，Algorithm `glute-dual-attachment-g1.1-reference-gravity-v1`，
GravityPolicy `body-attached-imported-1g-v1`。

类默认仍保持 Schema 1，避免旧资产省略字段被默认为新语义。
旧 Profile 可安全加载，继续原重力/限位行为，诊断明确显示 Upgrade Runtime required。
仅更新 DLL 不会把旧人物变成 G1.1。

## 13. 正式资产路径

| 输出 | Content Browser 根目录 |
|---|---|
| 第一人物，Generate 链构建 | `/Game/VamRuntime/R_732d900601c0c230e0980b1b` |
| 第二人物，正式 Upgrade Runtime | `/Game/VamRuntime/R_0bcc2602fdc30be9a7cf8028` |

两个目录均包含 `BP_VamCharacter`、`RC_Runtime`、`DA_GluteJiggle` 及既有结构/Corrective 资产。
例如第一人物 BP 为 `/Game/VamRuntime/R_732d900601c0c230e0980b1b/BP_VamCharacter`，
Profile 为 `/Game/VamRuntime/R_732d900601c0c230e0980b1b/DA_GluteJiggle`。
旧人物和源资产均保留；没有替换旧场景实例。
Generate 和 Upgrade 都自动调用新 Builder，不增加用户操作步骤。

## 14. 30/60/120 FPS

固定 120 Hz 的重力切换 fixture，在三种渲染帧率下峰值均为 3.575353999 cm，恢复后接近零。
匹配时间点的轨迹通过 <0.15 cm 差异检查；每帧最大移动分别为 1.05425 / 0.55067 / 0.27820 cm，
随帧间隔变化，不能把每帧位移当成相同时间间隔的轨迹误差。
force 切换的零时间推进测试验证没有直接重置状态。

## 15. Surface truth

下表为两侧中较大的表面 Max，单位 cm；比较相同当前 G0.5/G0.6.2 Pose/Morph 下 G1 OFF 与 ON。

| 状态 | 第一人物 | 第二人物 |
|---|---:|---:|
| Default standing settled | <0.000001 | <0.000001 |
| Supine settled | 1.809170 | 2.101102 |
| Side lying settled | 2.329053 | 2.717919 |
| Zero gravity settled | 1.829491 | 2.097555 |
| 1g restored | <0.000001 | <0.000001 |
| Flex90 + side gravity | 2.189784 | 2.572569 |

完整 P50/P90/P95/Max/RMS、Core/Upper/Lower/Medial/Lateral 和 silhouette proxy，
见本目录 `Primary/g1-surface.json`、`Secondary/g1-surface.json`、`summary.json`。
这是当前 native LOD0 的按需 CPU 重建审计；正式逐帧渲染仍使用 native GPU skinning。
数据不包含材质 WPO、衣服或 GPU readback，不构成视觉验收。

## 16. 性能

同进程 12,000 次五节点 Advance 的 Development CPU 测量：

- 第一轮最终测试：legacy 1.842033 μs → G1.1 2.069392 μs。
- 第二轮最终测试：legacy 1.867766 μs → G1.1 2.006400 μs。

默认路径增加约 0.14–0.23 μs/侧/调用。屏障激活时隐式迭代有额外成本：
此次表面快照中仰卧/侧卧/0g 的单次诊断约 5.2–11.5 μs/侧。
后者是单次样本，不是稳定平均基准；没有声称重负载下零性能增加。
无新增粒子、逐顶点运行时物理、逐帧 CPU skinning 或 Morph rebuild。

## 17. Build、测试、重载及 Cook

- Editor Development、Game Development、Game Shipping 均编译成功。
- Python 52/52 通过。
- 两个人物各 20 项 UE 自动测试通过：18 clean + 2 warnings，0 failed / 0 not run。
- 两条 warning 均为既有 Breast/Glute 临时测试世界缺少 EndPlay 的清理警告。
- 加长测试批次初期触发 Water 子系统 GC 崩溃；测试批次推迟周期 GC 并在结束后恢复清理间隔，最终两次完整运行正常退出。
- 两个人物分别由独立 writer/reloader/verifier 进程完成 committed output。
- 两份输出记录的 103 个算法文件哈希与最终源码完全匹配。
- Windows Cook：831 cooked + 7 platform skips = 838 packages；0 errors / 0 warnings。
- DLL 安装备份、哈希和构建日志均保存在本 evidence 目录。

## 18. 普通 Empty Level 操作

1. 从上述新目录拖入 `BP_VamCharacter`，Play 或 Simulate，选择运行中的人物并刷新 VaM 人物调试面板。
2. 如衣服遮挡，使用已有衣服/配饰显示开关；无需删除衣服。
3. 展开 **Glute Jiggle**。默认参数已经是用户选定的标准值。
4. 点击 **Gravity Default**、**Reset Orientation**：观察 standing，等待收敛。
5. 点击 **Rotate Character 90 Pitch**：整体平滑旋转；观察 supine 方向下的变化。实际朝向取决于人物最初 facing。
6. 点击 **Rotate Character 90 Roll**：观察 side lying；不要把与地板穿插解释为接触模拟。
7. **Gravity Zero** 切 0g；等待后点 **Gravity Default** 恢复场景 1g。Half/Double 为附加观察入口。
8. 在 **Glute Structural Debug** 选择 **Flexion 90**，然后使用 Roll，比较同方向不同 pose support。
9. 使用 **Show Rest vs Dynamic** 和 **对比 G1 OFF / 当前动态表面**；后者显示同姿态表面对比并保存分区统计。
10. 完成后点 **Gravity Default** 和 **Reset Orientation**。

重力按钮只影响当前人物的 Glute solver，不修改 WorldSettings、Breast 或项目配置。
Reset Orientation 恢复第一次使用朝向按钮时记录的 actor rotation。
诊断区逐侧显示 world/current/reference/residual gravity、GravityForce、ReferencePreload、
EffectiveGravityMagnitude，以及原有节点/solver 信息。

## 19. 当前限制

- 保留 120 Hz 一阶积分误差，自由落体与 0g 并非逐位精确一致。
- 接近行程边界会有额外隐式迭代成本；大量人物的目标平台性能仍需专项测量。
- 主体是五节点骨骼二次动态模型；无体积压力、软体碰撞、坐压、手压或组织医学标定。
- 非均匀 Actor scale 未纳入此次验证；人物比例使用已有 Shape 系统。
- Cook 成功不等于已完成人工 packaged-game 体验测试。
- 视觉自然度、姿态下的主观效果和参数是否符合预期，由用户人工验收。

本阶段停止于 G1.1，不继续 Thigh Jiggle。
