"""Publish the R2 report from actual UE captures and measured audit artifacts."""
import base64,hashlib,html,io,json,re,subprocess,sys
from pathlib import Path
from PIL import Image
repo=Path(__file__).resolve().parents[1];r=repo/'Saved/LowerBodyContact/C1.5-S1-S2-R2';old=r.parent/'C1.5-S1-S2';e=repo/'Evidence/LowerBodyS1S2-R2'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
h=read(r/'handoff.json');v=read(r/'surface-validation-summary.json');local=read(r/'local-folds.json');c15=read(r/'c15-summary.json');provenance=[]
assert h['technical_status']['joint']=='PASS' and v['captures']==42

def picture(path,label,crop=None):
 im=Image.open(path).convert('RGB');size=im.size
 if crop:im=im.crop(crop)
 buf=io.BytesIO();im.save(buf,format='PNG',optimize=True)
 provenance.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),label=label,crop=crop,original_size=size))
 return '<figure><a href="'+path.as_uri()+'"><img loading="lazy" src="data:image/png;base64,'+base64.b64encode(buf.getvalue()).decode()+'"></a><figcaption>'+html.escape(label)+'</figcaption></figure>'
def gallery(items):return '<div class="evidence-grid">'+''.join(picture(*x) for x in items)+'</div>'
g={}
g['S1IMAGES']=gallery([(root/pose/(view+'-material.png'),label+' · '+pose+' · '+view) for pose,view in [('abducted','rear'),('standing','rear'),('suspended-seat','side'),('suspended-seat','rear')] for root,label in [(old/'main-editor','修复前 v5'),(r/'main-regression/editor','R2 正常 BP / Editor')]])
g['CROPS']=gallery([(old/'main-editor/abducted/rear-material.png','修复前：右臀原像素裁切',(355,210,480,370)),(r/'main-regression/editor/abducted/rear-material.png','R2：同相机、同源骨骼矩阵裁切',(355,210,480,370))])
g['NORMALPROOF']=gallery([(r/'candidate-4-main-standard/complete/rear-gray.png','独立消融：同一资产，原法线传递'),(r/'candidate-4-main-recompute/complete/rear-gray.png','独立消融：同一资产，UE 原生重算；顶点位置差 0')])
g['S2IMAGES']=gallery([(old/'baseline-editor/knee-120/side-material.png','历史 S2：无有效 streaming view 的环状线'),(old/'baseline-valid-editor/knee-120/side-material.png','同一旧资产：正确 streaming view 后环状线消失'),(r/'main-regression/editor/knee-120/side-material.png','R2 Knee120 · Editor'),(r/'main-regression/pie/knee-120/side-world-normal.png','R2 Knee120 · PIE · World Normal'),(r/'main-regression/pie/knee-120/side-wireframe.png','R2 Knee120 · PIE · Wireframe'),(r/'main-regression/pie/knee-120/side-section-boundary.png','R2 Knee120 · PIE · Section 边界'),(r/'main-regression/pie/knee-120/side-base-color.png','R2 Knee120 · PIE · Base Color'),(r/'main-regression/pie/knee-120/side-unlit.png','R2 Knee120 · PIE · Unlit'),(r/'main-regression/editor/standing/side-material.png','R2 Standing · 侧视'),(r/'main-regression/lighting/knee-120/side-material.png','R2 Knee120 · 不同光照 +65°')])
g['REGRESSIONIMAGES']=gallery([(r/'main-regression/pie/abducted/rear-material.png','正常 BP · PIE · Abducted'),(r/'main-regression/editor/heldout-45-75/side-material.png','独立验证姿态：45°髋 / 75°膝请求；实际矩阵见 state'),(r/'main-regression/pie/jiggle-motion/rear-material.png','PIE：Jiggle 运行'),(r/'right-lighting/abducted/rear-material.png','右侧照明 -65°'),(r/'abducted-lighting/abducted/rear-material.png','相反照明 +65°'),(r/'review-all-editor.png','21 组实际 Editor 后视截图缩略汇总；原图保留')])
g['PLOTS']=gallery([(r/'figures/mirror-heatmap.png','数值诊断图：相对源 LBS 的额外镜像差异，不是 UE 截图'),(r/'figures/local-sections.png','数值诊断图：同一 source-ID 高度带局部截面，不是 UE 截图')])
metric=[]
for name,d in local['regions'].items():
 for ver,label in [('before','修复前'),('after','R2')]:
  z=d[ver]['03-corrective'];a=z['dihedral_deg'];b=z['extra_mirror_cm'];metric.append(f"| {name} / {label} | {a['rms']:.3f} / {a['p95']:.3f} / {a['max']:.3f} | {b['rms']:.3f} / {b['p95']:.3f} / {b['max']:.3f} | {z['worst_edge']} |")
regional=[]
for x in c15:
 a,b=x['before'],x['after'];regional.append(f"| {x['region']} | {a['pose_rms_mean_cm']:.3f} → {b['pose_rms_mean_cm']:.3f} | {a['worst_max_cm']:.3f} → {b['worst_max_cm']:.3f} | {b['worst_pose']} / {b['worst_source']} |")
seals='\n'.join(f"| {k} | `{val}` |" for k,val in h['final_data_seal'].items())
links=[]
for label,path in [('新资产构建及文件哈希',r/'final-bundle.json'),('最终交接身份 / B1 数据封印',r/'handoff.json'),('14项测试完整日志',r/'main-regression/tests.log'),('20个 authored pose',r/'main-regression/authored-targets.json'),('Editor 全套状态',r/'main-regression/editor/suite.json'),('PIE 全套状态',r/'main-regression/pie/suite.json'),('42次表面/接缝汇总',r/'main-regression/surface-validation.json'),('源对应局部折角与镜像差异',r/'local-folds.json'),('C1.5逐姿态/区域/阶段 CSV',r/'c15-replay/regional-comparison.csv'),('C1.5同矩阵检查',r/'c15-replay/comparison-gates.json'),('B2重烘焙字段和安全衰减',r/'c15-replay/fields/detail.json'),('体型变化运行记录',r/'shape-variants/variants.json'),('Contact历史问题单独记录',r/'contact-history-summary.json'),('历史基线哈希检查',r/'historical-baseline-validation.json'),('安装DLL哈希',r/'installed-final-binaries.json')]:
 assert path.exists(),path;links.append(f'- [{label}]({path.as_uri()})')
text=f'''---
title: C1.5-S1-S2-Final-Report · R2
subtitle: 2026-10-12 · 正常 BP · 主工程 Editor / PIE · 实际构建与回归
lang: zh
---
## 1 · 结论与新角色 {{span=2}}
**S1 = PASS；S2 = PASS；本轮 S1/S2 联合技术验收 = PASS。用户最终视觉验收待确认。**

已修复本轮定位出的右臀异常尖折、下缘过渡权重缺陷及法线造成的三角状凹折感；永久大腿接缝在正常渲染下未复现。Glute Structure、Corrective、Breast/Glute/Leg Jiggle 均保留。Contact 的既有 rest-volume 阻塞单独记录，不计入这次 S1/S2 的完成判定，也不宣称 Contact 已通过。

- 新身份：`{h['identity']}`。
- 正常人物：`{h['blueprint']}`。
- Configuration：`{h['configuration']}`。
- 最终 LOD0：{h['render_vertices']} 个渲染顶点；完整 Body / Profile / Bind / Morph / 辅助骨 / 拓扑身份见交接 JSON。
- 请在 UE 中使用上述新 BP 或 RC 进行视觉验收。旧生成资产没有被覆盖；旧角色不会被偷偷替换成新资产。
- {v['captures']} 次主工程采集、{v['image_count']} 张实际 UE 图、14 项原生测试；20 组静态 Editor/PIE 分层表面差均为 0 cm。

## 2 · 最早出现在哪一层 {{span=2}}
| 确定缺陷 | 最早异常阶段 | 实际证据与修复 |
| --- | --- | --- |
| 辅助骨槽位被微小源权重挤掉，中心拟合放大局部不连续 | 02 静态辅助骨 | 源边 19403–19495：源 LBS 4.14°，最终权重参考姿态 4.64°，静态辅助骨 68.44°，Corrective 后 82.69°。Glute 分配改为完整候选后统一压缩；Leg 扩展保留全部五个区域候选直到拟合完成。 |
| 过渡扩展用全区峰值向外传播，制造过强的骨盆支撑带 | 01 最终权重 | 源边 11270–11273 在同矩阵下：Glute 输入/Leg 转移均 11.24°，峰值扩展后 41.52°，中心拟合只能降到 22.14°。改为保留原支撑下限和核心边界的连续扩展，左右核心质量保留比均为 1.0000。 |
| Corrective 法线增量没有对齐网格外法线；蒙皮法线不能完整表达最终权重梯度和多 Morph 叠加 | Morph 法线生成及最终着色 | 原资产 61,748 个三角形叉积与 authored 外法线相反。修正增量方向，并让受下半身辅助骨影响的 5 个分区使用 UE Skin Cache 从最终位置重算法线。 |

涉及生产路径：`VamBreastWeightUtils.h`、`VamGluteStructureBuilder.cpp`、`VamLegJiggleBuilder.cpp`、`VamGluteCorrectiveBuilder.cpp`、`VamMorphNormalUtils.h`、`VamGluteFinalCorrective.cpp`、`VamGluteCorrectiveIdentity.cpp`。先前仅改槽位的 candidate-1 在另一条边恶化到 98.56°，已作为失败证据保留；没有把它交付为修复版。

**法线独立消融：同一 candidate-4、同一主工程、同一实际骨骼矩阵、相机、光照，00–04 阶段位置差全部为 0。仅重算法线就消除了大块三角阴影。** 最终 candidate-5 使用资产内持久化设置，默认 `r.SkinCache.RecomputeTangents=2`，不依赖强制全场景开关。

NORMALPROOF

## 3 · S1 前后图与局部数据 {{span=2}}
S1IMAGES

CROPS

| source-ID 局部区域 | 折角 RMS / P95 / Max（度） | 额外镜像差 RMS / P95 / Max（cm） | 最差几何边 |
| --- | --- | --- | --- |
{chr(10).join(metric)}

局部区域是已定位折痕周围两圈源拓扑，修复算法未使用这些顶点 ID。镜像差扣除各自的源 LBS 变形，不强制 Preset_QiMeng-b 左右几何相同。前后源骨骼矩阵最大差 0，相机相同；10 个腿辅助骨的原始组件矩阵随重新生成的支撑中心变化，不能把“所有辅助骨矩阵逐字相同”作为同姿态条件。

全后侧 478 对源对应点：Standing 最大额外镜像差 0.113→0.095 cm，Abducted 3.088→2.018 cm，Seated 4.566→3.132 cm，未参与修复调参的 45/75 姿态 2.082→1.399 cm。仍保留源体型和已有支撑造成的形态差异，不能把这些数值解读成 VaM 完整表面误差为零。

PLOTS

## 4 · S2 接缝回归 {{span=2}}
历史永久环状线的已证实主因是 SceneCapture 没有提供有效的 texture streaming view。同一个旧资产在有效 streaming view 下环状线消失。来源对应审计没有发现该环线处的几何裂缝；本轮保留原有 UV/材质分区和 source-ID 对应，没有按空间距离无条件 Weld。

42 次 Editor/PIE 采集均检查跨 Legs/Hips 的逻辑对应点：最大位置差 0 cm、权重差 0、Morph delta 差 0，低于 0.01 cm 的位置容差。导出的 bind 法线夹角最大 {v['normal_angle_deg']:.9g}°；这是 bind 属性统计，**不是 GPU 最终法线读回误差**。最终着色同时检查实际 World Normal、Lit、Unlit、Base Color、线框、Section 边界和不同光照截图。

S2IMAGES

## 5 · 构建、运行与独立回归 {{span=2}}
- UE 5.8 C++ 编译成功；资产 build / 独立 reload / verify 全部成功；安装的 3 个 DLL 与构建产物哈希一致。
- 14 项原生测试通过：Breast dynamics/weights、Glute mechanics/frame rate/attachments/calibration/pose/refinement/rotation blend、Leg support slots、material streaming、最终 influence budget、法线方向和 B1 FinalCorrective。
- B1：20 个 authored pose 最大 delta 误差 0.000192504 cm，不安全三角形 0。修改一个量化权重、辅助骨定义或最终法线设置都会使旧封印失效；还原后恢复有效。
- 21 Editor + 21 PIE 姿态，四视角、多种模式；覆盖站立、髋 30/60/90/110、膝 30/60/90/120、深屈曲、外展、镜像/单侧/不对称、坐姿、身体方向变化、Jiggle。请求角度和实际限位后的矩阵均保留；未修改生产限位。
- 20 组无动态状态的 Editor/PIE 00–03 表面最大差 0 cm。Jiggle 状态单独检查有限值和接缝，不把不同动态时序强行判为逐点相同。
- a07 - Body 的 0.75 / 0.9 / 1.0，各站立/Flex60，共 6 次正常 BP 运行和 A1 导出成功，表面有限；未声称这些变体具有新的 VaM 视觉真值。
- 历史基线 29 个文件哈希全部一致。旧报告、失败尝试和原始日志保留。

REGRESSIONIMAGES

## 6 · VaM 同矩阵对照与后续范围 {{span=2}}
C1.5 原有 24 组实际 VaM 骨骼矩阵在新 BP 上完成隔离 A1 重放；不是以 UI 姿态名称配对。前后源骨骼矩阵差 0，source-ID/细分来源映射保持有效，输出 1,344 条逐姿态/区域/阶段记录。B2 字段重新烘焙最大差为 {h['rebake_max_error_cm']:.3g} cm。

下表以冻结 C1.5 基线为“前”，不是上面的 v5 局部折痕基线。平均值只是逐姿态 RMS 的摘要；最大误差、最差姿态和完整 CSV 同时提供，未用全身平均掩盖极值。

| 区域 | 逐姿态 RMS 均值：前→后（cm） | 全姿态最大误差：前→后（cm） | 新版最差姿态 / 源顶点 |
| --- | --- | --- | --- |
{chr(10).join(regional)}

**这不是所有 VaM 表面误差均改善的声明。** 例如部分臀腿区 RMS 仍有增加；本轮保留已认可的 Glute 支撑，未用整体收缩换取更低 RMS。髋前、臀外侧、臀腿交界及膝前后内外侧的 LBS / TriAx-bulge / PSD 差异可交给 C1.5-5 比较。大屈曲髋膝人体形态、组织接触与压缩体积仍分别属于 C2 和后续接触阶段。

Contact 历史复查：正常 BP 的 PIE seat-contact 仍报告 `Current pose invalidates contact rest volume`，solver=0、steps=0、deformer=0。没有绕过安全门槛。**活动 Contact Deformer 的法线/碰撞/体积不在本轮已通过范围内**；它保留独立历史记录，不让 S1/S2 无限延期。本轮也未作新增 GPU 法线重算的游戏帧率承诺。

## 7 · 可复用身份、提交与原始证据 {{span=2}}
| 最终实际数据封印 | 摘要 |
| --- | --- |
{seals}

实现提交均已推送 `fix/allbody`：`0163c26` 首次异常层定位，`2e0250f` 完整候选和最终槽位，`76c5c9a` 连续扩展与法线方向，`0f20d8d` 原生最终表面法线与身份门槛。回归和本报告另作提交，完整 Git 结果随交付列出。

{chr(10).join(links)}

所有人物截图来自真实 UE 正常 BP。局部放大仅裁切原像素；热力图与截面明确标为数值图。HTML 文件和内嵌图像可离线查看；自动浏览器 file 协议预览先前被策略拒绝，本轮未绕过该限制，页面文件和图像来源以文件检查验证。
'''
md=e/'C1.5-S1-S2-Final-Report.md';md.write_text(text,encoding='utf-8');out=r/'C1.5-S1-S2-Final-Report.html';renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(md),'--output',str(out),'--no-open'],check=True)
s=out.read_text(encoding='utf-8')
for k,val in g.items():s=s.replace('<p>'+k+'</p>',val)
assert all(k not in s for k in g)
s=re.sub(r'\[([^\]]+)\]\((file://[^)]+)\)',lambda m:'<a href="'+html.escape(m.group(2),quote=True)+'">'+m.group(1)+'</a>',s)
s=s.replace('</head>','<style>.evidence-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}figure{margin:0;background:#171b20;padding:8px;border-radius:10px}figure img{width:100%;height:auto;display:block}figcaption{font-size:13px;color:#e0e7ef;padding:8px}a{overflow-wrap:anywhere}td{overflow-wrap:anywhere}</style></head>')
out.write_text(s,encoding='utf-8');(e/out.name).write_text(s,encoding='utf-8');(e/'screenshot-provenance.json').write_text(json.dumps(provenance,indent=2,ensure_ascii=False),encoding='utf-8');print('Report',out,'images',len(provenance),'bytes',out.stat().st_size)
