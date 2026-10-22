"""Same-matrix source-ID audit of actual VaM buffers and M A1 stages."""
import json, csv
from pathlib import Path
import numpy as np
from compare_c15_asset_replay import surface
from prepare_knee_vam_comparison import ROOT, OUT, read

def metrics(d, ids):
    e=np.linalg.norm(d,axis=1)
    return dict(count=len(e),rms_cm=float(np.sqrt(np.mean(e*e))),p95_cm=float(np.percentile(e,95)),max_cm=float(e.max()),worst_source=int(ids[e.argmax()]))

def main():
    request=read(OUT/'request.json');runtime=OUT/'runtime'
    masks=np.load(ROOT/'Saved/LowerBodyContact/C1.5/run-20261009-224219/c2-region-masks.npz')
    source_ids=masks['source_ids'].astype(int)
    P=np.eye(4);P[:3,:3]=[[0,0,1],[1,0,0],[0,1,0]]
    rows=[];gates=[];morphs=[];input_rows=[]
    standing_input=np.fromfile(runtime/'deep-kneel-0000-ue-reference-morphed.f32','<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100
    for pose in request['poses']:
        meta=read(runtime/(pose['id']+'-ue-reference.json'))
        s=read(Path(pose['ue_video']))['samples'][pose['frame']]
        # Metadata carries final bone order through the peak A1 snapshot.
        peak=read(Path(pose['ue_video']))['samples'][165]
        fb=read(Path(peak['audit_bundle'])/'01-final-weights/inputs.json')['bones']
        expected={b['name']:np.array(m).reshape(4,4).T for b,m in zip(fb,s['bone_matrices'])}
        errors=[]
        for b in meta['bones']:
            if b['id'] not in expected:continue
            m=P@np.array(b['worldMatrix'],float).reshape(4,4)@P.T;m[:3,3]*=100
            e=expected[b['id']]
            errors.append(dict(bone=b['id'],position_cm=float(np.linalg.norm(m[:3,3]-e[:3,3])),rotation_matrix_max=float(np.abs(m[:3,:3]-e[:3,:3]).max())))
        # VaM eye gaze still drives eye rotations; exclude only eyes, retain
        # their errors explicitly. They have no lower-body skin influence.
        accepted=[x for x in errors if x['bone'] not in ('lEye','rEye')]
        gate=dict(pose=pose['id'],bones=errors,excluded_bones=['lEye','rEye'],position_max_cm=max(x['position_cm'] for x in accepted),rotation_max=max(x['rotation_matrix_max'] for x in accepted))
        gates.append(gate)
        if gate['position_max_cm']>.01 or gate['rotation_max']>1e-4:raise RuntimeError(('Actual matrix mismatch',gate))
        morphs.append(dict(pose=pose['id'],morphs=meta['morphs'],drivers=meta['drivers']))
        if not pose.get('audit_bundle'):continue
        a=Path(pose['audit_bundle'])
        targets={k:np.fromfile(runtime/meta[k]['file'],'<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100 for k in ('raw','smoothed')}
        targets['preskin']=np.fromfile(runtime/(pose['id']+'-ue-reference-morphed.f32'),'<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100
        mapping=read(a/'source-mapping.json');pre=read(a/'00-source-lbs/preskin.json')['positions']
        prepoints={mapping['input_to_source'][j]:pre[i] for i,j in enumerate(mapping['render_to_input']) if 0<=j<len(mapping['input_to_source']) and mapping['input_to_source'][j]>=0}
        stages=['00-source-lbs','01-final-weights','02a-chain-disabled','02-static-helpers','03-corrective','04-jiggle']
        surfaces={stage:surface(a,stage) for stage in stages}
        eye_ids={i for i,b in enumerate(fb) if b['name'] in ('lEye','rEye')}
        fm=read(a/'mapping.json');lower_ids=set(int(source_ids[i]) for k in masks.files if k!='source_ids' for i in np.flatnonzero(masks[k]>.05))
        for v,inf in enumerate(read(a/'01-final-weights/inputs.json')['render_vertex_influences']):
            j=fm['render_to_input'][v]
            if j>=0 and fm['input_to_source'][j] in lower_ids:
                assert all(int(b) not in eye_ids or w==0 for b,w in inf), 'Excluded eye affects audited region'
        for region in masks.files:
            if region=='source_ids':continue
            idx=np.flatnonzero(masks[region]>.05)
            pairs=[(int(source_ids[i]),int(i)) for i in idx if int(source_ids[i]) in prepoints and all(int(source_ids[i]) in x for x in surfaces.values())]
            ids=np.array([v[0] for v in pairs]);vi=np.array([v[1] for v in pairs])
            if not len(ids):raise RuntimeError(('Missing region',region))
            if pose['id']=='deep-kneel-0165':
                for label,delta in [('standing-body-input',np.array([prepoints[i] for i in ids])-standing_input[vi]),('vam-pose-input-change',targets['preskin'][vi]-standing_input[vi])]:
                    input_rows.append(dict(region=region,kind=label,**metrics(delta,ids)))
            rows.append(dict(pose=pose['id'],region=region,kind='input-mismatch',stage='preskin',target='preskin',**metrics(np.array([prepoints[i] for i in ids])-targets['preskin'][vi],ids)))
            arrays={stage:np.array([points[i] for i in ids]) for stage,points in surfaces.items()}
            for stage,u in arrays.items():
                for target in ('raw','smoothed'):
                    rows.append(dict(pose=pose['id'],region=region,kind='surface-difference',stage=stage,target=target,**metrics(u-targets[target][vi],ids)))
            for first,last in zip(stages[:-1],stages[1:]):
                rows.append(dict(pose=pose['id'],region=region,kind='stage-change',stage=last,target=first,**metrics(arrays[last]-arrays[first],ids)))
            rows.append(dict(pose=pose['id'],region=region,kind='vam-smoothing',stage='smoothed',target='raw',**metrics(targets['smoothed'][vi]-targets['raw'][vi],ids)))
    (OUT/'matrix-gates.json').write_text(json.dumps(gates,indent=2))
    (OUT/'morph-states.json').write_text(json.dumps(morphs,indent=2))
    (OUT/'input-state-decomposition.json').write_text(json.dumps(input_rows,indent=2))
    with (OUT/'regional-errors.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    print('poses',len(gates),'rows',len(rows),'matrix position max cm',max(x['position_max_cm'] for x in gates))
if __name__=='__main__':main()
