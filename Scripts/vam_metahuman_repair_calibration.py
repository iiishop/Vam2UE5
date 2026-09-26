"""Explicit local calibration recipe for the locked Qimeng repair sample.

Not a universal Genesis/MH landmark map. Bone-guided surface correspondences
and camera calibration are retained for audit instead of UI mouse automation.
"""
import json, math, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'Saved/Python'))
import numpy as np
from PIL import Image
from vam_metahuman import to_creator,write_json
out=root/'Saved/MetaHuman/FitRepair'
recipe=json.loads((root/'Saved/MetaHuman/Jobs/MH00-local-proof/recipe.json').read_text(encoding='utf8'))
ir=json.loads(Path(recipe['inputs']['source_ir']['path']).read_text(encoding='utf8'))
old=json.loads((out/'old_target.json').read_text())
v=np.asarray(old['vertices'])[:,[1,0,2]]*[-1,1,1]
t=np.asarray(old['triangles']).reshape(-1,3)[:,[0,2,1]]
bones={b['name']:np.asarray(to_creator(b['position'])) for b in ir['skeleton']}
mh=json.loads((out/'preset-keypoints.json').read_text())
points,provenance={},{}
def nearest(p): return int(np.argmin(np.sum((v-p)**2,axis=1)))
def point(label,p,meaning):
    i=nearest(p);points[str(mh[label])]=v[i].tolist()
    provenance[label]={'mh_index':mh[label],'input_vertex':i,'meaning':meaning,'position':v[i].tolist()}
for side,labels in [('l',[82,83,84,85,96,97,98,99,100]),('r',[87,88,89,90,91,92,93,94,95])]:
    elbow=bones[side+'ForeArm'];hand=bones[side+'Hand']
    point('P'+str(labels[0]),elbow+[0,3,0],side+' elbow front')
    point('P'+str(labels[1]),elbow-[0,3,0],side+' elbow back')
    radial=bones[side+'Thumb1']-bones[side+'Pinky1'];radial/=np.linalg.norm(radial)
    point('P'+str(labels[2]),hand-2*radial,side+' ulnar wrist')
    point('P'+str(labels[3]),hand+2*radial,side+' radial wrist')
    for label,finger in zip(labels[4:],['Thumb','Index','Mid','Ring','Pinky']):
        last=bones[side+finger+'3'];previous=bones[side+finger+'2']
        point('P'+str(label),last+.8*(last-previous),side+' '+finger+' tip')
write_json(out/'hand-keypoints.json',{'keypoints':points,'provenance':provenance,'source_ir_sha256':recipe['inputs']['source_ir']['sha256']})
# Perspective rendering with explicit camera calibration; original geometry only.
size=512;cam=np.array([0.,65.,164.]);fov=40.;f=size/(2*math.tan(math.radians(fov/2)))
depth=cam[1]-v[:,1]
xy=np.column_stack((size/2+v[:,0]*f/depth,size/2-(v[:,2]-cam[2])*f/depth))
normal=np.zeros_like(v);face_n=np.cross(v[t[:,2]]-v[t[:,0]],v[t[:,1]]-v[t[:,0]])
for i in range(3):np.add.at(normal,t[:,i],face_n)
normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-9)
light=np.array([-.35,.85,.4]);light/=np.linalg.norm(light)
colors=95+150*np.maximum(0,normal@light)
pixels=np.full((size,size,3),35,dtype=np.uint8);zbuf=np.full((size,size),np.inf)
for ids in t:
    p=xy[ids];d=depth[ids]
    if d.min()<=0:continue
    lo=np.maximum(np.floor(p.min(0)).astype(int),0);hi=np.minimum(np.ceil(p.max(0)).astype(int),size-1)
    if np.any(lo>hi):continue
    x,y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
    denom=(p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
    if abs(denom)<1e-10:continue
    a=((p[1,1]-p[2,1])*(x-p[2,0])+(p[2,0]-p[1,0])*(y-p[2,1]))/denom
    b=((p[2,1]-p[0,1])*(x-p[2,0])+(p[0,0]-p[2,0])*(y-p[2,1]))/denom;c=1-a-b
    inv=a/d[0]+b/d[1]+c/d[2];z=1/np.maximum(inv,1e-10)
    oldz=zbuf[lo[1]:hi[1]+1,lo[0]:hi[0]+1];mask=(a>=0)&(b>=0)&(c>=0)&(z<oldz)
    shade=np.clip((a*colors[ids[0]]/d[0]+b*colors[ids[1]]/d[1]+c*colors[ids[2]]/d[2])/np.maximum(inv,1e-10),0,255).astype(np.uint8)
    oldz[mask]=z[mask];region=pixels[lo[1]:hi[1]+1,lo[0]:hi[0]+1];region[mask]=shade[mask,None]
Image.fromarray(pixels).save(out/'tracking-input.png')
write_json(out/'tracking-camera.json',{'location':cam.tolist(),'rotation':[0,-90,0],'fov':fov,'size':size})
