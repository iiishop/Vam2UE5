"""Publish real deep-flexion captures, actual angles and reused A1 seam audit."""
import argparse,gzip,json,shutil,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('editor',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
checks=read(a.run/'validation.json');assert len(checks)==4 and all(r['capture_valid'] for r in checks)
editor=read(a.editor/'validation.json');assert all(r['capture_valid'] for r in editor)
analysis=read(a.run/'analysis.json');assert analysis['reference_group']=='C'
for name in ['identity.json','validation.json','analysis.json','deep-edge-stages.json']:shutil.copy2(a.run/name,a.out/name)
shutil.copy2(a.run.parent/'editor-consistency.json',a.out/'editor-consistency.json');shutil.copy2(a.editor/'identity.json',a.out/'editor-identity.json');shutil.copy2(a.editor/'validation.json',a.out/'editor-validation.json')
for name in ['compile.log','compile-2.log','compile-3.log','compile-4.log']:shutil.copy2(a.run.parent/name,a.out/name)
sections=[];media=[]
for action,title in [('deep-kneel','站立 → 高跪姿 → 深屈曲跪姿 → 起身'),('single-flexion','单侧深屈膝：排除两腿遮挡')]:
 figures=[];clips={};fig,ax=plt.subplots(figsize=(9,3.4))
 for g in ['C','M']:
  src=a.run/g/(g+'-'+action);dst=a.out/src.name;dst.mkdir(exist_ok=True);v=read(src/'video.json')['samples'];clips[g]=v
  shutil.copy2(src/'state.json',dst/'state.json')
  with gzip.open(dst/'video.json.gz','wb') as f:f.write((src/'video.json').read_bytes())
  for view in ['side','rear','oblique']:
   im=Image.open(src/('deep-'+view+'.png'));im.save(dst/(view+'.png'),optimize=True)
  ax.plot([f['time_seconds'] for f in v],[f['actual_knee_deg'][0] for f in v],label=g+' left',linestyle='-' if g=='C' else '--')
  ass='[Script Info]\nScriptType: v4.00+\nPlayResX: 768\nPlayResY: 768\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,3,1,0,7,12,12,12,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
  for i,f in enumerate(v):
   status='ACTIVE' if f['contact_solver_active'] else 'INACTIVE' if g=='C' else 'OFF';angle=f['actual_knee_deg'][0]
   ass+=f'Dialogue: 0,0:00:{i/30:05.2f},0:00:{(i+1)/30:05.2f},Default,,0,0,0,,{g} | knee {angle:.1f} deg | Contact {status}\\NDEEP INPUT PROBE - production limits unchanged\n'
  (dst/'status.ass').write_text(ass)
  # Re-encode PNGs without UE archive tail bytes. Original captures remain intact.
  clean=a.run.parent/'encoding'/src.name;clean.mkdir(parents=True,exist_ok=True)
  for i in range(len(v)):
   path=clean/('frame-%04d.png'%i)
   if not path.exists():Image.open(src/'video'/path.name).save(path,compress_level=1)
  if not (dst/'motion.mp4').exists():subprocess.run(['ffmpeg','-v','error','-y','-framerate','30','-i',str((clean/'frame-%04d.png').resolve()),'-vf','subtitles=status.ass','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart','motion.mp4'],cwd=dst,check=True)
  probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration','-of','json',str(dst/'motion.mp4')]))['streams'][0];assert int(probe['nb_read_frames'])==len(v);media.append(dict(clip=src.name,**probe))
  figures.append(f'<article><h3>{g} · 最大屈曲 5.5 秒</h3>'+''.join(f'<p>{view}</p><img loading="lazy" src="{dst.name}/{view}.png">' for view in ['side','rear','oblique'])+'</article>')
 ax.set_xlabel('seconds');ax.set_ylabel('actual geometric knee angle (degrees)');ax.set_ylim(0,180);ax.legend();ax.grid(alpha=.25);fig.tight_layout();fig.savefig(a.out/(action+'-angles.png'),dpi=140);plt.close(fig)
 pair=a.out/(action+'-C-M.mp4')
 if not pair.exists():subprocess.run(['ffmpeg','-v','error','-y','-filter_complex_threads','1','-i',str(a.out/('C-'+action)/'motion.mp4'),'-i',str(a.out/('M-'+action)/'motion.mp4'),'-filter_complex','hstack=inputs=2','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(pair)],check=True)
 sheet=Image.new('RGB',(1200,660),'#edf1f4');draw=ImageDraw.Draw(sheet)
 for row,g in enumerate(['C','M']):
  for col,frame in enumerate([90,135,165]):
   im=Image.open(a.run/g/(g+'-'+action)/('video/frame-%04d.png'%frame));im=im.crop((130,180,680,720)).resize((400,310));sheet.paste(im,(col*400,row*330+20));draw.text((col*400+8,row*330+4),f'{g} | {frame/30:.1f}s | {clips[g][frame]["actual_knee_deg"][0]:.1f}deg',fill='black')
 sheet.save(a.out/(action+'-frames.png'),optimize=True)
 sections.append(f'<h2>{title}</h2><p>左 C / 右 M（新版 S）；同一骨骼输入、相机和灯光。视频文字显示实际膝角度及接触状态。</p><video controls preload="metadata" src="{pair.name}"></video><img src="{action}-angles.png"><p>上 C / 下 M，同帧同裁剪；点击视频全屏查看运动。</p><img src="{action}-frames.png"><details><summary>最大折腿时的侧/后/斜视原图</summary><div class="grid">'+''.join(figures)+'</div></details>')
with gzip.open(a.out/'muscle-paths.json.gz','wb') as f:f.write((a.run/'muscle-paths.json').read_bytes())
for g in ['C','M']:
 with gzip.open(a.out/(g+'-runtime.log.gz'),'wb') as f:f.write((a.run/g/'runtime.log').read_bytes())
with gzip.open(a.out/'editor-runtime.log.gz','wb') as f:f.write((a.editor/'M/runtime.log').read_bytes())
(a.out/'media-validation.json').write_text(json.dumps(media,indent=2))
rows=[]
for r in analysis['surfaces']:
 if r['group']=='M':rows.append(f'<tr><td>{r["action"]}</td><td>{r["frame"]/30:.1f}s</td><td>{r["seam_max_cm"]:.6f}</td><td>{r["new_sharp_edges"]}</td><td>{r["opposed_normals_vs_reference"]}</td><td>{r["max_vs_reference_cm"]:.3f}</td></tr>')
page='''<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>155° 深屈膝：C / 新版 S</title><style>body{font:16px/1.65 system-ui;max-width:1480px;margin:24px auto;padding:0 24px;background:#edf1f4;color:#182b38}video,img{width:100%}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}article,section{background:white;padding:18px;border-radius:10px;margin:16px 0}td,th{padding:8px 14px;border-bottom:1px solid #bac8d0}a{color:#195fa4}code{overflow-wrap:anywhere}@media(max-width:750px){.grid{grid-template-columns:1fr}}</style><h1>完全折腿方向：155° 深屈膝实跑对比</h1><section><p><b>本轮测试真实达到约 155°，不再使用被限位截短的中等屈曲代替。</b>正常 BP 的实际蒙皮、Corrective 和 Jiggle 继续运行；膝关节超限输入仅在独立录制工具内启用，髋与脚踝仍使用原有限位，生产 RigProfile 没有修改。</p><p>根移动保持平均踝关节高度，脚部为姿态配合。它是跪姿形变压力测试，尚非带地面接触和脚部 IK 的完整游戏动作。C 请求开启接触，M 关闭接触；状态逐帧显示。安全拒绝不会被算作接触通过。</p><p><a href="identity.json">新验证身份</a> · <a href="validation.json">PIE 实际角度</a> · <a href="editor-validation.json">Editor 实跑</a> · <a href="analysis.json">A1 数据</a> · <a href="compile-4.log">最终编译</a></p><h2>结论：达到了完整折腿，但新版链条尚未胜过 C</h2><p>同一帧的主骨矩阵误差为 0，相机与根移动也完全一致。双腿 155° 保持帧，新版 S 相对 C 的腿部表面 RMS 为 0.465 cm、最大差为 1.147 cm。275 组同源重复点间距为 0；这只说明没有重复点裂缝，并不说明膝部形态自然。</p><p><b>真实截图中的膝前尖折仍在，两组都有；新版 S 还新增左右各一处硬折标记。</b>其相邻面夹角：C 为 43.89° / 45.31°，新版 S 为 60.74° / 60.83°。单侧动作在相同左侧位置复现。没有把这个结果标为外形 PASS，也没有替换生产默认方案。</p><p>复用 A1 追踪这些边：最终权重层两组完全相同（约 129° / 131°）；开启静态链条后开始不同；Corrective 层产生上述 44° 对 61° 的结果；Jiggle 只改变不足 0.03°。因此这几处新增硬折来自静态链条与既有修形的组合，不是动态晃动幅度过大。<a href="deep-edge-stages.json">具体三角形、源顶点与逐层角度</a>。这是局部定位，不等于已经证明所有膝前形态问题的根因。</p><p>链条真实生效：左股直肌保持帧路径从 43.623 cm 变为 44.999 cm（+3.15%）。下一步应处理修形与新蒙皮算子的兼容性，再研究深屈曲的组织挤压；单纯继续加大链条幅度会放大问题。</p><p><b>接触对照限制：</b>C 双腿序列仅 49/270 帧求解器活动，单腿为 78/180 帧；155° 保持帧两者均不活动。原始日志保留，不能据此宣称已经表现出有效 Chaos 大小腿挤压。Editor 与 PIE 均实跑达到 155°，主骨矩阵一致；但动态辅助骨并非逐帧相同：全程最大平移差 2.101 cm，出现在第 143 帧 R_Glute_Lower，腿部辅助骨最大矩阵项差 0.854。<a href="editor-consistency.json">一致性失败记录</a>。因此没有宣称动态一致性或解剖外形通过；双列视频中的 C/M 都来自 PIE，同世界条件比较。</p></section>'''+''.join(sections)+'''<h2>深屈曲下的几何检查</h2><p>复用 A1，纳入主腿骨及辅助骨权重大于 5% 的顶点，覆盖刚性膝区与软组织过渡；按同源映射检查重复点；新增硬边标记为相对同姿态 C 的相邻面夹角增长超过 15° 且当前超过 45°。相对面法线反向不自动等于网格翻转。数据为骨骼/Morph/Jiggle 表面，未冒充最终 GPU 接触表面；接缝数值不能单独证明没有组织穿插。</p><table><tr><th>动作</th><th>时间</th><th>重复点差 cm</th><th>新增硬边标记</th><th>相对反向面标记</th><th>相对 C 最大变化 cm</th></tr>'''+''.join(rows)+'''</table><p>30 FPS 是编码速度，不是实际游戏性能。旧姿态、历史报告和生产资产未覆盖。</p></html>'''
(a.out/'report.html').write_text(page,encoding='utf-8');print(a.out/'report.html')
