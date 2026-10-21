---
title: 腿部骨链方案：资料与实验依据
subtitle: 先验证形态贡献，再决定是否替换 C
lang: zh
---
## A 当前代码发现
旧方案将整段腿的区域 Jiggle 辅助骨和新增站点一起放到同一 Hermite 曲线上。它没有独立的肌肉起止附着点。五个纵向站点之间仍使用线性蒙皮，不能等同 Blender Bendy Bones 的全部功能。
旧横向补偿为 sqrt(直线长度/曲线弧长)。固定端点条件下，弧长不短于直线，所以这个量只会缩小截面，不会表达缩短鼓起。同时轴向 scale 保持 1，因此不能称为严格局部体积保持。
## B 原始资料
- [Blender Bendy Bones 官方手册](https://docs.blender.org/manual/id/4.2/animation/armatures/bones/properties/bendy_bones.html)：曲线、分段、端点控制与缩放是相互关联的；不能只增加几根骨头。
- [UE Spline Rigging 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/control-rig-spline-rigging-in-unreal-engine)：提供骨链拟合、轴向、伸缩和子级传播控制；不等同于肌肉体积求解。
- [Robert Joosten 的运行时角色绑定案例](https://robertjoosten.github.io/projects/auto_rigger_unity/)：分开平移辅助骨、朝向辅助骨、扭转骨；肌肉具有 origin/insertion，并可叠加动态效果。
- [Autodesk Muscle Creator](https://download.autodesk.com/us/maya/2009help/files/Creating_Muscles_Create_muscles_with_the_Muscle_Creator.htm)：需要 Rest/Squash/Stretch 状态及附着点；这提示我们不能假定一条自动曲线就是解剖目标。
- [Autodesk cMuscleStretch](https://download.autodesk.com/us/maya/2011help/files/Muscle_Nodes_cMuscleStretch_node_.htm)：起点、中段、终点可分别控制拉伸、压缩和 Jiggle。
- [Blender Maintain Volume](https://docs.blender.org/manual/en/latest/animation/constraints/transform/maintain_volume.html)：轴向伸缩与横向补偿需要配套；单独缩小横截面不是完整保持体积。
以上是算法设计参考，不代表这些作者验证了本项目的角色、权重或参数。
## C 本轮实际比较
| 代号 | 结构 | 目的 |
| R | 无新链条、Jiggle 开、接触关 | C 的骨架形态参照，隔离接触 |
| L | 旧曲线旋转+位移+全弧长缩放 | 复现上轮新骨链 |
| T | 曲线只移动支撑点，保留骨的朝向与尺度 | 检查框架旋转是否导致折痕 |
| V | 曲线+局部伸长配对横向补偿 | 单骨仿射体积 determinant=1，不宣称网格体积守恒 |
| S | 只驱动新增站点，原 Jiggle 骨保持原参考框架 | 检查全区域同时跟随曲线的影响 |
| C | 无新链条、Jiggle+接触 | 保留用户认可的原组合，逐帧记录接触是否有效 |
## D 实验边界
保留主骨动画、现有辅助骨权重、Morph 和资产。仅运行时算子改变，每个录制保存新的验证身份；不将旧 B1 资产证书冒充新算子验证。
这轮是同一骨架上三种结构候选和消融比较，尚未构建独立解剖肌肉附着点系统。接触历史停算单独展示；关闭接触的组不具备座面压缩功能。视觉比较必须结合视频和 A1 数据，较小位移不等于较真实。
