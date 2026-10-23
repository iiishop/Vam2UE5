---
title: 头发为什么会动，又为什么会成团？
subtitle: UE 原生、公开游戏案例、VaM 与当前项目的机制对照 · 2026-10-23 · 源码基线 f726326
lang: zh
---

## A 先说结论 {span=2}

**我们已经使用 UE 原生 Groom / Niagara GPU 求解器。当前效果仍然不好，最值得优先检查的是 VaM 物理数据的转换、发束控制关系，以及模拟结果如何带动渲染发丝。** 当前方案包含多处近似，尚未复现 VaM 的完整动态行为。

这次是资料与源码核对；没有修改运行参数，也没有把旧录像当成新一轮修复验证。公开资料用于说明引擎能力；本机源码用于说明当前真实实现；既有实测用于判断已经发生的问题。

**已确认：** 波浪长发开启模拟后成团，关闭身体碰撞后仍然成团。**尚未确认：** 是模拟导线本身折叠，还是导线带动可见发丝时产生错误，或两者共同作用。

## B 把头发拆成四件事

可以把它理解为“少量可弯曲的控制线，带动大量可见头发”。控制线也叫导线，不必等同于人物骨骼。

```flow
原始造型与附着位置 -> 少量模拟导线: 决定哪些点固定，哪些点可以动
少量模拟导线 -> 重力、惯性、空气作用: 先预测下一步的位置
重力、惯性、空气作用 -> 长度、弯曲、发束连接、身体碰撞: 修正不合理的位置
长度、弯曲、发束连接、身体碰撞 -> 大量可见发丝: 根据导线运动插值，最后渲染
```

四个容易混淆的概念：

- **弯曲刚度：** 这根线有多难被折弯。
- **造型支撑：** 偏离设计位置后，被拉回原造型的力度。
- **阻尼：** 摆动逐渐停下来的速度。
- **发束连接：** 哪些头发要一起走，哪些可以散开。它与头发之间的碰撞是不同机制。

强造型支撑可以让柔软的头发仍然紧贴头部；增加阻尼也不能修正错误的发束连接。画面上的“偏硬”不一定由弯曲刚度单独造成。

## C UE 原生与游戏生产中有哪些路线 {span=2}

| 路线 | 用容易理解的话描述 | 优势与限制 |
|---|---|---|
| Groom 导线物理 | 把控制线当作可弯曲、可扭转的细杆，GPU 模拟后驱动细发丝 | 适合细致的散发；需要正确导线、约束和插值 |
| 发束骨链物理 | 给马尾或一束头发放一串骨骼，求骨骼摆动，表面跟着变形 | 整体轮廓容易控制；一条链不能自动提供大量独立散发运动 |
| 骨架／造型控制与 Groom 结合 | 用明确的束根和骨架控制大形，再通过 Groom 表现发丝 | 适合扎发；需要明确由谁控制每一层，避免重复模拟 |

UE 的 Groom 物理使用 Niagara，在 GPU 上运行 XPBD 约束求解。XPBD 可理解为：先预测运动，再反复修正“长度、弯曲和碰撞”等条件；允许材料有一定柔软度。原生有 Cosserat Rods 和 Angular Springs 两种模型。身体碰撞使用 PhysicsAsset 中的简化形状，不是逐根发丝与所有皮肤三角面精确碰撞。[Epic：Hair Physics Overview](https://dev.epicgames.com/documentation/unreal-engine/hair-physics-in-unreal-engine---overview)

骨链路线也有原生入口：Animation Blueprint 的 RigidBody 节点，官方明确列举了马尾用途，并支持身体和场景碰撞。KawaiiPhysics 则是外部开源骨骼摆动插件，使用球、胶囊、平面等约束，保留骨长。[Epic：RigidBody](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-rigid-body-in-unreal-engine) · [KawaiiPhysics 作者仓库](https://github.com/pafuhana1213/KawaiiPhysics)

Epic 新的 Hair Art Directability 文档还给出了用骨骼、蒙皮和 Deformer Graph 驱动 Groom 的路径，本机 UE 5.8 也包含 DG_GuidesSkinning 资产。文档中的骨架驱动配置要求关闭对应 Groom 模拟，避免模拟覆盖骨架结果。因此不能简单地把两套模拟同时打开，就认为已经完成混合。[Epic：Hair Art Directability](https://dev.epicgames.com/documentation/en-us/metahuman/hair-art-directability)

**发片和发丝主要描述“怎么画”，骨链和导线主要描述“怎么动”。两者不是一一对应的分类。** Groom 本身也支持发丝、发片和网格 LOD。[Epic：Groom Rendering and Simulation](https://dev.epicgames.com/documentation/unreal-engine/hair-rendering-and-simulation-in-unreal-engine)

### 可核实的实际项目

| 案例 | 第一方资料明确了什么 | 不能据此推断什么 |
|---|---|---|
| 光与影：33号远征队 | Epic 的开发者采访确认 UE5；KawaiiPhysics 作者采用名单列有该游戏 | 不能断言每个角色、每根头发都由 KawaiiPhysics 控制，或排除混合方案 |
| NC 的 Project M，2023 年公开开发案例 | NC 角色作者明确说使用 Unreal Groom，并结合动画调试模拟后的造型 | 这是一份开发案例，不能当成已发售游戏的全套性能与求解器报告 |

[33号远征队：Epic 开发者采访](https://www.unrealengine.com/developer-interviews/inside-the-development-journey-of-clair-obscur-expedition-33) · [KawaiiPhysics 作者采用名单](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE-Adoption) · [NC 角色作者访谈](https://about.ncsoft.com/en/news/article/theoriginality-sjm)

这些资料支持“导线和骨链均有实际应用”，不支持“UE5 游戏都用某一种万能方案”。

## D VaM 实际怎样保持发型

本机 VaM 的 HairSettings / HairPhysicsWorld 调用链属于 GPUTools.Hair。它也使用 GPU 粒子和约束，并不是给每一根可见头发建立 Unity 刚体。

| VaM 机制 | 对外观的意义 |
|---|---|
| PointJoints + 逐点 Rigidity | 发根、发中、发梢可分别支撑，也可使用作者涂绘的数据。扎紧处与散发能有不同自由度 |
| DistanceJoints | 控制相连粒子间距离，防止长度随意变化 |
| Compression / Spline 类约束 | 参与压缩与沿发丝的形状控制；具体 GPU 公式不能只由类名完全推导 |
| NearbyDistanceJoints | 保存作者建立的发束连接，让部分头发维持组合关系 |
| 球、连接球、平面碰撞 | 阻止头发穿过身体等碰撞代理 |
| 后续细分与插值 | 将模拟控制数据生成更多可见头发，并结合原有卷曲、分散等外观数据 |

本机正常模拟分支可追踪到：积分 → 点支撑 → 距离约束 → 可选压缩／邻接连接 → Spline／距离约束 → 碰撞 → 点支撑／速度更新。这个顺序与 UE 原生求解加我们自定义步骤的顺序并不相同。

**VaM 的 Rigidity 不能直接当成 UE 的 Bend Stiffness。** 前者在这里包含对原造型位置的支撑；后者控制抵抗折弯的能力。VaM 的 Weight 还参与本机调度的时间尺度计算，也不能照抄成 UE 的质量。

VaM 发型作者指南同样强调多发块组织、根中梢刚性及绘制刚性。作者已经把“哪里扎紧、哪里散开”的部分信息写进资源；转换时需要保留这些信息。[VaM 发型作者指南](https://hub.virtamate.com/resources/hairstyle-creation-guide-tips.423/)

本机证据：[调度](../../Saved/HairRuntime/HairPhysicsWorld.cs) · [逐点支撑构建](../../Saved/HairRuntime/BuildPointJoints.cs) · [发束连接构建](../../Saved/HairRuntime/BuildNearbyDistanceJoints.cs) · [此前实际 VaM 动态采样报告](../HairDynamicReference/report.html)。这里只概述调用链，不把未读取的 GPU 内核数学模型宣称为已经完整复刻。

## E 我们当前具体差在哪里 {span=2}

当前实际路径：VaM 导入数据 → 选择代表导线 → UE StableRodsSystem 的复制版本 → 自定义造型支撑／连接／接触处理 → Groom 插值与渲染。当前精细档是每组最多 128 条导线、32 控制点，配置 5 子步、8 次约束迭代。

| 已确认的实现差异 | 为什么值得关注 | 证据与结论边界 |
|---|---|---|
| 源控制网被减少 | 旧马尾源主体有 807 条控制线；当前每组预算最多 128 条。减少自由度后，局部散开和不同长度的运动更难保留 | 减少是事实；128 不是一切发型必然失败的门槛 |
| 每根渲染发丝只跟随一根导线 | 当前 bUseUniqueGuide=true。许多发丝共用一个驱动，分块同步运动的风险增大 | 这是代码事实；是否直接造成这次波浪束团，尚需单独看导线与插值输出 |
| 发束连接被重新映射和截断 | 每点最多保留两条连接；连接长度按目标造型重算，求解读取已发布的导线位置 | 保留了部分结构，但不能称为 VaM 原连接网络的等价复制 |
| 整组分类决定额外限制 | 用曲线形状和刚性中位数分 loose / supported / compact；后两类保留整体回形和位移边界 | 同组中局部柔软的发梢可能受到整组限制；这还需要逐点消融确认 |
| 支撑强度与时间语义是近似 | 按 60×源 Iterations 建立衰减时钟，使用指数回形；源 VaM 有自己的时间步和约束调度 | 相同参数数值不代表相同动态响应 |
| 控制线碰撞与最终发丝不同 | 求解检查简化身体代理上的导线；可见发丝再通过插值得到 | 导线没穿代理，不能推出最终发丝没有穿真实皮肤 |

源码位置：[导线与插值配置](../../Source/VamResourceBrowser/Private/VamHairBuilder.cpp) · [分类](../../Scripts/prepare_hair_physics.py) · [导线和连接转换](../../Scripts/prepare_hair_constraints.py) · [造型限制](../../Source/VamResourceBrowser/Private/VamHairStyleSolver.h) · [回形与连接计算](../../Source/VamResourceBrowser/Private/VamHairSourceConstraints.h)。

补充：supported / compact 的额外位移限制在相应步骤中分别约为 0.15 / 0.25 cm；它们属于当前适配器的策略，不是从 VaM 物理中严格推导的值。新的 loose 路径已经取消旧的整组额外限幅，不能再用历史 10 cm 限制解释所有长发问题。

Epic 文档明确说明 Use Unique Guide 使用单导线运动插值，并提供导线影响可视化。因此“导线动得正确”和“最终发丝动得正确”必须分开检查。[Epic：Groom Interpolation](https://dev.epicgames.com/documentation/en-us/unreal-engine/groom-interpolation-in-unreal-engine)

## F 实测支持哪些判断 {span=2}

[[WAVY_EVIDENCE]]

这三张为已有 UE 消融记录，均为同一波浪资源的后视首帧；包括不同运行模式，适合作定性定位，不作像素对齐误差比较。对应完整证据在 [HairDiversity](../HairDiversity/report.html)。

| 观察 | 支持的判断 | 尚不能推出 |
|---|---|---|
| 关闭模拟时造型顺；开启模拟后左后方成团；关闭身体碰撞后仍成团 | 异常进入于模拟／变形链路；身体碰撞不是唯一解释 | 不能据此断定某一种约束或插值已经被证实是唯一根因 |
| 波浪长发运动阶段导线长度最大比约 1.050，非根部代理穿入统计为零，画面仍明显异常 | 长度与代理穿入指标不覆盖卷曲、扭转、成团或插值错误 | 不能以“长度接近正常”宣布质量通过 |
| 齐肩短发主体被分为 supported，实测运动较小 | 分类及整体造型限制值得检查 | 不能把所有短发运动小都判断为错误 |
| 新双马尾没有识别出自动束根锚点，尾部长度比最高约 1.403 | 单束汇聚识别和双束结构之间可能存在覆盖缺口 | 不能把未识别锚点直接认定为所有外观问题的根因 |

新三款仅重新做了 VaM 静态采样与 UE 动态扩展测试，尚没有这三款各自的同动作 VaM 动态误差。此前长发和单马尾另有实际 VaM 动态采样。两组证据不能互相冒充。

## G 马尾、发髻与长发需要怎样的结构

以下是基于资料和当前证据的架构建议，尚未作为本轮修复实现。

```tree
通用发型资产：造型 + 作者约束 + 身体绑定
  附着区
    发根与头部连接
    扎发收束处或发髻内部支撑
  活动区
    长发：多束独立弯曲与适量关联
    马尾：从束根后方开始的自由尾部
    刘海与侧发：保留局部独立自由度
  可见外观
    导线运动传给细发丝
    检查最终发丝的轮廓和穿模
```

**通用应体现在统一的数据结构与求解流程。不同发型仍需携带不同的附着关系和刚性分布。** 自动导入可以优先利用 VaM 已有的绘制刚性、连接网络和发块；缺失的信息再做几何推断，并输出置信度。只靠整组平均值，无法完整表达“发髻内部固定、旁边几缕散发自由摆动”。

扎发值得评估原生骨架驱动 Groom 的路线；散发继续评估导线物理。最终应由同动作结果选择，不预先承诺某种混合必然更好，也不需要恢复粗略档或打开头发自碰撞。

## H 下一步最有价值的三个实验

1. **分开看模拟和渲染。** 对失败的波浪长发画出模拟导线、导线局部朝向与每根发丝的驱动归属。导线正确而渲染错误，优先修插值；导线已经折叠，继续查约束。
2. **保留造型做受控消融。** 用相同导线、动作、碰撞和采样时间，对比原生 Rods 与当前适配器；逐项恢复点支撑和发束连接。每次只改变一个机制。
3. **验证源结构保真。** 检查束根附近的刚性与连接覆盖，比较较完整的源控制网和当前压缩网；对照单／多导线插值，测造型、运动和成本。

每轮同时看原始造型、运动中的外形、停止后的恢复，以及真实可见发丝穿模。**当前优先级是把控制结构和动态语义对齐；继续增加迭代次数，不能补回丢掉的连接关系。**

研究产物基于 f726326；未更改身体、头发运行代码或生成资产。公开案例的使用范围以其实际披露为准。
