"""Publish real UE C/new-muscle comparison and measured attachment paths."""
import argparse,json,shutil,gzip,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
checks=read(a.run/'validation.json');assert len(checks)==4 and all(r['valid'] for r in checks)
analysis=read(a.run/'analysis.json');paths=read(a.run/'muscle-paths.json')
for f in ['identity.json','validation.json','analysis.json']:shutil.copy2(a.run/f,a.out/f)
with gzip.open(a.out/'muscle-paths.json.gz','wb') as f:f.write((a.run/'muscle-paths.json').read_bytes())
labels={'C':'C · 原外形参照 + 接触','M':'M · S + 独立前后肌群附着','S':'S · 上轮独立站点','R':'R · C 骨架 / 接触关闭'}
panels=[]
for action,title in [('flexion','髋膝联合屈伸'),('knee-flexion','单独屈膝（脚朝向沿用同一动画）'),('seated-stop','坐姿急停')]:
 cards=[]
 for g,label in labels.items():
  src=a.run/g/(g+'-'+action);dst=a.out/src.name;dst.mkdir(exist_ok=True);state=read(src/'state.json');v=read(src/'video.json')['samples'];assert len(v)==210
  shutil.copy2(src/'state.json',dst/'state.json')
  with gzip.open(dst/'video.json.gz','wb') as f:f.write((src/'video.json').read_bytes())
  for view in ['side','rear','oblique']:
   for mode in ['material','gray','world-normal']:Image.open(src/(view+'-'+mode+'.png')).save(dst/(view+'-'+mode+'.png'),optimize=True)
  ass='[Script Info]\nScriptType: v4.00+\nPlayResX: 768\nPlayResY: 768\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,22,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,3,1,0,7,12,12,12,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
  for i,f in enumerate(v):
   status='ACTIVE' if f['contact_solver_active'] else 'REJECTED' if g=='C' else 'OFF'
   ass+=f'Dialogue: 0,0:00:{i/30:05.2f},0:00:{(i+1)/30:05.2f},Default,,0,0,0,,{g} | Contact {status}\n'
  (dst/'status.ass').write_text(ass)
  if not (dst/'motion.mp4').exists():subprocess.run(['ffmpeg','-y','-v','error','-framerate','30','-i',str((src/'video/frame-%04d.png').resolve()),'-vf','subtitles=status.ass','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart','motion.mp4'],cwd=dst,check=True)
  probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration','-of','json',str(dst/'motion.mp4')]))
  assert probe['streams'][0]['nb_read_frames']=='210';(dst/'video-check.json').write_text(json.dumps(probe))
  cards.append(f'<article><h3>{label}</h3><video controls preload="metadata" src="{dst.name}/motion.mp4"></video><p>接触有效 {sum(f["contact_solver_active"] for f in v)}/210 帧</p><details><summary>多角度静态图</summary>'+''.join(f'<p>{view} / {mode}</p><img loading="lazy" src="{dst.name}/{view}-{mode}.png">' for view in ['side','rear','oblique'] for mode in ['material','gray','world-normal'])+'</details></article>')
 pair=a.out/(action+'-C-M.mp4')
 if not pair.exists():subprocess.run(['ffmpeg','-y','-v','error','-filter_complex_threads','1','-i',str(a.out/('C-'+action)/'motion.mp4'),'-i',str(a.out/('M-'+action)/'motion.mp4'),'-filter_complex','hstack=inputs=2','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p',str(pair)],check=True)
 sheet=Image.new('RGB',(1000,920),'#eef2f6');draw=ImageDraw.Draw(sheet)
 for j,g in enumerate(labels):
  im=Image.open(a.run/g/(g+'-'+action)/'video/frame-0045.png');x=j%2*500;y=j//2*460;sheet.paste(im.crop((160,230,650,650)),(x,y+30));draw.text((x+10,y+8),g,fill='black')
 sheet.save(a.out/(action+'-closeup.png'),optimize=True)
 fig,axes=plt.subplots(1,2,figsize=(11,3.6))
 for side,ax in zip(['L','R'],axes):
  for name in ['RectusFemoris','Hamstrings','VastusLateral']:
   records=[r for r in paths if r['action']==action and r['segment']==side+'_Thigh' and r['bone'].endswith('_'+name)];ax.plot([r['frame']/30 for r in records],[r['ratio'] for r in records],label=name)
  ax.axhline(1,c='gray',lw=.8);ax.set_title(side+' thigh: measured path length / rest');ax.set_xlabel('seconds');ax.legend(fontsize=8)
 fig.tight_layout();fig.savefig(a.out/(action+'-paths.png'),dpi=150);plt.close(fig)
 panels.append(f'<h2>{title}</h2><p>左 C / 右 M。实际主骨、相机相同；C 接触停算会明确标注。30 FPS 为编码速度，不代表游戏性能。</p><video controls preload="metadata" src="{pair.name}"></video><details><summary>第 45 帧局部图和四组完整视频</summary><p>上 C/M，下 S/R；相同裁剪窗口。</p><img src="{action}-closeup.png"><div class="grid">'+''.join(cards)+f'</div></details><h3>实际前后路径长度</h3><img src="{action}-paths.png">')
rows=[]
for r in analysis['surfaces']:
 rows.append(f'<tr><td>{r["group"]}</td><td>{r["action"]}/{r["frame"]}</td><td>{r["seam_max_cm"]:.6f}</td><td>{r["new_sharp_edges"]}</td><td>{r["opposed_normals_vs_R"]}</td><td>{r["max_vs_R_cm"]:.3f}</td></tr>')
page='''<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>独立肌群附着：S 与 C</title><style>body{font-family:system-ui;background:#eef2f6;color:#172632;max-width:1500px;margin:24px auto;padding:0 24px;line-height:1.65}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}article{background:white;border-radius:10px;padding:12px}video,img{width:100%}td,th{padding:6px 14px;border-bottom:1px solid #ccd}</style><h1>S 优化：独立前后肌群附着</h1><p>正常 BP_VamCharacter，复用现有区域权重，保持 C 的主骨与 Corrective。M 为新候选；S 为上轮方案；R 隔离接触；C 为用户外形参照。新增路径按当前几何推导，不针对这个人物写死左右旋转补丁。</p><p><a href="research.html">结构说明与参考案例</a> · <a href="validation-summary.json">编译、Editor 与 PIE 验证</a></p><div id="verdict"></div><h2>结构改动与边界</h2><p>每个区域都有独立起点、关节附近引导点与止点。股直肌和腘绳肌跨髋膝，Vasti 区域起点跟随股骨，并与股直肌共享前侧肌腱导向；内收肌止于股骨。小腿 Gastro 跨膝，其他区域跟随胫骨。各自测量路径长度，驱动局部纵向伸缩、横向补偿与被动张力，再叠加原 Jiggle 位移。原 Jiggle 幅度没有调小。</p><p>这是几何附着代理，尚非经过解剖标定的肌腱、主动肌肉或完整关节绕包求解。固定引导点不能保证路径不穿过骨体。单骨缩放行列式为 1，不等于整个混合蒙皮体积严格保持。保护采用平滑对数应变饱和，原始长度与限制状态均有记录。</p><p>权重、bind、Morph、拓扑没有重写；运行算子具有新的验证身份，不能用旧 B1 烘焙证书证明新算子正确。<a href="identity.json">身份</a> · <a href="analysis.json">A1 动作快照数据</a> · <a href="muscle-paths.json.gz">逐帧附着路径</a></p>'''+''.join(panels)+'''<h2>动作中的接缝与几何折线</h2><p>基于同一源顶点映射检查重复点，未使用最近邻。新增硬边指相对 R 增大超过 15° 且当前超过 45° 的相邻三角形夹角，需要配合画面判断。反向法线是相对 R 的几何面法线，不等价于已证明网格翻转；这些数据不含 GPU 接触表面或最终着色法线。</p><table><tr><th>组</th><th>动作/帧</th><th>重复点最大间距 cm</th><th>新增硬边标记</th><th>反向法线标记</th><th>相对 R 最大位移 cm</th></tr>'''+''.join(rows)+'''</table><h2>验证范围与两轮身份</h2><p>M 使用第二轮前侧导向修正；R/S/C 使用第一轮真实录制。第二轮的 C++ 差异仅涉及 mode 4 调用的 MusclePath 及其测试，控制组路径不受影响。两轮源文件摘要与验证身份分别保留，逐帧实际主骨与相机仍重新核对。</p><h2>原始参考</h2><p><a href="https://opensimconfluence.atlassian.net/wiki/spaces/OpenSim33/pages/53674058/Muscle%2BEditor">OpenSim：肌肉路径、固定点、引导点与绕包点</a>；<a href="https://download.autodesk.com/us/maya/2009help/files/Creating_Muscles_Set_muscle_pose_states_.htm">Autodesk：肌肉 Rest/Squash/Stretch 状态与附着滑动</a>。本实现借鉴路径概念，没有声称复现其全部生物力学求解。</p></html>'''
(a.out/'report.html').write_text(page,encoding='utf-8');print(a.out/'report.html')
