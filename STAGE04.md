# Stage 04：SourceMaterialIR 与 UE 来源外观参考

## 使用

在“导入计划 → 解码并预览”完成几何后，点击 **解析来源材质**，再点击 **在 UE5 查看**。如果旧 UE 检查窗口仍开着，先关闭它再加载新结果。浏览器仍显示几何检查颜色；真实贴图参考在 UE 窗口中查看。

材质生成沿用取消按钮、独立工作进程和进度状态，超时上限 600 秒。缺失、损坏、不支持项逐项记录，不阻断其他材质。缓存总量上限 2 GiB。不会写回 VaM，不建立 Canonical/Game Material，也不保存 SmartNPC 人物资产。UE 参考材质和贴图位于独立临时工程的内存包 `/Game/SourceAppearanceReference`，未主动保存。

## 输入与保留

- 输入：Stage02 锁定计划、对应 Stage03 Source IR、预览索引。检查计划/Source IR 内容哈希，检查已有来源指纹；不扫描目录、不按文件名猜贴图关系。
- 人体默认材质来自实际 DAZMergedMesh Material PPtr；Shader、Texture2D 通过锁定 bundle 的 CAB / 对象 ID 引用解析。保留原始对象数据、Shader 属性与各 Pass 状态。
- 人体材质槽来自实际 DAZCharacterMaterialOptions；皮肤分区来自 DAZCharacterTextureControl 的 face/torso/limb/genitalMaterialNums。
- 服装、饰品、自定义头皮的槽位来自 VAB MaterialOptions，材质配置来自实际 VAJ 和 Appearance 的精确 storable ID。动态网格默认 shader 来自 VaM DAZMesh 的实际 shaderNameForDynamicLoad。
- 依赖配置先应用、选中的 Appearance 最后应用。所有配置全文和原始 SHA 保留；空自定义贴图恢复来源默认值，`NULL` 与空字符串区分。循环依赖只处理一次。
- 所有外部图片只能通过锁定计划中的引用边读取，原始字节另外保留，PNG 仅作为解码/UE 参考副本。Unity 压缩贴图保留原始对象与来源 bundle 指纹，解码 PNG 不声称是原压缩数据的无损替代。

## 输出

插件 `Saved/SourceAppearance/<material_id>.materials.json` 包含：

- `materials`：每个 mesh/slot/region 的来源 shader、参数、贴图语义、颜色空间、UV、渲染状态、覆盖层来源、未知字段及引用材质策略。
- `source_configs` / `source_component_definitions`：原始配置及实际组件绑定定义。
- `source_hashes`：实际读取 bundle 的校验指纹。
- `diagnostics`：每个不支持参数的 document/field 或 shader/property，及受影响的 mesh/slot/region。
- `validation_scene`：固定相机、FOV、灯光和曝光设置。

`blobs/` 保存内容寻址的原始贴图、解码 PNG、原始 Unity 材质和 Shader 对象。Stage03 原始 IR 不覆盖，另写 `.appearance.preview.json` 供 UE 加载。

## 参考渲染能力与限制

| 项目 | 当前处理 |
|---|---|
| Diffuse / tint | 来源贴图、HSV/RGBA 颜色与分区绑定 |
| Decal | UE 参考中按贴图 alpha 叠加 |
| UV | 原始 UV/scale/offset 保留；V 翻转后 offset 为 `(ox, 1-sy-oy)` |
| Normal | 外部 RGB 法线作为线性法线图；UE V 翻转参考切线基使用 green flip；不是通用 Shader 精确等价声明 |
| Unity packed normal | 保存解码像素及原对象，报告未还原的通道打包，不当成普通 RGB 法线 |
| Alpha | 保留实际 pass blend/culling 与 tags；支持 opaque/masked/标准 alpha blend、cutoff、alpha adjust、hideMaterial |
| 独立外部透明图 | 按 VaM ImageLoaderThreaded 的灰度平均产生透明度；Unity 内置纹理使用 alpha |
| 双面 | 按来源首个 pass 的 culling 解析；多 pass 原样保留并报告 |
| Gloss/specular/SSS/IBL | 原参数与贴图完整保留，尚无等价 VaM BRDF adapter；UE 参考使用固定 roughness 0.55，逐参数报告 |
| Hair | 保留实际 Sim/材质参数及绑定；Stage03 发束细带与原始发丝密度、各向异性、宽度/curl 不等价 |
| 复杂眼睛/泪膜/折射 | 保留来源材质和透明信息；尚未复现完整 VaM 着色 |

因此当前输出为 **partial 来源外观参考**，不能声称完整像素一致还原。此状态与单项加载错误分开：材质可成功加载，但来源 shader 的部分效果仍不支持。

## 已执行的必要检查

2026-09-22 表面修复：网页拓扑进入 UE 时反转三角形顺序，匹配 GeometryCore 的法线约定；通过来源顶点映射跨材质及 UV 接缝计算平滑法线。眼部透明层识别预乘透明混合，并使用 diffuse alpha 与颜色 alpha；SeparateAlpha 家族继续使用独立透明贴图。旧缓存无需重新解码，重新打开 UE 预览时应用修复。

修复通过 3 个合成拓扑回归检查；最新本地人物在 UE 中创建 10 个组件、46 个参考材质，材质加载错误 0。此为加载检查，尚未做渲染图像一致性验收。

本机真实样本：SourceMaterialIR 生成 42 个槽绑定、116 个有效纹理绑定；UE 5.8.2 Python commandlet 创建 42 个参考材质，材质加载错误 0。固定相机、手动曝光及灯光创建成功。未进行 VaM/UE 逐像素一致性验收。
