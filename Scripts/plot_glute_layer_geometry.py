"""Orthographic numerical surface diagnostic from A1 data, NOT an Unreal screenshot."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('state',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);rd=lambda p:json.loads(p.read_text());b=Path(rd(a.state)['audit_bundle']);stages=['00-source-lbs','01-final-weights','02-static-helpers','03-corrective'];canvas=Image.new('RGB',(1600,540),(20,22,26))
for index,stage in enumerate(stages):
 d=rd(b/stage/'inputs.json');v=np.array(rd(b/stage/'surface.json')['positions']);tri=np.array(d['triangle_indices']).reshape(-1,3);t=v[tri];mask=(t[:,:,2].max(axis=1)>82)&(t[:,:,2].min(axis=1)<114)&(np.abs(t[:,:,1]).min(axis=1)<20);t=t[mask];im=Image.new('RGB',(400,500),(25,27,30));draw=ImageDraw.Draw(im)
 # Camera from posterior (-X), so paint increasing closeness (smaller X).
 for q in t[np.argsort(t[:,:,0].mean(axis=1))[::-1]]:
  n=-np.cross(q[1]-q[0],q[2]-q[0]);n/=max(1e-20,np.linalg.norm(n));light=np.array([-.7,-.25,.5]);light/=np.linalg.norm(light);c=int(255*(.25+.65*max(0,float(n@light))));coords=[(200+y*9,470-(z-82)*12) for x,y,z in q];draw.polygon(coords,fill=(c,c,c))
 canvas.paste(im,(index*400,35));ImageDraw.Draw(canvas).text((index*400+8,8),stage,fill='white')
canvas.save(a.out/'a1-layer-geometry.png');print(a.out/'a1-layer-geometry.png')
