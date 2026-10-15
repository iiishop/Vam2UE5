"""Compare two independent UE capture runs at actual transforms and frozen geometry."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('first',type=Path);p.add_argument('second',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
left={s['id']:s for s in read(a.first/'suite.json')['states']};right={s['id']:s for s in read(a.second/'suite.json')['states']};rows=[]
for name in sorted(left.keys()&right.keys()):
    x,y=left[name],right[name]
    bx={b['bone']:b['component_matrix'] for b in x['actual_bones']};by={b['bone']:b['component_matrix'] for b in y['actual_bones']}
    if bx.keys()!=by.keys():raise SystemExit('Bone topology mismatch: '+name)
    matrix=max(float(np.abs(np.array(bx[b])-by[b]).max()) for b in bx)
    ax,ay=Path(x['audit_bundle']),Path(y['audit_bundle'])
    vx=np.array(read(ax/'04-jiggle/surface.json')['positions']);vy=np.array(read(ay/'04-jiggle/surface.json')['positions'])
    if vx.shape!=vy.shape:raise SystemExit('Surface topology mismatch: '+name)
    maximum=float(np.linalg.norm(vx-vy,axis=1).max())
    rows.append({'case':name,'bone_matrix_max_error':matrix,'pre_contact_surface_max_cm':maximum,'contact_status_equal':x['contact_status']==y['contact_status'],'camera_matrix_max_error':max(float(np.abs(np.array(u['world_matrix'])-v['world_matrix']).max()) for u,v in zip(x['cameras'],y['cameras'])),'pose_replay_pass':matrix<1e-4 and maximum<1e-4})
if not rows:raise SystemExit('No shared cases')
a.output.write_text(json.dumps({'checks':rows,'scope':'Pose and pre-contact geometry replay; does not certify a blocked contact solver.'},indent=2),encoding='utf-8')
print(a.output)
