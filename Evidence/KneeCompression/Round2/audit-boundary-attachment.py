"""Boundary attachment consistency from actual A1 source mapping, no nearest-ID matching."""
import json,numpy as np
from pathlib import Path
r=Path('Saved/LowerBodyContact/KneeCompression/candidate-6/pie-soft');out=Path('Saved/LowerBodyContact/KneeCompression/round-2');states={p.parent.name:json.loads(p.read_text()) for p in r.glob('*/state.json')};d=Path(states['standing-soft']['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());parts=top['particles'];x=np.array(json.loads((d/'cage-canonical.json').read_text())['positions']);mapping=json.loads((d/'mapping.json').read_text());inv={v:i for i,v in enumerate(mapping['render_to_input'])};base=np.array(json.loads((d/'04-jiggle/preskin.json').read_text())['positions'])
ids=[];faces=[];weights=[];dist=[]
for i,p in enumerate(parts):
 if p['region']<2 or p['root_support']>.0001:continue
 f=p['source_triangle']
 if min(f)<0 or not all(j in inv for j in f):continue
 ri=[inv[j] for j in f];a,b,c=base[ri];v,w=np.linalg.lstsq(np.column_stack((b-a,c-a)),x[i]-a,rcond=None)[0];bary=np.array([1-v-w,v,w]);q=bary@base[ri];error=np.linalg.norm(x[i]-q)
 if min(bary)<-1e-3 or error>.01:continue
 ids.append(i);faces.append(ri);weights.append(bary);dist.append(error)
ids=np.array(ids);faces=np.array(faces);weights=np.array(weights);rows=[]
for pose,s in states.items():
 d=Path(s['audit_bundle']);surface=np.array(json.loads((d/'04-jiggle/surface.json').read_text())['positions']);target=np.array(json.loads((d/'cage-shaped-reference.json').read_text())['positions']);expected=np.einsum('ij,ijk->ik',weights,surface[faces]);error=np.linalg.norm(expected-target[ids],axis=1);order=np.argsort(error)[-20:][::-1]
 row={'pose':pose,'boundary_samples':len(ids),'max_canonical_plane_error_cm':max(dist),'rms_cm':float(np.sqrt(np.mean(error**2))),'p95_cm':float(np.percentile(error,95)),'max_cm':float(error.max()),'worst':[{'particle':int(ids[j]),'canonical':x[ids[j]].tolist(),'error_cm':float(error[j]),'source_triangle':parts[ids[j]]['source_triangle']} for j in order]};rows.append(row)
(out/'boundary-attachment.json').write_text(json.dumps(rows,indent=2));print(json.dumps([{k:v for k,v in q.items() if k!='worst'} for q in rows],indent=2))
