import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from vam_native_job_state import progress


class NativeJobProgressTests(unittest.TestCase):
    def test_status_replace_retries_when_reader_temporarily_blocks_it(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, VAM_BUILD_JOB=directory):
            original_replace = Path.replace
            attempts = []

            def intermittently_locked(source, target):
                attempts.append(1)
                if len(attempts) <= 2:
                    raise PermissionError(13, 'status file is in use')
                return original_replace(source, target)

            with patch.object(Path, 'replace', intermittently_locked):
                progress('calibrating', 'morph', done=4, total=10)

            self.assertEqual(len(attempts), 3)
            self.assertEqual(json.loads((Path(directory) / 'status.json').read_text(encoding='utf8')),
                             {'phase': 'calibrating', 'detail': 'morph', 'cancellable': True,
                              'done': 4, 'total': 10})
            self.assertEqual(list(Path(directory).glob('status.*.tmp')), [])


if __name__ == '__main__':
    unittest.main()
