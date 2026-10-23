from pathlib import Path
import subprocess,shutil
R=Path(__file__).resolve().parents[1];O=R/'Evidence/HairStructureRepair';O.mkdir(exist_ok=True)
refs={'interpolation':'HairStructureRepair/wavy-buffered','wavy':'HairDiversity/wavy','long':'HairDynamicRepair/long-prediction-m-final','ponytail':'HairDynamicRepair/ponytail-prediction-m-structured','bun':'HairDynamicRepair/bun-prediction-m-structured'}
ff=shutil.which('ffmpeg');assert ff
for key,ref in refs.items():
 old=R/'Saved'/ref/'pie/video';new=R/'Saved/HairStructureRepair'/('wavy-multi-guide' if key=='interpolation' else key+'-buffered')/'pie/video';out=O/(key+'-comparison.mp4')
 assert old.exists() and new.exists()
 for label,p in [('before',old),('after',new)]:shutil.copyfile(p/'frame-0000.png',O/(key+'-'+label+'.png'))
 if out.exists() and out.stat().st_size>10000:continue
 count=min(len(list(old.glob('frame-*.png'))),len(list(new.glob('frame-*.png'))))
 f="[0:v]scale=960:540,setsar=1[a];[1:v]scale=960:540,setsar=1[b];[a][b]hstack,pad=1920:580:0:40:color=0x101820,drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Before - recorded baseline':fontcolor=white:fontsize=22:x=20:y=8,drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='After - GPU data buffer':fontcolor=white:fontsize=22:x=980:y=8[v]"
 if key=='interpolation':f=f.replace('Before - recorded baseline','Single guide - fixed solver').replace('After - GPU data buffer','Multiple guides - same solver')
 cmd=[ff,'-hide_banner','-loglevel','error','-y','-c:v','mjpeg' if (old/'frame-0000.png').read_bytes()[:2]==b'\xff\xd8' else 'png','-framerate','30','-i',str(old/'frame-%04d.png'),'-c:v','mjpeg' if (new/'frame-0000.png').read_bytes()[:2]==b'\xff\xd8' else 'png','-framerate','30','-i',str(new/'frame-%04d.png'),'-filter_complex',f,'-map','[v]','-frames:v',str(count),'-c:v','libx264','-crf','20','-preset','fast','-pix_fmt','yuv420p','-movflags','+faststart',str(out)]
 subprocess.run(cmd,check=True,stderr=(O/(key+'-encode.log')).open('w'));print(key,count,out.stat().st_size,flush=True)
