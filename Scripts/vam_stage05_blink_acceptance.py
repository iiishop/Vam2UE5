"""Collect focused Stage05 eyelid evidence without claiming a full 05.1–05.8 rerun."""
import hashlib
import json
import math
from pathlib import Path

project=Path(__file__).resolve().parents[3]
saved=project/'Plugins/VamResourceBrowser/Saved/NativeBuild'
build=json.loads((saved/'latest-native-assets.json').read_text(encoding='utf8'))
check=json.loads((saved/'stage05-blink-check.json').read_text(encoding='utf8'))
contract=json.loads((saved/(build['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
log=(project/'Saved/stage05-blink-stage06-packaged-runtime.log').read_text(encoding='utf8',errors='replace')
marker=next(line.split('VAM_STAGE06_ACCEPTANCE ',1)[1] for line in log.splitlines() if 'VAM_STAGE06_ACCEPTANCE ' in line)
cooked=json.loads(marker)
assert build['independent_reload_verified'] and build['manifest_updated'] and not build['missing_parts']
assert check['status']=='passed' and check['folder']==build['folder'] and check['instances_isolated']
assert cooked['status']=='passed' and min(cooked['max_blink_left'],cooked['max_blink_right'])>.9
assert log.count('VAM_NATIVE_RUNTIME_LOADED Body='+build['folder']+'/SK_Body.SK_Body Parts=13 Morphs=8')==2
morphs={m['display_name']:m for m in contract['morphs'] if m.get('display_name') in ('Eyes Closed Left','Eyes Closed Right')}
assert set(morphs)=={'Eyes Closed Left','Eyes Closed Right'}
measure={}
for label,m in morphs.items():
    magnitudes=[math.sqrt(sum(x*x for x in delta)) for delta in m['deltas']]
    count=sum(value>1e-5 for value in magnitudes)
    upper_descent_cm=max((-delta[2] for delta in m['deltas']),default=0.0)
    lower_ascent_cm=max((delta[2] for delta in m['deltas']),default=0.0)
    assert m['default']==0 and m['maximum']>=1 and count>=500 and max(magnitudes)>.5
    assert upper_descent_cm>lower_ascent_cm>0.0,(label,upper_descent_cm,lower_ascent_cm)
    measure[label]={'target':m['name'],'nonzero_render_vertices':count,'max_displacement_cm':max(magnitudes),
                    'max_upper_descent_cm':upper_descent_cm,'max_lower_ascent_cm':lower_ascent_cm,
                    'upper_to_lower_vertical_ratio':upper_descent_cm/lower_ascent_cm}
assert {v['target'] for v in measure.values()}==set(check['targets'])
for asset,expected in build['asset_file_sha256'].items():
    path=project/'Content'/(asset.removeprefix('/Game/')+'.uasset')
    with path.open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,str(path)
result={'status':'passed','scope':'Stage05 bilateral eyelid supplement and Stage06 cooked drive',
        'folder':build['folder'],'independent_reload_verified':True,'assets_fingerprint_verified':len(build['asset_file_sha256']),
        'morphs':measure,'editor_instance_check':check,'cooked_runtime':cooked,
        'full_stage05_matrix_repeated':False,'gpu_eye_close_visual_review':'pending'}
out=saved/'stage05-blink-acceptance.json'
out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(out)
