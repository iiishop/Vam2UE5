"""Frame-to-frame head-local guide and rendered motion; diagnostics only."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
c=json.loads(a.capture.read_text());rows=[];prev={}
for f in ([] if c.get('temporal_samples') else c['gpu_samples']):
 for g in f['groups']:
  k=g['group'];t=f['time'];guides=np.asarray(g['guide_points']);render=dict(zip(g['render_point_ids'],g['render_points']))
  if k in prev:
   pt,pg,pr=prev[k];ids=sorted(render.keys() & pr.keys());dt=t-pt
   ds=[('guide',guides-pg),('render',np.asarray([render[i] for i in ids])-np.asarray([pr[i] for i in ids]))]
   for kind,d in ds:
    n=np.linalg.norm(d,axis=1);rows.append(dict(group=k,time=t,dt=dt,kind=kind,rms_step_cm=float(np.sqrt(np.mean(n*n))),max_step_cm=float(n.max()),rms_speed_cm_s=float(np.sqrt(np.mean(n*n))/dt)))
  prev[k]=(t,guides,render)
if c.get('temporal_samples'):
 for f in c['temporal_samples']:
  for g in f['groups']:
   prev[g['group']]=True
   for kind in ['guide','render']:
    rows.append(dict(group=g['group'],time=f['time'],dt=1/30,kind=kind,rms_step_cm=g[kind+'_rms_step_cm'],max_step_cm=g[kind+'_max_step_cm'],rms_speed_cm_s=g[kind+'_rms_step_cm']*30))
summary={str(k):{kind:dict(max_step_cm=max(r['max_step_cm'] for r in rows if r['group']==k and r['kind']==kind),max_rms_step_cm=max(r['rms_step_cm'] for r in rows if r['group']==k and r['kind']==kind)) for kind in ['guide','render']} for k in prev}
a.out.write_text(json.dumps(dict(summary=summary,frames=c.get('frames',len(c['gpu_samples'])),geometry_frames=len(c['gpu_samples']),temporal_intervals=len(c.get('temporal_samples',[])),rows=rows),indent=2));print(json.dumps(summary,indent=2))
