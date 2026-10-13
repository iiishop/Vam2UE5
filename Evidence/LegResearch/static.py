import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));out={}
for case in ['standing','suspended-seat','abducted','knee-120']:
 a=read(r.parent/'LegProduction/main-editor'/case/'state.json');b=read(r/'main-editor'/case/'state.json')
 def load(state):
  root=Path(state['audit_bundle']);m=read(root/'mapping.json');p=read(root/'03-corrective/surface.json')['positions'];w=read(root/'03-corrective/inputs.json')['render_vertex_influences'];return m,p,w
 am,ap,aw=load(a);bm,bp,bw=load(b);assert {k:v for k,v in am.items() if k!='path'}=={k:v for k,v in bm.items() if k!='path'};assert aw==bw;err=np.linalg.norm(np.array(ap)-bp,axis=1);assert err.max()<1e-6
 out[case]={'max_surface_difference_cm':float(err.max()),'rms_cm':float(np.sqrt(np.mean(err**2))),'mapping_and_actual_weights_identical':True,'cameras_identical':a['cameras']==b['cameras']}
(r/'static.json').write_text(json.dumps(out,indent=2));print(out)
