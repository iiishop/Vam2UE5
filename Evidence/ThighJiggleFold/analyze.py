import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;old=r.parent/'ThighSeamResidualOnly/candidate-3';read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
results={}
for pose in ['seated-jiggle','abducted-jiggle']:
 b=Path(read(old/'jiggle'/pose/'state.json')['audit_bundle']);s=read(b/'03-corrective/inputs.json');d=read(b/'04-jiggle/inputs.json');p=np.array(read(b/'04-jiggle/preskin.json')['positions']);q=np.c_[p,np.ones(len(p))]
 assert np.max(abs(p-np.array(read(b/'03-corrective/preskin.json')['positions'])))<1e-8
 mats=lambda i:np.array([np.array(x['inverse_bind_matrix_row_major']).reshape(4,4)@np.array(x['component_matrix_row_major']).reshape(4,4) for x in i['bones']])
 ms,md=mats(s),mats(d);names=[x['name'] for x in d['bones']];weights=d['render_vertex_influences'];mapping=read(b/'mapping.json');ids=np.array([mapping['input_to_source'][v] for v in mapping['render_to_input']]);n=int(ids.max())+1
 src=read(b/'source-mapping.json');si=np.array([src['input_to_source'][v] for v in src['render_to_input']]);tri=si[np.array(read(b/'00-source-lbs/inputs.json')['triangle_indices']).reshape(-1,3)]
 edgefaces={}
 for f,t in enumerate(tri):
  for u,v in zip(t,np.roll(t,1)):edgefaces.setdefault(tuple(sorted((int(u),int(v)))),[]).append(f)
 probes=sorted(read(old/'jiggle-band.json')[pose]['edges'],key=lambda x:x['angles']['04-jiggle'],reverse=True)[:12]
 def skin(m):
  out=np.zeros_like(p)
  for v,row in enumerate(weights):
   for bone,w in row:out[v]+=(q[v]@m[bone])[:3]*w
  return out
 surfaces={};contributions={}
 for label,selection in [('static',lambda name:False),('glute_only',lambda name:'_Glute_' in name),('leg_only',lambda name:'_Thigh_' in name or '_Calf_' in name),('all',lambda name:True)]:
  m=np.array([md[i] if selection(name) else ms[i] for i,name in enumerate(names)]);v=skin(m);x=np.zeros((n,3));valid=ids>=0;x[ids[valid]]=v[valid];surfaces[label]=x
  if label=='all':assert np.max(abs(v-np.array(read(b/'04-jiggle/surface.json')['positions'])))<.001
 rows=[]
 for e in probes:
  edge=tuple(e['source_edge']);faces=edgefaces[edge];angles={}
  for label,x in surfaces.items():
   t=x[tri[faces]];ns=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);angles[label]=float(np.degrees(np.arccos(np.clip(ns[0]@ns[1]/max(1e-20,np.linalg.norm(ns[0])*np.linalg.norm(ns[1])),-1,1))))
  vertices=[]
  for source in edge:
   v=int(np.where(ids==source)[0][0]);parts=[]
   for bone,w in weights[v]:
    delta=(q[v]@(md[bone]-ms[bone]))[:3]*w
    parts.append(dict(bone=names[bone],weight=w,weighted_motion_cm=delta.tolist(),bone_motion_cm=(q[v]@(md[bone]-ms[bone]))[:3].tolist()))
   vertices.append(dict(source=source,render=v,parts=parts))
  rows.append(dict(edge=edge,angles=angles,vertices=vertices))
 results[pose]=dict(scope='Exact A1 linear skinning replay; freeze same pre-skin morph and selectively replace actual helper matrices. Offline attribution, not independent runtime simulation.',preskin_equal=True,rows=rows)
 print(pose,rows[0]['edge'],rows[0]['angles'],flush=True)
(r/'attribution.json').write_text(json.dumps(results,indent=2))
