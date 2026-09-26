import json
import tempfile
import unittest
from pathlib import Path
from vam_metahuman_consent import scope, standing_consent, SCOPE_KEYS


class ConsentTests(unittest.TestCase):
    def test_existing_grant_only_covers_identical_scope(self):
        with tempfile.TemporaryDirectory() as root:
            disclosure = {key: [key] for key in SCOPE_KEYS}
            self.assertFalse(standing_consent(root, disclosure))
            path = Path(root)/'MetaHuman/cloud-consent.json'
            path.parent.mkdir()
            grant = {'granted': True, 'user_instruction': 'Explicit user grant', 'scope': scope(disclosure)}
            path.write_text(json.dumps(grant), encoding='utf8')
            self.assertTrue(standing_consent(root, dict(disclosure, character='another character')))
            for key in SCOPE_KEYS:
                self.assertFalse(standing_consent(root, dict(disclosure, **{key: ['expanded']})))
            grant['granted'] = False
            path.write_text(json.dumps(grant), encoding='utf8')
            self.assertFalse(standing_consent(root, disclosure))


if __name__ == '__main__':
    unittest.main()
