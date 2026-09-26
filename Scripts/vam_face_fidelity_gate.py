"""Receipt checks shared with the existing MH lifecycle; no new cloud service."""
import json
from pathlib import Path
from vam_metahuman import fingerprint


def require_receipt(recipe, key):
    ref = recipe.get(key)
    if not ref: raise ValueError('MissingFidelityReceipt:'+key)
    path = Path(ref['path'])
    if fingerprint(path) != ref['sha256']: raise ValueError('FidelityReceiptChanged:'+key)
    result = json.loads(path.read_text(encoding='utf8'))
    if not result.get('eligible_for_rig') or result.get('state') != 'OfficialTemplateGeometryVerified':
        raise ValueError('FidelityGeometryNotVerified')
    if result['character'].split('.')[0] != recipe['assets']['character']:
        raise ValueError('FidelityCharacterMismatch')
    if key == 'fidelity_post_rig' and result['character_sha256'] != recipe['owned']['character']:
        raise ValueError('FidelityPostRigCharacterChanged')
    return result
