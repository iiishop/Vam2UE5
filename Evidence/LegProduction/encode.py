from pathlib import Path
import subprocess,json,sys
r=Path(__file__).resolve().parent
exe=Path('C:/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0.1-full_build/bin')
out=r/'media';out.mkdir(exist_ok=True)
def run(args):
 with (out/'encode.log').open('a') as f:subprocess.run([str(exe/'ffmpeg.exe'),'-y',*args],stdout=f,stderr=subprocess.STDOUT,check=True)
for label in sys.argv[1:] or ['before','after']:
 for case in ['flexion','step','hard-stop','seated-stop']:
  folder=r/(label+'-v3')/case/'video';frames=list(folder.glob('frame-*.png'));assert len(frames)==210,(folder,len(frames))
  run(['-framerate','30','-i',str(folder/'frame-%04d.png'),'-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(label+'-'+case+'.mp4'))])
for case in ['flexion','step','hard-stop','seated-stop']:
 a=out/('before-'+case+'.mp4');b=out/('after-'+case+'.mp4')
 if a.exists() and b.exists():
  run(['-i',str(a),'-i',str(b),'-filter_complex','[0:v][1:v]hstack=inputs=2[v]','-map','[v]','-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(case+'-comparison.mp4'))])
  run(['-i',str(a),'-i',str(b),'-filter_complex','[0:v]crop=330:330:270:260[a];[1:v]crop=330:330:270:260[b];[a][b]hstack=inputs=2[v]','-map','[v]','-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(case+'-detail.mp4'))])
checks={}
for p in out.glob('*.mp4'):
 x=json.loads(subprocess.check_output([str(exe/'ffprobe.exe'),'-v','error','-count_frames','-show_entries','stream=nb_read_frames,duration,width,height','-of','json',str(p)]))['streams'][0]
 assert int(x['nb_read_frames'])==210 and abs(float(x['duration'])-7)<.01,x
 checks[p.name]=x
(out/'validation.json').write_text(json.dumps(checks,indent=2));print('Validated',len(checks),'videos')
