"""Controlled matrix ablation, not a new runtime character or rendered result."""
import json
from pathlib import Path
import numpy as np
from prepare_knee_vam_comparison import ROOT,OUT,read
from replay_deformation_audit import skin_snapshot

def main():
    records=read(OUT/'deep-edge-stages.json');result=[]
    for action in ('deep-kneel','single-flexion'):
        bundles={g:Path(read(ROOT/f'Saved/KneeDeepRepair/pie-final/{g}/{g}-{action}/video.json')['samples'][165]['audit_bundle']) for g in ('C','M')}
        inputs={g:read(d/'03-corrective/inputs.json') for g,d in bundles.items()}
        pre={g:np.array(read(d/'03-corrective/preskin.json')['positions']) for g,d in bundles.items()}
        assert inputs['C']['render_vertex_influences']==inputs['M']['render_vertex_influences']
        reproduced={g:skin_snapshot(inputs[g],pre[g]) for g in bundles}
        replay={g:float(np.linalg.norm(reproduced[g]-np.array(read(d/'03-corrective/surface.json')['positions']),axis=1).max()) for g,d in bundles.items()}
        assert max(replay.values())<1e-4
        # Keep every M morph value, weight, bind and main bone; swap only leg
        # helper component matrices to the same-frame chain-disabled C result.
        matrices=[b['component_matrix_row_major'] for b in inputs['M']['bones']]
        replaced=[]
        for i,b in enumerate(inputs['M']['bones']):
            assert b['name']==inputs['C']['bones'][i]['name']
            if '_Thigh_' in b['name'] or '_Calf_' in b['name']:
                matrices[i]=inputs['C']['bones'][i]['component_matrix_row_major'];replaced.append(b['name'])
        counter=skin_snapshot(inputs['M'],pre['M'],matrices)
        tri=np.array(inputs['M']['triangle_indices']).reshape(-1,3)
        source_ids=np.load(ROOT/'Saved/LowerBodyContact/C1.5/run-20261009-224219/c2-region-masks.npz')['source_ids'].astype(int)
        source_index={int(s):i for i,s in enumerate(source_ids)}
        meta=read(OUT/f'runtime/{action}-0165-ue-reference.json')
        targets={k:np.fromfile(OUT/'runtime'/meta[k]['file'],'<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100 for k in ('raw','smoothed')}
        def angle(q):
            n=np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0]);n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
            return float(np.degrees(np.arccos(np.clip(n[0]@n[1],-1,1))))
        edges=[]
        for r in records:
            if r['action']!=action:continue
            item=dict(triangles=r['triangles'],stages=r['stages'],counterfactual_no_M_helper_angle_deg=angle(counter[tri[r['triangles']]]))
            keys=r['source_keys']
            if all(k[0]=='source' and int(k[1]) in source_index for face in keys for k in face):
                ix=np.array([[source_index[int(k[1])] for k in face] for face in keys])
                for k,target in targets.items():item['vam_'+k+'_angle_deg']=angle(target[ix])
            else:item['vam_angle_unavailable']='refined vertex without direct source ID; no nearest-neighbour substitution'
            edges.append(item)
        result.append(dict(action=action,replay_max_cm=replay,pre_skin_M_vs_C_max_cm=float(np.linalg.norm(pre['M']-pre['C'],axis=1).max()),replaced_bones=replaced,edges=edges))
    (OUT/'M-corrective-interaction.json').write_text(json.dumps(result,indent=2))
    print([(x['action'],x['replay_max_cm'],len(x['edges'])) for x in result])
if __name__=='__main__':main()
