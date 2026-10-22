"""Compare identical-pose A1 surfaces across immutable knee repair assets."""
import json,argparse
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('--groups',nargs='+',default=['C','M']);p.add_argument('--actions',nargs='+',default=['deep-kneel','single-flexion']);a=p.parse_args();read=lambda p:json.loads(p.read_text());rows=[]
for g in a.groups:
 for action in a.actions:
  vs=[read(r/g/(g+'-'+action)/'video.json')['samples'] for r in [a.before,a.after]]
  for frame in [45,135,165]:
   ds=[Path(v[frame]['audit_bundle']) for v in vs];inputs=[read(d/'04-jiggle/inputs.json') for d in ds];names=[[b['name'] for b in i['bones']] for i in inputs];assert names[0]==names[1]
   assert inputs[0]['triangle_indices']==inputs[1]['triangle_indices'];assert inputs[0]['render_vertex_influences']==inputs[1]['render_vertex_influences']
   primary=[j for j,n in enumerate(names[0]) if not n.startswith(('L_','R_'))];err=float(np.max(abs(np.array(vs[0][frame]['bone_matrices'])[primary]-np.array(vs[1][frame]['bone_matrices'])[primary])));assert err<1e-6
   record=dict(group=g,action=action,frame=frame,primary_matrix_max_error=err,stages=[])
   leg={j for j,n in enumerate(names[0]) if n in ['lThigh','rThigh','lShin','rShin'] or '_Thigh_' in n or '_Calf_' in n};mask=np.array([sum(w for b,w in inf if b in leg)>.05 for inf in inputs[0]['render_vertex_influences']]);tri=np.array(inputs[0]['triangle_indices']).reshape(-1,3)
   for stage in ['01-final-weights','02-static-helpers','03-corrective','04-jiggle']:
    ps=[np.array(read(d/stage/'surface.json')['positions']) for d in ds];delta=np.linalg.norm(ps[1]-ps[0],axis=1)[mask];angles=[]
    for pos in ps:
     n=np.cross(pos[tri[:,1]]-pos[tri[:,0]],pos[tri[:,2]]-pos[tri[:,0]]);n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
     angles.append([float(np.degrees(np.arccos(np.clip(n[x]@n[y],-1,1)))) for x,y in [(1322,1326),(2630,2631)]])
    record['stages'].append(dict(stage=stage,rms_change_cm=float(np.sqrt(np.mean(delta**2))),max_change_cm=float(delta.max()),tracked_edge_angles_before=angles[0],tracked_edge_angles_after=angles[1]))
   rows.append(record)
(a.after/'before-after-audit.json').write_text(json.dumps(rows,indent=2));print('Verified',len(rows),'actual-pose snapshots')
