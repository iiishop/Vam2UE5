"""Compare captured A1 layers by retained source IDs; reuse sealed B2 regions."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from audit_lower_body_boundaries import read, data, stats

p=argparse.ArgumentParser()
p.add_argument('baseline',type=Path)
p.add_argument('contract',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--world',choices=['editor','pie'],default='editor')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
states=[read(a.baseline/'main-regression'/a.world/pose/'state.json') for pose in ['standing','knee-120']]
bundles=[Path(s['audit_bundle']) for s in states]
detail=read(a.baseline/'c15-replay/fields/detail.json')
mapping=read(bundles[0]/'mapping.json')
source_ids=np.array([mapping['input_to_source'][i] for i in mapping['render_to_input']])
regions={}
for side in range(2):
    for name in ['anterior_hip','lateral_glute','glute_thigh']:
        f=detail['fields'][f's{side}_roi_{name}']
        field=np.fromfile(a.baseline/'c15-replay/fields'/f['file'],dtype='<f4')
        regions[f'{side}_{name}']=set(source_ids[field[np.array(mapping['render_to_input'])]>.05].tolist())
# Same documented posterior diagnostic bounds as audit_glute_fold.py.
ids,positions=data(bundles[0],'03-corrective')
regions['posterior_fold_region']=set(ids[(abs(positions[:,1])>3)&(abs(positions[:,1])<17)&(positions[:,0]<-2)&(positions[:,2]>82)&(positions[:,2]<110)].tolist())
layers={};surfaces={}
for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective','04-jiggle']:
    surfaces[stage]=[{int(i):v for i,v in zip(*data(b,stage)) if i>=0} for b in bundles]
    rows={}
    for name,roi in regions.items():
        selected=sorted(roi & surfaces[stage][0].keys() & surfaces[stage][1].keys())
        d=np.array([np.linalg.norm(surfaces[stage][1][v]-surfaces[stage][0][v]) for v in selected])
        rows[name]=dict(displacement_cm=stats(d),worst_source_vertex=selected[int(d.argmax())],above_001cm=int((d>.01).sum()))
    layers[stage]=rows
actual=[{b['bone']:np.array(b['component_matrix']).reshape(4,4) for b in s['actual_bones']} for s in states]
bone_changes=[]
for name,m in actual[0].items():
    n=actual[1][name];error=float(np.max(abs(n-m)))
    if error>1e-8:
        rot=m[:3,:3]@np.linalg.inv(n[:3,:3]);angle=float(np.degrees(np.arccos(np.clip((np.trace(rot)-1)/2,-1,1))))
        bone_changes.append(dict(bone=name,matrix_max_difference=error,rotation_delta_deg=angle))
manifests=[read(b/'manifest.json') for b in bundles]
inputs=[read(b/'03-corrective/inputs.json') for b in bundles]
contract=read(a.contract);raw={}
for v,b,w in contract['influences']:raw.setdefault(v,[]).append((b,w))
worst=sorted(set(r['worst_source_vertex'] for r in layers['03-corrective'].values()))
weights=[]
source_input=read(bundles[0]/'00-source-lbs/inputs.json');source_map=read(bundles[0]/'source-mapping.json')
source_render=[source_map['input_to_source'][i] for i in source_map['render_to_input']]
for sid in worst:
    rv=int(np.flatnonzero(source_ids==sid)[0]);sv=source_render.index(sid)
    weights.append(dict(source_vertex=sid,standing_position=surfaces['03-corrective'][0][sid].tolist(),
        contract_weights=[(contract['bones'][b]['name'],w) for b,w in raw[sid]],
        source_render_weights=[(source_input['bones'][b]['name'],w) for b,w in source_input['render_vertex_influences'][sv]],
        final_render_weights=[(inputs[0]['bones'][b]['name'],w) for b,w in inputs[0]['render_vertex_influences'][rv]]))
# Explain every final ROI motion from the original source's changed-bone support.
changed={v['bone'] for v in bone_changes};violations=[];moving=[]
for sid in set.union(*regions.values()):
    d=float(np.linalg.norm(surfaces['03-corrective'][1][sid]-surfaces['03-corrective'][0][sid]))
    if d>1e-5:
        moving.append(sid)
        if not any(contract['bones'][b]['name'] in changed and w>0 for b,w in raw.get(sid,[])):violations.append(sid)
out=dict(schema='standing-knee-audit/1',world=a.world,asset=manifests[0]['configuration'],states=[s['request'] for s in states],
    body_transform_equal=states[0]['body_to_world']==states[1]['body_to_world'],shape_equal=manifests[0]['shape']==manifests[1]['shape'],
    active_corrective_morphs_equal=inputs[0]['active_morphs']==inputs[1]['active_morphs'],
    changed_bones=bone_changes,layers=layers,weight_examples=weights,
    moving_roi_source_vertices=len(moving),moving_without_original_changed_bone_weight=violations,
    source_contract_sha256=hashlib.sha256(a.contract.read_bytes()).hexdigest(),
    provenance=dict(bundles=[str(b) for b in bundles],fields=str(a.baseline/'c15-replay/fields/detail.json')),
    scope='Source-ID matched actual UE A1 surfaces, deduplicated. B2 engineering masks threshold .05. No biological deformation ground truth inferred from fitted LBS weights. GPU final surface unavailable with contact off; images separately validate actual rendering.')
assert out['shape_equal'] and out['active_corrective_morphs_equal'] and not violations
assert not any('Glute' in b['bone'] or 'Thigh' in b['bone'] or b['bone'] in ['hip','pelvis'] for b in bone_changes)
(a.output/'analysis.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps(dict(regions=layers['03-corrective'],unexplained=violations),indent=2))
