"""Add animation adapters to saved characters without touching Stage05 assets."""
import json
import sys
from pathlib import Path

import unreal as u

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ue_native_retarget import build

data = Path(__file__).resolve().parents[1] / 'Saved'
native = data / 'NativeBuild'
index = json.loads((native / 'native-destinations.json').read_text(encoding='utf8'))
entries, failures = [], []
for destination in index.values():
    folder, identity = destination['folder'], destination['commit_id']
    marker = native / (identity + '.commit.json')
    if not marker.exists():
        continue
    try:
        original = json.loads(marker.read_text(encoding='utf8'))
        body = u.load_asset(folder + '/SK_Body')
        assert body, 'Missing body'
        contract = json.loads((native / (original['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
        assets, adapter = build(folder, body, contract['bones'])
        for asset in assets:
            assert u.EditorAssetLibrary.save_loaded_asset(asset, False), 'Save failed: '+asset.get_path_name()
        entries.append({'folder': folder, 'source_digest': original['source_digest'],
                        'original_stage05_verified': bool(original.get('independent_reload_verified')),
                        'assets': [asset.get_path_name() for asset in assets], 'adapter': adapter})
    except Exception as error:
        failures.append({'folder': folder, 'error': str(error)})
pending = native / 'retarget-adapters-pending.json'
pending.write_text(json.dumps({'schema': 1, 'entries': entries, 'failures': failures},
                              ensure_ascii=False, indent=2), encoding='utf8')
u.log(f'VAM_RETARGET_RETROFIT built={len(entries)} failed={len(failures)} pending={pending}')
if failures:
    raise RuntimeError('Retarget adapters failed: '+json.dumps(failures,ensure_ascii=False))
