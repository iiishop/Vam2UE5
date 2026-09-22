# Stage 04：SourceMaterialIR 与 UE 来源外观参考

## 使用

当前场景集成：编译并启用 VaMResourceBrowser 后，通过 UE 的“窗口 → VaM 资源浏览器”打开面板。预览请求由插件交给当前编辑器执行，替换带有 VamSourcePreview 标签的旧预览人物，使用当前场景灯光；不再启动独立预览工程。人物仍为临时预览，贴图与材质仅在当前编辑器会话内缓存。

加载优化：按贴图内容和颜色语义复用贴图，按材质参数复用材质；相同内容再次预览跳过导入与材质重建。首次加载仍需导入新贴图和编译 shader。半透明材质使用 Surface ForwardShading，改善饰品和薄衣料受光；不承诺完整 VaM 衣料着色等价。

本机同进程检查：27 个组件、148 个材质，首次创建 35.11 秒、缓存复用后 21.18 秒，两次材质加载错误均为 0。这里只计 UE 内预览创建，不含 VaM 解码、来源材质解析及编辑器启动。插件二进制已通过 UE 5.8 编译并安装到本项目，工程中已启用。

在“导入计划 → 解码并预览”完成几何后，点击 **解析来源材质**，再点击 **在 UE5 查看**。浏览器显示几何检查颜色，真实贴图参考显示在当前编辑器场景中。

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

头发预览更新：UE 端将旧导引带转换为沿切线的双面交叉发片，保持中心线与发根，按来源 width 和有上限的 hairMultiplier 覆盖近似生成宽度，向发梢收细，并显示 rootColor/tipColor 渐变。无需重新解码即可生效。此为预览适配，不等同于 VaM 的密度插值、卷曲、各向异性和模拟；头皮及穿衣贴合问题不能由加宽发片消除。

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

后续修复：材质连接现在检查 API 返回值；Clamp、ComponentMask、TextureSample 的首输入使用 UE 实际可识别的空引脚名，修正透明材质编译失败及 UV 变换未连接的问题。来源 renderQueue 保留浮点值，不再因小数字符串中断整个材质配置。使用真实渲染后端执行的本地检查创建了 148 个材质，连接错误 0，日志未出现材质编译失败；此前 null-RHI 加载检查不能代表 shader 编译成功。

人体附加网格加入来源 DAZMergedMesh Boundary 权重传递，保持局部 Morph 并跟随身体接缝移动。该修改需要重新执行几何解码及材质解析。

头发方向调整为 UE 原生 Groom：来源曲线、发根与参数作为输入，独立构建曲线导入、密度及头皮绑定。此入口尚未接通；当前交叉发片和覆盖宽度补偿仅为过渡预览，不是 Groom，也不是 MetaHuman 人物系统。

2026-09-22 表面修复：网页拓扑进入 UE 时反转三角形顺序，匹配 GeometryCore 的法线约定；通过来源顶点映射跨材质及 UV 接缝计算平滑法线。眼部透明层识别预乘透明混合，并使用 diffuse alpha 与颜色 alpha；SeparateAlpha 家族继续使用独立透明贴图。旧缓存无需重新解码，重新打开 UE 预览时应用修复。

修复通过 3 个合成拓扑回归检查；最新本地人物在 UE 中创建 10 个组件、46 个参考材质，材质加载错误 0。此为加载检查，尚未做渲染图像一致性验收。

本机真实样本：SourceMaterialIR 生成 42 个槽绑定、116 个有效纹理绑定；UE 5.8.2 Python commandlet 创建 42 个参考材质，材质加载错误 0。固定相机、手动曝光及灯光创建成功。未进行 VaM/UE 逐像素一致性验收。
