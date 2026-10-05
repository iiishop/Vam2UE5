# Stage05 — 正式骨骼资产与可编辑体型内核

本文对应 05.1–05.8 范围。`Saved/NativeBuild/stage05-kernel-acceptance.json` 是补齐眼睑前、六参数配方的完整验收记录；新八参数配方的专项证据见下方“眼睑 Morph 补齐”。两份记录对应不同资产目录，不可混用。

## 入口与产物

重启编辑器后，在「窗口 → VaM 资源浏览器」完成当前人物解码、来源材质解析，点击「生成 UE 人物资产」。临时预览入口保持独立。

构建窗口提供项目 Content 目标目录、显式 Morph 集 JSON、阶段进度、取消及重导入。计算在独立 UE 进程中执行，不阻塞当前编辑器。保存前支持协作取消；进入保存和独立重载提交阶段后不接受取消。异常写入该任务的 build.log/status.json。操作系统文件锁阻止同一插件数据目录的并行构建。

默认 Morph 集是 `Config/Stage05MorphSet.json`：FBMBodySize、PBMWaistWidth、PHMNoseWidth，以及表情用的 PHMEyesClosedL、PHMEyesClosedR；初始值都是 0。左右闭眼是有真实顶点位移的来源 Morph；零 delta 的 CTRLEyesClosed 控制器不冒充几何目标。可以选择自己的集合文件，builtin 项由名称和来源性别验证，catalog_resources 项使用资源浏览器目录 ID；每次最多显式选 32 项，不遍历解码整库。该集合产生独立 `Saved/MorphSets/<SHA256>.json` 锁定计划，不改原 Appearance 的锁定计划。重复选择、身份/性别/来源哈希不符会拒绝。

每个人物的版本化目录包含：

- SK_Body、独立 SK_Part_*、USkeleton、真实 UMorphTarget；SK_Body 只含身体，不是整套服装和头发。
- CD_Character：运行时人物定义、身体/部件引用、参数和值域、来源与限制。
- SD_Shape：中性局部绑定、可编辑集合身份、骨骼公式分类与几何契约引用。
- AP_Imported：导入时的绝对参数预设。
- GD_Bindings：版本化拓扑、来源/输入/最终渲染顶点映射、三角形区域、ClothGeometryData、原头发数据。
- DA_SourceMapping：完整来源 IR、材质语义、构建配方、原始 TriAx/bulge、误差和部件对应，供编辑器追溯。
- Materials：持久化参考材质与贴图，不引用 transient 或 Saved 贴图路径。
- BP_VamCharacter：引用该 CD 的可放置原生运行时宿主。

插件 Content 另有通用空宿主 `/VamResourceBrowser/Blueprints/BP_VamCharacter`，以及完全原创的 `/VamResourceBrowser/Examples/Stage05` 双骨骼面板样例。样例不是用户人物，也不是人物转换的替代品。

## 05.1 实际基线和模块边界

已复用 Stage03/04 的轴/单位、绕序、UV seam、graft Boundary、SkinWrap、材质透明/分区修复。浏览器临时预览仍是 DynamicMesh；正式构建独立生成 USkeletalMesh。已存在的 p0 烘焙外观不能再次作为 X0 叠加。

VamResourceBrowser 是 Editor 模块；VamCharacterRuntime 只依赖 Core/CoreUObject/Engine。运行时装配、参数事务和测试面板都在 Runtime，人物装载不执行 Python、UnityPy、VAR 读取或 HTTP 请求。CharacterDefinition/Shape/Appearance/Geometry 的类均可在非 Editor 环境加载。

## 05.3–05.4 形状、骨架和 Morph

身体的支持语义为 `X(p) = X0 + Σ p_i Δ_i`。正式 SK_Body 保存导入外观 `X(p0)`，运行时只设置 `p_i - p0_i`。X0、真实 delta、p0、原范围/分组/公式分别保存，归零与恢复导入预设是不同操作。

Graft Boundary 先按来源域传播；重复顶点 delta 按来源累加规则处理，UV/材质拆点复制相应 delta。不能线性表示的来源操作显式分类/拒绝或作为受限部件保存，不包装成 UMorphTarget。

Skeleton 从真实 Transform 父链构建，父先子后。保留 DAZ parent、Transform parent、稳定 source_object ID、来源朝向和旋转顺序。世界旋转经过完整四元数基变换，再求父相对局部绑定；米转换为厘米，保存 world bind/inverse bind。多根来源森林可以添加有记录的单位根，非单位来源 Transform 缩放当前拒绝，不猜绑定矩阵。

BoneCenter 的同一 Morph 同轴重复公式采用来源替换语义，不同 Morph 叠加；执行 parentForMorphOffsets。运行时每实例从 neutral_local_bind 加权求新参考姿态，通过 UE SetRefPoseOverride 同时更新该组件的参考姿态及 inverse bind。自有轻量 AnimInstance 让姿态实际求值，而不是只修改数组。共享 USkeleton/USkeletalMesh 不被写入。

GeneralScale、ScaleXYZ、OrientationXYZ、RotationXYZ 等未执行公式逐项保留原语义和诊断，缺少来源骨骼的目标另列；不宣称它们是顶点 Morph。参数分为 Shape、Expression、Pose 或 Unclassified，未知项不靠名称臆测。

构建后以最终 render-to-input 映射再次验证顶点、UV、每槽三角形数、Morph delta 和 skin influences。允许的单分量数值误差：位置 0.00002 cm，Morph 0.0001 cm，UV 0.000001；最多 8 个影响的 16 位量化/归一化误差 8/65535。UE MorphThresholdPosition 设置为 0.000001 cm，避免细微服装 Morph 被默认 0.015 cm 阈值剔除。阈值不是造型误差门槛。

## 05.5 蒙皮的证据和边界

按照真实 `_useGeneralWeights` 分支判断是否使用 generalWeights；不能只看 `_hasGeneralWeights`。fullyWeightedVertices 属于来源 TriAx 分支，不能重复叠加到 general 权重。

TriAx 适配使用可追溯源码数学移植的单关节轴旋转/bulge 参考，拟合非负、和为 1、最多 8 影响的 LBS。训练 ±15/±30 度，留出 ±22 度验证，保存逐关节及整体 RMS/P95/max。不是 XYZ 简单平均。`Config/Stage05Quality.json` 明确来源、姿态与门槛，超限拒绝人物入库。

本机 CAT29 数学参考误差约 RMS 0.1733 cm、P95 0.3675 cm、最大 1.6894 cm。**这不是 VaM 运行时捕获真值；复合姿态、关节修正与动态等价仍标记待校准。** 最新 Stage05 规格允许此类诚实的待校准适配，不把它隐藏成“无损转换”。不通过增加物理掩盖基础蒙皮问题。

身体骨架每个配方独立生成；部件只有层级、名称、父链、完整局部绑定均相等才共享它，不对其他人物的 Skeleton 执行合并。

## 05.6 部件、来源与后续接口

服装/附件保留来源网格、SkinWrap 三角对应、静态贴合、权重传递和原始/构建后映射。源材质区域由实际 DAZCharacterTextureControl 槽映射给出 face/torso/limbs/genitals；未知精细解剖区域保持未分配，不永久写入坐标阈值猜测。

CAT29 的领结、裙子非线性贴合保留准确 p0 形状和原数据，支持骨骼跟随但不声称任意体型联动；其他可可靠部件传递真实 Morph。变化通知保守地标记相关部件，使后续 Cloth/贴合系统可重建。比较“已蒙皮衣服”和“对已蒙皮身体做静态 SkinWrap”的数值差异单独存证；这不是衣服物理。

原头发曲线、发根、头皮绑定、分组和参数保留在 GD_Bindings/SourceMapping。正式资产不保存旧交叉调试发片，Groom 在 Stage10，因此正式 BP 暂不显示来源长发。这是阶段范围，不是遗漏导入。

## 05.7 Blueprint 形状事务

Character 组件接口：GetShapeState、PreviewParameters、SetParameter、CommitShape、CancelShape、ResetToBaseShape、ResetToImportedAppearance、OnShapeChanged。

整批参数先校验再改写：未知 ID/NaN 拒绝整批；有效参数按保存值域限制。预览更新外观，Commit 发布定型状态，Cancel 恢复最近提交。OnShapeChanged 返回参数、来源区域、骨骼、保守部件集合及单调 ShapeRevision。

GetCharacterState 区分 CommittedShape、PreviewShape、当前组件空间 Pose。Stage05 未启用仿真时 bHasSimulation=false、SimulationShapeRevision=-1、SimulatedSurface 为空；不会把摆姿或未来求解器输出保存成捏人基线。

`L_ShapeValidation` 是两个共享资产、独立状态的实例和原生 HUD。点击条形参数可预览，Commit/Cancel/Base/Imported/Next 分别提交、取消、归零、恢复导入、换实例。不依赖 Level Blueprint。运行时只改组件 Morph/参考绑定，不改共享材料或 SkeletalMesh。

## 05.8 提交、重导入与分发

逻辑来源身份与内容配方分离；配方含形状、骨架、skin、Morph、部件、材质、转换版本和目标目录。相同配方复用同一目录。保存后由独立 UE 进程重新加载，核对原生类型/引用/映射并记录全部资产文件 SHA-256，成功后才原子更新 Stage02 ImportState。

重导入检测到资产文件被用户改动就拒绝覆盖；用户派生/override 资产应保存在自己的目录。配方变更产生新版本目录，不自动替换旧场景实例。保存过程中断留下的未提交目录也拒绝覆盖，避免误删用户资产。预提交取消不发布清单。

Build.ps1 复制 Runtime/Editor/Content、阶段文档、配置及顶层 Python 脚本，经 UAT BuildPlugin 输出；不带 Saved、用户 VAR、锁、索引、解码缓存或 __pycache__。-NoInstall 仅生成包，不替换当前编辑器 DLL。更新 C++ 后必须保存当前工作并重启用户编辑器；构建测试不会强制关闭用户编辑器。

## 验收记录

完成结果和固定资产路径由本轮 `stage05-kernel-acceptance.json` 提供；验收必须同时覆盖独立重载、22 项来源/形状回归、真实 UE 两实例参数事务、最终渲染域映射、原生动画驱动、取消/重导入保护、UAT 包内容、干净工程安装及 Windows Cook/运行。仅编译通过或静态截图不算完成。

运行时测试的 CPU 顶点探针只在 Editor 验收构建执行，不进入正常形状更新路径。Cooked 构建检查求值后的关节和独立状态，并保存 Imported/Base GPU 截图进行外观检查；不将 Cook 后被剥离的 Morph 源数据当成可读取的 CPU 动态表面。

## 本轮固定验收资产

人物目录是 `/Game/VamCharacters/C_c6e9958caee9598900461d95`：88 根真实来源骨骼、6 个参数、13 个独立部件。打开此目录的 `L_ShapeValidation` 并 Play，可以操作双实例测试面板。`BP_VamCharacter` 可以从同目录拖入其他关卡；不要用早期 `/Game/VamStage05Tests/BP_TestPanel` 代替人物。

目前这套 6 参数包括导入的 Body/Head/Genital 三项，以及显式选择、默认值为零的 Body Size/Waist Width/Nose Width。点击参数条预览，Cancel 回到上次 Commit，Base 得到基础形状，Imported 恢复来源外观。领结/裙子属于已标记的非线性部件，归零或大幅改体型时不保证重新贴合；完整动态服装适配属于后续阶段。

`A_ShoulderValidation` 是原创 22 度肩部测试动画。在动画编辑器预览，肩以下骨骼应运动；它不是 VaM 动作真值。原生运行验证测得末端位移约 22.35 cm，13 个部件的跟随骨骼误差为 0，另一实例位移为 0。

材质持久化同时保存 SkeletalMesh 和 MorphTargets 使用标记，防止参数变化后回退灰模。形状切换会刷新实例动画缓存，测试包含同一帧 Base→Commit→Imported→Cancel；实际顶点恢复误差为 0。形状求值不写共享骨骼或其他实例。

## 眼睑 Morph 补齐（Stage06 反馈）

来源女性目录中，`PHMEyesClosedL` 与 `PHMEyesClosedR` 各有 506 条实际顶点位移；`CTRLEyesClosed` 是零顶点位移的控制器。原默认 Morph 集只选身体尺寸、腰宽和鼻宽，导致六参数正式网格缺少可见闭眼驱动。这是默认导入配方遗漏，不是来源网格没有眼睑数据。

现已将左右来源闭眼 Morph 加入默认显式集合，归入 Expression，初始权重为 0，范围为 0–1。新的校准合同对每侧得到 519 个非零构建顶点（含 UV/材质拆点），最大位移约 1.18 cm。按重导入保护规则，生成新的正式目录 `/Game/VamCharacters/C_1facd7a930a8e1eca11a18a6`，保留旧六参数资产不覆盖。独立重载要求左右目标同时存在；`stage05-blink-check.json` 检查两实例权重到 1、归零及互不污染。`stage05-blink-acceptance.json` 汇总 86 个资产指纹、来源位移和 Cooked 眨眼运行证据。

闭眼外观复核：这两个来源 Morph 并非上下眼睑等量移动。校准顶点在 UE 竖直方向的最大下移为 1.157 cm，最大上移为 0.251 cm，左右一致；上眼睑主导的最大位移约为下眼睑的 4.61 倍。该值是来源形变的位移上限，不等于沿整个眼裂的局部接触比例，也不能单靠它证明主观外观自然。`Eyes Closed` 保持来源原样，权重 1 对应完整闭眼表情；日常自然眨眼若需要减少下眼睑上提，应另用上、下眼睑独立控制并验证闭合处无裂缝或穿插，不能直接削弱此 Morph 的下眼睑顶点。

新目录的专项眼睑与独立重载已通过；原 `stage05-kernel-acceptance.json` 的完整 05.1–05.8 测试矩阵只证明旧六参数资产，不自动证明新目录全部验收项。

分发声明将 PythonScriptPlugin/GeometryScripting 限定为 Editor。BuildPlugin 会移除 EnabledByDefault，因此 Build.ps1 在输出包恢复显式 false，保证纯 Blueprint 干净宿主打包时生成并链接包含 Runtime 的原生目标，而不是误用不包含插件代码的 stock UnrealGame。

最终是否通过以 `Saved/NativeBuild/stage05-kernel-acceptance.json` 为准。验收脚本同时要求最新资产的独立重新加载、真实图形 Standalone 与 Cooked 运行成功、分发文件一致、编辑器动画跟随、事务、取消和重导入保护通过；旧阶段报告不参与完成判定。

## 自动骨骼映射与第三人称动作

正式人物构建时，插件读取已校准来源骨架的真实父链和绑定位置，自动建立 `Animations/IK_Vam`。如果工程具有 `Config/RetargetSource.json` 指定的第三人称 Quinn 网格和动作，还会建立 `IK_Quinn`、`RTG_QuinnToVam`，并输出 `A_VamIdle`、`A_VamWalkForward`、`A_VamJogForward`。输出动作直接引用该人物的 USkeleton；不再使用 UE 自动识别 Daz 骨架，也不把零长度手臂链作为成功。重定向链覆盖脊柱、双臂、双腿和头部；每条链检查来源祖先关系、绑定长度和导出动作的多时刻直立姿态。保存后的独立编辑器进程重新加载并验证，才更新新的导入清单。

现有正式人物通过 `ue_native_retarget_retrofit.py` 增量补建，独立 `ue_native_retarget_verify.py` 验证，结果保存在 `Saved/NativeBuild/retarget-adapters.json`。补建不改动既有 Stage05 Mesh、Skeleton、材质或用户衍生资产。来源模板缺失时仍生成可验证的 VaM IK Rig，并在报告中写明 `source_missing`；不能把这种状态描述为已生成可用步行动作。未知层级或不符合链跨度的角色会明确失败，不猜测正确骨骼。

这些资产解决骨骼**对应关系及动作重定向**。现阶段人物组件不会自行根据第三人称角色速度切换待机、走路和跑步；把 `BP_VamCharacter` 当作受第三人称角色控制的外观，仍须在角色动画逻辑里使用这些已重定向的动作。`A_ShoulderValidation` 只用于蒙皮验证，不用于日常动作。头发 Groom、动态布料与复杂手指动作也不由此适配器保证。
