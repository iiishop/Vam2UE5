import json,sys,numpy as np
from pathlib import Path
sys.path.insert(0,'Scripts');from audit_glute_fold import analyze
root=Path('Saved/LowerBodyContact');out=root/'GluteShapeRecovery';new=out/'candidate-15/preview/seated/state.json';old=root/'SeatedFold/candidate-11/matched-light/seated/state.json';safe=root/'SeatedFoldOrigin/candidate-14/preview/seated/state.json'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
fixed=read(root/'SeatedFoldOrigin/fixed-region-angles.json');edges={tuple(r['edge']) for r in fixed['rows']};rows={tuple(r['source_edge']):r for r in analyze(new,edges)['regional_edges']};angles=[rows[e]['angles_deg']['03-corrective'] for e in edges];result=dict(fixed_edges=len(edges),max_angle=max(angles),over45=sum(a>45 for a in angles),rows=[dict(edge=e,angle=rows[e]['angles_deg']['03-corrective']) for e in sorted(edges)])
def surface(state,stage):
 b=Path(read(state)['audit_bundle']);m=read(b/'mapping.json');ix=np.array([m['input_to_source'][i] for i in m['render_to_input']]);p=np.array(read(b/stage/'surface.json')['positions']);return {int(i):v for i,v in zip(ix,p) if i>=0}
a=surface(old,'03-corrective');b=surface(safe,'03-corrective');c=surface(new,'03-corrective');base=surface(old,'02-static-helpers');ids=sorted(set(a)&set(b)&set(c));x=np.array([a[i] for i in ids]);y=np.array([b[i] for i in ids]);z=np.array([c[i] for i in ids]);p=np.array([base[i] for i in ids]);region=(abs(p[:,1])>3)&(abs(p[:,1])<17)&(p[:,0]<-2)&(p[:,2]>82)&(p[:,2]<110)
result['shape_retention']={}
for sign in [-1,1]:
 mask=region&(p[:,1]*sign>0);entry={}
 for name,pos in [('previous_safe',y),('new',z)]:
  error=np.linalg.norm(pos[mask]-x[mask],axis=1);entry[name]=dict(rms_cm=float(np.sqrt(np.mean(error**2))),p95_cm=float(np.percentile(error,95)),max_cm=float(error.max()))
 result['shape_retention'][str(sign)]=entry
result['upstream_max_cm']={}
for stage in ['01-final-weights','02-static-helpers']:
 u=surface(old,stage);v=surface(new,stage);result['upstream_max_cm'][stage]=max(float(np.linalg.norm(u[i]-v[i])) for i in u.keys()&v.keys())
(out/'actual-metrics.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2))
