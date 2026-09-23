"""Trusted editor entry: consume one local request, never execute source content."""
import json
import os
from pathlib import Path
import runpy
import traceback

scripts=Path(__file__).resolve().parent
saved=scripts.parent/'Saved'
active=saved/'preview-active.json'
request=None
previous={key:os.environ.get(key)for key in ('VAM_PREVIEW_REQUEST','VAM_CURRENT_EDITOR','VAM_STAGE_PROGRESS')}
try:
    request=Path(json.loads(active.read_text(encoding='utf8'))['request']).resolve()
    if request.parent!=(saved/'Decoded').resolve():raise ValueError('Invalid preview request path')
    os.environ['VAM_PREVIEW_REQUEST']=str(request)
    os.environ['VAM_CURRENT_EDITOR']='1'
    os.environ['VAM_STAGE_PROGRESS']=str((saved/'Decoded/ue-progress.json').resolve())
    runpy.run_path(str(scripts/'ue_geometry_preview.py'),run_name='__main__')
except Exception:
    if request and request.parent==(saved/'Decoded').resolve():
        request.with_suffix('.ue-result.json').write_text(json.dumps({'status':'failed','error':traceback.format_exc()}),encoding='utf8')
finally:
    active.unlink(missing_ok=True)
    for key,value in previous.items():
        if value is None:os.environ.pop(key,None)
        else:os.environ[key]=value
