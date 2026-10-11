"""Package actual UE screenshots and recorded S1/S2 results; never marks residual issues passed."""
import base64,csv,hashlib,html,io,json,subprocess,sys
from pathlib import Path
from PIL import Image
repo=Path(__file__).resolve().parents[1];r=repo/'Saved/LowerBodyContact/C1.5-S1-S2';e=repo/'Evidence/LowerBodyS1S2'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
b=read(r/'s1s2-v5/bundle.json');before=read(r/'boundary-baseline.json');after=read(r/'boundary-v5.json')
rows=[]
for pose in ['standing','abducted','suspended-seat','heldout-45-75']:
 for label,data in [('before',before),('after',after)]:
  for x in data['mirror']:
   if x['pose']==pose and x['stage']=='03-corrective':
    s=x['extra_mirror_cm'];rows.append(f"| {pose} / {label} | {s['rms']:.6f} | {s['p95']:.6f} | {s['max']:.6f} | {x['worst_pair']} |")
state=read(r/'main-editor/standing/state.json');audit=Path(state['audit_bundle']);manifest=read(audit/'manifest.json');mapping=read(audit/'mapping.json')
identity={'asset':b['identity'],'configuration':manifest['configuration'],'mapping_path':mapping['path'],'topology_digest':mapping['topology_digest'],'render_vertices':len(mapping['render_to_input']),'output_files':b['output_files'],'source_files':b['source_files'],'verification_code_commit':'67b8104','status':'PARTIAL; not C1.5-5 production acceptance'}
(e/'handoff-v5.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
images=[]
def picture(rel,label,crop=None):
 p=r/rel;im=Image.open(p).convert('RGB');original_size=im.size
 if crop:im=im.crop(crop)
 out=io.BytesIO();im.save(out,format='PNG',optimize=True)
 images.append(dict(source=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),label=label,crop=crop,original_size=original_size))
 return '<figure><a href="'+p.as_uri()+'"><img loading="lazy" src="data:image/png;base64,'+base64.b64encode(out.getvalue()).decode()+'"></a><figcaption>'+html.escape(label)+'</figcaption></figure>'
def gallery(items):return '<div class="evidence-grid">'+''.join(picture(*x) for x in items)+'</div>'
g={}
g['S1GALLERY']=gallery([(f'{folder}/{pose}/{view}-material.png',f'{label} · {pose} · {view}') for pose,view in [('abducted','rear'),('standing','rear'),('suspended-seat','side'),('suspended-seat','rear')] for folder,label in [('baseline-valid-editor','旧资产 / 同有效采集视图'),('main-editor','新资产 / 主工程 Editor')]])
g['S1CROP']=gallery([('baseline-valid-editor/abducted/rear-material.png','旧资产右 Hip 局部（原图像素裁切）',(355,200,475,400)),('main-editor/abducted/rear-material.png','新资产右 Hip 局部：残余折痕仍可见',(355,200,475,400)),('figures/mirror-heatmap.png','数值差异热力图：源 ID 对应，不是 UE 渲染'),('figures/local-sections.png','局部截面数值图：不是 UE 渲染')])
g['S2GALLERY']=gallery([('baseline-editor/knee-120/side-material.png','旧资产 / 原采集路径：环状线'),('baseline-valid-editor/knee-120/side-material.png','同一旧资产 / 有效 streaming view：环状线消失'),('main-editor/knee-120/side-material.png','新资产 / 主工程 Editor'),('main-pie/knee-120/side-world-normal.png','新资产 / 主工程 PIE / World Normal'),('main-pie/knee-120/side-wireframe.png','主工程 PIE / Wireframe'),('main-pie/knee-120/side-section-boundary.png','主工程 PIE / Material Section 标签'),('main-editor/standing/side-material.png','Standing 侧视'),('main-editor/suspended-seat/side-material.png','悬空坐姿侧视'),('main-lighting/knee-120/side-material.png','不同灯光：Yaw 65°'),('main-pie/knee-120/side-base-color.png','主工程 PIE / Base Color'),('main-pie/knee-120/side-unlit.png','主工程 PIE / Unlit'),('main-pie/knee-120/side-gray.png','主工程 PIE / 灰模')])
g['S2CROP']=gallery([('baseline-editor/knee-120/side-material.png','接缝局部 / 原采集',(290,265,460,440)),('main-editor/knee-120/side-material.png','接缝局部 / 新采集',(290,265,460,440))])
g['HELDOUT']=gallery([('v5-editor/heldout-45-75/side-material.png','未参与权重修复调参：45°髋 / 75°膝请求，实际矩阵见 state'),('main-pie/jiggle-motion/rear-material.png','正常 BP / 主工程 PIE / Jiggle 实际运行')])
text='''---
title: C1.5-S1-S2-Final-Report
subtitle: 2026-10-11 · 实际构建与运行证据 · 联合状态 PARTIAL
lang: zh
---
## 1 · 结论与资产身份 {span=2}
**S1 = PARTIAL；S2 = PARTIAL（已复现的截图环状接缝已消除，动态 Contact 路径未验收）；联合 = PARTIAL。不能作为 C1.5-5/6 已验收基础。**

已修复两处真实臀部权重损失，未关闭 Glute Structure、Corrective 或 Leg Jiggle。右侧仍有可见的臀下缘折痕，不能声称异常全部消除，也尚不能将其全部归为正常源体型或 C2 缺少 PSD。用户视觉验收仍待进行。

S2 的既有环状线在旧资产上也能通过恢复有效纹理 streaming view 消除，故根因是诊断采集没有获得足够纹理 mip，不是本次重新生成网格才修好的几何裂缝。最终生成材质另已补齐 UV footprint 元数据，但该元数据修复单独并不足以消除旧采集问题。

- 分支 `fix/allbody`，冻结起点 `96b05ac`，验证代码 `67b8104`；原有未提交 C1.5 工作先完整保留为 `3fc3297`。
- 固定 Preset_QiMeng-b，源 contract `8dc812aa2ab4cfdefb293e47e…`（完整 identity 以原始证据文件为准）。旧配置 identity `c8a07713af49af03e3b8affd27d3141d8b10b86aa1bb91c69a993e63a9334c9b`。
- 新 identity：`NEWIDENTITY`。
- 正常生成 BP：`NEWBLUEPRINT`。最终配置：`NEWCONFIG`。
- 主工程插件已安装，原 DLL/PDB 备份保留在 `install-backup`，哈希见 `installed-binaries.json`。没有覆盖历史审计目录或删除旧角色资产。
- 最终 LOD0 实际渲染顶点 `VERTICES`；体型/Bind/Morph/Profile/拓扑/文件 SHA 见 `handoff-v5.json` 与 bundle。

## 2 · S1 根因、调用链与数值 {span=2}
**确定根因 1：** `ue_runtime_build.py → VamLegJiggleBuilder.cpp → 臀腿 transition 扩展 / 支撑中心拟合` 将已存在的臀辅助骨质量替换成扩散场，并重新分配原始语义权重。源点 341 / 11269 的原始辅助骨总权重约 0.1969 / 0.3984，被旧扩展改成 0 / 0.6012，形成显著侧向偏差。修复为保留原始总量和每个辅助骨下限，只拟合新增部分。

**确定根因 2：** 同文件的 Leg 权重分配为了容纳 8 influences 会截断既有臀辅助骨，再将质量分给幸存辅助骨。修复为只使用剩余槽位，从主骨转移给 Leg；满 8 槽时不破坏原支撑。旧/新同一 post-weld 阶段比较影响 533 个输入渲染点 / 475 个源点，最大单骨权重差 0.108034。早期 701 点统计混入了 Weld 前后差异，已明确作废。

最早可证实的异常发生在 **最终权重生成阶段**，不是必须等 Corrective 才发生。静态辅助骨会将这一权重错误变为姿态形变。源体型本身并非左右镜像；514 对 canonical 对应中 478 对具备最终渲染映射，未按空间最近邻匹配。源体型左右形状差异保留，未复制左侧覆盖右侧。

以下为 **相对于各侧 source LBS 的额外镜像位移**，单位 cm；不是直接 VaM 形态误差，也不要求数值归零。

| 姿态 / 版本 | RMS | P95 | Max | 最差源点对 |
|---|---:|---:|---:|---|
METRICROWS

外展 RMS 下降约 35.6%，Max 下降约 33.1%。镜像动作实际 component 旋转差：大腿 0.02144°、小腿 0.01690°、足 0°；生产限位未改。不能据此排除所有剩余体型/支撑目标差异。

S1GALLERY

S1CROP

## 3 · S2 因果定位与连续性 {span=2}
`VamPoseSuite.h → SceneCapture → StreamingManager`：SceneCapture 无常规玩家视图；先前添加的 override view 在非电影状态下被引擎过滤。`InvestigateTexture` 记录显示 Size=0、Wanted=64×64；改为正常 streaming view 后 Size≈318.15、驻留提升到 256×256，旧资产和新资产的环状线都消失。未更换皮肤像素，没有用材质遮盖几何，也没有全局关闭纹理流送。

`VamStage06AssetEditor.cpp::BuildAffineMaterialStreaming → ue_stage06_materials.py → ue_runtime_build.py reload guard` 补齐实际 UV 运算的 footprint 元数据；支持可验证的仿射 UV 图，遇到不支持图拒绝写入，不以假值通过。

66 对 Legs/Hips 跨 Section 顶点由相同 source ID 证明对应。23 个 Editor 姿态，以及主工程 PIE 的 Jiggle 运动帧：分层 position gap RMS/P95/Max 均 0 cm，bind gap、最终量化权重差、Morph delta 差均 0。归一化 bind normal 夹角最大 0.00000148°。这是 bind normal 指标，不能冒充 GPU 最终法线统计；实际 World Normal、Unlit、Wireframe 等截图另行核查。

原站姿边界相邻三角形二面角最大约 4.28°、knee120 约 4.43°，没有发现与永久环线对应的裂缝或硬折线。姿态局部曲率、逐点权重、UV/Section、Morph 及法线原始数据保留在 A1 和 seam-checks 中。最差几何 gap 为 0，因此不存在非零最差裂缝顶点；完整源点列表可追踪。

**S1 与已复现 S2 没有同一个已证实根因。** S1 是构建阶段支撑权重损失；S2 是截图视图的纹理流送不足。没有无条件 Weld 相邻皮肤。

S2GALLERY

S2CROP

## 4 · 实际构建、Editor / PIE 与测试 {span=2}
- UE 5.8 C++ 模块编译成功；新的 runtime build、独立进程 reload、verify 均成功，bundle 为 committed。
- B1 在最终实际量化权重和渲染拓扑上重放 20 个 authored target：最大误差 **0.000192503 cm**，unsafe triangle=0；权重/辅助骨改变使旧摘要失效的测试通过。
- 11 个 UE 自动测试成功：Breast.Dynamics；Glute.G1.AttachmentDamping / CharacterCalibration / FrameRateStability / Mechanics；Glute.FinalCorrective / PoseFunction / PoseRefinement / RotationBlendReference；Leg.PreserveSupportSlots；Material.AffineStreaming。
- 23 个 Editor 姿态、12 个 PIE 姿态已采集；安装主工程插件后又运行 4 个 Editor 关键姿态、7 个 PIE 状态、替代灯光和旧资产接触对照。所有 process exit=0 **仅代表测试流程结束，不代表被安全检查阻止的接触通过**。
- 10 个共享静态状态 Editor/PIE pre-contact 表面最大差约 5.7e-14 cm。Editor 不执行本测试动态 tick，因此不能把 Editor/PIE 的 Jiggle 动态也宣称相同；PIE 实际 Jiggle 位移最大 1.531998 cm，有限数值，缝合对位置差仍 0。
- A1 原有 replay_deformation_audit.py 重放 Standing、Abducted、独立 45/75 和主工程 PIE Jiggle：各层最大重建误差 0.000016384 cm，小于 0.001 cm。
- `a07 - Body` 的 0.75 / 0.9 / 1.0 × 两姿态，在正常 BP Editor 上共 6 次检查：有限数值、重复点无裂缝。它不是所有可能体型的穷举，也没有另体型 VaM 目标可供比较。
- 临时诊断材质原先在 PIE 缺少 usage flags，现已修正、编译并重拍；最终图使用 main-pie，不采用先前无效的灰模/Section 图。原始失败日志保留。
- **Contact 回归 BLOCKED：** 旧 c8 和新 v5 均在站立/座面姿态触发 `Current pose invalidates contact rest volume`，solver=0。未关闭安全检查，未把开关打开视为求解成功，未推进 D/E 修复。因此不能给出 GPU contact 启用后的无回归结论。

HELDOUT

## 5 · C1.5 源对照与剩余形态 {span=2}
重用已有 C1 原版 VaM 采样、同一体型与 Morph 开关组，转换实际骨骼矩阵，在新正常 BP 重放 24 个状态；矩阵 gate 差为 0，14 区域 × 4 层 × 24 状态得到 1344 条 before/after 统计。使用原 source→render 映射；Unity→UE [z,x,y]×100，没有最近邻替换。没有新增 VaM 插件，也不将组合隐藏 Morph 变化归为单一 Bend Fix。

**并非所有 VaM RMS 都降低：** 髋 90°左臀腿区最终 RMS 2.249→2.431 cm，右侧 2.433→2.517 cm；但右侧 Max 10.443→9.757 cm。髋 60°右侧 Max 6.764→5.869 cm。保留支撑并不意味着更接近 VaM 每个顶点；不能用整体 RMS 掩盖右臀可见折痕。

独立 45/75 未参与权重修复调参，额外镜像 RMS 0.424 cm；未完成对应旧资产的独立姿态 before 重跑，报告不填造 before 值。膝大角度缺少 PSD 与同侧软组织接触仍是后续 C2 / D 需求；但剩余右臀折痕目前尚无充分证据完全归入这些正常需求，S1 保持 PARTIAL。

完整局部 RMS/P95/Max、最差源点以及14区域记录见 regional-comparison.csv；各阶段镜像、边界二面角、截面与图像可以直接核查。膝120名称是请求，实际被限位缩减，比较采用记录矩阵而非名称。

## 6 · Git 与证据入口 {span=2}
| 提交 | 内容 | 推送 |
|---|---|---|
| 3fc3297 | 保留已有 C1.5 工作 | 成功 |
| a4cf746 | 基线冻结与来源证明的 seam attributes | 成功 |
| 9cc1a15 | 保留原始臀部支撑，仅拟合新增过渡权重 | 成功 |
| b9e47fe | 有效 streaming view 与材质 footprint 元数据 | 成功 |
| 3de64ce | 8 influence 槽位保留臀辅助骨 | 成功 |
| 67b8104 | 主工程运行和联合回归证据 | 成功 |

代码：Scripts/ue_runtime_build.py、ue_stage06_materials.py；Private/VamLegJiggleBuilder.cpp、VamStage06AssetEditor.cpp；Tests/VamPoseSuite.h；既有 VamDeformationAudit.cpp 分层导出。未重写无关 Runtime 求解器。

EVIDENCELINKS

## 7 · 交接与验收边界 {span=2}
新资产可在主工程正常拖入 BP_VamCharacter 查看，RC_Runtime 和 FinalCorrective/SK_Body 都在同一 `R_0db007…` 目录；不要用旧 B1 配置冒充新版本。

当前可靠内容：来源映射、连续顶点对应、最终权重/Bind/Morph/Profile/拓扑身份、已修复支撑丢失、B1 一致性。**尚不能交接为“没有右臀额外实现缺陷”的已验收基础**：残余折痕仍需进一步归因和视觉确认；接触开启回归因历史 rest-volume 检查受阻。

C1.5-5 可复用现有数据进行准备，但本次没有推进 LBS+PSD / TriAx-bulge 路线开发。后续重点比较髋前、臀外侧、臀腿过渡及膝前后内外侧；大角度膝形态和真实组织接触留给各自阶段，不通过削弱臀部支撑换取更低整体 RMS。

浏览器自动预览因 file 协议安全策略被拒绝，未尝试绕过；HTML 文件、28张内嵌图像及原图哈希已用文件检查验证，浏览器布局未完成自动视觉复核。

最终没有宣告 S1/S2 联合 PASS。所有截图是实际 UE 正常 BP 输出；数值热图/截面已单独标注，局部放大仅裁切原像素。历史错误、失败构建和先前不正确采集均保留。
'''
text=text.replace('NEWIDENTITY',b['identity']).replace('NEWBLUEPRINT',b['blueprint']).replace('NEWCONFIG',b['configuration']).replace('VERTICES',str(len(mapping['render_to_input']))).replace('METRICROWS','\n'.join(rows))
# Exact source identity, not a hand-entered abbreviated digest.
text=text.replace('8dc812aa2ab4cfdefb293e47e…','8dc812aa2ab4cfdefb293e47b9b6160d47364968ab440497c7cf40a7628cbc55')
links=[]
for label,path in [('构建资产与哈希',r/'s1s2-v5/bundle.json'),('B1 全部20目标',r/'s1s2-v5/authored-targets.json'),('UE 自动测试日志',r/'s1s2-v5/regression-tests.log'),('主工程 Editor',r/'main-editor/suite.json'),('主工程 PIE',r/'main-pie/suite.json'),('C1.5 区域 CSV',r/'c15-regression-v5/regional-comparison.csv'),('镜像分层数值',r/'boundary-v5.json'),('66对 seam逐点数据',r/'seam-checks-v5/abducted.json'),('主工程Jiggle seam',r/'seam-main-pie-jiggle.json'),('冻结基线',r/'baseline-seal.json'),('原始日志与截图根目录',r),('最终交接身份',e/'handoff-v5.json')]:
 assert path.exists(),path
 links.append(f'- [{label}]({path.as_uri()})')
text=text.replace('EVIDENCELINKS','\n'.join(links))
md=e/'C1.5-S1-S2-Final-Report.md';md.write_text(text,encoding='utf-8');out=r/'C1.5-S1-S2-Final-Report.html'
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(md),'--output',str(out),'--no-open'],check=True)
s=out.read_text(encoding='utf-8')
for key,val in g.items():s=s.replace('<p>'+key+'</p>',val)
s=s.replace('</head>','<style>.evidence-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}figure{margin:0;background:#16191e;padding:8px;border-radius:10px}figure img{display:block;width:100%;height:auto}figcaption{color:#dce3ed;font-size:13px;padding:8px}a{overflow-wrap:anywhere}</style></head>')
assert all(k not in s for k in g),'Unexpanded gallery'
out.write_text(s,encoding='utf-8');(e/out.name).write_text(s,encoding='utf-8');(e/'screenshot-provenance.json').write_text(json.dumps(images,indent=2,ensure_ascii=False),encoding='utf-8');print(out,len(s),'characters',len(images),'images')
