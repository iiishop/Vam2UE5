import heapq,json,numpy as np
from pathlib import Path
root=Path('Saved/LowerBodyContact'); r=root/'GluteShapeRecovery';a=np.load(root/'SeatedFoldOrigin/parts.npz'); ids=a['source_ids'];tri=a['tri'];p=a['base'];f=a['source_before'];proc=a['procedural_before'];old=a['actual'];amp=np.linalg.norm(f,axis=1)
initial=np.clip(np.sum(a['source']*f,axis=1)/np.maximum(1e-12,np.sum(f*f,axis=1)),0,1);initial[amp<1e-5]=1
edges=np.unique(np.sort(np.concatenate([tri[:,[0,1]],tri[:,[1,2]],tri[:,[2,0]]]),axis=1),axis=0)
edgefaces={}
for i,t in enumerate(tri):
 for u,v in zip(t,np.roll(t,-1)):edgefaces.setdefault(tuple(sorted((int(ids[u]),int(ids[v])))),set()).add(i)
# Render split duplicates can repeat faces; collapse using source triangle identities.
facekey={i:tuple(sorted(ids[t])) for i,t in enumerate(tri)}
pairs=[]
for e,faces in edgefaces.items():
 unique={facekey[i]:i for i in faces}
 if len(unique)==2 and min(e)>=0:pairs.append((e,list(unique.values())))
roi=(np.abs(p[:,1])>3)&(np.abs(p[:,1])<17)&(p[:,0]<-2)&(p[:,2]>82)&(p[:,2]<110)
first={};alias=[]
for v,i in enumerate(ids):
 if i<0:continue
 if i in first:alias.append((v,first[i]))
 else:first[i]=v
out=[]
for factor in [4,3,2,1.5,1,.75,.5]:
 adj=[[] for _ in p];m=initial.copy()
 for u,v in edges:
  c=np.linalg.norm(p[u]-p[v])/max(1e-5,factor*max(amp[u],amp[v]));adj[u].append((v,c));adj[v].append((u,c))
 for u,v in alias:adj[u].append((v,0));adj[v].append((u,0))
 q=list(zip(m.tolist(),range(len(p))));heapq.heapify(q)
 while q:
  val,u=heapq.heappop(q)
  if val>m[u]+1e-12:continue
  for v,c in adj[u]:
   if m[v]>val+c+1e-12:m[v]=val+c;heapq.heappush(q,(m[v],v))
 x=p+(f+proc)*m[:,None];t=x[tri];n=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);n/=np.maximum(1e-20,np.linalg.norm(n,axis=1))[:,None]
 angles=[np.degrees(np.arccos(np.clip(n[ts[0]]@n[ts[1]],-1,1))) for e,ts in pairs if e[0] in first and roi[first[e[0]]]]
 row=dict(factor=factor,max_angle=max(angles),over45=int(sum(v>45 for v in angles)),sides={})
 for sign in [-1,1]:
  ix=roi&(p[:,1]*sign>0);err=np.linalg.norm(x[ix]-old[ix],axis=1);row['sides'][str(sign)]=dict(oldshape_rms=float(np.sqrt(np.mean(err**2))),retained=float(np.mean(m[ix])))
 out.append(row);np.savez_compressed(r/f'probe-{factor}.npz',positions=x,mask=m)
(r/'sweep.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))

