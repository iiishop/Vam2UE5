"""Recover source-surface 3D anchors from saved face curves and calibrated rays.

Diagnostic for the locked Qimeng recipe, not a universal landmark mapping.
Historical experiment only. New calibrations use vam_face_calibration_cli.py;
do not promote this sample-specific script into the production import path.
Epic's local vertex IDs are read at runtime, never copied into the plugin.
"""
import json, sys, math, hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'Saved/Python'))
import numpy as np
p=root/'Saved/MetaHuman/FitRepair'
data=json.loads((p/'old_target.json').read_text())
v=np.asarray(data['vertices'])[:,[1,0,2]]*[-1,1,1]
t=np.asarray(data['triangles']).reshape(-1,3)
t=t[(v[t,2].min(1)>150)]
cal=json.loads((p/'face-calibration.json').read_text())
landmarks=json.loads(Path('I:/Program/Epic Games/UE_5.8/Engine/Plugins/MetaHuman/MetaHumanCharacter/Content/Face/IdentityTemplate/face_landmarks.json').read_text())
cam=np.array([0.,65.,164.]); focal=512/(2*math.tan(math.radians(40)/2))
e1=v[t[:,1]]-v[t[:,0]]; e2=v[t[:,2]]-v[t[:,0]]; s=cam-v[t[:,0]]
points={}; provenance={}
fit=np.asarray(json.loads((p/'calibrated_posed.json').read_text())['vertices'])[:,[0,2,1]]
def hit(pixel):
    ray=np.array([(pixel[0]-256)/focal,-1.,-(pixel[1]-256)/focal])
    h=np.cross(np.broadcast_to(ray,e2.shape),e2);det=(e1*h).sum(1)
    inv=np.divide(1,det,out=np.zeros_like(det),where=abs(det)>1e-9)
    a=inv*(s*h).sum(1);q=np.cross(s,e1);b=inv*(q*ray).sum(1);distance=inv*(e2*q).sum(1)
    valid=(abs(det)>1e-9)&(a>=0)&(b>=0)&(a+b<=1)&(distance>0)
    if not valid.any():raise ValueError('Curve misses source surface')
    distance=np.where(valid,distance,np.inf);i=int(distance.argmin())
    pos=cam+distance[i]*ray
    if pos[1]<3:
        # Eyelid contours can cross the open eye socket. Never anchor to the
        # back of the skull: recover a nearby visible skin boundary instead.
        candidates=np.where((v[:,1]>3)&(v[:,2]>163)&(v[:,2]<167))[0]
        projected=np.column_stack((256+v[candidates,0]*focal/(65-v[candidates,1]),256-(v[candidates,2]-164)*focal/(65-v[candidates,1])))
        error=np.linalg.norm(projected-pixel,axis=1);nearest=int(error.argmin())
        if error[nearest]>3:raise ValueError('Eye contour has no nearby source skin boundary')
        idx=int(candidates[nearest]);return v[idx], [idx]
    return pos, t[i].tolist()
for name,curve in cal['CurveTrackingPoints'].items():
    if not ('eyelid' in name or 'lip_upper_outer' in name or 'lip_lower_outer' in name):continue
    ids=landmarks[name]['vIDs']
    pixels=np.array([[q['X'],q['Y']] for q in curve['TrackingPoints']])
    if 'eyelid' in name:
        # Epic's eyelid vIDs are a set, not a contour traversal. Match by
        # lateral location, not array position (which crosses the eyelid).
        ids=sorted(ids,key=lambda i:fit[i,0])
        pixels=pixels[np.argsort(pixels[:,0])]
    # Sparse equally spaced samples avoid overwhelming existing shape constraints.
    for j in sorted(set([0,len(ids)//2,len(ids)-1])):
        fraction=(fit[ids[j],0]-fit[ids[0],0])/(fit[ids[-1],0]-fit[ids[0],0]) if 'eyelid' in name else j/(len(ids)-1)
        at=fraction*(len(pixels)-1);left=int(at);right=min(left+1,len(pixels)-1)
        pixel=pixels[left]*(1-(at-left))+pixels[right]*(at-left)
        if 'eyelid' in name:
            x=pixels[0,0]+fraction*(pixels[-1,0]-pixels[0,0])
            pixel=np.array([x,np.interp(x,pixels[:,0],pixels[:,1])])
        pos,tri=hit(pixel);key=str(ids[j]);points[key]=pos.tolist()
        provenance[key]={'curve':name,'pixel':pixel.tolist(),'source_triangle':tri}
# Explicit sample-specific nose-tip correspondence, verified in the front/side mesh.
indices=np.where((abs(v[:,0])<.3)&(v[:,2]>160)&(v[:,2]<164))[0]
idx=int(indices[np.argmax(v[indices,1])])
points['2925']=v[idx].tolist();provenance['2925']={'meaning':'nose tip','source_vertex':idx}
(p/'face-anchors.json').write_text(json.dumps({'schema':'vam-face-calibration-diagnostic/1',
    'reference':'/Game/VamCharacters/C_b41dfaaabfc8427b0ef4e065',
    'target_sha256':hashlib.sha256((p/'old_target.json').read_bytes()).hexdigest(),
    'tracking_sha256':hashlib.sha256((p/'face-calibration.json').read_bytes()).hexdigest(),
    'coordinate_frame':'creator-centimeters-x-lateral-y-forward-z-up/v2',
    'keypoints':points,'provenance':provenance,'visual_acceptance_passed':False},indent=2),encoding='utf8')
print('Saved',len(points),'source-surface face anchors')
