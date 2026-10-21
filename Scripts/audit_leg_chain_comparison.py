"""Compare A1 layers and actual primary poses for the three leg experiment groups."""
import argparse,json
from pathlib import Path
import numpy as np
from audit_knee_structure import stats
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
rows=[];states={};videos={}
for group in ['A','B','C']:
 for path in sorted((a.run/group).glob('*/state.json')):
  s=read(path);key=s['id'].split('-',1)[1];states[group,key]=s;folder=Path(s['audit_bundle'])
  videos[group,key]=read(path.parent/'video.json')['samples']
  data=read(folder/'03-corrective/inputs.json');surfaces={k:np.asarray(read(folder/k/'surface.json')['positions']) for k in ['02a-chain-disabled','02-static-helpers','03-corrective','04-jiggle']}
  assert all(np.isfinite(v).all() for v in surfaces.values())
  regions={}
  for side in ['L','R']:
   for part in ['Thigh','Calf']:
    ids={i for i,b in enumerate(data['bones']) if b['name'].startswith(side+'_'+part+'_')}
    mask=np.array([sum(w for b,w in v if b in ids)>.05 for v in data['render_vertex_influences']])
    assert mask.any()
    chain=np.linalg.norm(surfaces['02-static-helpers']-surfaces['02a-chain-disabled'],axis=1)
    jiggle=np.linalg.norm(surfaces['04-jiggle']-surfaces['03-corrective'],axis=1)
    regions[side+'_'+part]=dict(vertices=int(mask.sum()),chain=stats(chain[mask]),jiggle=stats(jiggle[mask]))
  raw=np.array([n['raw_cm'] for frame in videos[group,key] for segment in frame['leg_pipeline'] for n in segment['nodes']])
  assert np.isfinite(raw).all()
  rows.append(dict(group=group,action=key,chain_enabled=s['leg_chain_enabled'],contact_active=s['contact_solver_active'],contact_status=s['contact_status'],regions=regions,audit=str(folder),video_raw_jiggle_max_cm=float(np.linalg.norm(raw,axis=1).max())))
checks=[]
for action in sorted(k for g,k in states if g=='A'):
 reference=states['A',action]
 for group in ['B','C']:
  s=states[group,action];assert s['cameras']==reference['cameras'],'Camera mismatch'
  primary=lambda x:{r['bone']:np.array(r['component_matrix']) for r in x['actual_bones'] if not r['bone'].startswith(('L_','R_'))}
  x,y=primary(s),primary(reference);assert x.keys()==y.keys()
  error=max(float(abs(x[k]-y[k]).max()) for k in x)
  primary_ids=[i for i,r in enumerate(reference['actual_bones']) if r['bone'] in x]
  va,vb=videos['A',action],videos[group,action]
  assert len(va)==len(vb)==210
  video_error=max(float(np.abs(np.asarray(f['bone_matrices'])[primary_ids]-np.asarray(g['bone_matrices'])[primary_ids]).max()) for f,g in zip(va,vb))
  root_error=max(float(np.abs(np.array(f['root_offset_cm'])-g['root_offset_cm']).max()) for f,g in zip(va,vb))
  checks.append(dict(action=action,group=group,primary_matrix_max_error=error,video_primary_matrix_max_error=video_error,video_root_max_error_cm=root_error,seat_matrix_max_error=float(abs(np.array(s['seat_world'])-reference['seat_world']).max())))
assert all(r['primary_matrix_max_error']<1e-6 and r['video_primary_matrix_max_error']<1e-6 and r['video_root_max_error_cm']<1e-6 and r['seat_matrix_max_error']<1e-6 for r in checks),'Actual pose or seat mismatch; do not label as same-condition comparison'
result=dict(layers=rows,comparability=checks,scope='Displacements are differences, not accuracy versus an anatomical target. Contact solver active does not prove a particular tissue pair touched.')
(a.run/'layer-comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps(checks,indent=2))
