"""Reconstruct cage targets from captured actual A1 matrices; never a GPU acceptance substitute."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);a=p.parse_args();out=[]
for statefile in sorted(a.capture.glob('*/state.json')):
 s=json.loads(statefile.read_text());d=Path(s['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());x=np.asarray(json.loads((d/'cage-canonical.json').read_text())['positions']);q=json.loads((d/'04-jiggle/inputs.json').read_text());b=q['bones'];m=np.array([np.array(v['inverse_bind_matrix_row_major']).reshape(4,4)@np.array(v['component_matrix_row_major']).reshape(4,4) for v in b]);y=[]
 for v,particle in zip(x,top['particles']):
  z=np.zeros(3)
  for bone,w in particle['influences']:z+=(np.append(v,1)@m[bone])[:3]*w
  y.append(z)
 y=np.array(y);tet=np.asarray(top['tetrahedra']);reg=np.array([v['region'] for v in top['particles']])[tet[:,0]]
 def volumes(v):
  t=v[tet];return np.einsum('ij,ij->i',t[:,1]-t[:,0],np.cross(t[:,2]-t[:,0],t[:,3]-t[:,0]))/6
 actual=np.asarray(json.loads((d/'cage-shaped-reference.json').read_text())['positions']);actual_vol=volumes(actual) if actual.shape==x.shape else None
 v0=volumes(x);v1=volumes(y);out.append(dict(pose=s['id'],audit=str(d),regions={str(r):dict(cells=int((reg==r).sum()),canonical_min=float(v0[reg==r].min()),posed_min=float(v1[reg==r].min()),posed_invalid=int((v1[reg==r]<=0).sum()),runtime_initial_invalid=int((actual_vol[reg==r]<=0).sum()) if actual_vol is not None else None,runtime_initial_min=float(actual_vol[reg==r].min()) if actual_vol is not None else None) for r in sorted(set(reg))}))
a.output.write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
