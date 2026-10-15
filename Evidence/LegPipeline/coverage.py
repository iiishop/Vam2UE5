import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
b=Path(read(r.parent/'ThighJiggleFold/runtime/seated-leg-only/state.json')['audit_bundle'])
source=read(b/'00-source-lbs/inputs.json');final=read(b/'04-jiggle/inputs.json')
sm=read(b/'source-mapping.json');fm=read(b/'mapping.json')
sid=[sm['input_to_source'][i] for i in sm['render_to_input']];fid=[fm['input_to_source'][i] for i in fm['render_to_input']]
orig={s:w for s,w in zip(sid,source['render_vertex_influences']) if s>=0};end={s:w for s,w in zip(fid,final['render_vertex_influences']) if s>=0}
names=[x['name'] for x in source['bones']];fn=[x['name'] for x in final['bones']];regions={};ids={}
for bone,prefix in [('lThigh','L_Thigh_'),('rThigh','R_Thigh_'),('lShin','L_Calf_'),('rShin','R_Calf_')]:
 bi=names.index(bone);members=[s for s,w in orig.items() if dict(w).get(bi,0)>.5 and s in end];ids[prefix]=members
 ws=np.array([sum(v for k,v in end[s] if fn[k].startswith(prefix) and not fn[k].endswith('_Anchor')) for s in members]);counts=np.array([sum(1 for k,v in end[s] if fn[k].startswith(prefix) and not fn[k].endswith('_Anchor') and v>0) for s in members]);regions[prefix]=dict(count=len(members),no_leg_helper=int((counts==0).sum()),one_leg_helper=int((counts==1).sum()),helper_weight_percentiles=np.percentile(ws,[0,5,50,95,100]).tolist())
(r/'coverage.json').write_text(json.dumps(dict(scope='Unique retained source vertices with original source primary bone weight > 0.5; includes joint transition, not a claim that all should jiggle equally.',regions=regions,source_ids=ids),indent=2));print(json.dumps(regions,indent=2))
