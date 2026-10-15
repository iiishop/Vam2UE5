"""Publish real UE captures and separate source-surface research evidence."""
import base64,csv,hashlib,html,json,subprocess,sys
from pathlib import Path
from PIL import Image
import numpy as np

r=Path('Saved/LowerBodyContact/KneeStructure').resolve(); old=Path('Saved/LowerBodyContact/C1.5-S1-S2-R2/main-regression/editor').resolve()
bundle=json.loads((r/'candidate-2/bundle.json').read_text());metrics=list(csv.DictReader((r/'metrics/source-errors.csv').open()))
runtime=json.loads((r/'runtime-analysis.json').read_text());validation=json.loads((r/'regression/surface-validation.json').read_text())
assert len(runtime)==50 and all(x['relative_normal_reversals']==0 for x in runtime)
assert all(json.loads((r/'main-extra'/w/'process.json').read_text())['exit_code']==0 for w in ['editor','pie'])
images=[]
def photo(p,label):
 raw=p.read_bytes();Image.open(p).verify();images.append(dict(path=str(p),sha256=hashlib.sha256(raw).hexdigest()))
 return '<figure><a href="'+html.escape(p.as_uri())+'"><img loading="lazy" src="data:image/png;base64,'+base64.b64encode(raw).decode()+'"></a><figcaption>'+html.escape(label)+'</figcaption></figure>'
gallery=[]
for pose,label in [('standing','Standing'),('knee-90','Knee90 请求角'),('knee-120','Knee120 请求角（生产限位缩减）'),('suspended-seat','悬空坐姿')]:
 a,b=old/pose,r/'regression/editor'/pose
 sa,sb=[json.loads((p/'state.json').read_text()) for p in [a,b]]
 # Same primary pose and view: do not use lighting changes to imply geometry fixes.
 assert sa['cameras']==sb['cameras'] and sa['light_yaw_offset']==sb['light_yaw_offset']
 assert all(np.max(np.abs(np.array(x['component_matrix'])-np.array(y['component_matrix'])))<1.e-7 for x,y in zip(sa['actual_bones'][:88],sb['actual_bones'][:88]))
 gallery.append('<div class="photos">'+photo(a/'side-gray.png',label+' · 修复前 · UE 灰模')+photo(b/'side-gray.png',label+' · 修复后 · UE 灰模')+'</div>')
table=[]
for pose in ['knee30','knee60','knee90','knee120','seated']:
 for region,label in [('0_knee_anterior','左膝前'),('0_knee_posterior','左膝后')]:
  pair=[next(x for x in metrics if x['pose']==pose and x['mode']=='off' and x['stage']=='03-corrective' and x['region']==region and x['version']==v) for v in ['before','after']]
  table.append('| '+pose+' | '+label+' | '+' | '.join(f"{float(x[k]):.3f}" for x in pair for k in ['rms_cm','p95_cm','max_cm'])+' |')
jiggle=[]
for x in runtime:
 if x['suite']=='main-extra' and x['world']=='pie' and x['jiggle']:
  jiggle.append('| '+x['pose']+' | '+' | '.join(f"{v['max_cm']:.3f}" for v in x['dynamic'].values())+' |')
maxgap=max(v['position_gap_cm']['max'] for x in validation['captures'] for v in x['statistics'].values())
worlderror=max(v['max_cm'] for p in validation['editor_pie'] for v in p['stages'].values())
text=f'''---
title: 膝部结构修复：源蒙皮算子与真实 UE 验证
subtitle: 正常 BP_VamCharacter · VaM 同矩阵对照 · 大小腿 Jiggle 保留
lang: zh
---

## 1 · 已确认的根因 {{span=2}}
膝部的大幅塌陷在 **源 contract LBS** 层已经存在，最终辅助骨与 Jiggle 不是首次产生问题的层。原导入路线用 ±15°/±30° 的样本拟合 LBS，并以 ±22° 留出验证；源 DAZSkinV2 的逐轴加权角度旋转及 bulge 行为不能由这一组固定线性权重在大角度下保持。

恢复源膝关节算子的第一轮结构修复已经实现、构建并在 Editor/PIE 实跑。膝前轮廓和关节塌陷明显改善；**不宣称完整的人体膝部、深屈膝后侧与软组织接触已经完成。** 120° 膝后残差仍在下表公开记录。

## 2 · 正常弯膝应如何理解 {{span=2}}
弯膝既涉及胫股关节相对运动，也涉及髌股运动和受载状态；固定铰链不能完整描述人体。动态 MRI 研究测量到负载相关的胫股与髌股运动：[原始研究](https://pmc.ncbi.nlm.nih.gov/articles/PMC3759229/)。本次没有足够的该角色骨骼证据去编造髌骨位移或改变骨骼中心。

技术上，旋转混合用于处理线性蒙皮塌陷已有成熟研究：[Dual Quaternion Skinning](https://users.cs.utah.edu/~ladislav/dq/index.html)。VaM 的 [Better Bends 作者示例](https://hub.virtamate.com/resources/better-bends.57609/) 使用姿态驱动的雕刻修形，并强调体型和权重适配；它是插件，不等于 VaM 原版真值。这里优先恢复当前角色确实丢失的源算子，再保留真实残差供后续解剖修形使用。

## 3 · 实际修改 {{span=2}}
1. 从导入 IR 保留当前角色的左右 shin 逐轴权重、bulge 因子、源摘要；没有硬编码 QiMeng 的目标顶点。
2. 在所有辅助骨、最终量化权重、渲染拓扑确定后，计算源加权角度旋转相对于最终 LBS 的残差，并反变换为原生 Morph。
3. 运行时使用实际局部旋转相对体型参考的有符号膝屈曲分量驱动邻近基形。中性基是显式零，不靠虚假顶点位移保活。
4. 使用 UE 原生 Morph/GPU 渲染链路；保留源 ID 对应、最终数据摘要与失效检查。A1 把新修形归入 03-corrective，不混入源 LBS 层。
5. 原来的 Glute Structure、Corrective、Leg Jiggle 保留。没有修改肌肉刚度、阻尼或骨骼限位来掩盖静态塌陷。

采样域为 -15°～150°，15° 间隔；这是源算子的近似重建，非完整 TriAx 多轴实现。超出域会钳制。现有安全衰减保留：深屈膝的一些局部目标受限制，不能据此宣称完整复现了源网格。

## 4 · VaM 同姿态数值对照 {{span=2}}
24 组原版 core-on/core-off 重放均使用同一真实骨骼矩阵、源捕获和 request ID；修复前后矩阵最大差 0。以下展示 **core-off 原始表面**，单位 cm。CSV 同时保留两种状态、双侧和四个膝部子区域。没有把组合自动 Morph 的变化归给单一 Morph。

| 实际源姿态 | 区域 | 前 RMS | 前 P95 | 前 Max | 后 RMS | 后 P95 | 后 Max |
|---|---|---|---|---|---|---|---|
{chr(10).join(table)}

源中性站立的修形差异小于 3e-8 cm；00/01/02 层修复前后完全不变。原来小腿拟合权重对臀外侧的少量牵动也被源残差纠正：右臀外侧真实 knee120 的最大源误差从 0.2421 cm 降至 0.00225 cm。复合深屈膝的非膝区域 RMS 最大回退约 0.00050 cm，未以扩大整个臀部来换取低误差。

SECTIONPLOT

截面图来自保留源 ID 的表面数据；**这是几何分析图，不是 UE 截图或 VaM 屏幕截图**。没有最近邻配准。各层无对应的源 ID 列在 coverage.json，前后覆盖一致。

## 5 · 同姿态、同相机的真实 UE 截图 {{span=2}}
PAIRS

截图中的 Knee120 是生产请求名称，不是实际 120°；限位保持原样。它与上表来自真实 VaM 120° 的隔离几何审计分开。驱动的实际参考相对角与“腿骨方向夹角”也不是同一数值；全部矩阵及激活 Morph 保存在 state.json/A1 中。

## 6 · 大小腿仍然会 Jiggle {{span=2}}
主工程 PIE 对角色施加既有周期位移刺激，测量 03-corrective → 04-jiggle 的真实区域表面位移。下列为辅助骨影响大于 0.2 的区域最大位移，cm；不是新设置的振幅参数。

| 姿态 | 左大腿 | 左小腿 | 右大腿 | 右小腿 |
|---|---|---|---|---|
{chr(10).join(jiggle)}

EXTRAS

## 7 · 验证与资产 {{span=2}}
- C++ 编译成功；新资产构建、独立进程重新加载及验证通过。
- 14 项既有 Breast/Glute/Leg/权重/法线/B1 测试通过；新增 Knee.SourceOperatorDriver 测试通过。
- Host 正常 BP：21 姿态 × Editor/PIE；主工程正常 BP：4 附加姿态 × Editor/PIE。另保留不同光照 Knee120。
- 50 个核心/附加捕获的几何有限值有效，相对于修形前同姿态的三角形法线翻转为 0。这个局部检测不是全身自交证明。
- 原 42 个回归捕获的源 ID 接缝位置最大差 {maxgap:.8f} cm；静态 Editor/PIE 分层表面最大差 {worlderror:.8f} cm。真实法线图另行保存。
- 新模块已安装到主工程；安装前版本有备份，DLL 哈希与 Host 一致。
- 代码提交 `49f60e4` 已推送。首轮构建因 UE 丢弃零 Morph 失败，改为显式零基后重新生成，失败日志没有删除。
- 资产身份：`{bundle['identity']}`。
- RuntimeConfiguration：`{bundle['configuration']}`。
- 正常人物：`{bundle['blueprint']}`。

## 8 · 仍需处理的边界 {{span=2}}
**深屈膝后侧尚未完成完整解剖验收。** 已知受安全衰减和当前单轴近似影响，但没有把全部剩余误差武断归为同一原因。当前方案没有恢复全套多轴 TriAx、髌骨轨迹、肌腱/主动肌肉模型或大小腿软组织挤压。

Contact 历史 rest-volume 失败独立记录，本轮姿态测试明确关闭 Contact；没有宣称这次验证了接触求解。用户可以在正常 BP 上检查外形和动态效果，后续应依据局部残差继续改进膝后及组合姿态，不能用增大 Jiggle 或灯光掩盖它们。

## 9 · 可复核资料 {{span=2}}
- `metrics/source-errors.csv`：双侧、分阶段、core-on/off 的 RMS/P95/Max 与最差源点。
- `metrics/stage-changes.csv`、`gates.json`、`coverage.json`：层间变化、矩阵和映射门槛。
- `runtime-analysis.json`：每个姿态的激活膝 Morph、Jiggle 位移及三角形检查。
- `regression/surface-validation.json`、`regression/authored-targets.json`：接缝、Editor/PIE 和 B1 最终实际权重验证。
- `candidate-2/bundle.json`、`install.json`：完整生成身份、模块安装哈希。
- `replay/ue-replays.json`：真实 VaM 捕获与本次 A1 bundle 对应路径。
'''
(r/'report.md').write_text(text,encoding='utf8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(r/'report.md'),'--output',str(r/'report.html'),'--no-open'],check=True)
extras='<div class="photos">'+photo(r/'main-extra/pie/knee-75-heldout/side-material.png','主工程 PIE · 独立 75° 请求角')+photo(r/'regression/pie/knee-120/side-world-normal.png','PIE · World Normal 原始截图')+'</div>'
s=(r/'report.html').read_text(encoding='utf8').replace('<p>PAIRS</p>',''.join(gallery)).replace('<p>SECTIONPLOT</p>',photo(r/'source-sections.png','真实表面的几何截面分析')).replace('<p>EXTRAS</p>',extras)
s=s.replace('</head>','<style>.photos{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px}figure{margin:0}figure img{width:100%;display:block}figcaption{padding:8px}td{font-variant-numeric:tabular-nums}</style></head>')
(r/'report.html').write_text(s,encoding='utf8');(r/'image-provenance.json').write_text(json.dumps(images,indent=2))
print(r/'report.html')
