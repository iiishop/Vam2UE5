import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Scripts'))
from vam_job_progress import publish


class JobProgressTests(unittest.TestCase):
    def test_atomic_status_is_optional_and_counts_are_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'progress.json'
            with patch.dict(os.environ, {'VAM_STAGE_PROGRESS': str(target)}):
                publish('decode', 'mesh', 2, 5)
                self.assertEqual(json.loads(target.read_text(encoding='utf8')),
                                 {'phase': 'decode', 'detail': 'mesh', 'done': 2, 'total': 5})
                self.assertFalse(target.with_suffix('.tmp').exists())
                publish('verify', 'source')
                self.assertNotIn('total', json.loads(target.read_text(encoding='utf8')))
            with patch.dict(os.environ, {}, clear=True):
                publish('disabled')
            self.assertEqual(json.loads(target.read_text(encoding='utf8'))['phase'], 'verify')


if __name__ == '__main__':
    unittest.main()
