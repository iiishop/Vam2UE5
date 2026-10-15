import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));out={}
for p in sorted((r/'main-editor').glob('*/state.json')):
 a=read(r/'editor-before'/p.parent.name/'state.json');b=read(p)
 def surface(s):
  d=Path(s['audit_bundle']);m=read(d/'mapping.json');v=read(d/'03-corrective/surface.json')['positions'];return m,{i:pos for i,pos in zip(m['render_to_input'],v)}
 am,ap=surface(a);bm,bp=surface(b);assert am['input_to_source']==bm['input_to_source'];ids=sorted(ap.keys()&bp.keys());err=np.linalg.norm(np.array([ap[i] for i in ids])-np.array([bp[i] for i in ids]),axis=1)
 out[p.parent.name]={'scope':'Exact retained input vertex mapping including refined vertices; no nearest-neighbor matching. Render duplicate counts may differ with section bone palettes.','vertices':len(ids),'rms_cm':float(np.sqrt(np.mean(err**2))),'p95_cm':float(np.percentile(err,95)),'max_cm':float(err.max()),'worst_input_vertex':ids[int(err.argmax())],'same_cameras':a['cameras']==b['cameras'],'max_primary_matrix_error':max(float(np.max(abs(np.array(x['component_matrix'])-y['component_matrix']))) for x,y in zip(a['actual_bones'],b['actual_bones']) if x['bone'] in ['hip','pelvis','lThigh','rThigh','lShin','rShin'])}
(r/'static-regression.json').write_text(json.dumps(out,indent=2))
