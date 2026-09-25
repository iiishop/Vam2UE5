"""Commandlet entry; editor UI remains responsive and owns only the job request."""
import json,os,re,runpy,sys,traceback
from pathlib import Path
import unreal as u
scripts=Path(__file__).resolve().parent;sys.path.insert(0,str(scripts))
from vam_native_job_state import progress,BuildCancelled
match=re.search(r'-VamJob=(?:"([^"]+)"|(\S+))',u.SystemLibrary.get_command_line())
assert match,'Missing pinned job request'
job=Path(match.group(1) or match.group(2));request=json.loads((job/'request.json').read_text(encoding='utf8'))
os.environ.update(VAM_BUILD_JOB=str(job),VAM_NATIVE_UNATTENDED='1')
try:
    import subprocess
    if request.get('upgrade_asset'):
        upgrade_asset=request['upgrade_asset']
    else:
        os.environ.update(VAM_NATIVE_TARGET_ROOT=request['target'],VAM_MORPH_SET_FILE=request['morph_set'],
                          VAM_NATIVE_PREVIEW_FILE=request['preview'])
        scope=runpy.run_path(str(scripts/'ue_native_build.py'))
        report=json.loads((job/'built.json').read_text(encoding='utf8'))
        # Publish only after a different process has reloaded the exact transaction.
        receipt=job/'receipt.json';receipt.write_text(json.dumps(report,ensure_ascii=False),encoding='utf8')
        progress('verifying','Independent reload and manifest publication',False)
        env=dict(os.environ,VAM_NATIVE_REPORT_FILE=str(receipt))
        result=subprocess.run([str(Path(u.Paths.engine_dir())/'Binaries/Win64/UnrealEditor-Cmd.exe'),u.Paths.get_project_file_path(),
            '-run=pythonscript','-script='+str(scripts/'ue_native_reload.py'),'-NullRHI','-unattended','-abslog='+str(job/'reload.log')],
            env=env,capture_output=True,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode==0,'Independent reload failed; see reload.log'
        upgrade_asset=report['definition']
    progress('runtime', 'Native assets saved; preparing Runtime upgrade', False)
    result=subprocess.run([str(Path(u.Paths.engine_dir())/'Binaries/Win64/UnrealEditor-Cmd.exe'),u.Paths.get_project_file_path(),
        '-run=pythonscript','-script='+str(scripts/'ue_runtime_upgrade.py'),'-NullRHI','-unattended','-nosplash',
        '-abslog='+str(job/'upgrade.log')],env=dict(os.environ,VAM_UPGRADE_ASSET=upgrade_asset),
        stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,timeout=4200,creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode==0,'Native assets retained, Runtime upgrade failed; see '+str(job/'upgrade.log')

except BuildCancelled as exc:progress('cancelled',str(exc),False)
except Exception:
    progress('failed',traceback.format_exc(),False)
    raise
