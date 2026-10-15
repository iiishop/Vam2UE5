import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));out={}
for label,root in [('before',r.parent/'LegProduction/after-v3'),('after',r/'runtime')]:
 rows={}
 for case in ['flexion','step','hard-stop','seated-stop']:
  samples=read(root/case/'video.json')['samples'];t=np.array([s['time_seconds'] for s in samples]);stop=4 if case in ['flexion','step'] else 3
  segments={}
  for si in range(4):
   states=[s['leg_pipeline'][si] for s in samples];p=np.array([[n['raw_cm'] for n in s['nodes']] for s in states]);q=np.array([[n['final_cm'] for n in s['nodes']] for s in states]);dev=np.linalg.norm(p-p[-15:].mean(0),axis=2).max(1);tail=dev[t>=stop];dt=t[t>=stop]-stop;peak=float(tail.max());cross=dt[tail>peak*.1]
   zeta=[];freq=[]
   for n in states[0]['nodes']:
    k=np.array(n['support']);c=np.array(n['damping']);m=n['mass'];zeta.append((c/(2*np.sqrt(m*k))).tolist());freq.append((np.sqrt(k/m)/(2*np.pi)).tolist())
   segments[states[0]['name']]={'stop_seconds':stop,'post_stop_peak_raw_cm':peak,'last_above_ten_percent_seconds_after_stop':float(cross.max()) if len(cross) else 0,'time_resolution_seconds':1/30,'max_final_offset_cm':float(np.linalg.norm(q,axis=2).max()),'equilibrium_small_signal_zeta':zeta,'equilibrium_uncoupled_frequency_hz':freq}
  rows[case]=segments
 out[label]=rows
(r/'decay.json').write_text(json.dumps(out,indent=2))
print(json.dumps({label:{case:{name:v['last_above_ten_percent_seconds_after_stop'] for name,v in rows.items()} for case,rows in data.items()} for label,data in out.items()},indent=2))
