"""Independent process gate for additive adapters; Stage05 hashes stay intact."""
import hashlib
import json
import sys
from pathlib import Path

import unreal as u

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ue_native_retarget import build

native = Path(__file__).resolve().parents[1] / 'Saved' / 'NativeBuild'
pending = json.loads((native / 'retarget-adapters-pending.json').read_text(encoding='utf8'))
assert pending['schema'] == 1 and not pending['failures']
verified = {'schema': 1, 'entries': {}, 'failures': []}
for entry in pending['entries']:
    folder = entry['folder']
    for path in entry['assets']:
        assert u.load_asset(path), 'Adapter asset did not reload: '+path
    body = u.load_asset(folder+'/SK_Body')
    assert body
    contract = json.loads((native / (entry['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
    assets, adapter = build(folder, body, contract['bones'])
    assert adapter == entry['adapter'] and {a.get_path_name() for a in assets} == set(entry['assets']), \
        'Retarget mapping or sampled pose changed after independent reload: '+folder
    fingerprints = {}
    for path in entry['assets']:
        package = path.split('.')[0]
        filename = Path(u.Paths.project_content_dir()) / (package[len('/Game/'):]+'.uasset')
        with filename.open('rb') as stream:
            fingerprints[package] = hashlib.file_digest(stream, 'sha256').hexdigest()
    verified['entries'][folder] = {'source_digest': entry['source_digest'],
                                   'original_stage05_verified': entry['original_stage05_verified'],
                                   'assets': entry['assets'], 'adapter': adapter,
                                   'asset_file_sha256': fingerprints}
manifest = native / 'retarget-adapters.json'
temporary = manifest.with_suffix('.tmp')
temporary.write_text(json.dumps(verified, ensure_ascii=False, indent=2), encoding='utf8')
temporary.replace(manifest)
u.log('VAM_RETARGET_ADAPTERS_VERIFIED '+str(len(verified['entries'])))
