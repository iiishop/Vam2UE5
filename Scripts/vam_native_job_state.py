"""Cooperative cancellation and stage progress for an isolated native build job."""
import json,os
from contextlib import contextmanager
from pathlib import Path

class BuildCancelled(RuntimeError):pass

@contextmanager
def exclusive_build(data):
    """OS-owned lock survives a crashed worker without a stale ownership file."""
    import msvcrt
    path=Path(data)/'NativeBuild/build.lock';path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('a+b') as stream:
        if stream.tell()==0:stream.write(b'0');stream.flush()
        stream.seek(0)
        try:msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
        except OSError as exc:raise RuntimeError('Another native build is active; wait or cancel its progress window') from exc
        try:yield
        finally:
            stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)

def check_cancel():
    job=os.environ.get('VAM_BUILD_JOB')
    if job and (Path(job)/'cancel').exists():raise BuildCancelled('Cancelled before publication; ImportState unchanged')

def progress(phase,detail='',cancellable=True):
    job=os.environ.get('VAM_BUILD_JOB')
    if not job:return
    path=Path(job)/'status.json';temp=path.with_suffix('.tmp')
    temp.write_text(json.dumps({'phase':phase,'detail':detail,'cancellable':cancellable},ensure_ascii=False),encoding='utf8');temp.replace(path)
