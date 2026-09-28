import unittest
from vam_glute_corrective_source import recover

class CorrectiveSourceTests(unittest.TestCase):
    def test_unsupported(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported family'):
            recover({}, 'missing', {})

    def test_missing_library(self):
        self.assertEqual(recover({}, 'missing', {'glute_corrective': {}})['mode'], 'procedural')

    def test_retained_chain_offline(self):
        source = {'version': 1, 'target': 'test', 'source_hashes': {'a': '123'}, 'deltas': [[0, 1, 2, 3]]}
        family = {'glute_corrective': {'source_target': 'test'}, 'glute_corrective_source': source}
        self.assertEqual(recover({'source_hashes': {'a': '123'}}, 'missing', family), source)

    def test_retained_digest_rejected(self):
        source = {'version': 1, 'target': 'test', 'source_hashes': {'a': '123'}, 'deltas': [[0, 1, 2, 3]]}
        family = {'glute_corrective': {'source_target': 'test'}, 'glute_corrective_source': source}
        self.assertEqual(recover({'source_hashes': {'a': '456'}}, 'missing', family)['mode'], 'procedural')
