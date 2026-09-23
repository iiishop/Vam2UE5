"""Collect latest 05.1–05.8 evidence. Never infer completion from older host tests."""
import argparse,hashlib,json
from pathlib import Path

def collect(project):
    data=project/'Plugins/VamResourceBrowser/Saved/NativeBuild'
    read=lambda name:json.loads((data/name).read_text(encoding='utf8'))
    build=read('kernel-verification.json');folder=build['folder']
    assert build['shape_kernel_version']==1 and build['independent_reload_verified'] and not build['missing_parts']
    shape=read('shape-kernel-runtime-check.json');runtime=read('stage05-final-runtime-check.json')
    reimport=read('shape-reimport-check.json');cancel=read('shape-cancel-check.json')
    animation=read('shape-animation-check.json')
    for r in (shape,runtime,reimport,animation):assert r['status']=='passed' and r['folder']==folder
    assert animation['descendant_motion_cm']>5 and animation['follower_pose_error_cm']<.001
    job=json.loads((data/'Jobs/acceptance-v7/status.json').read_text(encoding='utf8'))
    assert job['phase']=='complete' and job['detail']==folder
    assert cancel['status']=='passed' and cancel['manifest_sha256_before']==cancel['manifest_sha256_after']
    assert runtime['parameters']>=6 and runtime['components']==build['parts']+1
    assert shape['maximum_evaluated_bone_error_cm']<.001
    contract=json.loads((data/(build['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
    lock=contract['editable_morph_lock'];lock_path=data.parent/'MorphSets'/(lock['lock_id']+'.json')
    assert json.loads(lock_path.read_text(encoding='utf8'))==lock
    assert len(lock['applications'])>=3 and all(p['value']==0 for p in lock['applications'])
    assert contract['skin'].get('quality_gate',{}).get('passed',contract['skin']['use_general'])
    for asset,expected in build['asset_file_sha256'].items():
        file=project/'Content'/(asset.removeprefix('/Game/')+'.uasset')
        with file.open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,str(file)
    pose=read('stage05-kernel-clothing-pose-errors.json')
    assert pose['decode_id']==build['source_digest'] and len(pose['parts'])==build['parts']
    audit=read('shape-distribution-check.json');assert audit['status']=='passed'
    logs={name:(project/'Saved'/name).read_text(encoding='utf8',errors='replace') for name in (
        'stage05-clean-install.log','stage05-kernel-package.log','stage05-kernel-standalone.log','stage05-kernel-packaged-runtime.log','stage05-kernel-regression.log')}
    assert 'VAM_NATIVE_RELOAD_OK' in logs['stage05-clean-install.log']
    assert 'BUILD SUCCESSFUL' in logs['stage05-kernel-package.log']
    for name in ('stage05-kernel-standalone.log','stage05-kernel-packaged-runtime.log'):
        log=logs[name]
        assert 'VAM_SHAPE_COOKED_OK' in log and 'VAM_SHAPE_COOKED_FAILED' not in log
        assert log.count('VAM_NATIVE_RUNTIME_LOADED Body='+folder+'/SK_Body.SK_Body Parts=13 Morphs=6 Missing=')==2
        assert 'Fatal error:' not in log
    assert 'CPUVertexProbe=1' in logs['stage05-kernel-standalone.log']
    assert 'CPUVertexProbe=0' in logs['stage05-kernel-packaged-runtime.log']
    assert 'Ran 22 tests' in logs['stage05-kernel-regression.log'] and '\nOK' in logs['stage05-kernel-regression.log']
    exe=project/'Saved/Stage05KernelPackaged/Windows/Stage05Clean.exe';assert exe.is_file()
    return {'status':'passed','stage05_complete':True,'scope':'latest Stage05 05.1–05.8; declared source-reference adapters',
        'folder':folder,'blueprint':build['blueprint'],'test_map':build['shape_validation_map'],
        'parts':build['parts'],'parameters':runtime['parameters'],'bones':len(contract['bones']),
        'shape_runtime':shape,'load_runtime':runtime,'animation_runtime':animation,'build_job':job,
        'reimport':reimport,'cancel':cancel,'distribution':audit,
        'skin_reference':contract['skin']['calibration'],'source_dynamic_equivalence_verified':False,
        'clothing_static_vs_skinned_max_cm':max(p['maximum_cm'] for p in pose['parts']),
        'package_executable':str(exe),'evidence_logs':list(logs),
        'declared_limits':['TriAx compound/real VaM runtime calibration pending; mathematical reference only',
            'BoneCenter executable; other source bone formula types retained and classified',
            'Nonlinear collar/skirt retain p0 and skin; no arbitrary-shape fit claim',
            'Reference materials partial; Groom/Cloth/Flesh outside Stage05']}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--project',type=Path,required=True);args=parser.parse_args()
    result=collect(args.project)
    output=args.project/'Plugins/VamResourceBrowser/Saved/NativeBuild/stage05-kernel-acceptance.json'
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(output)
