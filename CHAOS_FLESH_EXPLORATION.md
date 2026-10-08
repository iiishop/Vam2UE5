# Chaos Flesh 扩展调研：接触、体积和实时软组织

日期：2026-09-30。决定：保留 Chaos Flesh 方向。
本轮为公开资料与本地 UE 5.8 源码研究；没有运行示例、性能实验或修改生产求解器。

## 1. 可复查案例与借鉴范围

| 资料 | 实际证据 | 对当前项目的价值 | 不能据此宣称 |
|---|---|---|---|
| Evelyn Schwab Meat Cube，UE5.3 | 作者说明实时四面体实验，并公开项目 | 简单软体碰撞、最小配置对照 | 当前5.8性能、胸部精度或完整双向作用 |
| Epic Starfish / Flesh Quickstart | 官方约束、碰撞、表面绑定示例 | 将模拟顶点接触和皮肤显示分别定位 | 运行了示例或已达到实时预算 |
| Electric Dreams 轮胎 | 官方说明内部射线接触与GPU表面位移 | 局部受压、固定支承、低分辨率到渲染映射 | 射线法可直接替代任意软体互撞 |
| Epic Emil Muscle 5.6 | 工程师Yushan Han发布肌肉资产教程 | 骨骼/肌肉/脂肪关系的后续重点 | 已完整读到视频内容、已复现资产 |
| Vellum tetrahedral softbody | 官方分开距离与局部体积约束，模拟网格映射回原网格 | 分离形状弹性、体积和表面映射 | Vellum参数可直接复制到Chaos |
| Soft Tissue with Skin 2021 | 分区体积、局部压缩惩罚与表皮模型 | 整体体积与局部压扁需分别控制 | 我们的单一体积投影就是完整论文复现 |
| PBNG 2024 | 作者描述位置式非线性GS的准静态收敛性质 | 审视当前准静态模式与求解路径是否匹配 | 仅打开开关就得到更快、更稳定的胸部 |
| IPC 2020 | 软块强压、挤过狭缝、多物体接触及公开代码 | 接触穿透与单元翻转应有独立几何指标 | 论文时间步0.01秒意味着计算能跑100FPS |

Meat Cube作者提出的摩擦/材料限制来自UE5.3，不能直接推广到5.8。
本次没有下载或执行其Content资产；已核实仓库、作者报告与版本。
Epic社区教程页面有的只有标题可读取；没有把不可读视频当作已审阅内容。

## 2. UE5.8源码新增发现

`ChaosDeformableSolverGroups.h`：
- `bUseGaussSeidelConstraints`：原生GS路径开关，默认false。
- `bUseGSNeohookean`、SOR相关配置。
- `SpringCollision.bDoSpringCollision`：组件间接触。
- `InComponentSpringCollision.bDoInComponentSpringCollision`：组件内接触。
- 接触搜索半径、刚度、是否滑动等参数。

`ChaosDeformableSolver.cpp`：
- 初始化读取`FVolumeConstraintFacade`，创建`FGaussSeidelUnilateralVolumeConstraints`。
- 有明确检查：该体积约束需要启用GS。
- GS路径包含点-三角形空间哈希接触，组件内/组件间采用不同入口。

这些是代码能力存在的证据，不是功能已在我们人物上工作的证据。
尤其单边体积约束不等于每侧总量精确守恒，需读清其能量与目标定义。
当前项目只打开quasistatics，未显式启用GS，且为每人物创建隔离的双求解器。
因此不能假设它在运行论文PBNG，也不能让不同私有求解器天然产生互撞。
需要评估共享solver/接触岛；共享不可变数据与每人物独立状态仍需保持。

## 3. 从案例推导的工程方向（尚待实验）

### 接触覆盖
保留真实表面样本，改进体积网格质量；不能只用凸包极值点代表接触面。
测球内可动顶点数、最大穿入深度、约束前后穿入及皮肤位移。
若顶点没有接触，先解决采样和碰撞注册，不降低弹性刚度掩盖。

### 支承与体积
根部运动学固定、向外软支承；避免所有可接触粒子被动画目标过度锁定。
体积守恒、局部抗塌陷和表面拉伸分开建模。
同时观察每侧体积误差、最小Jacobian、局部压缩和接触穿透；整体V不变不足以判定成功。
不要在接触求解结束后统一膨胀网格，避免重新穿入按压物。

### 求解与性能
先比较原生动态路径和原生GS准静态路径的适用性，再考虑自定义能量。
双求解器相减不是上述公开案例所要求的标准流程，是当前项目的选择。
优先研究单个求解器、动画目标与接触休眠的组合，保持已有Jiggle表现。
共享接触岛也需预算与跨人物调度，不能仅把所有对象塞进一个solver。
缓存参考体积和拓扑邻接、复用梯度内存，减少子步读回和GPU上传。
性能需测CPU/渲染/GPU；论文的物理时间步与计算耗时是两个量。

### 软软与刚体反作用
先审计本地GS点-三角形接触所需的数据注册与排除表，不能依据2024旧帖宣布引擎全无自碰撞。
官方当前Quickstart仍将世界刚体交互说明为单向；反作用需要明确实现与验证。
任意第三方软体也需共享接触数据协议，不会仅因同处UE场景而自动互撞。

## 4. 建议的探索顺序（未执行）

1. 单软块+平板：明确真实接触、压缩、卸载、体积与CPU耗时。
2. 固定背面的半球+球压头：分离根部支承和局部按压。
3. 动画驱动半球：检查无接触时形状/Jiggle保持及参考系。
4. 两软体：同solver接触与不同实例状态隔离。
5. 可动刚体：质量变化、冲量与反作用一致性。
6. 最后接入人物自动构建与GPU表面映射。

以上是研究实验顺序，不引入正式人物对测试地图的依赖。
本轮没有新增或运行测试，没有新的视觉验收结论。

## 来源
- https://github.com/EvelynSchwab/Chaos-flesh-meat-cube
- https://evelynschwab.com/meatcube/
- https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart
- https://dev.epicgames.com/community/learning/tutorials/BEZV/unreal-engine-electric-dreams-tire-deformation
- https://dev.epicgames.com/community/learning/tutorials/RZwB/unreal-engine-chaos-flesh-muscle-simulation-tutorial-5-6
- https://www.sidefx.com/docs/houdini/shelf/vellumtetsoftbody.html
- https://elrnv.com/projects/volume-preserving-simulation-of-soft-tissue-with-skin/
- https://zhanzhangzz.com/publication/pbgs/
- https://ipc-sim.github.io/
