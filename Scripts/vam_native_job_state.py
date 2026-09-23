"""Cooperative cancellation and stage progress for an isolated native build job."""
import json,os,time
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

def progress(phase,detail='',cancellable=True,done=None,total=None):
    job=os.environ.get('VAM_BUILD_JOB')
    if not job:return
    path=Path(job)/'status.json';temp=path.with_name('status.'+str(os.getpid())+'.tmp')
    state={'phase':phase,'detail':detail,'cancellable':cancellable}
    if done is not None:state['done']=int(done)
    if total is not None:state['total']=int(total)
    temp.write_text(json.dumps(state,ensure_ascii=False),encoding='utf8')
    # On Windows the editor's status reader can briefly hold the destination
    # without FILE_SHARE_DELETE, making an otherwise valid replace fail.
    deadline=time.monotonic()+3.0
    try:
        while True:
            try:
                temp.replace(path)
                break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.025)
    finally:
        temp.unlink(missing_ok=True)
