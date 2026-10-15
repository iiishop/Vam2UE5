"""Exact translation-only skin update for fixed-pose clips; assert the premise."""
import json
from pathlib import Path
import numpy as np
r=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
results={}
for label in ['before','after']:
 folder=(r.parent/'LegProduction/after-v3' if label=='before' else r/'runtime')/'seated-stop';samples=read(folder/'video.json')['samples'];t=np.array([s['time_seconds'] for s in samples])
 b=Path(read(folder/'state.json')['audit_bundle']);m=read(b/'mapping.json');ids=np.array([m['input_to_source'][i] for i in m['render_to_input']]);inp=read(b/'04-jiggle/inputs.json')
 rest=np.array(read(b/'04-jiggle/preskin.json')['positions']);surface=np.array(read(b/'04-jiggle/surface.json')['positions'])
 matrices=np.array([s['bone_matrices'] for s in samples]).reshape(len(samples),-1,4,4)
 base=np.array([x['component_matrix_row_major'] for x in inp['bones']]).reshape(-1,4,4)
 rot_error=float(np.max(abs(matrices[:,:,:3,:3]-base[None,:,:3,:3])));assert rot_error<1e-6,rot_error
 vertices={int(s):i for i,s in enumerate(ids) if s>=0}
 edges=read(r/('seam-'+label+'.json'))['seated-stop']['edges'];source_edges={tuple(x['source_edge']) for x in edges}
 tri=np.array(inp['triangle_indices']).reshape(-1,3);source_tri=ids[tri]
 adj={}
 for i,face in enumerate(source_tri):
  if min(face)<0:continue
  for u,v in zip(face,np.roll(face,1)):
   e=tuple(sorted((int(u),int(v))))
   if e in source_edges:adj.setdefault(e,[]).append(i)
 pairs=[f for f in adj.values() if len(f)==2]
 used=np.unique(tri[np.array(pairs).ravel()]);local={int(v):i for i,v in enumerate(used)}
 weights=np.zeros((len(used),len(base)))
 for i,v in enumerate(used):
  for k,w in inp['render_vertex_influences'][v]:weights[i,k]=w
 p=surface[used][None,:,:]+np.einsum('vb,tbd->tvd',weights,matrices[:,:,3,:3]-base[None,:,3,:3])
 localtri=np.array([[local[int(v)] for v in tri[f]] for f in np.array(pairs).ravel()]).reshape(len(pairs),2,3)
 faces=p[:,localtri];norm=np.cross(faces[:,:,:,1]-faces[:,:,:,0],faces[:,:,:,2]-faces[:,:,:,0]);length=np.linalg.norm(norm,axis=-1)
 cos=np.sum(norm[:,:,0]*norm[:,:,1],axis=-1)/np.maximum(1e-20,length[:,:,0]*length[:,:,1]);angles=np.degrees(np.arccos(np.clip(cos,-1,1)))
 steady=p[(t>=1)&(t<2.9)];deviation=np.linalg.norm(steady-steady.mean(0),axis=-1)
 results[label]={'scope':'Fixed seated pose, actual quantized weights and recorded bone translations. A1 surface plus exact LBS translation delta; rotation constancy asserted. Excludes per-frame GPU normal readback and global self-intersection.','frames':len(samples),'edges':len(pairs),'rotation_matrix_max_difference':rot_error,'maximum_edge_angle_deg':float(angles.max()),'p95_edge_angle_all_frames_deg':float(np.percentile(angles,95)),'frames_with_edge_over_45':int(np.any(angles>45,axis=1).sum()),'minimum_double_triangle_area_cm2':float(length.min()),'band_temporal_rms_cm':float(np.sqrt(np.mean(deviation**2))),'band_max_temporal_deviation_cm':float(deviation.max())}
(r/'dynamic-seam.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
