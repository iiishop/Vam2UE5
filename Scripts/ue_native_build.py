"""Separate native source-reference build; preserves unresolved source semantics."""
import json
import os
from pathlib import Path
import sys
import traceback
import unreal

SCRIPTS=Path(__file__).resolve().parent
if str(SCRIPTS) not in sys.path:sys.path.insert(0,str(SCRIPTS))
from vam_native_job_state import BuildCancelled,exclusive_build


def run():
    from vam_native_job_state import check_cancel,progress
    check_cancel();progress('preparing','Reading locked source and selected Morph set')
    data=SCRIPTS.parent/'Saved'
    from vam_native_input import load_preview
    preview=load_preview(data)
    decode_id=preview['decode_id']
    if len(decode_id)!=64 or any(c not in '0123456789abcdef' for c in decode_id):
        raise ValueError('Invalid immutable decode identifier')
    import subprocess
    import importlib
    calibrated=data/'NativeBuild'/(decode_id+'.calibrated.json')
    # Supplemental selection is explicitly locked on every build; never reuse a different set.
    stale_calibration=True
    if stale_calibration:
        python=Path(unreal.Paths.engine_dir())/'Binaries/ThirdParty/Python3/Win64/python.exe'
        completed=subprocess.run([str(python),'-I',str(SCRIPTS/'vam_native_calibrate.py'),'--data',str(data)],
            capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900,
            creationflags=subprocess.CREATE_NO_WINDOW)
        check_cancel()
        if completed.returncode:raise RuntimeError(completed.stdout+'\n'+completed.stderr)
    progress('parts','Transferring SkinWrap influences and Morph correspondence')
    import ue_native_character
    parts_path=data/'NativeBuild'/(decode_id+'.parts.json')
    contract=json.loads(calibrated.read_text(encoding='utf8'))
    part_cache=json.loads(parts_path.read_text(encoding='utf8')) if parts_path.exists() else {}
    stale_parts=part_cache.get('contract_id')!=contract['contract_id'] or part_cache.get('adapter_version')!=3
    if stale_parts:
        python=Path(unreal.Paths.engine_dir())/'Binaries/ThirdParty/Python3/Win64/python.exe'
        completed=subprocess.run([str(python),'-I',str(SCRIPTS/'vam_native_parts.py'),'--data',str(data)],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900,creationflags=subprocess.CREATE_NO_WINDOW)
        check_cancel()
        if completed.returncode:raise RuntimeError(completed.stdout+'\n'+completed.stderr)
    progress('assets','Building native assets')
    importlib.reload(ue_native_character)
    report=ue_native_character.build(data)
    if os.environ.get('VAM_BUILD_JOB'):
        (Path(os.environ['VAM_BUILD_JOB'])/'built.json').write_text(json.dumps(report,ensure_ascii=False),encoding='utf8')
    if not report.get('independent_reload_verified') and not os.environ.get('VAM_NATIVE_UNATTENDED'):
        # The independent verifier must reload THIS transaction, not a moving latest pointer.
        receipt=data/'NativeBuild'/('reload-'+report['folder'].rsplit('/',1)[-1]+'.json')
        receipt.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        editor=Path(unreal.Paths.engine_dir())/'Binaries/Win64/UnrealEditor-Cmd.exe'
        project=unreal.Paths.get_project_file_path()
        completed=subprocess.run([str(editor),project,'-run=pythonscript','-script='+str(SCRIPTS/'ue_native_reload.py'),
            '-unattended','-nosplash','-NullRHI','-abslog='+str(data/'NativeBuild/reload.log')],
            capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600,creationflags=subprocess.CREATE_NO_WINDOW,
            env=dict(os.environ,VAM_NATIVE_REPORT_FILE=str(receipt.resolve())))
        if completed.returncode:raise RuntimeError('Assets saved, but independent reload failed; manifest was not published. See NativeBuild/reload.log')
    if not os.environ.get('VAM_NATIVE_UNATTENDED'):
        unreal.EditorDialog.show_message('原生人物资产已保存',
            report['folder']+'\n可打开 SK_Body 或拖入 BP_VamCharacter。\n可编辑 Morph 与 BoneCenter 由 Character 组件控制。来源参考着色、TriAx 校准及部件限制见 CD_Character。',unreal.AppMsgType.OK)
    return report


_previous_preview=os.environ.get('VAM_NATIVE_PREVIEW_FILE')
try:
    with exclusive_build(SCRIPTS.parent/'Saved'):run()
except BuildCancelled:
    raise
except Exception:
    message=traceback.format_exc()
    unreal.log_error(message)
    if not os.environ.get('VAM_NATIVE_UNATTENDED'):
        unreal.EditorDialog.show_message('生成 UE 人物资产失败',message,unreal.AppMsgType.OK)
    else:
        raise
finally:
    if _previous_preview is None:
        os.environ.pop('VAM_NATIVE_PREVIEW_FILE',None)
    else:
        os.environ['VAM_NATIVE_PREVIEW_FILE']=_previous_preview
