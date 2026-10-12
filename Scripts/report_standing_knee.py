"""Publish controlled UE photographs and retained A1 evidence, without editing history."""
import base64,hashlib,html,json,subprocess,sys
from pathlib import Path
from PIL import Image
import numpy as np

r=Path('Saved/LowerBodyContact/Standing-Knee-Audit').resolve()
b=Path('Saved/LowerBodyContact/C1.5-S1-S2-R2').resolve()
j=json.loads((r/'analysis.json').read_text())
new=r/'standing-light65-retry/standing'
assert (new/'state.json').exists()
assert json.loads((new.parent/'process.json').read_text())['exit_code']==0
new_state=json.loads((new/'state.json').read_text())
old_standing=json.loads((b/'main-regression/editor/standing/state.json').read_text())
assert new_state['actual_bones']==old_standing['actual_bones']
groups=[('相同光照 0°',b/'main-regression/editor/standing',b/'main-regression/editor/knee-120'),
        ('相同光照 +65°',new,b/'main-regression/lighting/knee-120')]
provenance=[];galleries=[];checks=[]
for title,left,right in groups:
    states=[json.loads((p/'state.json').read_text()) for p in [left,right]]
    assert states[0]['actor_class']==states[1]['actor_class']
    assert states[0]['light_yaw_offset']==states[1]['light_yaw_offset']
    assert np.max(abs(np.array(states[0]['cameras'][0]['world_matrix'])-np.array(states[1]['cameras'][0]['world_matrix'])))<1e-8
    figs=[]
    for pose,folder in [('Standing',left),('Knee120（实际约92°）',right)]:
        path=folder/'side-material.png';raw=path.read_bytes();Image.open(path).verify()
        provenance.append(dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest(),state=str(folder/'state.json')))
        figs.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(raw).decode()}"><figcaption>{pose} · {title}</figcaption></figure>')
    galleries.append('<div class="photos">'+''.join(figs)+'</div>')
    # Invariant camera, explicitly bounded posterior skin outline, no global RMS.
    arrays=[np.array(Image.open(p/'side-material.png').convert('RGB')) for p in [left,right]]
    errors=[]
    for y in range(225,310):
        edges=[np.flatnonzero(np.max(im[y,300:450],axis=1)>15) for im in arrays]
        if all(len(e)>0 for e in edges):errors.append(abs(int(edges[0][0])-int(edges[1][0])))
    checks.append(dict(light=title,posterior_outline_max_px=max(errors),sampled_rows=len(errors),roi_pixels=[300,225,450,310],scope='Left visible skin/background boundary at fixed camera, threshold RGB>15. Pixel resolution only, not subpixel geometric accuracy.'))
rows=[]
names={'0_anterior_hip':'左髋前','1_anterior_hip':'右髋前','0_lateral_glute':'左臀外侧','1_lateral_glute':'右臀外侧','0_glute_thigh':'左臀腿过渡','1_glute_thigh':'右臀腿过渡','posterior_fold_region':'既有后臀折痕审计区'}
for name,row in j['layers']['03-corrective'].items():
    d=row['displacement_cm'];rows.append(f"| {names[name]} | {d['count']} | {d['rms']*10:.4f} | {d['p95']*10:.4f} | {d['max']*10:.4f} | {row['worst_source_vertex']} |")
text='''---
title: Standing 与 Knee120：臀部是否真的改变？
subtitle: 同一 R2 正常 BP · 实际矩阵与逐层数据 · 同相机同光照补充验证
lang: zh
---

## 1 · 结论 {span=2}
**没有发现截图所暗示的臀部整体收缩。存在很小的真实局部变化，但它来自源 LBS 拟合权重中原有的小腿影响。** 骨盆、大腿、Glute 辅助骨矩阵相同；体型、Corrective 激活权重相同；Contact 与 Jiggle 关闭。上轮报告将 Standing 的 0° 光照和 Knee120 的 +65° 光照放在相邻卡片，不能据此判断姿态形变。

本次未修改生产蒙皮、未强行清零远端权重、未重新生成不必要的角色。源拟合小权重不能当成人体生理真值，也不能仅凭其很小就证明解剖正确；但本次没有证据支持把它归为 S1/S2 的新增缺陷。需比较真实 VaM/TriAx 表面的拟合残差仍归 C1.5-5。

## 2 · 实际变化和最早阶段 {span=2}
- 请求膝屈曲 120°，实际小腿相对 standing 旋转约左 92.0248°、右 92.0107°，经过生产限位；没有改动限位。
- 仅小腿、脚及其辅助骨改变。髋、骨盆、大腿和臀部辅助骨矩阵不变。
- `00-source-lbs`：本次各 ROI 量化后的源渲染表面变化为零。
- `01-final-weights`：小腿残余权重被保留后出现微小位移，是 A1 中最早可见的阶段。
- `02-static-helpers`：与上一层变化量一致，没有增加膝驱动臀部的影响。
- `03-corrective / 04-jiggle`：激活 Morph 相同；只有同一 Morph 在有小腿权重的点上随蒙皮旋转造成微小数值差。Jiggle 关闭。
- 最终原生渲染截图另外检验，不能把 CPU A1 表面当作 GPU 读回。

## 3 · 源顶点分区统计 {span=2}
复用 B2 region fields，阈值 0.05；按 source ID 去重。数值是 Knee120 与 Standing 的位移，不是 VaM 真值误差。工程 ROI 包含下缘/大腿上部，不等价于纯臀大肌范围。

| 区域 | 源顶点数 | RMS mm | P95 mm | Max mm | 最差源顶点 |
| --- | --- | --- | --- | --- | --- |
'''+ '\n'.join(rows)+'''

不能只看最大值推断整个臀部改变：臀腿过渡区至少 95% 对应点位移为零，极值集中在带有原始小腿权重的下缘。分析 JSON 同时保留各层 RMS/P95/Max 和权重样例。

## 4 · 为什么原始层为零、最终层非零 {span=2}
源 contract 的顶点 6082 带 `lShin=0.00330311`，最终渲染权重为 `0.00329595`；顶点 16802 带 `rShin=0.00369104`，最终为 `0.00369268`。另两个臀腿点 348、11287 原有小腿权重约 0.00123947、0.00212497。

源渲染资产的低精度权重量化丢掉了这些很小的影响；Builder 的 `VamBreastJiggleBuilder.cpp::Extract` 从保留的 MeshDescription 获取权重，而最终资产使用高精度权重。于是它们重新出现在实际渲染权重中。**来源是已存在的 contract 拟合，不是这次谐波扩展把小腿权重扩散进臀部。** 本次所有发生变化的 ROI 源顶点，都在 contract 中找到改变骨骼的原始权重，未发现无源权重的新增联动。

这解释了算法行为，不证明真实人体完全如此。仅屈膝可能涉及多关节肌肉长度变化，但当前这点位移没有肌肉激活或组织耦合证据，不能包装成生物学模拟。

## 5 · 真正可比较的截图 {span=2}
PAIRS

均为真实 UE5 正常 BP、相同相机和同组光照；+65° Standing 为本次新运行，Knee 为冻结历史截图，身份与相机矩阵校验通过，新 Standing 的全部实际骨骼矩阵也与原 Standing 完全一致。两组光照各自的后臀轮廓 85 行扫描最大差均为 0 像素（768px 截图，ROI x=300..449 / y=225..309，RGB>15 阈值；不代表亚像素几何完全相同）。完整结果见 image-checks.json。截图不作光照修图。

## 6 · 交付与限制 {span=2}
- 新增可复用 `Scripts/audit_standing_knee.py`，读取既有 A1/B2 数据，不重建姿态系统。
- 新增同光照 HTML 对照，修正之前视觉比较容易混淆的问题。冻结 R2 报告和历史图像保持原样。
- 本次补拍首次沙箱运行因 DDC 没有可写节点失败；原始日志保留。正常权限重试成功，进程退出码 0，真实 BP 状态与截图已导出。
- 没有生产源码变更，故不声称进行了新 C++ 构建或资产重生成。Contact 历史问题未混入本次结论。
- 角色身份：`2c25b9406af35637a4998f668e0517d77f478360ddeb53ed9c468427297f18b1`。
'''
(r/'report.md').write_text(text,encoding='utf-8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(r/'report.md'),'--output',str(r/'report.html'),'--no-open'],check=True)
s=(r/'report.html').read_text(encoding='utf-8').replace('<p>PAIRS</p>',''.join(galleries)).replace('</head>','<style>.photos{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}figure img{width:100%;display:block}figcaption{padding:8px}td{font-variant-numeric:tabular-nums}</style></head>')
(r/'report.html').write_text(s,encoding='utf-8')
(r/'image-checks.json').write_text(json.dumps(dict(provenance=provenance,outline=checks),indent=2),encoding='utf-8')
print(json.dumps(checks,indent=2))
