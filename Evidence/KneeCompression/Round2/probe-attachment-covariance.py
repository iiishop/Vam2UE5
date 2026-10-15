import json,numpy as np
from pathlib import Path
r=Path('Saved/LowerBodyContact/KneeCompression/candidate-6/pie-soft');states={p.parent.name:json.loads(p.read_text()) for p in r.glob('*/state.json')};d=Path(states['standing-soft']['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());parts=top['particles'];x=np.array(json.loads((d/'cage-canonical.json').read_text())['positions']);mapping=json.loads((d/'mapping.json').read_text());inv={v:i for i,v in enumerate(mapping['render_to_input'])};base=np.array(json.loads((d/'04-jiggle/preskin.json').read_text())['positions']);t=np.array(top['tetrahedra']);regs=np.array([p['region'] for p in parts])[t[:,0]];rows=[]
for pose,s in states.items():
 d=Path(s['audit_bundle']);q=json.loads((d/'04-jiggle/inputs.json').read_text());pre=np.array(json.loads((d/'04-jiggle/preskin.json').read_text())['positions']);y=np.array(json.loads((d/'cage-shaped-reference.json').read_text())['positions']);z=y.copy();mat=np.array([np.array(b['inverse_bind_matrix_row_major']).reshape(4,4)@np.array(b['component_matrix_row_major']).reshape(4,4) for b in q['bones']]);w=q['render_vertex_influences']
 for i,p in enumerate(parts):
  if p['region']<2:continue
  f=p['source_triangle']
  if min(f)<0 or not all(j in inv for j in f):continue
  ri=[inv[j] for j in f];a,b,c=base[ri];v,h=np.linalg.lstsq(np.column_stack((b-a,c-a)),x[i]-a,rcond=None)[0];bary=np.maximum(0,[1-v-h,v,h]);bary/=sum(bary);center=bary@pre[ri];correction=np.zeros(3)
  for weight,vertex in zip(bary,ri):
   for bone,bw in w[vertex]:correction+=((pre[vertex]-center)@mat[bone,:3,:3])*weight*bw
  z[i]+=(1-p['root_support'])*correction
 def vols(v):
  a=v[t];return np.einsum('ij,ij->i',a[:,1]-a[:,0],np.cross(a[:,2]-a[:,0],a[:,3]-a[:,0]))/6
 old,new=vols(y),vols(z);rows.append({'pose':pose,'old_invalid':int(((old<=0)&(regs>=2)).sum()),'new_invalid':int(((new<=0)&(regs>=2)).sum()),'max_move_cm':float(np.linalg.norm(z-y,axis=1).max())})
print(json.dumps(rows,indent=2));Path('Saved/LowerBodyContact/KneeCompression/round-2/attachment-covariance-probe.json').write_text(json.dumps(rows,indent=2))
