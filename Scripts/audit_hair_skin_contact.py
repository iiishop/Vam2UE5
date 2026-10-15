"""Cross-check hair contacts against the actual A1 skin, including render samples.

An outer-head hull rejects impossible inside classifications. Remaining points
use generalized winding of the full exported body (not nearest-face normal sign).
Open/nonmanifold source surfaces remain a limitation; report winding explicitly.
"""
import argparse,csv,json
from pathlib import Path
import numpy as np
from scipy.spatial import ConvexHull,cKDTree

p=argparse.ArgumentParser();p.add_argument('mesh',type=Path);p.add_argument('source',type=Path);p.add_argument('capture',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
m=np.load(a.mesh);v=m['vertices'];tri=m['triangles'];tv=v[tri];h=ConvexHull(v[m['head_mask']]);tree=cKDTree(tv.mean(1))
source=json.loads(a.source.read_text());capture=json.loads(a.capture.read_text());rows=[];worst=[]

def winding(points):
    result=[]
    for q in points:
        A=tv[:,0]-q;B=tv[:,1]-q;C=tv[:,2]-q
        la=np.linalg.norm(A,axis=1);lb=np.linalg.norm(B,axis=1);lc=np.linalg.norm(C,axis=1)
        numerator=np.einsum('ij,ij->i',A,np.cross(B,C))
        denominator=la*lb*lc+np.einsum('ij,ij->i',A,B)*lc+np.einsum('ij,ij->i',B,C)*la+np.einsum('ij,ij->i',C,A)*lb
        result.append(abs(np.arctan2(numerator,denominator).sum()/(2*np.pi)))
    return np.asarray(result)

def distance(points):
    if not len(points):return np.zeros(0)
    _,ix=tree.query(points,k=48);t=tv[ix];Q=points[:,None,:]
    ab=t[:,:,1]-t[:,:,0];ac=t[:,:,2]-t[:,:,0];n=np.cross(ab,ac);n/=np.maximum(np.linalg.norm(n,axis=2)[:,:,None],1e-15)
    sd=np.sum((Q-t[:,:,0])*n,axis=2);pr=Q-sd[:,:,None]*n-t[:,:,0]
    aa=np.sum(ab*ab,2);bb=np.sum(ab*ac,2);cc=np.sum(ac*ac,2);dd=np.sum(pr*ab,2);ee=np.sum(pr*ac,2);den=aa*cc-bb*bb
    U=np.divide(cc*dd-bb*ee,den,out=np.zeros_like(den),where=abs(den)>1e-15);V=np.divide(aa*ee-bb*dd,den,out=np.zeros_like(den),where=abs(den)>1e-15)
    best=np.where((U>=0)&(V>=0)&(U+V<=1),sd*sd,np.inf)
    for k in range(3):
        A=t[:,:,k];E=t[:,:,(k+1)%3]-A;alpha=np.clip(np.sum((Q-A)*E,2)/np.maximum(np.sum(E*E,2),1e-15),0,1);best=np.minimum(best,np.sum((Q-A-alpha[:,:,None]*E)**2,2))
    return np.sqrt(best.min(1))

for frame in capture['gpu_samples']:
    for g in frame['groups']:
        if g['group'] not in (1,2):continue
        for kind in ['guide','render']:
            points=np.asarray(g[kind+'_points'])
            if kind=='guide':nodes=len(points)//g['guide_curves'];ids=np.arange(len(points))
            else:
                counts=set(map(len,source['groups'][g['group']]['render_curves']))
                assert len(counts)==1, 'Render root exclusion needs explicit variable-length offsets'
                nodes=next(iter(counts));ids=np.asarray(g['render_point_ids'])
            free=ids%nodes>=max(2,int(nodes*.1));points=points[free];ids=ids[free]
            inside=np.zeros(len(points),bool)
            for start in range(0,len(points),256):inside[start:start+256]=(points[start:start+256]@h.equations[:,:3].T+h.equations[:,3]).max(1)<=0
            candidates=points[inside];w=winding(candidates);selected=candidates[w>.5];d=distance(selected)
            row=dict(time=frame['time'],group=g['group'],kind=kind,free_samples=len(points),hull_candidates=len(candidates),winding_inside=len(selected),inside_over_02cm=int((d>.2).sum()),max_depth_cm=float(d.max()) if len(d) else 0.)
            rows.append(row)
            if len(d):j=int(d.argmax());worst.append(dict(**row,point=selected[j].tolist(),winding=float(w[w>.5][j])))
a.out.mkdir(parents=True,exist_ok=True)
with (a.out/'skin.csv').open('w',newline='') as f:writer=csv.DictWriter(f,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
summary={k:dict(max_inside_over_02cm=max(r['inside_over_02cm'] for r in rows if r['kind']==k),max_depth_cm=max(r['max_depth_cm'] for r in rows if r['kind']==k)) for k in ['guide','render']}
(a.out/'summary.json').write_text(json.dumps(dict(summary=summary,worst=sorted(worst,key=lambda x:x['max_depth_cm'],reverse=True)[:12],scope='Static A1 head, once/sec geometry, sampled render points; root prefix excluded; winding threshold 0.5; nearest-48 triangle distances'),indent=2));print(json.dumps(summary))
