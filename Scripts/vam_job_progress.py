"""Optional, atomic worker progress. Never part of source or asset identity."""
import json
import os
from pathlib import Path


def publish(phase, detail='', done=None, total=None):
    name = os.environ.get('VAM_STAGE_PROGRESS')
    if not name:
        return
    path = Path(name)
    state = {'phase': str(phase), 'detail': str(detail)}
    if done is not None:
        state['done'] = int(done)
    if total is not None:
        state['total'] = int(total)
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_text(json.dumps(state, ensure_ascii=False), encoding='utf8')
        temporary.replace(path)
    except OSError:
        pass  # A display-only sidecar must never fail source decoding or import.
