"""Content-verified reuse of expensive Stage05 calibration.

The receipt is an acceleration hint, never a substitute for the source lock.
Custom catalog Morph selections still take the original full calibration path.
"""
import hashlib
import json
from pathlib import Path

from vam_native_source import digest

SCRIPTS = Path(__file__).resolve().parent
CODE = ('vam_native_cache.py', 'vam_native_calibrate.py', 'vam_native_source.py',
        'vam_triax_lbs.py', 'vam_fit.py', 'vam_editable_morphs.py', 'vam_unity.py',
        'vam_decode.py', 'vam_ue_mesh.py', 'vam_plan.py', 'vam_index.py',
        'vam_native_input.py', 'vam_zip_compat.py')
CONFIG = ('BuiltinCatalog.json', 'DecodeLayouts.json', 'Stage05Quality.json')


def _sha_file(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def _inputs(data, preview, selection_path):
    selection = json.loads(Path(selection_path).read_bytes())
    if selection.get('catalog_resources'):
        return None  # Their dependency resolver must revalidate the current catalog.
    ir_path = Path(data) / 'Decoded' / (preview['decode_id'] + '.ir.json')
    plan = json.loads((Path(data) / 'Plans' / (preview['plan_id'] + '.json')).read_bytes())
    character = next(i for i in plan['items']
                     if i.get('builtin_mapping', {}).get('entry', {}).get('role') == 'character')
    root = Path(plan['source_root']).resolve()
    files = dict(character['builtin_mapping']['files'])
    signature = digest({'decode_id': preview['decode_id'], 'ir_sha256': _sha_file(ir_path),
                        'plan_id': preview['plan_id'], 'root': str(root), 'selection': selection,
                        'code': {name: _sha_file(SCRIPTS / name) for name in CODE},
                        'config': {name: _sha_file(SCRIPTS.parent / 'Config' / name)
                                   for name in CONFIG}})
    return signature, root, files, selection, ir_path


def _locked_files(root, files):
    verified = {}
    for record in files.values():
        relative, expected = record['path'], record['sha256']
        path = (root / relative).resolve()
        if not path.is_relative_to(root) or not path.is_file() or _sha_file(path) != expected:
            return None
        verified[relative] = expected
    return verified


def write_receipt(data, preview, selection_path, contract):
    values = _inputs(data, preview, selection_path)
    if values is None:
        return
    signature, root, files, selection, ir_path = values
    if contract['editable_morph_lock']['selection'] != selection:
        raise ValueError('Calibration receipt: Morph selection changed')
    for item in contract['editable_morph_lock']['items']:
        if item.get('source') != 'builtin':
            raise ValueError('Calibration receipt: non-builtin Morph cannot be cached')
        files.update(item['builtin_mapping']['files'])
    if _locked_files(root, files) is None:
        raise ValueError('Calibration receipt: locked source changed during calibration')
    target = Path(data) / 'NativeBuild' / (preview['decode_id'] + '.calibrated.json')
    receipt = {'schema': 1, 'signature': signature, 'source_files': files,
               'contract_sha256': _sha_file(target), 'contract_id': contract['contract_id']}
    path = target.with_suffix('.receipt.json')
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(receipt, ensure_ascii=False, separators=(',', ':')), encoding='utf8')
    temporary.replace(path)


def load_if_verified(data, preview, selection_path):
    try:
        values = _inputs(data, preview, selection_path)
        if values is None:
            return None
        signature, root, _, selection, ir_path = values
        target = Path(data) / 'NativeBuild' / (preview['decode_id'] + '.calibrated.json')
        receipt = json.loads(target.with_suffix('.receipt.json').read_bytes())
        if receipt['schema'] != 1 or receipt['signature'] != signature:
            return None
        if _locked_files(root, receipt['source_files']) is None:
            return None
        if _sha_file(target) != receipt['contract_sha256']:
            return None
        contract = json.loads(target.read_bytes())
        if (contract['contract_id'] != receipt['contract_id'] or
                digest({k: v for k, v in contract.items() if k != 'contract_id'}) != contract['contract_id'] or
                contract['source_ir'] != {'path': str(ir_path.resolve()), 'sha256': _sha_file(ir_path)} or
                contract['editable_morph_lock']['selection'] != selection or
                contract['plan_id'] != preview['plan_id'] or contract['decode_id'] != preview['decode_id'] or
                not contract['native_reference_ready']):
            return None
        return contract
    except (OSError, KeyError, ValueError, TypeError, StopIteration, json.JSONDecodeError):
        return None
