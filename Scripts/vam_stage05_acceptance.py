"""Collect pinned, reproducible native-host evidence without changing ImportState."""
import argparse
import hashlib
import json
from pathlib import Path


def collect(report_path, project, package):
    build=json.loads(report_path.read_text(encoding='utf8'))
    evidence=report_path.parent
    runtime=json.loads((evidence/'stage05-final-runtime-check.json').read_text(encoding='utf8'))
    pose=json.loads((evidence/'stage05-final-clothing-pose-errors.json').read_text(encoding='utf8'))
    assert build['independent_reload_verified'] and runtime['status']=='passed'
    assert runtime['folder']==build['folder'] and pose['decode_id']==build['source_digest']
    assert runtime['components']==build['parts']+1 and len(pose['parts'])==build['parts']
    assert not build['missing_parts']
    for asset,digest in build['asset_file_sha256'].items():
        path=project/'Content'/(asset.removeprefix('/Game/')+'.uasset')
        with path.open('rb') as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
        assert actual==digest,f'Asset changed after independent reload: {asset}'
    package_log=(project/'Saved/stage05-final-package.log').read_text(encoding='utf8',errors='replace')
    assert 'BUILD SUCCESSFUL' in package_log
    executable=package/'Windows/SmartNPC.exe'
    assert executable.is_file()
    log_path=project/'Saved/stage05-final-packaged-runtime.log'
    log=log_path.read_text(encoding='utf8',errors='replace')
    expected=f"Body={build['folder']}/SK_Body.SK_Body Parts={build['parts']} Morphs={runtime['parameters']} Missing="
    assert 'VAM_NATIVE_RUNTIME_LOADED '+expected+'\n' in log.replace('\r\n','\n')
    assert 'Fatal error:' not in log and 'Engine exit requested' in log
    return {'status':'native_asset_host_verified_with_declared_limitations',
            'stage05_full_source_equivalence':False,'folder':build['folder'],
            'runtime':runtime,'clothing_pose_error_max_cm':max(p['maximum_cm'] for p in pose['parts']),
            'clothing_reference':pose['reference'],'independent_reload_verified':True,
            'saved_asset_fingerprints_verified':True,'windows_package_verified':True,
            'package_executable':str(executable),'runtime_log':str(log_path),
            'limitations':build['limitations'],
            'manual_visual_feedback':'User reports overall visual appearance is approximately correct; not source pose calibration.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--project',type=Path,required=True)
    parser.add_argument('--package',type=Path,required=True)
    args=parser.parse_args()
    result=collect(args.report,args.project,args.package)
    output=args.report.parent/'stage05-acceptance.json'
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(output)
