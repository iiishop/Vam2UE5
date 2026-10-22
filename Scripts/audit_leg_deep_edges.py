"""Trace deep-flexion flagged edges through existing final-topology A1 stages."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();read=lambda p:json.loads(p.read_text());data=read(a.run/'analysis.json');rows=[]
for r in data['surfaces']:
 if r['group']!='M' or r['frame']!=165:continue
 for detail in r['new_sharp_edge_details']:
  item=dict(action=r['action'],frame=r['frame'],**detail,stages={})
  for g in ['C','M']:
   f=read(a.run/g/(g+'-'+r['action'])/'video.json')['samples'][r['frame']];d=Path(f['audit_bundle']);st=[]
   for name in ['01-final-weights','02a-chain-disabled','02-static-helpers','03-corrective','04-jiggle']:
    inp=read(d/name/'inputs.json');pos=np.array(read(d/name/'surface.json')['positions']);tri=np.array(inp['triangle_indices']).reshape(-1,3)[detail['triangles']];q=pos[tri];n=np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0]);n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12);angle=float(np.degrees(np.arccos(np.clip(n[0]@n[1],-1,1))))
    st.append(dict(stage=name,angle_deg=angle,center_cm=q.mean(axis=(0,1)).tolist()))
   item['stages'][g]=st
  rows.append(item)
(a.run/'deep-edge-stages.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
