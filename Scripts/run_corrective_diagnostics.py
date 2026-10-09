"""Export B2 corrective attribution, capture actual poses, and generate the report.
Runs in the supplied UE project; does not modify the source configuration/mesh.
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--project',type=Path,required=True);p.add_argument('--configuration',required=True);p.add_argument('--blueprint',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--editor',default='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe');a=p.parse_args()
r=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
env=os.environ.copy();env.update(VAM_B2_CONFIG=a.configuration,VAM_B2_OUTPUT=str(out))
cmd=[a.editor,str(a.project.resolve()),'-run=pythonscript','-script='+str(r/'Scripts/ue_corrective_diagnostics.py'),'-NullRHI','-unattended','-nosplash','-abslog='+str(out/'export.log')]
(out/'command.json').write_text(json.dumps(cmd,indent=2));(out/'source-state.txt').write_text(subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True)+subprocess.check_output(['git','status','--short'],cwd=r,text=True))
with (out/'export-console.log').open('w') as f:res=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
if res.returncode:raise SystemExit(res.returncode)
status=json.loads((out/'export-status.json').read_text())
if status['error']:raise SystemExit(status['error'])
subprocess.run([sys.executable,str(r/'Scripts/run_lower_pose_suite.py'),'--project',str(a.project),'--editor',a.editor,'--config',a.configuration,'--blueprint',a.blueprint,'--output',str(out/'poses')],check=True)
subprocess.run([sys.executable,str(r/'Scripts/report_corrective_diagnostics.py'),str(out)],check=True)
print(out/'compare.html')
