"""Locate the first invalid cage stage using existing A1 evidence, with per-cell provenance."""
import argparse,csv,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
summary=[]
for sf in sorted(a.capture.glob('*/state.json')):
 s=json.loads(sf.read_text());d=Path(s['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());x=np.array(json.loads((d/'cage-canonical.json').read_text())['positions']);t=np.array(top['tetrahedra']);particles=top['particles'];regions=np.array([v['region'] for v in particles]);kin=np.array([v['kinematic'] for v in particles]);center=x[t].mean(1)
 def vol(q):
  z=q[t];return np.einsum('ij,ij->i',z[:,1]-z[:,0],np.cross(z[:,2]-z[:,0],z[:,3]-z[:,0]))/6
 layers={'canonical':x}
 for stage in ['01-final-weights','02-static-helpers','03-corrective','04-jiggle']:
  q=json.loads((d/stage/'inputs.json').read_text());mat=np.array([np.array(b['inverse_bind_matrix_row_major']).reshape(4,4)@np.array(b['component_matrix_row_major']).reshape(4,4) for b in q['bones']]);out=[]
  for v,particle in zip(x,particles):out.append(sum(((np.append(v,1)@mat[b])[:3]*w for b,w in particle['influences']),np.zeros(3)))
  layers[stage+'-bones-only']=np.array(out)
 actual=np.array(json.loads((d/'cage-shaped-reference.json').read_text())['positions'])
 if actual.shape==x.shape:layers['runtime-with-morph']=actual
 volumes={k:vol(q) for k,q in layers.items()};bad=np.unique(np.concatenate([np.flatnonzero((v<=1e-8)&(regions[t[:,0]]>=2)) for v in volumes.values()]))
 rows=[]
 for i in bad:
  row=dict(cell=int(i),region=int(regions[t[i,0]]),particles=t[i].tolist(),canonical_center=center[i].tolist(),kinematic_count=int(kin[t[i]].sum()),collision_groups=[particles[j]['collision_group'] for j in t[i]],root_support=[particles[j].get('root_support') for j in t[i]])
  row.update({k:float(v[i]) for k,v in volumes.items()});rows.append(row)
 (a.output/(s['id']+'-cells.json')).write_text(json.dumps(rows,indent=2));summary.append(dict(pose=s['id'],audit=str(d),layers={k:{str(r):dict(invalid=int(((v<=1e-8)&(regions[t[:,0]]==r)).sum()),min_cm3=float(v[regions[t[:,0]]==r].min())) for r in [2,3]} for k,v in volumes.items()}))
(a.output/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
