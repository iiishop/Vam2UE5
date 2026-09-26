"""Same-pose local head/hand mesh comparisons, preserving actual coordinates."""
import json
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'Saved/Python'))
import numpy as np
from PIL import Image,ImageDraw
folder=root/'Saved/MetaHuman/FitRepair'
target=json.loads((folder/'old_target.json').read_text())
source=np.asarray(target['vertices'])[:,[1,0,2]]*[-1,1,1]
src_t=np.asarray(target['triangles']).reshape(-1,3)[:,[0,2,1]]
fit=json.loads((folder/(sys.argv[1] if len(sys.argv)>1 else 'posed_state.json')).read_text())
solved=np.asarray(fit['vertices'])[:,[0,2,1]]
fit_t=np.asarray(fit['triangles']).reshape(-1,3)
print('source',source.min(0),source.max(0),'posed fit',solved.min(0),solved.max(0))
views=[('Face front',(0,1,2),(-12,12,148,179)),('Face side',(1,0,2),(-13,22,148,179)),
       ('Hand right front',(0,1,2),(49,79,115,148)),('Hand left front',(0,1,2),(-79,-49,115,148)),
       ('Hand right top',(0,2,1),(49,79,-13,20)),('Hand left top',(0,2,1),(-79,-49,-13,20))]
im=Image.new('RGB',(960,480*len(views)),'#22272d');draw=ImageDraw.Draw(im)
for row,(label,axes,bounds) in enumerate(views):
    for col,(v,t,name) in enumerate([(source,src_t,'SOURCE'),(solved,fit_t,'FIT same pose')]):
        q=v[:,axes];coords=q[t]
        lo,hi,bottom,top=bounds
        valid=(coords[:,:,0].max(1)>lo)&(coords[:,:,0].min(1)<hi)&(coords[:,:,2].max(1)>bottom)&(coords[:,:,2].min(1)<top)
        coords=coords[valid]
        normal=np.cross(coords[:,1]-coords[:,0],coords[:,2]-coords[:,0]);n=np.linalg.norm(normal,axis=1)
        shade=np.abs(normal@np.array([-.35,.8,.45]))/np.maximum(n,1e-9)
        scale=min(440/(hi-lo),420/(top-bottom))
        tile=Image.new('RGB',(480,480),'#22272d');td=ImageDraw.Draw(tile)
        for i in np.argsort(coords[:,:,1].mean(1)):
            pts=[(240+(p[0]-(lo+hi)/2)*scale,260-(p[2]-(bottom+top)/2)*scale) for p in coords[i]]
            c=int(65+160*min(shade[i],1));td.polygon(pts,fill=(c,c,c))
        td.text((12,12),label+' / '+name,fill='white');im.paste(tile,(col*480,row*480))
im.save(folder/'details.png')
