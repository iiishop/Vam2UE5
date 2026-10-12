"""Audit captured knee driver, regional leg dynamics and final triangulated surfaces."""
import json,sys
from pathlib import Path
import numpy as np
from compare_c15_asset_replay import read
from audit_knee_structure import stats

r=Path(sys.argv[1]);rows=[]
for root in [r/'regression',r/'main-extra']:
 for world in ['editor','pie']:
  for path in sorted((root/world).glob('*/state.json')):
   s=read(path);a=Path(s['audit_bundle']);inputs=read(a/'03-corrective/inputs.json')
   x,y=[np.array(read(a/stage/'surface.json')['positions']) for stage in ['03-corrective','04-jiggle']]
   assert np.isfinite(x).all() and np.isfinite(y).all()
   dynamic={};d=np.linalg.norm(x-y,axis=1)
   for side in ['L','R']:
    for part in ['Thigh','Calf']:
     prefix=side+'_'+part+'_';bones={i for i,b in enumerate(inputs['bones']) if b['name'].startswith(prefix) and not b['name'].endswith('_Anchor')}
     mask=np.array([sum(w for b,w in v if b in bones)>.2 for v in inputs['render_vertex_influences']])
     dynamic[side+'_'+part]=dict(count=int(mask.sum()),**stats(d[mask]))
   # A triangle normal changing sides relative to its own pre-corrective pose
   # flags possible local folding; this is not a whole-body self-intersection test.
   base=np.array(read(a/'02-static-helpers/surface.json')['positions']);t=np.array(inputs['triangle_indices']).reshape(-1,3)
   n=[np.cross(p[t[:,1]]-p[t[:,0]],p[t[:,2]]-p[t[:,0]]) for p in [base,x]]
   valid=np.linalg.norm(n[0],axis=1)>1.e-8
   flipped=int(np.sum(np.sum(n[0]*n[1],axis=1)[valid]<=0))
   rows.append(dict(suite=root.name,world=world,pose=s['id'],jiggle=s['jiggle_enabled'],dynamic=dynamic,relative_normal_reversals=flipped,
     active_knee_morphs={k:v for k,v in inputs['active_morphs'].items() if k.startswith('Knee_') and v>1.e-5},audit=str(a)))
(r/'runtime-analysis.json').write_text(json.dumps(rows,indent=2))
assert rows
assert all(x['relative_normal_reversals']==0 for x in rows), 'Inspect actual triangle reversals before acceptance'
assert all(v['max_cm']>.001 for x in rows if x['world']=='pie' and x['jiggle'] for v in x['dynamic'].values()), 'Missing live thigh/calf dynamics'
print('Captures',len(rows),'relative normal reversals',max(x['relative_normal_reversals'] for x in rows))
for x in rows:
 if x['world']=='pie' and x['jiggle']:print(x['pose'],{k:round(v['max_cm'],5) for k,v in x['dynamic'].items()})
