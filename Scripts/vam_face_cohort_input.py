"""Prepare an independent preset cohort through the existing locked p0 adapter.

Camera framing uses source material ownership and bounds, never a person ID.
The image is local tracker evidence, not a verified topology annotation.
"""
import argparse,json,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
import numpy as np
from PIL import Image
from vam_metahuman import prepare,verify_recipe,write_json
from vam_face_surface import normals


def tracking_image(source,output,size=512):
    v=np.asarray(source['vertices']);t=np.asarray(source['triangles']).reshape(-1,3)
    face=t[np.isin(source['triangle_materials'],('Face','Lips','Nostrils'))]
    if not len(face):raise ValueError('SourceFaceMaterialMissing')
    p=v[np.unique(face)];lo=p.min(0);hi=p.max(0);center=(lo+hi)/2
    fov=40.;span=max(hi[0]-lo[0],hi[2]-lo[2]);distance=1.25*span/(2*math.tan(math.radians(fov)/2))
    camera=np.array([center[0],hi[1]+distance,center[2]])
    focal=size/(2*math.tan(math.radians(fov)/2));depth=camera[1]-v[:,1]
    xy=np.c_[size/2+(v[:,0]-camera[0])*focal/depth,size/2-(v[:,2]-camera[2])*focal/depth]
    n,_=normals(v,t);light=np.array([-.35,.85,.4]);light/=np.linalg.norm(light)
    color=65+175*np.abs(n@light);image=np.full((size,size,3),35,np.uint8);zbuf=np.full((size,size),np.inf)
    projected=xy[t];boxes_lo=np.maximum(np.floor(projected.min(1)).astype(int),0);boxes_hi=np.minimum(np.ceil(projected.max(1)).astype(int),size-1)
    for ids in t[np.all(boxes_lo<=boxes_hi,axis=1)]:
        q=xy[ids];d=depth[ids]
        if d.min()<=0:continue
        low=np.maximum(np.floor(q.min(0)).astype(int),0);high=np.minimum(np.ceil(q.max(0)).astype(int),size-1)
        x,y=np.meshgrid(np.arange(low[0],high[0]+1)+.5,np.arange(low[1],high[1]+1)+.5)
        den=(q[1,1]-q[2,1])*(q[0,0]-q[2,0])+(q[2,0]-q[1,0])*(q[0,1]-q[2,1])
        if abs(den)<1e-12:continue
        a=((q[1,1]-q[2,1])*(x-q[2,0])+(q[2,0]-q[1,0])*(y-q[2,1]))/den
        b=((q[2,1]-q[0,1])*(x-q[2,0])+(q[0,0]-q[2,0])*(y-q[2,1]))/den;c=1-a-b
        inv=a/d[0]+b/d[1]+c/d[2];z=1/np.maximum(inv,1e-12)
        old=zbuf[low[1]:high[1]+1,low[0]:high[0]+1];mask=(a>=0)&(b>=0)&(c>=0)&(z<old)
        shade=np.clip((a*color[ids[0]]/d[0]+b*color[ids[1]]/d[1]+c*color[ids[2]]/d[2])/np.maximum(inv,1e-12),0,255).astype(np.uint8)
        old[mask]=z[mask];image[low[1]:high[1]+1,low[0]:high[0]+1][mask]=shade[mask,None]
    Image.fromarray(image).save(output)
    return {'CameraViewInfo':{'Location':dict(zip(('X','Y','Z'),camera.tolist())),
        'Rotation':{'Pitch':0,'Yaw':-90,'Roll':0},'FOV':fov,'AspectRatio':1},'ImageSize':{'X':size,'Y':size},
        'framing_evidence':'source Face/Lips/Nostrils material bounds; shared FOV and relative margin'}


def run(request):
    r=json.loads(Path(request).read_text(encoding='utf8'));out=Path(r['output']);job=out/'Initial'
    if not (job/'recipe.json').exists():
        prepare(r['data'],r['preview'],r['initial_name'],r['destination'],job,r['engine'])
    recipe=verify_recipe(job);source=json.loads((job/'target.json').read_text(encoding='utf8'))
    if not (out/'tracking-camera.json').exists():
        write_json(out/'tracking-camera.json',tracking_image(source,out/'tracking-input.png'))
    write_json(out/'source-summary.json',{'preset':r['preset'],'decode_id':recipe['decode_id'],'plan_id':recipe['plan_id'],
        'topology_family':source['source_topology_family'],'topology_digest':source['source_base_topology_sha256'],
        'included_morphs':source['included_morphs'],'excluded_morphs':source['excluded_morphs'],
        'source_inputs':recipe['inputs'],'visual_acceptance_passed':False})
    print(json.dumps({'preset':r['preset'],'state':'PreparedNeutralP0','output':str(out)},ensure_ascii=False),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);a=p.parse_args();run(a.request)
