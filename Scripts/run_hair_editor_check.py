"""Run the real visible Editor verification and preserve its actual completion."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('out',type=Path);p.add_argument('--project',default='I:/Document/UE5/SmartNPC/SmartNPC.uproject');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1];env=os.environ.copy();env['VAM_HAIR_REPORT']=str(a.bundle.resolve());env['VAM_HAIR_EDITOR_OUTPUT']=str((a.out/'check.json').resolve())
cmd=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',a.project,'-unattended','-nosplash','-dx12','-ExecCmds=py '+str(root/'Scripts/ue_hair_editor_check.py'),'-abslog='+str((a.out/'runtime.log').resolve())]
(a.out/'command.json').write_text(json.dumps(cmd,indent=2))
with (a.out/'stdout.log').open('w') as log:r=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
(a.out/'process.json').write_text(json.dumps(dict(exit_code=r.returncode)))
assert r.returncode==0
result=json.loads((a.out/'check.json').read_text());assert result['status']=='passed',result
print(json.dumps(result,indent=2))
