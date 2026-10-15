import json,subprocess,shutil,gzip,hashlib
from pathlib import Path
from PIL import Image,ImageDraw
r=Path(__file__).resolve().parent
repo=r.parents[2]
out=repo/'Evidence/LocomotionSuite';out.mkdir(parents=True,exist_ok=True)
media=out/'media';media.mkdir(exist_ok=True)
take=r/'take-2';frames=take/'continuous/video'
assert len(list(frames.glob('frame-*.png')))==720
ff=Path('C:/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0.1-full_build/bin')
def run(args):
 with (out/'encode.log').open('a') as f:subprocess.run([str(ff/'ffmpeg.exe'),'-y',*args],stdout=f,stderr=subprocess.STDOUT,check=True)
run(['-framerate','30','-i',str(frames/'frame-%04d.png'),'-c:v','libx264','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(media/'continuous.mp4')])
run(['-i',str(media/'continuous.mp4'),'-vf','setpts=2*PTS','-an','-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(media/'half-speed.mp4')])
checks={p.name:json.loads(subprocess.check_output([str(ff/'ffprobe.exe'),'-v','error','-count_frames','-show_entries','stream=nb_read_frames,duration,width,height','-of','json',str(p)]))['streams'][0] for p in media.glob('*.mp4')}
(out/'video-validation.json').write_text(json.dumps(checks,indent=2))
grid=Image.new('RGB',(1280,4*390),(22,25,30));d=ImageDraw.Draw(grid)
for i,(f,label) in enumerate([(0,'Idle'),(90,'Walk'),(180,'Turn'),(270,'Run'),(345,'Stop'),(384,'Jump'),(495,'Crouch'),(690,'Final')]):
 im=Image.open(frames/f'frame-{f:04d}.png').convert('RGB');im.resize((640,360)).save(out/f'{label.lower()}.jpg',quality=95)
 x=i%2*640;y=i//2*390;grid.paste(im.resize((640,360)),(x,y+30));d.text((x+15,y+8),f'{label} / {f/30:.2f}s',fill='white')
grid.save(out/'keyframes.jpg',quality=95)
for src,name in [(r/'take-2-analysis.json','analysis.json'),(r/'compile-2.log','compile.log'),(r/'recipe.json','recipe.json'),(r/'analyze.py','analyze.py'),(r/'publish.py','publish.py')]:shutil.copy2(src,out/name)
for name in ['asset-identity.json','command.json','process.json','suite.json']:
 if (take/name).exists():shutil.copy2(take/name,out/name)
shutil.copy2(take/'continuous/state.json',out/'state.json')
for src,name in [(take/'runtime.log','runtime.log.gz'),(take/'continuous/locomotion.json','locomotion.json.gz')]:
 with gzip.open(out/name,'wb') as f:f.write(src.read_bytes())
print(out)
