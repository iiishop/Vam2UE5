"""Compare actual official Face component, rigidly returned to saved source pose.

Pose transform is measured only between MH states, never fitted to source face.
"""
import sys,json
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'Saved/Python'))
import numpy as np
p=root/'Saved/MetaHuman/FitRepair';name=sys.argv[1]
posed=np.array(json.loads((p/(name+'_posed.json')).read_text())['vertices'])[:,[0,2,1]]
apose=np.array(json.loads((p/(name+'_apose.json')).read_text())['vertices'])[:,[0,2,1]]
height=float(np.ptp(posed[:,2]))
mask=posed[:,2]>posed[:,2].max()-.04*height
assert np.count_nonzero(mask)>3,'Insufficient neutral head points for pose recovery'
a=apose[mask];b=posed[mask];ac=a.mean(0);bc=b.mean(0)
u,s,vt=np.linalg.svd((a-ac).T@(b-bc));rotation=u@vt
assert np.linalg.det(rotation)>0,'Unexpected reflected pose transform'
error=np.linalg.norm((a-ac)@rotation+bc-b,axis=1)
assert error.max()<.05,'Head pose is not rigid enough for this comparison'
raw=json.loads((p/(name+'_Face.json')).read_text())
v=(np.asarray(raw['vertices'])-ac)@rotation+bc
raw['vertices']=v[:,[0,2,1]].tolist()
raw['comparison_note']='Actual Face component with MH A-pose-to-saved-pose rigid transform; no fitting to source'
raw['pose_transform_max_residual_cm']=float(error.max())
raw['normal_rotation']=rotation.tolist()
(p/(name+'_component.json')).write_text(json.dumps(raw),encoding='utf8')
print('MH-only pose transform residual cm',error.max())
