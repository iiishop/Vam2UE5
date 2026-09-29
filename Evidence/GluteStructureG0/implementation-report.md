# G0 — Structural Glute 完成报告

## 1. 分支、HEAD 与安装

- 分支：`feature/jiggle/hip`，基于最新 master 的 `7f325187fd8af912c40e129913c6dfb218c80344`。
- 最终 Git HEAD 仍为该提交；本轮源码、文档和 evidence 保留为未提交工作区改动，未 push。
- Editor、Game Development、Game Shipping 编译成功。新版 Editor DLL 已安装到 SmartNPC，并逐文件校验 SHA-256；原二进制备份位置见 `install.json`。
- 最终三份资产构建凭据中的 Source 哈希均与当前源码一致。完整记录见 `summary.json`。

## 2. 实际结构模型

每侧使用 pelvis 主参考系与 thigh 次参考。五个区域由固定的 pelvis tether、随股骨运动的 attachment、被动拉伸和体积保持局部伸缩共同决定 structural rest。输出完全取决于当前 Shape 和最终 pelvis/thigh 姿态。

新增 `UVamGluteStructureProfile`、`VamGluteStructure` 纯求值器、`UVamGluteSkeletalMeshComponent` 和 `UVamGluteStructureBuilder`。数据资产不可变共享，Shape 后的 rest 数据及诊断状态属于各个实例。

## 3. 与 Breast 的区别

G0 没有弹簧积分器、速度、余振、时间累加器或自由 whole-glute angular mode。其局部旋转是双附着几何决定的受约束姿态变形。Breast V3 的求解模型与参数保持原实现；复用了原生网格提取和 donor compression，并让 Breast 验证器接受其后追加的 helper。

## 4. 自动 Glute region

来源为 glute/pelvis/近端 thigh 权重、实际 Morph delta 支持、原生三角拓扑和 source-ID 接缝对应。连续 posterior、左右中线、上下范围门控与八轮拓扑扩散共同限制区域。

本轮来源保留 `LGlute/RGlute`，但其原始蒙皮支持为零。因此使用来源 glute bind landmark 定位 pelvis/近端 thigh 的合法候选组织，再进行上述分析。这是通用证据备用路径，已记录到 Profile provenance。这些 glute 骨不会成为运行时运动参考。family 缺失或证据无法确定方向时明确拒绝构建。

## 5. Pelvis / Thigh 双支承

每个区域保存 pelvis attachment point、thigh-local attachment point，以及独立的 pelvis/thigh share。share 根据原始 donor 权重、区域核平均及到两类支点的几何距离归一化得到。股骨支点带几何推导的偏轴量，使内外旋也能影响结构。

主样本左侧实际 pelvis share：Core **0.828**、Upper **0.850**、Lower **0.788**、Medial **0.843**、Lateral **0.754**。对应 thigh share 为其补数；这些是生成结果，不是人物配置常数。

## 6. 五区域职责与层级

每侧 `pelvis → L/R_Glute_Anchor → Core / Upper / Lower / Medial / Lateral`。

- Core：主体和后方 projection。
- Upper：上部骨盆支承。
- Lower：近端股骨连续性。
- Medial：靠近中线的骨盆 tether。
- Lateral：外侧股骨/筋膜影响。

源骨 0–87、Breast helper 88–99 保留。新增 G0 helper 为 100–111。原 `LGlute/RGlute` 保留。

## 7. Passive Pose Tension

对每个区域分别计算当前与 rest 附着纤维长度比：`tension = gain × max(log(length ratio), 0)²`。由此平滑更新该区域的 attachment share 和 future support baseline。它只表示被动拉伸，不推断主动肌肉收缩，也没有全臀部统一 stiffness scalar。

## 8. 各种髋部姿态如何改变 rest

屈曲、伸展、外展/内收、内旋/外旋改变 thigh-local 支点在 pelvis frame 中的位置，进而改变各区域纤维长度、方向、受约束旋转与轴向/横向伸缩。轴向 log stretch 用连续饱和函数限制；横向采用其平方根倒数。

同一 Shape 与 pelvis/thigh 相对姿态重复求值得到相同结果；没有根据 DeltaTime 改变形状的逻辑。读取位置在最终原生姿态发布前，能看到动画、Pose/IK/约束和 rigid blend 的最终源骨姿态。G0 和 Breast 写入不同 helper。

## 9. 主体支承与未来 G1

每个区域的三个伸缩系数乘积为 1，并在 pelvis 解剖坐标中保留连续的后方 projection 下界；pelvis tether 不会释放。未来 G1 应围绕这些 structural transforms 叠加 residual，不能替换主体支承。

这是局部 scaffold 的数学约束，不保证混合蒙皮后的整个臀部网格精确保体积，也不代表形状已经通过人工判断。

## 10. Helpers、蒙皮和几何校准

RBF 核在连续 region 内分配 helper 权重；最多转移合法 donor 的 70%，保留原支承。复用 Breast donor compression，最终不超过 8 influences 并重新归一化。源顶点、Morph delta、骨索引与 bind 保持；零姿态重建容差为 0.001 cm。

Effective volume 使用区域三角面到 pelvis 支承平面的 cone proxy，COM 使用 cone 质心；区域体积、质量比例候选、尺寸、lever arm 与惯量候选均由几何推导，不使用固定质量百分比。它们不是医学体积。Shape 响应更新 rest、COM、体积、尺寸、附着及支承，沿用实例级参考姿态覆盖，不修改共享 Skeleton。

## 11. 自动生成和升级

现有“生成 UE5 人物资产”在 Breast V3 之后自动进行 G0 区域分析、helper 追加、双附着校准、蒙皮重分配、Shape 响应和 Profile 构建，再生成最终 RuntimeConfiguration/BP，经过独立重载与发布验证。

旧 BP 的正式 Upgrade Runtime 入口已实际运行，生成新的 committed 输出。原人物及源资产没有原地迁移。最终扩展 body/skeleton/Shape/parts 位于各 runtime root 的 `Glute/` 子目录。

## 12. 实际资产路径

| 样本 | Content Browser runtime 目录 |
|---|---|
| 主样本 A | `/Game/VamRuntime/R_63c4791def59b8e34504a5bc/` |
| 正式升级样本 B | `/Game/VamRuntime/R_295247eac43ac01125059132/` |
| 额外旧原生资产/多部件样本 C | `/Game/VamRuntime/R_a1a8a6813534636c2f7ab1ca/` |

每个目录均含：

- `BP_VamCharacter`
- `RC_Runtime`
- `DA_GluteStructure`
- `DA_BreastJiggle`
- `Glute/SK_Body` 及其扩展 Skeleton、Shape、部件资产。

A 导入形状左右 effective volume 为 **190.88 / 224.32 cm³**；B 为 **198.09 / 196.49 cm³**。两者初始差异不大；C 与 B 的主体几何相同，额外覆盖另一旧资产/部件组合。

因此还实际测试了明显改变几何的合法 Shape 范围：A 左侧 proxy 从 **190.88 → 300.35 cm³（1.573×）**，B 从 **198.09 → 429.42 cm³（2.168×）**，并在改变后的形状上运行四种姿态。A 的 `a07 - Body = 0`、`Body Size = 1` 变体也保存在工程观察 fixture 的第二个普通 BP 实例中。这些数值证明几何与结构输入发生了较大变化，不作外观评判。

## 13. 自动测试、重载与 Cook

- 每份最终资产运行 **9 项自动测试**：2 项 G0、7 项 Breast 回归，三份均通过。
- 已安装到实际 SmartNPC 工程后，再运行相同 **9 项**，全部通过；合计 **36 次测试执行成功**。
- 覆盖源骨索引/bind、helper 层级、8 influences/归一化、零姿态重建、每个原生 Morph delta 保持、实例隔离、Shape identity、原生最终姿态接入。
- 结构测试覆盖 neutral、镜像、三轴正负姿态、连续扫描、attachment 有限/归一化、局部 determinant、posterior floor、实际蒙皮网格变形有界、大幅 Shape 后各姿态、30/60/120 求值/原生 hook 等价。
- 三份资产都完成独立进程保存、重载和发布验证；所有最终 Source 哈希一致。
- Windows Cook 成功：**935 packages cooked、7 platform-skipped，0 errors / 0 warnings，exit 0**。
- Cook fixture 仅含普通 BP 实例，没有专用 runtime Test Actor 或 Level Blueprint 初始化。

各样本 `audit.json`、`tests.json`、`runtime-report.json`，以及 `installed-tests.json`、`cook.log`、构建日志、`summary.json` 均保存在本目录。

## 14. 当前技术限制

来源 surface/skin weights 无法恢复真实个体内部肌肉/筋膜解剖；区域与附着是工程估计。较大 Shape 使用有界一阶预计算响应。局部保体积不等于整个混合蒙皮网格严格保体积。区域覆盖、主体轮廓、姿态下的具体外观仍需人工检查。

Fold 只保留 medial infragluteal / mid transition / lateral fade 语义。没有 G1 Jiggle、自由臀部旋转、坐压、接触、按压、自碰撞、Chaos Flesh、肌肉主动收缩推断、Fold/wrinkle corrective 或衣服形变。

## 15. 在新建 Empty Level 中观察

1. 重新打开 UE。创建 **Empty Level**，从第 12 节任一目录拖入 `BP_VamCharacter`，Play/Simulate。
2. 打开 **窗口 → VaM 人物调试**，选中运行中的人物，点击 **使用选中人物 / 刷新**。
3. 需要露出身体时点击现有 **隐藏衣服 / 配饰**，再次点击可恢复。
4. 折叠 Breast 区域，展开其下方 **Glute Structure - G0**。
5. 使用以下按钮观察；这些是髋关节 debug offset，正常关节限制仍生效：

| 按钮 | 请求的姿态 |
|---|---|
| Neutral standing | 清除两侧 thigh 的 G0 debug offset |
| Hip flexion | 髋屈曲 70°；可用于观察抬腿/弯腰相关的髋部结构输入 |
| Hip extension | 髋伸展 20° |
| Abduction | 镜像外展 30° |
| External rotation | 镜像外旋 30° |
| Reset | 清除两侧 thigh 的 G0 debug offset |

这些按钮不制作完整平衡站姿/弯腰动画，也不会清空已有 Base Animation 或 Pose Control。要单独比较，可使用本轮无 Base Animation 的输出并保持其他 pose 控制为零；真实弯腰动画同样走最终 pelvis/thigh 输入。

`Enabled` 切换结构求值；关闭时 helper 回到当前 Shape 的 neutral rest。`Show Glute Region` 显示导入区域证据，`Show Structural Bones` 显示骨架，`Show Pelvis Attachments`/`Show Thigh Attachments` 显示两种附着线，`Show Pose Tension` 标注被动拉伸。下方 Diagnostics 显示 hip angles、体积/COM/尺寸、逐区域附着比例、rest offset、tension 与 support。

最终视觉效果由用户人工判断。本轮到 G0 为止。
