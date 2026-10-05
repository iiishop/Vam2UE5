# 阶段 03：真实数据解码与临时几何检查

在已生成的“导入计划”中（允许含缺失或不支持项）点击 **解码并预览**，可以旋转、缩放、隐藏部件、查看线框、骨架和 UV 棋盘。点击 **在 UE5 查看**，在独立的临时 UE 检查工程中建立 DynamicMeshActor；不会向 SmartNPC 的 Content 保存人物 `.uasset`，也不会更新入库状态。

这是静态几何检查，不是完整 VaM 运行时复现。人物使用解码的合并基础网格和预设的直接顶点 Morph；材质按区域检查，衣服和自定义头皮按来源 SkinWrap 静态贴合，发根跟随自定义头皮位移；骨骼中心应用 BoneCenter 公式。三轴皮肤变形、其他 Morph 公式、姿势、布料模拟和原始材质着色尚未执行，界面报告这些限制。骨架在浏览器可叠加检查，UE 中当前为静态 Dynamic Mesh，并非已绑定好的 SkeletalMesh。

## 格式能力矩阵

| 来源 | 已验证布局 | 解码 / 检查 | 预览限制 |
|---|---|---|---|
| VMI | 严格 JSON；numDeltas 与 VMB 对照 | 原参数、未知字段、公式保留 | 公式不执行 |
| VMB | little-endian int32 数量 + N × (int32 顶点索引 + float32 xyz)，总长度严格为 4+16N | 数量、有限数值、非负索引；运行时兼容模式保留重复增量与超域数据 | 直接 delta 可作用于基础网格 |
| VAM | 实际 itemType/uid/displayName 元数据 | 保留原始字段 | 不根据名称推测二进制布局 |
| VAJ | 实际 components 顺序与 storables | 组件顺序控制 VAB 读取 | 未知组件整项失败 |
| VAB DynamicStore | 1.0，.NET 7-bit 字节长度 UTF-8 字符串；little-endian | 严格读取至 EOF | 不容忍未知尾部 |
| DAZMesh | 1.0 | 基础位置、材质名、基础/UV 多边形、UV、复制索引 | 只接受三角形/四边形 |
| DAZSkinWrap + Store | 两层 1.0；按 UV 顶点记录 4 int32 + 6 float32 | 原始绑定记录保留；数量/有限值检查 | 校验目标三角形后静态贴合；不执行厚度、平滑、动态物理 |
| MaterialOptions | 1.0；overrideId + 材质槽数组 | 槽位范围校验 | 不执行 VaM shader |
| ClothGeometryData | 1.0 | UV 三角索引、粒子、双向索引、约束组、邻接 CSR、blend/strength | 不运行物理 |
| RuntimeHairGeometryCreator | 1.0 / 1.1；1.1 增加 rigidities 数组 | 头皮索引、发束、原始顶点、邻接记录 | 以检查用细带显示曲线，不等同 VaM 发丝材质 |
| Unity 人物 | Unity 2018.1.9f1；已记录的类型树签名 | DAZMesh / DAZMergedMesh、DAZBone、DAZSkinV2 / Merged、MorphSubBank | 未登记类型树或其他 Unity 版本拒绝解码 |
| builtin 衣服 / 头发 | 阶段 02 已映射，但其组件解码尚未接入 | 显式 builtin_component_pending | 不会静默省略并声称成功 |

这里列出的版本来自本机 `Assembly-CSharp.dll` 中实际读写方法及实际文件交叉校验，不是根据扩展名猜测。研究用反编译结果只在项目 Saved/FormatResearch；不把它们作为插件源码发布。`Config/DecodeLayouts.json` 仅记录布局签名。

## 中间表示与坐标

- 原始 VaM 数据：米，Y 向上，人物 Z 向前。DAZ 导入到 VaM 的原始缩放/翻转已在源数据中完成，不重复应用。
- UE 检查数据：厘米、Z 向上、X 向前，`UE = 100 × (VaM.z, VaM.x, VaM.y)`；逆变换 `(UE.y,UE.z,UE.x)/100`。
- UV：原始 UV 保留，检查 UV 为 `(u,1-v)`。
- 多边形按 VaM 的实际三角化规则：三角形 `(c,b,a)`，四边形追加 `(a,d,c)`。轴变换为正行列式置换，不额外翻转索引。
- `converted_to_source_vertex` 保留 UV 接缝拆分后的顶点来源；`triangle_to_source_polygon` 保留每个材质区的三角形来源。原始基础/UV 多边形和复制表同样保留。
- 骨骼包含稳定来源对象 ID、名称、父节点、男女位置/朝向和旋转顺序。三轴权重、bulge、generalWeights 等原样保留。三轴权重不是普通 LBS 权重，不以“归一化”掩盖差异。

输出位于插件 `Saved/Decoded/`：

- `<decode_id>.ir.json`：原始基础数据、皮肤/骨骼/部件、来源索引、配置参数、统计、诊断；其 ID 为规范 JSON 内容 SHA-256。
- `<decode_id>.preview.json`：预览几何与转换映射。
- `latest.json`：最近完成的结果；失败或取消不会把新任务写成成功。
- `worker.log`、`ue-preview.log`、`*.ue-result.json`：明确的解码 / UE 加载诊断。

原始资产仅保留在本机集成输出目录，不作为测试 fixture 或插件分发内容。阶段 03 缓存不会写回 VaM，也不会修改阶段 02 入库清单。

## 环境与验证

二进制 VMI/VMB/VAB 解析器使用标准库。Unity 容器解码使用插件私有 `Saved/Python` 中的 `UnityPy==1.25.3`，与 UE 全局 Python 隔离；当前机器已安装该私有依赖。迁移插件时可运行 `SetupDecoder.ps1`。UE 临时窗口仅启用引擎自带 PythonScriptPlugin 和 GeometryScripting，不需要编译新增 C++。

```powershell
& 'I:\Program\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' -m unittest discover -s Plugins/VamResourceBrowser/Tests -p 'test_*.py' -q
```

47 项测试通过。新增测试使用手写三角形、两个小发束和少量权重，覆盖 VAB 每一个截断长度、未知版本/组件、VMB 长度与索引、NaN/Inf、UV/材质映射、权重/骨骼引用及坐标往返。

真实本地样本 `A1X.RUDI.1 / Preset_Rudi.vap`：5 组几何，71,031 个预览顶点，105,491 个三角形，34 个材质区，88 根骨骼，64 个直接 Morph。身体本体为 24,928 个 UV 顶点；服装/饰品为 15,699、2,757、583；头发检查细带为 27,064。UE 5.8.2 的真实 Python commandlet 已成功建立 5 个临时 DynamicMeshActor，返回 `VAM_PREVIEW_READY`，未保存人物资产。

收尾验证：真实样本重复解码的 SHA-256 完全一致（1aae697138f101468775254ac8b0e4e617ff0910167f972fa3734908d6c91107）。GUI 启动已设置 keep_python_script_alive，实际 UE 窗口在建立网格后保持运行。


## 2026-09-22 修复

- 缺失、不支持、单项损坏会保留来源引用诊断并跳过该项，其他几何继续生成；UE 预览不再因 `errors` 非空而禁用。没有任何可用几何仍为失败，部分结果保持 `partial`。
- VMB 在解码层保留全部有限增量。预览遵循已核对的 DAZMorphBank/DAZMesh 行为：累加重复顶点，忽略连接网格 UV 范围之外的增量，UV 复制点最终由基础顶点覆盖。不能将这些索引直接用于合并后网格，否则会误改 graft 区域。
- BoneCenterX/Y/Z 按每个 Morph 的 setter 语义应用，并处理 parentForMorphOffsets；其他公式仍明确报告，不用统一平移猜测修复。
- DAZSkinWrap 以真实绑定的三角形、投影系数构建静态服装/头皮。三角形按 Unity 的材质槽顺序排列，并验证顶点身份。自定义头皮的发根支持基础/UV 两种索引域；目前只跟随位移，未复现旋转和发丝物理。
- 原始发束包含 NaN/Inf 的资源仍拒绝解码，不填零伪造坐标；不影响其他部件。
- 新增 6 项合成数据回归：重复/超域 Morph、graft 保护、骨骼中心 setter、父偏移、SkinWrap 绑定/平移、材质三角顺序，以及缺失+损坏+有效资源混合计划的确定性与部分成功。全套 53 项通过。


### 内置头皮跟随修复

已接入 a_per 中按实际名称定位的内置头皮 DAZMesh，以及 h_zzz_mat 中引用的 DAZSkinWrapStore（包括 SoleilScalp、LeytonScalp、UdaneScalp）。对应类型树签名登记在 DecodeLayouts.json；来源文件按本机目录中的指纹校验并记录进 IR。发根现在跟随该头皮对 Morph 后人物的绑定结果，避免内置头皮停留在基础人物高度。未知头皮或不匹配绑定会跳过该资源并保留诊断，不套用猜测偏移。当前仍是发根位移跟随，尚未实现发束方向旋转、完整骨骼蒙皮和物理。按用户要求本次未运行整套验收，仅做语法检查。

### Morph 子网格索引修复

发现 female_genitalia / male_genitalia Morph 的局部顶点索引曾被误用于身体主网格，导致手脚等部位出现突刺。现根据来源目录区分 Morph 域，使用实际 DAZMergedMesh.graftMesh 与 startGraftVertIndex 映射附加网格；原始增量保留，applied_morphs 记录 vertex_offset。此修复不使用平滑掩盖异常。
