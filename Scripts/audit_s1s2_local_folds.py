"""Local fold statistics on source-provenance neighborhoods, not production masks."""
import argparse,json
from pathlib import Path
import numpy as np
from audit_lower_body_boundaries import read,data,stats
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('pairs',type=Path);p.add_argument('output',type=Path);a=p.parse_args();states=[read(a.before),read(a.after)];bundles=[Path(s['audit_bundle']) for s in states];pairs=read(a.pairs)['pairs'];b=bundles[0];mapping=read(b/'source-mapping.json');ids=np.array([mapping['input_to_source'][i] for i in mapping['render_to_input']]);tri=ids[np.array(read(b/'00-source-lbs/inputs.json')['triangle_indices']).reshape(-1,3)];adj={};edges={}
for t,face in enumerate(tri):
 for u,v in zip(face,np.roll(face,-1)):
  adj.setdefault(int(u),set()).add(int(v));adj.setdefault(int(v),set()).add(int(u));edges.setdefault(tuple(sorted((int(u),int(v)))),[]).append(t)
rois={}
for name,seeds in [('upper-right',[19403,19495,11221,19501]),('lower-right',[11270,11273,21011,21014,21015])]:
 vertices=set(seeds)
 for _ in range(2):vertices|=set.union(*(adj.get(v,set()) for v in vertices))
 rois[name]=vertices
out={'scope':'Two source-topology rings around independently ray-picked fold edges; source IDs are diagnostic locations, never repair constants. Triangle normal angles are geometry diagnostics, not GPU shading-normal readback.','regions':{},'pose':states[1]['request']}
for label,b in zip(['before','after'],bundles):
 allpos={}
 for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective']:
  ids,pos=data(b,stage);allpos[stage]={int(i):p for i,p in zip(ids,pos) if i>=0}
 src=allpos['00-source-lbs']
 for name,vertices in rois.items():
  rows={}
  for stage,points in allpos.items():
   t=np.array([[points[int(i)] for i in face] for face in tri]);n=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);n/=np.maximum(1e-20,np.linalg.norm(n,axis=1))[:,None]
   ee=[(e,ts) for e,ts in edges.items() if len(ts)==2 and set(e)<=vertices];angle=np.array([np.degrees(np.arccos(np.clip(n[ts[0]]@n[ts[1]],-1,1))) for e,ts in ee]);pp=[(l,r) for l,r in pairs if r in vertices and l in points and r in points];extra=[np.linalg.norm((points[l]-src[l])-(points[r]-src[r])*[1,-1,1]) for l,r in pp]
   rows[stage]={'dihedral_deg':stats(angle),'worst_edge':ee[int(angle.argmax())][0],'extra_mirror_cm':stats(extra),'mirror_pairs':pp,'displacement_from_source_cm':stats([np.linalg.norm(points[v]-src[v]) for v in vertices])}
  out['regions'].setdefault(name,{'source_vertices':sorted(vertices)})[label]=rows
out['same_actual_bones']=states[0]['actual_bones']==states[1]['actual_bones']
source_bones=[{x['name']:np.array(x['component_matrix_row_major']) for x in read(b/'00-source-lbs/inputs.json')['bones']} for b in bundles]
out['source_pose_matrix_max_difference']=max(float(np.max(np.abs(source_bones[0][k]-source_bones[1][k]))) for k in source_bones[0])
out['same_camera']=states[0]['cameras']==states[1]['cameras']
actual=[{x['bone']:np.array(x['component_matrix']) for x in st['actual_bones']} for st in states]
out['changed_auxiliary_matrices']=[k for k in actual[0] if k in actual[1] and not np.array_equal(actual[0][k],actual[1][k])]
assert out['source_pose_matrix_max_difference']<1.e-8 and out['same_camera'],'Pose/camera gate failed'
a.output.write_text(json.dumps(out,indent=2));print(json.dumps({k:{v:d[v]['03-corrective'] for v in ['before','after']} for k,d in out['regions'].items()},indent=2))
