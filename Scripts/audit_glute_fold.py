"""Compare glute fold dihedrals on the same retained source topology across A1 layers."""
import argparse,json
from pathlib import Path
import numpy as np

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def analyze(state, source_edges=()):
 s=read(state);b=Path(s['audit_bundle']);sm=read(b/'source-mapping.json');si=np.array([sm['input_to_source'][v] for v in sm['render_to_input']]);tri=si[np.array(read(b/'00-source-lbs/inputs.json')['triangle_indices']).reshape(-1,3)];n=int(tri.max())+1
 edges={}
 for i,t in enumerate(tri):
  for a,c in zip(t,np.roll(t,-1)):edges.setdefault(tuple(sorted((int(a),int(c)))),[]).append(i)
 ee=[(edge,ts) for edge,ts in edges.items() if len(ts)==2];out={};vertices={}
 for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective','04-jiggle']:
  if not (b/stage/'surface.json').exists():continue
  m=sm if stage.startswith('00') else read(b/'mapping.json');ix=np.array([m['input_to_source'][x] for x in m['render_to_input']]);p=np.array(read(b/stage/'surface.json')['positions']);v=np.zeros((n,3));valid=(ix>=0)&(ix<n);v[ix[valid]]=p[valid];vertices[stage]=v
  t=v[tri];normal=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]);normal/=np.maximum(1e-20,np.linalg.norm(normal,axis=1))[:,None]
  out[stage]=np.array([np.degrees(np.arccos(np.clip(normal[ts[0]]@normal[ts[1]],-1,1))) for edge,ts in ee])
 final=vertices['03-corrective'];roi=[i for i,(edge,_) in enumerate(ee) if edge in source_edges or 3<abs(final[edge[0],1])<17 and final[edge[0],0]<-2 and 82<final[edge[0],2]<110]
 rows=[]
 for i in roi:
  edge=ee[i][0];rows.append(dict(source_edge=edge,position_cm=final[edge[0]].tolist(),angles_deg={k:float(v[i]) for k,v in out.items()}))
 rows.sort(key=lambda x:x['angles_deg']['03-corrective'],reverse=True)
 return dict(scope='Source-ID topology correspondence, no nearest-neighbor; requested diagnostic posterior bounds are measurements, not production repair masks. Excludes contact when solver unavailable.',actor=s['actor_class'],bundle=str(b),request=s['request'],regional_edges=rows,edge_count=len(ee))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('state',type=Path);p.add_argument('output',type=Path);p.add_argument('--source-edges',default='');a=p.parse_args();edges={tuple(sorted(map(int,e.split(':')))) for e in a.source_edges.split(',') if e};d=analyze(a.state,edges);d['explicit_probe_edges']=[list(e) for e in sorted(edges)];a.output.write_text(json.dumps(d,indent=2));print(json.dumps(d['regional_edges'][:10],indent=2))
