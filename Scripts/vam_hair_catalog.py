"""Body-independent source hair assets and reference-only hairstyle compositions."""
import copy
import hashlib
import json
from pathlib import PurePosixPath


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                    ensure_ascii=False, allow_nan=False).encode()).hexdigest()


def catalog(ir):
    scalps = {r['name']: r for r in ir['records'] if r.get('kind') == 'builtin_scalp'}
    assets, entries = {}, []
    for record in ir['records']:
        data = record.get('data', {})
        hair = (data.get('dynamic') or {}).get('hair')
        if not hair:
            continue
        source = copy.deepcopy(data)
        source.pop('source_path', None)  # Machine location is provenance, not asset identity.
        scalp = None if hair['scalp'] == 'CustomScalp' else scalps.get(hair['scalp'])
        if hair['scalp'] != 'CustomScalp' and scalp is None:
            raise ValueError('Missing source scalp: ' + hair['scalp'])
        payload = {'schema': 'vam-hair-asset/1', 'source': source,
                   'scalp': None if scalp is None else {'name': scalp['name'],
                            'mesh': scalp['mesh'], 'wrap': scalp['wrap']}}
        identity = digest(payload)
        assets.setdefault(identity, dict(payload, identity=identity,
                          source_path=record['path'],
                          display_name=PurePosixPath(record['path'].replace('\\', '/')).stem))
        uid = data['vam'].get('uid', '') + 'Sim'
        overrides = {}
        for document in ir.get('plan_documents', {}).values():
            for storable in document.get('parameters', {}).get('storables', []):
                if storable.get('id') == uid:
                    overrides.update({k: copy.deepcopy(v) for k, v in storable.items() if k != 'id'})
        entries.append({'hair_identity': identity, 'enabled': True,
                        'appearance_overrides': overrides, 'source_path': record['path']})
    style = {'schema': 'vam-hairstyle/1', 'entries': entries}
    # A body/preset does not own these shared resources. Fitted Groom outputs are separate caches.
    style['identity'] = digest(style)
    return {'assets': list(assets.values()), 'hairstyle': style}
