"""Run the opt-in UE pose/capture suite against a real generated Blueprint."""
import argparse,json,subprocess,datetime,sys,shutil,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--project',type=Path,required=True);p.add_argument('--editor',default='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe');p.add_argument('--config',default='/Game/VamRuntime/LowerContact_20261009_v5/RC_Runtime');p.add_argument('--blueprint',default='/Game/VamRuntime/LowerContact_20261009_v5/BP_VamCharacter.BP_VamCharacter_C');p.add_argument('--case',default='');p.add_argument('--output',type=Path);a=p.parse_args()
r=Path(__file__).resolve().parents[1];out=a.output or r/'Saved/LowerBodyContact'/('A2-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out=out.resolve();out.mkdir(parents=True,exist_ok=False)
recipe=out/'pose-recipe.json';shutil.copyfile(r/'Config/LowerBodyPoseSuite.json',recipe)
provenance={'recipe_sha256':hashlib.sha256(recipe.read_bytes()).hexdigest(),'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'working_tree':subprocess.check_output(['git','status','--short'],cwd=r,text=True),'script_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__).resolve(),r/'Source/VamResourceBrowser/Private/Tests/VamPoseSuite.h']}}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
cmd=[a.editor,str(a.project),'-VamBreastTestConfig='+a.config,'-VamAuditBP='+a.blueprint,'-VamPoseSuite='+str(recipe),'-VamContactEvidence='+str(out),'-ExecCmds=Automation RunTests Vam.Breast.ContactRuntime','-TestExit=Automation Test Queue Empty','-unattended','-nosplash','-RenderOffscreen','-dx12','-ini:Engine:[/Script/Engine.RendererSettings]:r.SkinCache.CompileShaders=True','-ini:Engine:[SystemSettings]:r.SkinCache.Mode=1','-abslog='+str(out/'runtime.log')]
if a.case:cmd+=['-VamPoseCase='+a.case]
(out/'command.json').write_text(json.dumps(cmd,indent=2));print(out,flush=True)
with (out/'console.log').open('w') as f:res=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=1800,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
if res.returncode:raise SystemExit(res.returncode)
if not (out/'suite.json').exists():raise SystemExit('UE did not produce suite.json')
subprocess.run([sys.executable,str(r/'Scripts/report_lower_pose_suite.py'),str(out)],check=True)
