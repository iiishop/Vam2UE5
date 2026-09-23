"""Local, source-locked Stage05 calibration report; never executes source scripts."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
SCRIPTS=Path(__file__).resolve().parent
sys.path[:0]=[str(SCRIPTS),str(SCRIPTS.parent/'Saved/Python')]
from vam_decode import require
from vam_unity import Bundle,skeleton
from vam_native_source import prepare,digest,reference_bones
from vam_triax_lbs import fit


def calibrate(data):
    from vam_native_input import load_preview
    preview=load_preview(data)
    ir_path=data/'Decoded'/(preview['decode_id']+'.ir.json')
    ir=json.loads(ir_path.read_text(encoding='utf8'))
    plan=json.loads((data/'Plans'/(ir['plan_id']+'.json')).read_text(encoding='utf8'))
    character=next(i for i in plan['items'] if i.get('builtin_mapping',{}).get('entry',{}).get('role')=='character')
    ref=character['builtin_mapping']['files']['a_per'];path=(Path(plan['source_root'])/ref['path']).resolve()
    require(path.is_relative_to(Path(plan['source_root']).resolve()),'source_path',str(path))
    with path.open('rb') as f:require(hashlib.file_digest(f,'sha256').hexdigest()==ref['sha256'],'source_changed',str(path))
    source=skeleton(Bundle(path),character['builtin_mapping']['entry']['gender'])
    by_name={b['name']:b for b in source}
    hierarchy=[]
    for bone in ir['skeleton']:
        b=by_name[bone['name']]
        require(b['source_object']==bone['source_object'],'bone_identity',bone['name'])
        for transform in [b['transform_parameters'],*[t['parameters'] for t in b['intermediate_transforms']]]:
            require(all(abs(float(transform['m_LocalScale'][axis])-1)<1e-6 for axis in 'xyz'),
                    'scaled_source_bind_pending',b['name']+': non-unit Transform scale needs an explicit bind adapter')
        hierarchy.append({'name':b['name'],'daz_parent':b['parent'],'transform_parent':b['transform_parent'],
                          'transform_object':b['transform_object'],'intermediate_transforms':b['intermediate_transforms']})
        bone['parent']=b['transform_parent']
        bone['transform_provenance']=hierarchy[-1]
    from vam_editable_morphs import extend
    selection=os.environ.get('VAM_MORPH_SET_FILE',str(SCRIPTS.parent/'Config/Stage05MorphSet.json'))
    ir,morph_lock=extend(ir,plan,data,selection)
    contract=prepare(ir)
    contract['editable_morph_lock']=morph_lock
    contract['neutral_bones']=contract['bones']
    reference=copy.deepcopy(ir['skeleton'])
    for bone in reference:bone['base_position']=bone['position']
    contract['bones']=reference_bones(reference)
    contract['bind_signature']=digest(contract['bones'])
    contract['skeleton_baseline']='Appearance p0 BoneCenter positions; original neutral binds retained separately. Orientation/scale/rotation formulas not yet evaluated.'
    from vam_fit import apply_bone_centers,finalize_bone_centers
    neutral_source=copy.deepcopy(ir['skeleton'])
    for b in neutral_source:
        b['position']=b.get('base_position',b['position'])[:]
        b.pop('morph_center_offset',None)
    morph_records={r['id']:r for r in ir['records'] if r.get('kind')=='morph'}
    for morph in contract['morphs']:
        source_morph=morph_records[morph['source_id']]
        adjusted=copy.deepcopy(neutral_source);diagnostics=[]
        apply_bone_centers(adjusted,source_morph['data'],1.,diagnostics)
        finalize_bone_centers(adjusted)
        for b in adjusted:b['base_position']=b['position'][:]
        unit=reference_bones(adjusted)
        morph['bone_centers']=[{'bone_index':i,'local_translation':[x-y for x,y in zip(b['translation'],contract['neutral_bones'][i]['translation'])]}
                              for i,b in enumerate(unit) if any(abs(x-y)>1e-10 for x,y in zip(b['translation'],contract['neutral_bones'][i]['translation']))]
        meta=source_morph['data']['parameters'];editable=source_morph.get('editable_metadata',{})
        morph['display_name']=str(meta.get('displayName') or meta.get('morphName') or meta.get('name') or source_morph['path'])
        source_group=str(meta.get('group') or '')
        category='Expression' if source_group.startswith(('Expressions','Pose Controls/Head/Expressions')) else 'Pose' if source_group.startswith('Pose Controls') else 'Shape' if source_group=='Characters' else 'Unclassified'
        morph['group']=editable.get('group',category)
        morph['unit']=editable.get('unit','source coefficient')
        morph['formula_diagnostics']=diagnostics
        changed={i for i,d in enumerate(morph['deltas']) if any(abs(x)>1e-10 for x in d)}
        morph['affected_regions']=['source_material_'+str(i) for i,s in enumerate(contract['body']['sections']) if changed.intersection(s)]
        morph['minimum']=min(0.,float(morph['minimum']),float(morph['default']))
        morph['maximum']=max(0.,float(morph['maximum']),float(morph['default']))
    bone_error=max(abs(contract['neutral_bones'][i]['translation'][k]+sum(m['default']*next((x['local_translation'][k] for x in m['bone_centers'] if x['bone_index']==i),0.) for m in contract['morphs'])-b['translation'][k]) for i,b in enumerate(contract['bones']) for k in range(3))
    require(bone_error<1e-5,'bone_center_basis','p0 local reference reconstruction differs from source')
    contract['bone_center_p0_error_cm']=bone_error
    contract['shape_kernel_version']=1
    contract['formula_classification']=[{'source_id':m['source_id'],'executed_bone_center_offsets':m['bone_centers'],
        'retained_unexecuted_or_missing_targets':m['formula_diagnostics'],'original_formulas':m['source_parameters'].get('formulas',[])} for m in contract['morphs']]
    contract['blockers']=[b for b in contract['blockers'] if b['code']!='formula_adapter_pending']
    merged=next(r for r in ir['records'] if r.get('class')=='DAZMergedMesh')
    skin=next(r for r in ir['records'] if r['kind']=='skin' and str(r['parameters']['dazMesh']['m_PathID'])==merged['object'])
    if not contract['influences']:
        import numpy as np
        appearance=np.asarray(contract['body']['vertices'])
        for morph in contract['morphs']:appearance+=morph['default']*np.asarray(morph['deltas'])
        contract['influences'],contract['skin']['fit']=fit(skin['parameters'],appearance,contract['bones'])
        contract['skin']['calibration']='mathematical_reference_fit_pending_runtime'
        contract['skin']['calibration_shape']='X(p0); Morph geometry remains X0 plus absolute parameters'
    contract['hierarchy_provenance']=hierarchy
    if 'fit' in contract['skin']:
        quality=json.loads((SCRIPTS.parent/'Config/Stage05Quality.json').read_text(encoding='utf8'))
        metrics=contract['skin']['fit'];limits=quality['thresholds_cm']
        require(metrics['rms_cm']<=limits['global_rms'] and metrics['p95_cm']<=limits['global_p95'] and
                all(x['maximum_cm']<=limits['per_joint_maximum'] for x in metrics['per_joint'].values()),'skin_quality_gate','Source-reference fit exceeds declared Stage05 thresholds')
        contract['skin']['quality_gate']={'passed':True,'policy':quality}
    contract['source_ir']={'path':str(ir_path.resolve()),'sha256':hashlib.sha256(ir_path.read_bytes()).hexdigest()}
    contract['status']='calibrated_reference_only'
    # A native asset may carry this reference fit, but it is NOT full Stage05 acceptance.
    contract['native_reference_ready']=bool(contract['influences']) and not any(b['code'] in ('nonlinear_graft_pending','nonlinear_shape','source_hierarchy_pending') for b in contract['blockers'])
    contract['contract_id']=digest(contract)
    out=data/'NativeBuild';out.mkdir(exist_ok=True)
    target=out/(ir['decode_id']+'.calibrated.json')
    target.write_text(json.dumps(contract,ensure_ascii=False,separators=(',',':'),allow_nan=False),encoding='utf8')
    print(json.dumps({'path':str(target),'skin':contract['skin'],'bones':len(contract['bones']),
                      'remaining':contract['blockers']},ensure_ascii=True),flush=True)
    return contract

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--data',type=Path,required=True)
    calibrate(p.parse_args().data)
