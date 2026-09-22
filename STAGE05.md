# Stage05 — 实现状态：部分完成，不能按整阶段验收

## 已实现

- `VamCharacterRuntime` 独立 Runtime 模块，无 Python、UnityPy、VAR、HTTP 或编辑器模块依赖。
- `UVamCharacterDefinition`、异步装配 `UVamCharacterComponent`、`AVamCharacterActor` 和插件内容 `Blueprints/BP_VamCharacter`。仅加载原生资产；Morph 按绝对参数设置，不在预览已烘焙形状上再次叠加 p0。
- 编辑器 `UVamNativeBuilder`：真正的 USkeletalMesh、独立 USkeleton、MeshDescription Morph 属性、UV、材质区及 skin influences。UE 构建后检查 `MeshToImportVertexMap`，不假定构建前后顶点数量相同。骨架修改器先结束生命周期，再进入 UE 构建器。
- 构建器验证父节点顺序、单位缩放、旋转合法性、索引、有限数值、权重覆盖/归一/最多 8 个影响、Morph 域。已有包拒绝覆盖；每次独立 Skeleton，避免修改共享人物绑定。
- `UVamSourceMapping` 来源资产类型，预留原始 IR、材质 IR、构建契约、最终渲染到输入及来源映射；重数据在 cook 中移除。**尚未接入正式人物提交。**
- 浏览器顶部增加独立“生成 UE 人物资产”入口。**当前实现仅执行来源预检查并显示阻断报告，还没有接通正式人物提交。** 临时预览入口保持原样。

## 来源契约与核对结果

`vam_native_source.py` 仅消费不可变 Stage03 IR，不重新扫描目录，不把临时 DynamicMesh 当资产输入。

- X0 从原始中性 merged mesh 重建，并应用中性 graft Boundary；p0 单独保留。
- 每个 Morph 使用真实增量和 Stage03 的重复增量、UV 复制、graft 域规则。记录原始 delta、来源参数与范围。BoneCenter、比例、旋转等公式单独保存，不能声称都是 UMorphTarget。
- 用已有来源求值逻辑重建 X(p0)，核对 X0 + Σp0·delta。当前本地样本顶点最大误差约 `2.93e-14 cm`。**此比较不包含未实现公式，不是完整 VaM 姿态真值验证。**
- 引用朝向按 DAZBone 实际 orientation 模式组合四元数，再完整变换旋转基。世界参考位置转换为 UE 厘米，再转父级局部参考变换，保留 world bind / inverse bind。姿态 rotation order 单独保留，不能混同 reference orientation。
- 本地当前样本 `DAZ parentBone` 有 hip、lNipple、rNipple 三个根；IR 未保存实际 Transform 父链。临时身份根只用于表达已知的森林，**并不被认定为正确来源绑定**，正式提交被拦截。
- general 权重仅在 `_useGeneralWeights` 和 `_hasGeneralWeights` 同时有效且覆盖/域校验通过时转换。核对来源 CPU general 分支：它不额外使用 `fullyWeightedVertices`；重复 general 项相加。没有兜底 root 权重，也不静默裁剪或归一化不合法数据。
- 当前样本使用 TriAx。未实现经过源姿态参考验证的适配器，不平均 XYZ，不伪造训练/验证姿态或误差。原轴权重与 bulge 仍在原始 IR 中。
- 发丝原曲线、头皮、发根、参数仍保留于来源 IR；调试发片不进入正式头发资产。Groom 留到 Stage10。

来源报告：`Saved/NativeBuild/latest-report.json`；详细中性网格、p0、Morph 与骨架契约：同目录 `<decode_id>.native-source.json`。

## 仍未完成（阻止完整人物验收）

1. 实际 Transform 父链、完整绑定来源补齐与源姿态验证。
2. TriAx 非负有界 LBS 拟合、独立验证姿态和误差报告；bulge 近似策略。
3. 骨骼中心/比例/旋转及非线性 Morph 公式求值与 X0/X(p0) 完整验证。
4. 服装/附件 SkinWrap 权重传递、独立资产、蒙皮与静态包裹误差。
5. Stage04 partial 材质/贴图持久化、人体各子域绑定与完整来源映射提交。
6. 稳定来源身份→原生资产的事务提交、独立重新加载、ImportState manifest 更新，以及派生资产更新/冲突工作流。当前从不更新 manifest；底层构建器拒绝覆盖。
7. 正式人物的 cook / 打包 / 独立运行验收。Runtime 目标编译成功不等于这些验收通过。

## 已执行的技术验证

- UE 5.8.2 编辑器模块编译。
- UnrealGame Win64 Development 的 Runtime 模块编译。
- 5 个小型合法数据测试：父链/完整旋转/绑定往返、循环拒绝、实际权重模式、重复权重和未覆盖顶点。
- 原创四顶点双骨骼面板创建 SkeletalMesh、Skeleton、Bend Morph、CharacterDefinition 和测试 Blueprint，保存成功；另起 UE 进程重新加载成功。测试不包含、也不分发真实 VaM 资产。
- 日志位于项目 `Saved/stage05-native-build.log`、`Saved/stage05-native-reload.log`、`Saved/stage05-runtime-compile.txt`。

## 人工可检查的部分

1. 打开 **SmartNPC.uproject**（不是旧的 VamPreview 独立工程），在“窗口 → VaM 资源浏览器”查看新的生成按钮。
2. 解码一个人物后点“生成 UE 人物资产”：当前 TriAx 样本应显示明确的待校准项和报告路径，**不应出现成功入库、不应更新 ImportState**。原临时预览仍能用。
3. Content Browser 打开 `/Game/VamStage05Tests/SK_TestPanel`。它是测试面板，不是人物；应看到 root/tip 两根骨骼和 Bend Morph。Bend 从 0→1 时面板上端沿 X 移动 20 cm。重新打开工程，资产仍存在。
4. 将 `/Game/VamStage05Tests/BP_TestPanel` 拖入关卡，点击 Actor 的“Load Character”，或进入 PIE。应加载面板并使用默认 Bend=0.25。宿主只读原生资产；可以在关闭资源浏览器和本地索引服务后检查。正式测试 Blueprint 位于插件内容 `/VamResourceBrowser/Blueprints/BP_VamCharacter`，默认不绑定人物。
5. **现在不能验收真实人物动画、换装、完整 Morph 或打包人物。** 上述测试仅验证已落地的原生资产基础。
