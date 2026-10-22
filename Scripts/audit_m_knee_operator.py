"""Compare actual M GPU output against frozen baseline and same-matrix VaM.

Does not substitute offline skinning for GPU readback or nearest-neighbour map.
"""
import argparse,json
from pathlib import Path
import numpy as np
from compare_c15_asset_replay import surface
from replay_deformation_audit import run as replay

read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def stats(x):
    x=np.asarray(x)
    return dict(rms=float(np.sqrt(np.mean(x*x))),mean=float(x.mean()),median=float(np.median(x)),p95=float(np.percentile(x,95)),max=float(x.max()))
def angle(pos,tri):
    q=pos[tri];n=np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0]);n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
    return float(np.degrees(np.arccos(np.clip(n[0]@n[1],-1,1))))
def main():
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
    root=Path.cwd();old=root/'Saved/KneeDeepRepair/pie-final';vam=root/'Saved/LowerBodyContact/C1.5/knee-M-20261022'
    # Committed report is a selected copy of the original runtime capture.
    if not (vam/'runtime').exists():vam=root/'Evidence/KneeVaMComparison'
    masks=np.load(root/'Saved/LowerBodyContact/C1.5/run-20261009-224219/c2-region-masks.npz')
    ids=masks['source_ids'].astype(int);out=dict(actions=[])
    for action in ['deep-kneel','single-flexion']:
        f=a.run/'M'/('M-'+action)/'video.json'
        if not f.exists():continue
        new=read(f)['samples'];before=read(old/'M'/('M-'+action)/'video.json')['samples']
        assert len(new)==len(before)
        gate=[]
        for x,y in zip(new,before):
            gate.append(dict(frame=x['frame'],knee_max_deg=float(np.max(np.abs(np.array(x['actual_knee_deg'])-y['actual_knee_deg']))),camera_max=float(np.max(np.abs(np.array(x['camera_matrix'])-y['camera_matrix'])))))
        item=dict(action=action,pose_camera_gates=gate,cpu_us=stats([x['knee_operator_us'] for x in new]) if 'knee_operator_us' in new[0] else None,solver_active_frames=sum(x['contact_solver_active'] for x in new),frames=[])
        for frame in [45,135,165]:
            b=Path(new[frame]['audit_bundle']);previous=Path(before[frame]['audit_bundle']);replay(b)
            inp=read(b/'04a-operator-corrected/inputs.json');prev=read(previous/'04-jiggle/inputs.json')
            assert inp['triangle_indices']==prev['triangle_indices']
            assert inp['render_vertex_influences']==prev['render_vertex_influences']
            pre_error=float(np.max(np.abs(np.array(read(b/'04a-operator-corrected/preskin.json')['positions'])-np.array(read(previous/'04-jiggle/preskin.json')['positions']))))
            bind_error=max(float(np.max(np.abs(np.array(x['inverse_bind_matrix_row_major'])-y['inverse_bind_matrix_row_major']))) for x,y in zip(inp['bones'],prev['bones']))
            assert pre_error<1e-4 and bind_error<1e-6,(pre_error,bind_error)
            # Primary bone matrices must match before reusing VaM snapshots.
            errors=[]
            for x,y in zip(inp['bones'],prev['bones']):
                assert x['name']==y['name']
                if any(t in x['name'] for t in ['_Thigh_','_Calf_','_Glute_','_Breast_']):continue
                errors.append(float(np.max(np.abs(np.array(x['component_matrix_row_major'])-y['component_matrix_row_major']))))
            gpu=read(b/'gpu-surface-status.json');entry=dict(frame=frame,audit_bundle=str(b),preskin_max_cm=pre_error,bind_matrix_max=bind_error,primary_matrix_max=max(errors),gpu_status=gpu,replay=read(b/'replay-report.json').get('gpu_reconstruction'),regions=[])
            assert entry['primary_matrix_max']<1e-4,entry
            assert gpu['status']=='native_render_readback',entry
            pos=np.array(read(b/'05-gpu-surface.json')['positions']);cpu=np.array(read(b/'04a-operator-corrected/surface.json')['positions'])
            entry['gpu_vs_cpu_cm']=stats(np.linalg.norm(pos-cpu,axis=1))
            tri=np.array(inp['triangle_indices']).reshape(-1,3)
            uncompensated=np.array(read(b/'04-jiggle/surface.json')['positions'])
            cross=lambda v:np.cross(v[tri[:,1]]-v[tri[:,0]],v[tri[:,2]]-v[tri[:,0]])
            entry['new_triangle_orientation_reversals']=int(np.sum(np.sum(cross(pos)*cross(uncompensated),axis=1)<0))
            mapping=read(b/'mapping.json');groups={}
            for v,j in enumerate(mapping['render_to_input']):
                if j>=0:groups.setdefault(j,[]).append(v)
            pairs=np.array([(v[0],i) for v in groups.values() for i in v[1:]],dtype=int)
            entry['logical_duplicate_max_gap_cm']=float(np.linalg.norm(pos[pairs[:,0]]-pos[pairs[:,1]],axis=1).max()) if len(pairs) else None
            normals=np.array(read(b/'05-gpu-normals.json')['positions']);entry['gpu_normals_finite']=bool(np.isfinite(normals).all())
            pre=np.array(read(previous/'04-jiggle/surface.json')['positions'])
            entry['edges']=[dict(triangles=ix,before_deg=angle(pre,tri[ix]),after_deg=angle(pos,tri[ix])) for ix in [[901,903],[50,51],[35,37]]]
            meta=read(vam/'runtime'/f'{action}-{frame:04d}-ue-reference.json')
            targets={k:np.fromfile(vam/'runtime'/meta[k]['file'],'<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100 for k in ['raw','smoothed']}
            points=surface(b,'04a-operator-corrected');prior=surface(previous,'04-jiggle')
            for region in masks.files:
                if region=='source_ids':continue
                ix=[i for i in np.flatnonzero(masks[region]>.05) if int(ids[i]) in points and int(ids[i]) in prior]
                if not ix:continue
                for label,pts in [('before',prior),('after',points)]:
                    for target,t in targets.items():entry['regions'].append(dict(region=region,state=label,target=target,**stats(np.linalg.norm(np.array([pts[int(ids[i])] for i in ix])-t[ix],axis=1))))
            item['frames'].append(entry)
        out['actions'].append(item)
    out['gpu_tolerance_cm']=.02
    out['gpu_matches_frozen_cpu']=all(f['gpu_vs_cpu_cm']['max']<=.02 for x in out['actions'] for f in x['frames'])
    (a.run/'operator-analysis.json').write_text(json.dumps(out,indent=2))
    print(a.run/'operator-analysis.json')
    if not out['gpu_matches_frozen_cpu']:raise RuntimeError('Native GPU output exceeds frozen snapshot tolerance; do not certify frame agreement')
if __name__=='__main__':main()
