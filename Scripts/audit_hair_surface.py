"""Head-local guide/surface proximity audit. Signed nearest-face distance is a
local diagnostic, not a watertight-body penetration certificate. Excludes the
first 10% of each guide (intentional scalp attachment). No image inference.
"""
import argparse,json,csv
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('mesh',type=Path);p.add_argument('root',type=Path);p.add_argument('out',type=Path);p.add_argument('cases',nargs='+');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
mesh=np.load(a.mesh);v=mesh['vertices'];tri=mesh['triangles'];tv=v[tri];centers=tv.mean(1);tree=cKDTree(centers)
n=np.cross(tv[:,1]-tv[:,0],tv[:,2]-tv[:,0]);n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
# Imported UE triangle winding is inward in this export. Derive orientation
# from signed volume, rather than assuming the cross product points outward.
orientation=np.sign(np.sum(np.einsum('ij,ij->i',tv[:,0],np.cross(tv[:,1],tv[:,2]))))
assert orientation!=0
n*=orientation
def signed_distance(points):
 result=[]
 for start in range(0,len(points),256):
  q=points[start:start+256];_,ix=tree.query(q,k=48);t=tv[ix];normal=n[ix];Q=q[:,None,:];ab=t[:,:,1]-t[:,:,0];ac=t[:,:,2]-t[:,:,0];ap=Q-t[:,:,0]
  signed=np.sum(ap*normal,axis=2);proj=Q-signed[:,:,None]*normal;vp=proj-t[:,:,0]
  aa=np.sum(ab*ab,2);bb=np.sum(ab*ac,2);cc=np.sum(ac*ac,2);dd=np.sum(vp*ab,2);ee=np.sum(vp*ac,2);den=aa*cc-bb*bb
  U=np.divide(cc*dd-bb*ee,den,out=np.zeros_like(den),where=abs(den)>1e-15);V=np.divide(aa*ee-bb*dd,den,out=np.zeros_like(den),where=abs(den)>1e-15)
  best=np.where((U>=0)&(V>=0)&(U+V<=1),signed*signed,np.inf)
  for k in range(3):
   A=t[:,:,k];edge=t[:,:,(k+1)%3]-A;alpha=np.clip(np.sum((Q-A)*edge,2)/np.maximum(np.sum(edge*edge,2),1e-15),0,1);best=np.minimum(best,np.sum((Q-A-alpha[:,:,None]*edge)**2,2))
  j=best.argmin(1);result.extend(np.sqrt(best[np.arange(len(q)),j])*np.sign(signed[np.arange(len(q)),j]))
 return np.array(result)
columns=min(3,len(a.cases));row_count=(len(a.cases)+columns-1)//columns
rows=[];rest_rows=[];fig,axes=plt.subplots(row_count,columns,figsize=(5*columns,6*row_count),squeeze=False)
for ax in list(axes.flat)[len(a.cases):]:ax.set_visible(False)
head=v[mesh['head_mask']]
for name,ax in zip(a.cases,axes.flat):
 data=json.loads((a.root/name/'capture.json').read_text());worst=None
 for frame_index,frame in enumerate(data['gpu_samples']):
  for g in frame['groups']:
   if g['group'] not in (1,2):continue
   points=np.array(g['guide_points']);nodes=len(points)//g['guide_curves'];free=np.arange(len(points))%nodes>=max(2,int(nodes*.1));points=points[free];sd=signed_distance(points)
   if frame_index==0:
    rest_sd=signed_distance(np.array(g['guide_rest_points'])[free]);rest_rows.append(dict(case=name,group=g['group'],free_nodes=len(rest_sd),inside_02cm=int((rest_sd<-.2).sum()),minimum_signed_cm=float(rest_sd.min())))
   spheres=[s for s in frame['collision_proxies_head_local'] if s['bone']=='head']
   A=np.array([s['a'] for s in spheres]);B=np.array([s['b'] for s in spheres]);edge=B-A;delta=points[:,None,:]-A[None,:,:];alpha=np.clip(np.sum(delta*edge,axis=2)/np.maximum(np.sum(edge*edge,axis=1),1e-12),0,1)
   proxy=np.min(np.linalg.norm(delta-alpha[:,:,None]*edge,axis=2)-np.array([s['radius_cm'] for s in spheres]),axis=1)
   rows.append(dict(case=name,time=frame['time'],group=g['group'],free_nodes=len(points),inside_02cm=int((sd<-.2).sum()),inside_05cm=int((sd<-.5).sum()),minimum_signed_cm=float(sd.min()),inside_actual_head_spheres_02cm=int((proxy<-.2).sum()),minimum_sphere_distance_cm=float(proxy.min())))
   if worst is None or sd.min()<worst[0]:worst=(sd.min(),points,sd,frame['time'])
 ax.scatter(head[:,0],head[:,2],s=.2,c='gray',alpha=.3);ax.scatter(worst[1][:,0],worst[1][:,2],s=3,c=np.where(worst[2]<-.2,'red','tab:blue'));ax.set_aspect('equal');ax.set_title(f'{name}: worst local clearance\nt={worst[3]:.1f}s, {worst[0]:.2f} cm');ax.set_xlabel('Head local X (cm)')
fig.tight_layout();fig.savefig(a.out/'surface-clearance.png',dpi=160)
with (a.out/'surface-clearance.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
summary={name:dict(max_nodes_below_minus_02cm=max(r['inside_02cm'] for r in rows if r['case']==name),minimum_signed_cm=min(r['minimum_signed_cm'] for r in rows if r['case']==name),max_nodes_inside_head_proxy_02cm=max(r['inside_actual_head_spheres_02cm'] for r in rows if r['case']==name),minimum_head_proxy_distance_cm=min(r['minimum_sphere_distance_cm'] for r in rows if r['case']==name)) for name in a.cases};(a.out/'surface-clearance-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
(a.out/'rest-clearance.json').write_text(json.dumps(rest_rows,indent=2))
