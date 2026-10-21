"""Publish actual three-way UE leg recordings, without substituting offline renders."""
import argparse,json,shutil,subprocess,html
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('bundle',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
checks=read(a.run/'validation.json');assert len(checks)==3 and all(r['returncode']==0 and r['states']==3 for r in checks),'Incomplete recordings cannot be published'
analysis=read(a.run/'layer-comparison.json');shutil.copy2(a.run/'layer-comparison.json',a.out/'layer-comparison.json');shutil.copy2(a.bundle,a.out/'bundle.json');shutil.copy2(a.run/'validation.json',a.out/'validation.json')
labels={'A':'Jiggle + 新骨链 + Chaos','B':'Jiggle + 新骨链','C':'Jiggle + Chaos'}
panels=[]
for action,name in [('flexion','屈伸'),('step','交替迈步'),('seated-stop','坐姿急停 / 座面接触')]:
 cards=[]
 for g,label in labels.items():
  src=a.run/g/(g+'-'+action);dst=a.out/(g+'-'+action);dst.mkdir(exist_ok=True)
  state=read(src/'state.json');v=read(src/'video.json');assert len(v['samples'])==210
  active=sum(s['contact_solver_active'] for s in v['samples'])
  warning=f'接触实际运行 {active}/210 帧。'+('该组合未通过：部分帧被安全检查停算。' if g!='B' and active!=210 else 'B 组按设计关闭接触。' if g=='B' else '全片接触求解保持运行。')
  shutil.copy2(src/'state.json',dst/'state.json');shutil.copy2(src/'video.json',dst/'video.json')
  for view in ['side','rear','front','oblique']:
   for mode in ['material','gray']:
    shutil.copy2(src/(view+'-'+mode+'.png'),dst/(view+'-'+mode+'.png'))
  first=src/'video/frame-0000.png';codec='mjpeg' if first.read_bytes()[:2]==b'\xff\xd8' else 'png'
  video=dst/'motion.mp4'
  ass='[Script Info]\nScriptType: v4.00+\nPlayResX: 768\nPlayResY: 768\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,3,1,0,7,12,12,12,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
  tm=lambda f:f'0:00:{f/30:05.2f}'
  start=0
  for i in range(1,211):
   if i==210 or v['samples'][i]['contact_solver_active']!=v['samples'][start]['contact_solver_active']:
    status='ACTIVE' if v['samples'][start]['contact_solver_active'] else 'OFF (requested)' if g=='B' else 'STOPPED / REJECTED'
    ass+=f'Dialogue: 0,{tm(start)},{tm(i)},Default,,0,0,0,,{g} | Chain {"ON" if state["leg_chain_enabled"] else "OFF"} | Contact {status}\n';start=i
  (dst/'status.ass').write_text(ass,encoding='utf-8')
  if not video.exists():subprocess.run(['ffmpeg','-y','-v','error','-framerate','30','-c:v',codec,'-i',str((src/'video/frame-%04d.png').resolve()),'-vf','subtitles=status.ass','-c:v','libx264','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart','motion.mp4'],cwd=dst,check=True)
  probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration,width,height','-of','json',str(video)]))
  assert int(probe['streams'][0]['nb_read_frames'])==210 and abs(float(probe['streams'][0]['duration'])-7)<.01
  (dst/'video-check.json').write_text(json.dumps(probe,indent=2))
  cards.append(f'<section><h3>{label}</h3><p><strong>{warning}</strong></p><video controls preload="metadata" src="{dst.name}/motion.mp4"></video><p>骨链 {state["leg_chain_enabled"]}；静态截图时接触求解 {state["contact_solver_active"]}</p><details><summary>同条件静态截图</summary>'+''.join(f'<p>{view}</p><img src="{dst.name}/{view}-material.png"><img src="{dst.name}/{view}-gray.png">' for view in ['side','rear','front','oblique'])+'</details></section>')
 panels.append(f'<h2>{name}</h2><button onclick="sync(\'{action}\')">三组从头同步播放</button><div class="grid" id="{action}">'+''.join(cards)+'</div>')
metrics=[]
for row in analysis['layers']:
 peak=max(r['chain']['max_cm'] for r in row['regions'].values());jiggle=max(r['jiggle']['max_cm'] for r in row['regions'].values())
 metrics.append(f'<tr><td>{row["group"]}</td><td>{row["action"]}</td><td>{peak:.4f}</td><td>{jiggle:.4f}</td></tr>')
b=read(a.bundle)
page='''<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>腿部骨链三组实跑对比</title><style>body{font-family:system-ui,sans-serif;background:#eef2f6;color:#182331;margin:30px auto;max-width:1550px;padding:0 20px;line-height:1.65}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}section{background:white;padding:14px;border-radius:12px}video,img{width:100%}table{border-collapse:collapse}td,th{padding:8px 16px;border-bottom:1px solid #bbc5d0}button{padding:10px;margin:12px 0}code{overflow-wrap:anywhere}@media(max-width:800px){.grid{grid-template-columns:1fr}}</style><h1>腿部骨链：三组同条件对比</h1><p>正常 BP_VamCharacter · 相同新资产、主骨姿态、相机、灯光和动作。5 个纵向站 × 双侧大小腿，共 20 个新增变形骨；原有 20 个区域 Jiggle 节点保留。</p><p>骨链是连续曲线驱动的辅助变形，横截面补偿只是体积近似，不是完整解剖肌肉模拟。这里的 Chaos 指项目现有下半身 GPU 接触系统。所有组保留既有臀部结构与 Corrective。</p><p>坐垫在三组中均存在；B 组关闭接触求解。交替迈步是测试动作，不冒充实际行走。7 秒片段按 30 FPS 编码，不能用编码帧率推算游戏性能。</p>'''+''.join(panels)+'''<h2>A1 分层数据</h2><p>数值是该层引起的位移，不是相对真实人体的精度分数。各区域 RMS / P95 / Max、主骨和座面对齐检查见 <a href="layer-comparison.json">数据</a>。求解器开启不代表每一对软组织已接触，需结合接触状态和实拍判断。</p><table><tr><th>组</th><th>动作</th><th>骨链局部最大位移 cm</th><th>Jiggle 局部最大位移 cm</th></tr>'''+''.join(metrics)+f'</table><h2>资产</h2><p><code>{html.escape(b["blueprint"])}</code></p><p><a href="bundle.json">新身份与构建记录</a> · <a href="validation.json">三组运行检查</a></p><script>function sync(id){{document.querySelectorAll("#"+id+" video").forEach(v=>{{v.pause();v.currentTime=0;v.play().catch(()=>{{}});}});}}</script></html>'
(a.out/'report.html').write_text(page,encoding='utf-8');print(a.out/'report.html')
