import json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
out=Path('Evidence/KneeDeepRepair');run=Path('Saved/KneeDeepRepair/pie-final');rows=[]
for g in ['C','M']:
 for action in ['deep-kneel','single-flexion']:
  stem=g+'-'+action;n=270 if action=='deep-kneel' else 180
  for name in ['after.mp4','before-after.mp4']:
   video=out/stem/name;probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=nb_read_frames,width,height,duration','-of','json',str(video)]))['streams'][0];assert int(probe['nb_read_frames'])==n and abs(float(probe['duration'])-n/30)<.01
   for frame in [45,135,165]:
    raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(video),'-vf',f'select=eq(n\\,{frame})','-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','-']);decoded=np.frombuffer(raw,dtype=np.uint8).reshape(probe['height'],probe['width'],3)[:,-768:];src=np.array(Image.open(run/g/stem/'video'/('frame-%04d.png'%frame)).convert('RGB'));mse=np.mean((decoded.astype(float)-src.astype(float))**2);psnr=float(10*np.log10(255**2/max(mse,1e-12)));assert psnr>30,(stem,name,frame,psnr);rows.append(dict(clip=stem,video=name,frame=frame,psnr_db=psnr))
(out/'media-validation.json').write_text(json.dumps(rows,indent=2));print('Verified 8 streams and',len(rows),'decoded frames; min PSNR',min(x['psnr_db'] for x in rows))
