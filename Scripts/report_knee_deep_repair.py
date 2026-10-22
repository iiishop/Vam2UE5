"""Publish actual before/after UE knee repair footage without replacing baseline evidence."""
import argparse,json,shutil,subprocess,gzip
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('out',type=Path);p.add_argument('--groups',nargs='+',default=['C','M']);a=p.parse_args();a.out.mkdir(exist_ok=True,parents=True)
old=Path('Saved/LegDeepKneel/pie-2');sections=[]
for action in ['deep-kneel','single-flexion']:
 for g in a.groups:
  stem=g+'-'+action;src=a.run/g/stem;dst=a.out/stem;dst.mkdir(exist_ok=True)
  v=json.loads((src/'video.json').read_text())['samples'];b=json.loads((old/g/stem/'video.json').read_text())['samples'];assert len(v)==len(b)
  for x,y in zip(v,b):
   assert x['actual_knee_deg']==y['actual_knee_deg'] and x['camera_matrix']==y['camera_matrix']
  clean=a.run.parent/'encode'/stem;clean.mkdir(exist_ok=True,parents=True)
  for i in range(len(v)):
   f='frame-%04d.png'%i
   if not (clean/f).exists():Image.open(src/'video'/f).save(clean/f,compress_level=1)
  vid=dst/'after.mp4'
  if not vid.exists():subprocess.run(['ffmpeg','-v','error','-y','-framerate','30','-i',str(clean/'frame-%04d.png'),'-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(vid)],check=True)
  pair=dst/'before-after.mp4'
  if not pair.exists():subprocess.run(['ffmpeg','-v','error','-y','-filter_complex_threads','1','-i',str(Path('Evidence/LegDeepKneel')/stem/'motion.mp4'),'-i',str(vid),'-filter_complex','hstack=inputs=2','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(pair)],check=True)
  sheet=Image.new('RGB',(1200,680),'#edf1f4');draw=ImageDraw.Draw(sheet)
  for row,root in enumerate([old,a.run]):
   for col,frame in enumerate([90,135,165]):
    im=Image.open(root/g/stem/'video'/('frame-%04d.png'%frame)).crop((130,180,680,720)).resize((400,310));sheet.paste(im,(col*400,row*340+25));draw.text((col*400+8,row*340+4),f'{"BEFORE" if row==0 else "AFTER"} | {g} | {frame/30:.1f}s',fill='black')
  sheet.save(dst/'comparison.png')
  for view in ['side','rear','oblique']:Image.open(src/('deep-'+view+'.png')).save(dst/(view+'.png'))
  with gzip.open(dst/'video.json.gz','wb') as f:f.write((src/'video.json').read_bytes())
  sections.append(f'<h2>{g} · {action}</h2><p>左修复前 / 右候选；下方静帧为上修复前 / 下候选。同一实际膝角度和相机。</p><video controls src="{stem}/before-after.mp4"></video><img src="{stem}/comparison.png"><details><summary>真实 UE 三视图</summary>'+''.join(f'<img src="{stem}/{view}.png">' for view in ['side','rear','oblique'])+'</details>')
for name in ['identity.json','validation.json']:
 shutil.copy2(a.run/name,a.out/name)
(a.out/'report.html').write_text('<!doctype html><meta charset="utf-8"><title>深屈膝结构修复</title><style>body{max-width:1500px;margin:30px auto;font:17px/1.7 system-ui;background:#edf1f4;color:#123;padding:20px}video,img{width:100%}details img{max-width:768px}</style><h1>深屈膝结构修复 · 同条件对比</h1><section id="findings">候选视觉与分层验收待填写；不代表已通过。</section>'+''.join(sections),encoding='utf-8')
print(a.out/'report.html')
