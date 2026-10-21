"""Publish six actual UE operator recordings with contact-state labels."""
import argparse,json,shutil,gzip,subprocess,html
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
valid=read(a.run/'validation.json');assert len(valid)==6 and all(x['valid'] for x in valid)
labels={'R':'参照：Jiggle，无骨链/接触','L':'旧骨链','T':'候选：仅平移支撑点','V':'候选：局部伸长/体积补偿','S':'候选：新增站点独立驱动','C':'用户参照：Jiggle + Chaos'}
for name in ['validation.json','analysis.json','identity.json']:shutil.copy2(a.run/name,a.out/name)
for name in ['research.html','research.md']:shutil.copy2(Path('Saved/LegChainVariants')/name,a.out/name)
sections=[]
for action,title in [('flexion','屈伸'),('step','交替迈步'),('seated-stop','坐姿急停')]:
 cards=[]
 for g,label in labels.items():
  src=a.run/g/(g+'-'+action);dst=a.out/src.name;dst.mkdir(exist_ok=True)
  s=read(src/'state.json');v=read(src/'video.json')['samples'];active=sum(x['contact_solver_active'] for x in v)
  shutil.copy2(src/'state.json',dst/'state.json')
  with gzip.open(dst/'video.json.gz','wb') as f:f.write((src/'video.json').read_bytes())
  for view in ['side','rear','oblique']:
   for mode in ['material','gray','world-normal']:shutil.copy2(src/(view+'-'+mode+'.png'),dst/(view+'-'+mode+'.png'))
  codec='mjpeg' if (src/'video/frame-0000.png').read_bytes()[:2]==b'\xff\xd8' else 'png'
  ass='[Script Info]\nScriptType: v4.00+\nPlayResX: 768\nPlayResY: 768\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,22,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,3,1,0,7,12,12,12,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
  for i,f in enumerate(v):
   state='ACTIVE' if f['contact_solver_active'] else 'REJECTED' if g=='C' else 'OFF'
   ass+=f'Dialogue: 0,0:00:{i/30:05.2f},0:00:{(i+1)/30:05.2f},Default,,0,0,0,,{g} | Method {s["leg_chain_method"]} | Contact {state}\n'
  (dst/'status.ass').write_text(ass)
  if not (dst/'motion.mp4').exists():subprocess.run(['ffmpeg','-y','-v','error','-threads','2','-framerate','30','-c:v',codec,'-i',str((src/'video/frame-%04d.png').resolve()),'-vf','subtitles=status.ass','-c:v','libx264','-threads','2','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart','motion.mp4'],cwd=dst,check=True)
  probe=read_probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration,width,height','-of','json',str(dst/'motion.mp4')]))
  assert int(probe['streams'][0]['nb_read_frames'])==210
  (dst/'video-check.json').write_text(json.dumps(probe))
  cards.append(f'<article><h3>{g} · {label}</h3><video controls preload="metadata" src="{dst.name}/motion.mp4"></video><p>接触有效 {active}/210 帧</p><details><summary>侧 / 后 / 斜视：材质、灰模、法线</summary>'+''.join(f'<p>{view} · {mode}</p><img loading="lazy" src="{dst.name}/{view}-{mode}.png">' for view in ['side','rear','oblique'] for mode in ['material','gray','world-normal'])+'</details></article>')
 combined=a.out/(action+'-six-way.mp4')
 if not combined.exists():
  cmd=['ffmpeg','-y','-v','error','-filter_complex_threads','1']
  for g in labels:cmd+=['-i',str(a.out/(g+'-'+action)/'motion.mp4')]
  cmd+=['-filter_complex','[0:v][1:v][2:v]hstack=inputs=3[top];[3:v][4:v][5:v]hstack=inputs=3[bottom];[top][bottom]vstack=inputs=2','-c:v','libx264','-threads','2','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(combined)]
  subprocess.run(cmd,check=True)
 sections.append(f'<h2>{title}</h2><p>上排 R / L / T，下排 V / S / C。固定步长录制，30 FPS 编码不代表游戏帧率。</p><video controls preload="metadata" src="{combined.name}"></video><div class="grid">'+''.join(cards)+'</div>')
metrics=read(a.run/'analysis.json')
rows=[]
for row in metrics['rows']:
 if row['action']=='seated-stop':
  for region,value in row['regions'].items():rows.append(f'<tr><td>{row["group"]}</td><td>{region}</td><td>{value["vs_R"]["rms_cm"]:.4f}</td><td>{value["vs_R"]["p95_cm"]:.4f}</td><td>{value["vs_R"]["max_cm"]:.4f}</td></tr>')
page='''<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>腿部骨链：研究与六组实测</title><style>body{font-family:system-ui;background:#eef2f6;color:#172632;max-width:1600px;margin:24px auto;padding:0 24px;line-height:1.65}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}article{background:white;border-radius:12px;padding:14px}video,img{width:100%}td,th{padding:6px 18px;border-bottom:1px solid #ccd}code{overflow-wrap:anywhere}@media(max-width:800px){.grid{grid-template-columns:1fr}}</style><h1>骨链研究：三种结构候选与 C 对照</h1><p><b>实验比较，不自动替换生产方案。</b>三个候选均使用现有正常 BP 的真实运行链路；本轮保持同一份 mesh、权重、Morph 与拓扑，隔离运行时算子的影响。不是三套已完成解剖标定的肌肉绑定。</p><p><a href="research.html">资料与实现依据</a> · <a href="analysis.json">A1 分层数据与姿态一致性</a> · <a href="identity.json">本轮运行身份</a> · <a href="validation.json">录制检查</a></p><p>原 Jiggle 参数一致。R/L/T/V/S 都关闭接触，C 单独保留；C 若出现 REJECTED，表示历史接触安全检查已停算，不能把后续变化归功于 Chaos。座面所有组一致，接触关闭组没有座面压缩。旧 Corrective 资产身份未改变，其旧验证不自动证明候选算子的姿态保真。</p>'''+''.join(sections)+'''<h2>坐姿最终骨骼蒙皮表面相对 R 的位移（cm）</h2><p>这是变化量，不是人体真实性误差；不含最终 GPU 接触位移。主骨动画、根移动和相机已经由实际数据检查。完整区域及各动作见 analysis.json。</p><table><tr><th>方案</th><th>区域</th><th>RMS</th><th>P95</th><th>Max</th></tr>'''+''.join(rows)+'</table></html>'
(a.out/'report.html').write_text(page,encoding='utf-8')
print(a.out/'report.html')
