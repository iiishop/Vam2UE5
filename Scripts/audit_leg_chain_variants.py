"""Compare measured surfaces and primary poses, not names or UI angles."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
stats=lambda x:dict(rms_cm=float(np.sqrt(np.mean(x*x))),p95_cm=float(np.percentile(x,95)),max_cm=float(x.max()))
rows=[];checks=[]
for action in ['flexion','step','seated-stop']:
 ref=a.run/'R'/('R-'+action);rs=read(ref/'state.json');rv=read(ref/'video.json')['samples'];rd=Path(rs['audit_bundle'])
 data=read(rd/'03-corrective/inputs.json');bones=data['bones'];ids=[i for i,b in enumerate(rs['actual_bones']) if not b['bone'].startswith(('L_','R_'))]
 baseline=np.array(read(rd/'04-jiggle/surface.json')['positions'])
 for group in ['R','L','T','V','S','C']:
  root=a.run/group/(group+'-'+action);s=read(root/'state.json');v=read(root/'video.json')['samples'];d=Path(s['audit_bundle'])
  assert len(v)==len(rv)==210 and s['cameras']==rs['cameras']
  primary=max(float(np.abs(np.asarray(x['bone_matrices'])[ids]-np.asarray(y['bone_matrices'])[ids]).max()) for x,y in zip(v,rv))
  root_error=max(float(np.abs(np.asarray(x['root_offset_cm'])-np.asarray(y['root_offset_cm'])).max()) for x,y in zip(v,rv))
  checks.append(dict(group=group,action=action,primary_matrix_error=primary,root_error_cm=root_error,camera_equal=True))
  assert primary<1e-6 and root_error<1e-6
  surfaces={k:np.asarray(read(d/k/'surface.json')['positions']) for k in ['02a-chain-disabled','02-static-helpers','03-corrective','04-jiggle']}
  assert all(np.isfinite(x).all() and x.shape==baseline.shape for x in surfaces.values())
  regions={}
  for side in ['L','R']:
   for part in ['Thigh','Calf']:
    bids={i for i,b in enumerate(bones) if b['name'].startswith(side+'_'+part+'_')}
    mask=np.array([sum(w for b,w in inf if b in bids)>.05 for inf in data['render_vertex_influences']])
    chain=np.linalg.norm(surfaces['02-static-helpers']-surfaces['02a-chain-disabled'],axis=1)
    delta=np.linalg.norm(surfaces['04-jiggle']-baseline,axis=1)
    regions[side+'_'+part]=dict(vertices=int(mask.sum()),chain=stats(chain[mask]),vs_R=stats(delta[mask]))
  allmat=np.array([f['bone_matrices'] for f in v]);assert np.isfinite(allmat).all()
  raw=np.array([n['raw_cm'] for f in v for seg in f['leg_pipeline'] for n in seg['nodes']]);assert np.isfinite(raw).all()
  rows.append(dict(group=group,action=action,method=s['leg_chain_method'],regions=regions,raw_jiggle_max_cm=float(np.linalg.norm(raw,axis=1).max()),contact_active_frames=sum(f['contact_solver_active'] for f in v)))
(a.run/'analysis.json').write_text(json.dumps(dict(rows=rows,comparability=checks,scope='04 is final bone/Jiggle skinned surface, not final GPU contact output. Displacement versus R is not anatomical error.'),indent=2))
print(json.dumps(checks,indent=2))
