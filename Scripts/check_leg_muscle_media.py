"""Verify encoded sample frames against original UE captures, excluding labels."""
import argparse,json,struct,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('out',type=Path);p.add_argument('--groups',nargs='+',default=['C','M','S','R']);p.add_argument('--actions',nargs='+',default=['flexion','knee-flexion','seated-stop']);p.add_argument('--frames',nargs='+',type=int,default=[45,135]);a=p.parse_args();rows=[]
for action in a.actions:
 for g in a.groups:
  stem=g+'-'+action;video=a.out/stem/'motion.mp4';src=a.run/g/stem/'video'
  probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration','-of','json',str(video)]))['streams'][0]
  expected=len(list(src.glob('frame-*.png')));assert int(probe['nb_read_frames'])==expected and abs(float(probe['duration'])-expected/30)<.01
  b=(src/'frame-0000.png').read_bytes();pos=8
  while pos+12<=len(b):
   n=struct.unpack('>I',b[pos:pos+4])[0];typ=b[pos+4:pos+8];pos+=n+12
   if typ==b'IEND':break
  for frame in a.frames:
   decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(video),'-vf',f'select=eq(n\\,{frame})'.replace('\\\\','\\'),'-fps_mode','passthrough','-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','-'])
   x=np.array(Image.open(src/('frame-%04d.png'%frame)).convert('RGB'));y=np.frombuffer(decoded,dtype=np.uint8).reshape(x.shape)
   mse=float(np.mean((x[60:].astype(float)-y[60:].astype(float))**2));psnr=float(10*np.log10(255**2/max(mse,1e-12)));assert psnr>30,(stem,frame,psnr)
   rows.append(dict(clip=stem,frame=frame,decoded_psnr_db=psnr,source_first_png_trailing_bytes=len(b)-pos))
 for suffix in ['C-M']:
  video=a.out/(action+'-'+suffix+'.mp4');probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,duration','-of','json',str(video)]))['streams'][0];assert int(probe['nb_read_frames'])==expected
(a.out/'media-validation.json').write_text(json.dumps(dict(samples=rows,scope='Frame count/duration and decoded pixel comparison outside status label. Source UE PNG files have trailing bytes after IEND; ffmpeg warnings did not prevent decoding these verified frames.'),indent=2));print(len(rows),'decoded frames verified; min PSNR',min(r['decoded_psnr_db'] for r in rows))
