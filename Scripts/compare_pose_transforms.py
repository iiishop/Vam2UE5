"""Compare VaM transforms AFTER conversion to UE axes/units with captured UE bones.
Raw UI Euler values are deliberately not supported.
"""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('ue_state',type=Path);p.add_argument('converted_vam',type=Path);a=p.parse_args()
u=json.loads(a.ue_state.read_text(encoding='utf-8-sig'));v=json.loads(a.converted_vam.read_text(encoding='utf-8-sig'))
if v.get('space')!='ue_component_cm_row_major':raise SystemExit('Reference must contain converted component matrices in UE cm, not UI Euler angles')
ub={b['bone']:np.array(b['component_matrix']).reshape(4,4) for b in u['actual_bones']};out=[]
for b in v['actual_bones']:
    if b['bone'] not in ub:raise SystemExit('Unmapped reference bone: '+b['bone'])
    x=ub[b['bone']];y=np.array(b['component_matrix'],float).reshape(4,4)
    if not np.isfinite(y).all():raise SystemExit('Nonfinite reference matrix')
    def rotation(m):
        left,_,right=np.linalg.svd(m[:3,:3]);return left@right
    cosine=np.clip((np.trace(rotation(x)@rotation(y).T)-1)/2,-1,1)
    out.append({'bone':b['bone'],'translation_error_cm':float(np.linalg.norm(x[3,:3]-y[3,:3])),'rotation_error_degrees':float(np.degrees(np.arccos(cosine))),'matrix_max_error':float(abs(x-y).max())})
if not out:raise SystemExit('No reference bones supplied')
print(json.dumps({'comparison':'actual converted transforms, not Euler labels','bones':out},indent=2))
