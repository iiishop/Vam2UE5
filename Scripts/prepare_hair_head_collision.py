"""Fit head collision from the actual A1 skinned surface, not from hair clearance."""
from pathlib import Path
import json,hashlib,argparse
import numpy as np
from scipy.spatial import cKDTree
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('audit',type=Path);p.add_argument('source',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
d=json.loads((a.audit/'04-jiggle/inputs.json').read_text());v=np.array(json.loads((a.audit/'04-jiggle/surface.json').read_text())['positions']);bones=d['bones'];head=next(i for i,b in enumerate(bones) if b['name']=='head');m=np.array(bones[head]['component_matrix_row_major']).reshape(4,4);v=(np.c_[v,np.ones(len(v))]@np.linalg.inv(m))[:,:3]
children={head}
for i,b in enumerate(bones):
 if b['parent'] in children:children.add(i)
mask=np.array([sum(w for b,w in row if b in children)>.8 for row in d['render_vertex_influences']]);tri=np.array(d['triangle_indices']).reshape(-1,3);tv=v[tri];norm=np.cross(tv[:,1]-tv[:,0],tv[:,2]-tv[:,0]);norm/=np.maximum(np.linalg.norm(norm,axis=1)[:,None],1e-12)
# Use a horizontal ray so the open neck cut does not close over the jaw.
e1=tv[:,1]-tv[:,0];e2=tv[:,2]-tv[:,0];direction=np.array([1.,.013,0.]);h=np.cross(direction,e2);det=np.sum(e1*h,axis=1);inv=np.divide(1.,det,out=np.zeros_like(det),where=np.abs(det)>1e-10)
pts=v[mask];lo=pts.min(0);hi=pts.max(0);axes=[np.arange(lo[k]+.4,hi[k],.8) for k in range(3)];grid=np.array(np.meshgrid(*axes,indexing='ij')).reshape(3,-1).T;inside=[]
for start in range(0,len(grid),32):
 q=grid[start:start+32];s=q[:,None,:]-tv[None,:,0];u=np.sum(s*h,axis=2)*inv;tq=np.cross(s,e1);w=np.sum(tq*direction,axis=2)*inv;t=np.sum(tq*e2,axis=2)*inv;hit=(u>=0)&(w>=0)&(u+w<=1)&(t>1e-7)&(np.abs(det)>1e-10);inside.extend((hit.sum(1)%2)==1)
grid=grid[np.array(inside)];centers=tv.mean(1);norm*=1 if np.mean(np.sum((centers-pts.mean(0))*norm,axis=1))>0 else -1;tree=cKDTree(centers);maxrad=np.linalg.norm(tv-centers[:,None,:],axis=2).max();vtree=cKDTree(pts)
def distance(q):
 initial=vtree.query(q)[0];ix=tree.query_ball_point(q,initial+maxrad+1e-6);t=tv[ix];ab=t[:,1]-t[:,0];ac=t[:,2]-t[:,0];ap=q-t[:,0];n=norm[ix];project=q-n*np.sum(ap*n,axis=1)[:,None];vp=project-t[:,0];aa=np.sum(ab*ab,axis=1);bb=np.sum(ab*ac,axis=1);cc=np.sum(ac*ac,axis=1);dd=np.sum(vp*ab,axis=1);ee=np.sum(vp*ac,axis=1);den=aa*cc-bb*bb;U=np.divide(cc*dd-bb*ee,den,out=np.zeros_like(den),where=abs(den)>1e-15);V=np.divide(aa*ee-bb*dd,den,out=np.zeros_like(den),where=abs(den)>1e-15);best=np.where((U>=0)&(V>=0)&(U+V<=1),np.sum((q-project)**2,axis=1),np.inf)
 for k in range(3):
  A=t[:,k];B=t[:,(k+1)%3];edge=B-A;alpha=np.clip(np.sum((q-A)*edge,axis=1)/np.maximum(np.sum(edge*edge,axis=1),1e-15),0,1);best=np.minimum(best,np.sum((q-A-alpha[:,None]*edge)**2,axis=1))
 j=int(np.argmin(best));return float(np.sqrt(best[j])) if np.dot(q-t[j,0],n[j])<0 else -1.0
radii=np.array([distance(q) for q in grid])*.97-.06;valid=radii>.18;grid=grid[valid];radii=radii[valid];covered=np.zeros(len(grid),bool);chosen=[]
# Greedy medial-ball cover, bounded for the native GPU primitive search.
from scipy.sparse import csr_matrix
neighbors=cKDTree(grid).query_ball_point(grid,radii);indptr=np.cumsum([0]+[len(x) for x in neighbors]);indices=np.concatenate(neighbors);cover=csr_matrix((np.ones(len(indices)),indices,indptr),shape=(len(grid),len(grid)))
for _ in range(80):
 scores=np.asarray(cover@(~covered).astype(float)).ravel();scores[chosen]=-1;i=int(np.argmax(scores))
 if scores[i]<=0:break
 chosen.append(i);covered[neighbors[i]]=True
 if covered.mean()>.995:break
spheres=[dict(center=grid[i].tolist(),radius_cm=float(radii[i])) for i in chosen]
source=json.loads(a.source.read_text());source['head_collision']={'version':'a1-surface-medial-spheres-1','reference_body':json.loads((a.audit/'manifest.json').read_text())['stages']['04-jiggle']['mesh'] if 'stages' in json.loads((a.audit/'manifest.json').read_text()) else '', 'space':'head_bone_local_cm','audit':str(a.audit),'surface_sha256':hashlib.sha256((a.audit/'04-jiggle/surface.json').read_bytes()).hexdigest(),'spheres':spheres,'interior_grid_coverage':float(covered.mean()),'candidate_count':len(grid),'mesh_triangles':len(tri)}
source.pop('identity');source['identity']=hashlib.sha256(json.dumps(source,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest();a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(source,separators=(',',':')))
np.savez(a.out.with_suffix('.npz'),vertices=v,triangles=tri,head_mask=mask)
fig,ax=plt.subplots(1,2,figsize=(12,7))
for axis,ids in zip(ax,[(0,2),(1,2)]):
 axis.scatter(pts[:,ids[0]],pts[:,ids[1]],s=.2,c='gray',alpha=.35)
 for s in spheres:axis.add_patch(plt.Circle((s['center'][ids[0]],s['center'][ids[1]]),s['radius_cm'],fill=False,alpha=.35,color='tab:blue'))
 axis.set_aspect('equal');axis.set_xlabel('Head-local cm');axis.set_title('Actual A1 surface / proposed inner spheres')
fig.tight_layout();fig.savefig(a.out.with_suffix('.png'),dpi=150);print(json.dumps(source['head_collision'],indent=2))
