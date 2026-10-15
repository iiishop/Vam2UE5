"""Measure actual guide/render readbacks; inactive buffer tails are not observations."""
import argparse,csv,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('output',type=Path);p.add_argument('cases',nargs='+');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
rows=[];details={}
fig,axes=plt.subplots(2,2,figsize=(12,8),sharex=True)
for name in a.cases:
 c=json.loads((a.root/name/'capture.json').read_text());first={};case=[]
 for frame in c['gpu_samples']:
  for g in frame['groups']:
   k=g['group'];points=np.asarray(g['guide_points']);render=dict(zip(g['render_point_ids'],g['render_points']));first.setdefault(k,(points,render))
   common=sorted(render.keys() & first[k][1].keys());rd=np.asarray([render[i] for i in common])-np.asarray([first[k][1][i] for i in common]);gd=points-first[k][0]
   assert np.isfinite(rd).all() and np.isfinite(gd).all()
   row=dict(case=name,time=frame['time'],group=k,guide_rms_cm=float(np.sqrt(np.mean(np.sum(gd**2,axis=1)))),guide_max_cm=float(np.linalg.norm(gd,axis=1).max()),render_rms_cm=float(np.sqrt(np.mean(np.sum(rd**2,axis=1)))),render_max_cm=float(np.linalg.norm(rd,axis=1).max()),matched_render_points=len(common),active_render_points=g['render_point_count'])
   rows.append(row);case.append(row)
 for k,ax in enumerate(axes.flat):
  rr=[r for r in case if r['group']==k];ax.plot([r['time'] for r in rr],[r['render_rms_cm'] for r in rr],label=name);ax.set_title(f'Group {k} rendered motion');ax.set_ylabel('RMS from first sample (cm)');ax.grid(alpha=.3);ax.legend()
 details[name]=dict(actor=c['actor'],world=c['world'],frames=c['frames'],parameters=[{k:v for k,v in g.items() if k in ['group','solver','substeps','iterations','density','thickness_cm','bend_stiffness','air_drag']} for g in c['gpu_samples'][0]['groups']])
fig.supxlabel('Recorded time (s)');fig.tight_layout();fig.savefig(a.output/'render-motion.png',dpi=160)
with (a.output/'metrics.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
(a.output/'runtime-parameters.json').write_text(json.dumps(details,indent=2));(a.output/'metrics.json').write_text(json.dumps(rows,indent=2));print(json.dumps({n:{g:max(r['render_rms_cm'] for r in rows if r['case']==n and r['group']==g) for g in range(4)} for n in a.cases},indent=2))
