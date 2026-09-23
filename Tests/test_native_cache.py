import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Scripts'))

from vam_native_cache import load_if_verified, write_receipt
from vam_native_source import digest


class NativeCacheTests(unittest.TestCase):
    def test_verified_reuse_and_source_contract_selection_invalidation(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            for name in ('Decoded', 'Plans', 'NativeBuild', 'source'):
                (base / name).mkdir()
            decode_id, plan_id = 'a' * 64, 'b' * 64
            preview = {'decode_id': decode_id, 'plan_id': plan_id}
            source = base / 'source' / 'mesh.bin'
            source.write_bytes(b'original source')
            source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
            ir = base / 'Decoded' / (decode_id + '.ir.json')
            ir.write_text('{"source":"locked"}', encoding='utf8')
            plan = {'source_root': str(base / 'source'), 'items': [
                {'builtin_mapping': {'entry': {'role': 'character'}, 'files': {
                    'mesh': {'path': 'mesh.bin', 'sha256': source_sha}}}}]}
            (base / 'Plans' / (plan_id + '.json')).write_text(json.dumps(plan), encoding='utf8')
            selection = base / 'selection.json'
            selection.write_text('{"groups":[]}', encoding='utf8')
            contract = {'decode_id': decode_id, 'plan_id': plan_id,
                        'source_ir': {'path': str(ir.resolve()),
                                      'sha256': hashlib.sha256(ir.read_bytes()).hexdigest()},
                        'editable_morph_lock': {'selection': json.loads(selection.read_bytes()), 'items': []},
                        'native_reference_ready': True}
            contract['contract_id'] = digest(contract)
            target = base / 'NativeBuild' / (decode_id + '.calibrated.json')
            target.write_text(json.dumps(contract), encoding='utf8')
            write_receipt(base, preview, selection, contract)
            self.assertEqual(load_if_verified(base, preview, selection), contract)

            source.write_bytes(b'changed source')
            self.assertIsNone(load_if_verified(base, preview, selection))
            source.write_bytes(b'original source')
            target.write_text(target.read_text(encoding='utf8') + ' ', encoding='utf8')
            self.assertIsNone(load_if_verified(base, preview, selection))
            target.write_text(json.dumps(contract), encoding='utf8')
            selection.write_text('{"groups":["other"]}', encoding='utf8')
            self.assertIsNone(load_if_verified(base, preview, selection))


if __name__ == '__main__':
    unittest.main()
