import json,subprocess,shutil,gzip,hashlib,struct,re
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=Path(__file__).resolve().parent;out=r.parents[2]/'Evidence/LegResearch';out.mkdir(exist_ok=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
decay=read(r/'decay.json');bundle=read(r/'v1/bundle.json');motion=read(r/'motion-analysis.json');seam=read(r/'dynamic-seam.json')
fig,axes=plt.subplots(1,2,figsize=(11,3.6),layout='constrained')
for ax,case in zip(axes,['hard-stop','seated-stop']):
 baseline=read(r.parent/'LegProduction/after-v3'/case/'video.json')['samples']
 bt=np.array([v['time_seconds'] for v in baseline]);bv=np.array([v['leg_pipeline'][2]['nodes'][1]['raw_cm'] for v in baseline]);bv-=bv[-15:].mean(axis=0)
 axis=int(np.argmax(np.max(np.abs(bv[(bt>=3)&(bt<=4.3)]),axis=0)))
 for label,folder in [('Before',r.parent/'LegProduction/after-v3'),('After',r/'runtime')]:
  s=read(folder/case/'video.json')['samples'];t=np.array([v['time_seconds'] for v in s]);x=np.array([v['leg_pipeline'][2]['nodes'][1]['raw_cm'][axis] for v in s]);x=(x-x[-15:].mean())*10;sel=(t>=3)&(t<=4.3);ax.plot(t[sel]-3,x[sel],label=label)
 ax.set(title=case+' / right hamstrings / local '+'XYZ'[axis],xlabel='Time after stop (s)',ylabel='Local displacement (mm)');ax.grid(alpha=.2);ax.legend()
fig.savefig(r/'ring-down.png',dpi=160);plt.close(fig)
rows=''
for case,title in [('hard-stop','站立急停'),('seated-stop','坐姿急停')]:
 for segment,name in [('R_Thigh','右大腿'),('R_Calf','右小腿')]:
  a=decay['before'][case][segment]['last_above_ten_percent_seconds_after_stop'];b=decay['after'][case][segment]['last_above_ten_percent_seconds_after_stop'];rows+=f'| {title} · {name} | {a:.3f} s | {b:.3f} s |\n'
md=f'''---
title: 腿部 Jiggle：论文参考与小幅阻尼调整
subtitle: 2026-10-14 · 左侧为上一版，右侧为本轮微调 · 真实 UE 运动对照
lang: zh
---

## A 本轮做了什么

保留你已认可的腿部形态、连续权重和姿态平衡方案，仅增加默认阻尼及重置值。大腿 0.30 → **0.40**；小腿 0.40 → **0.55**。振幅仍为 3，支撑仍为 0.85，频率、区域节点、被动张力和耦合不变。

这是参考研究方向后进行的工程微调，不是人体材料参数实测拟合。没有新增主动肌肉激活模型，也没有用增大刚度压住所有晃动。

## B 论文提供的依据

- **Wakeling & Nigg，2001**：腿部振动频率、阻尼与肌肉收缩有关，也存在方向差异。不能只根据膝盖弯曲角度断定主动肌肉张力。[原始研究摘要](https://pubmed.ncbi.nlm.nih.gov/11160036/)
- **Wakeling 等，2002**：接近组织共振时，肌肉活动和阻尼增大；自然频率未按输入频率作对应改变。支持本轮先试阻尼。[原始研究摘要](https://pubmed.ncbi.nlm.nih.gov/12183507/)
- **Boyer & Nigg，2006**：同一股四头肌区域的不同测点，峰值与出现时间不同，主导频率却接近。因此保留区域节点，而非整条腿完全同相运动。[原始研究摘要](https://pubmed.ncbi.nlm.nih.gov/16439234/)
- **Behling 等，2021**：分析了 32 名跑者，小腿内侧腓肠肌主导频率约 10.4–10.5 Hz；个体差异和测试条件不能忽略。[原始研究全文](https://pmc.ncbi.nlm.nih.gov/articles/PMC8370632/)
- **Dyna，SIGGRAPH 2015**：用动作历史、身体/肢体速度与加速度、体型预测软组织形变。与保留惯性和历史状态的方向一致，本轮没有接入 Dyna。[作者项目页](https://is.mpg.de/ps/de/publications/dyna-siggraph-2015)

## C 不能直接抄论文数字

2021 年研究用 `exp(-c·t)` 描述功率衰减，信号幅度按 `exp(-c·t/2)` 衰减；`c` 的单位是 s⁻¹。它不是我们界面中的无量纲阻尼倍率，也不是质量—弹簧方程中的粘性阻尼系数。[方法说明](https://pmc.ncbi.nlm.nih.gov/articles/PMC8370632/)

对当前孤立线性节点，近似有效阻尼比为 `profile ζ × damping / √support`。基础 ζ=0.35 时，大腿约 **0.114 → 0.152**，小腿约 **0.152 → 0.209**。胫骨前肌的基础 ζ=0.5，另有差异。实际还有耦合、被动张力、限幅和数值积分，所以最终使用录制后的衰减作判断。

现有模型表现较大范围、较低频的表面运动；跑步冲击下的加速度频谱不能直接当作它的目标频率。本轮频率未强行调至实验小腿均值。主动肌肉状态、体型相关频率与方向性标定，仍需要后续数据支持。

## D 实际急停结果

下表是停止后，五个节点中的最大位移偏差最后一次超过该次余振峰值 10% 的时间；不是医学上的组织松弛常数。采样间隔约 0.033 秒。

| 状态 | 上一版 | 本轮 |
|---|---:|---:|
{rows}

坐姿运动固定大腿区域的最大相邻面夹角：**{seam['before']['maximum_edge_angle_deg']:.2f}° → {seam['after']['maximum_edge_angle_deg']:.2f}°**。新版超过 45° 的帧数为 {seam['after']['frames_with_edge_over_45']} / 210。该几何审计由真实 A1 表面、实际量化权重和固定姿态下的骨骼平移重建，已校验旋转不变；不冒充每帧 GPU 网格读回或全局自交测试。

每个并排视频的主骨骼矩阵最大差为 0，相机、灯光和采样时间一致。每段 7 秒、210 帧、30 fps，模拟步长 1/60 秒；这不是性能帧率测试。Contact 关闭，Glute / Leg Jiggle 开启，Breast Jiggle 关闭。屈伸和原地迈步在 4 秒停止，根节点急停在 3 秒停止；原地迈步不含落脚接触。

## E 验证与使用

UE 编译、资产 build/reload/verify、15 项相关回归已通过。主工程 Editor / PIE 四个静态姿态和额外坐姿 Jiggle 的结果见 [验证记录](main-validation.json)。静态网格与上一版的对应比较见 [静态对照](static.json)。本轮没有重新评估历史接触问题。

新角色：`{bundle['blueprint'].split('.')[0]}`。

新配置：`{bundle['configuration']}`。

生成身份：`{bundle['identity']}`。已安装主工程插件，旧二进制和上一版证据保留。已有角色若保存了自定义阻尼，请用“重置”或手动改到 0.40 / 0.55；新生成角色使用新默认值。

## F 数据与复现

[逐动作统计](motion-analysis.json) · [衰减统计](decay.json) · [动态接缝](dynamic-seam.json) · [论文与工程选择](sources.json) · [B1 验证](authored-targets.json) · [资产清单](bundle.json) · [测试](tests.json) · [安装记录](installation.json) · [视频帧数](media/validation.json)

前版录像直接复用已归档的 LegProduction 正式结果，未重新标记旧诊断探针。全套原始帧、A1 路径和日志保留在本轮 Saved 目录；论文条目已区分全文、摘要和作者项目页。
'''
(r/'report.md').write_text(md,encoding='utf-8')
renderer='C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py'
subprocess.run(['F:/Program/Python311/python.exe','-X','utf8',renderer,'render',str(r/'report.md'),'--output',str(r/'report.html'),'--no-open'],check=True)
for n in ['report.md','report.html','ring-down.png','sources.json','decay.json','motion-analysis.json','dynamic-seam.json','main-validation.json','static.json','installation.json','compile.log','report.py','analyze.py','decay.py','dynamic_seam.py','encode.py','verify_main.py','static.py']:shutil.copy2(r/n,out/n)
for n in ['bundle.json','recipe.json','family.json','motion-recipe.json','tests.json','authored-targets.json']:shutil.copy2(r/'v1'/n,out/n)
for n in ['bundle.json','tests.json','authored-targets.json']:shutil.copy2(r/'v1'/n,r/n)
shutil.copytree(r/'media',out/'media',dirs_exist_ok=True)
for group in ['runtime','main-editor','main-pie']:
 d=r/group;dest=out/group;dest.mkdir(exist_ok=True);assert 'Result={Success} Name={ContactRuntime}' in (d/'runtime.log').read_text(errors='replace')
 for n in ['command.json','asset-identity.json','suite.json','process.json']:shutil.copy2(d/n,dest/n)
 with gzip.open(dest/'runtime.log.gz','wb') as f:f.write((d/'runtime.log').read_bytes())
 for state in d.glob('*/state.json'):
  cd=dest/state.parent.name;cd.mkdir(exist_ok=True);shutil.copy2(state,cd/'state.json')
  if (state.parent/'video.json').exists():
   with gzip.open(cd/'video.json.gz','wb') as f:f.write((state.parent/'video.json').read_bytes())
  if state.parent.name=='seated-stop':shutil.copy2(state.parent/'oblique-material.png',cd/'oblique-material.png')
for n in ['build.log','reload.log','verify.log','tests.log']:
 with gzip.open(out/(n+'.gz'),'wb') as f:f.write((r/'v1'/n).read_bytes())
media='<section class="evidence"><h2>急停后的位移曲线</h2><img src="ring-down.png" style="width:100%"><h2>真实 UE 同条件录像</h2><p><strong>左：上一版正式参数；右：本轮阻尼微调。</strong></p>'
for case,title in [('hard-stop','站立急停'),('seated-stop','坐姿急停'),('flexion','屈伸'),('step','原地迈步')]:
 media+=f'<h3>{title}</h3><video controls preload="metadata" src="media/{case}-comparison.mp4"></video><details><summary>局部放大</summary><video controls preload="metadata" src="media/{case}-detail.mp4"></video></details>'
media+='</section>';style='<style>.evidence{max-width:1250px;margin:24px auto;padding:24px;background:#eaf0f6;border-radius:16px;color:#172a40}.evidence video{display:block;width:100%;max-height:768px;background:#000;margin:12px 0}.evidence h3{margin-top:26px}</style>'
html=(r/'report.html').read_text(encoding='utf-8').replace('</head>',style+'</head>').replace('</main>',media+'</main>')
for p in [r/'report.html',out/'report.html']:p.write_text(html,encoding='utf-8')
for p in out.rglob('*.png'):
 data=p.read_bytes();i=8
 while i+12<=len(data):
  size=struct.unpack('>I',data[i:i+4])[0];kind=data[i+4:i+8];i+=12+size
  if kind==b'IEND':break
 assert kind==b'IEND' and i<=len(data);p.write_bytes(data[:i])
for a in re.findall(r'(?:href|src)="([^"]+)"',html):
 if not a.startswith(('#','http','data:','javascript:')):assert (out/a.split('#')[0]).exists(),a
(out/'manifest.json').write_text(json.dumps({'source_root':str(r),'baseline':'../LegProduction/after-v3 and ../LegProduction/media','files':{str(p.relative_to(out)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='manifest.json'}},indent=2))
print(out/'report.html')
