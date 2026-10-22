"""Reuse actual M capture matrices; no Euler-angle or nearest-point matching."""
import json, hashlib
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'Saved/LowerBodyContact/C1.5/knee-M-20261022'
def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if (OUT/'runtime').exists(): raise RuntimeError('Historical capture exists')
    poses=[]
    for action, frames in [('deep-kneel',[0,45,90,120,135,150,165,225,269]),('single-flexion',[0,45,90,135,165,179])]:
        case=ROOT/f'Saved/KneeDeepRepair/pie-final/M/M-{action}'
        video=read(case/'video.json'); state=read(case/'state.json')
        audit=Path(video['samples'][165]['audit_bundle'])
        final=read(audit/'01-final-weights/inputs.json')['bones']
        source=read(audit/'00-source-lbs/inputs.json')['bones']
        indices={b['name']:i for i,b in enumerate(final)}
        # Unity [x,y,z] -> UE [z,x,y], column-vector matrices.
        P=np.eye(4); P[:3,:3]=[[0,0,1],[1,0,0],[0,1,0]]
        for frame in frames:
            s=video['samples'][frame]; matrices=[np.array(m).reshape(4,4).T for m in s['bone_matrices']]
            bs=[]
            for b in source:
                m=matrices[indices[b['name']]]
                local=np.linalg.inv(matrices[indices[source[b['parent']]['name']]])@m if b['parent']>=0 else m
                local=P.T@local@P;local[:3,3]/=100
                bs.append(dict(name=b['name'],local_matrix=local.ravel().tolist()))
            camera=np.array(s['camera_matrix']).reshape(4,4)
            # Video light remains the last static oblique-view light.
            lc=np.array(state['cameras'][-1]['world_matrix']).reshape(4,4)
            light=lc[0,:3]*230-lc[2,:3]*90;light/=np.linalg.norm(light)
            view=dict(name='motion',position=((camera[3,:3]-s['root_offset_cm'])[[1,2,0]]/100).tolist(),forward=camera[0,:3][[1,2,0]].tolist(),up=camera[2,:3][[1,2,0]].tolist(),light_forward=light[[1,2,0]].tolist(),vertical_fov=42)
            poses.append(dict(id=f'{action}-{frame:04d}',width=state['width'],height=state['height'],bones=bs,views=[view],ue_video=str(case/'video.json'),frame=frame,time_seconds=s['time_seconds'],actual_knee_deg=s['actual_knee_deg'],audit_bundle=s.get('audit_bundle'),ue_png=str(case/f'video/frame-{frame:04d}.png')))
    bundle=read(ROOT/'Saved/KneeDeepRepair/candidate-3/bundle.json')
    request=dict(ue_identity=bundle['identity'],configuration=bundle['configuration'],mode='M: method4, jiggle ON, contact OFF',vam_mode='core automatic morph ON, dynamics OFF; settled motion keyframes, not dynamic equivalence',poses=poses)
    raw=json.dumps(request,indent=2);(OUT/'request.json').write_text(raw)
    (OUT/'request.sha256').write_text(hashlib.sha256(raw.encode()).hexdigest())
    print(OUT, len(poses))
if __name__=='__main__':main()
