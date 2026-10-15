"""Run existing UE pose-suite harness against a published runtime, preserving commands and logs."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('out',type=Path);p.add_argument('--world',choices=['Editor','PIE'],default='Editor');p.add_argument('--recipe',type=Path,default=Path('Config/LowerBodyPoseSuite.json'));p.add_argument('--case');p.add_argument('--project',default='I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject');p.add_argument('--light-yaw',type=float,default=0);p.add_argument('--light-from-below',action='store_true');p.add_argument('--inline-skinning',action='store_true',help='Diagnostic negative control: disable Skin Cache compilation');p.add_argument('--streaming-probe');p.add_argument('--hair-view',action='store_true');p.add_argument('--preview',action='store_true');p.add_argument('--hair-rest',action='store_true');a=p.parse_args();bundle=json.loads(a.bundle.read_text());assert bundle['status']=='committed' and bundle['independent_reload_verified'];a.out.mkdir(parents=True,exist_ok=False);recipe=a.out/'pose-recipe.json';recipe.write_bytes(a.recipe.read_bytes());cmd=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',a.project,'-VamPoseSuite='+str(recipe.resolve()),'-VamSeamAudit','-VamPoseStreaming','-VamPoseWorld='+a.world,'-ExecCmds=r.SkinCache.CompileShaders,r.SkinCache.Mode,r.SkinCache.RecomputeTangents,Automation RunTests Vam.Breast.ContactRuntime','-TestExit=Automation Test Queue Empty','-unattended','-nosplash','-RenderOffscreen','-dx12','-VamBreastTestConfig='+bundle['configuration'],'-VamAuditBP='+bundle['blueprint']+'_C','-VamContactEvidence='+str(a.out.resolve()),'-abslog='+str((a.out/'runtime.log').resolve())];
# Match the production project's post-skin normal reconstruction prerequisite.
# Setting RecomputeTangents alone does nothing when these shaders were not compiled.
cmd.append('-ini:Engine:[/Script/Engine.RendererSettings]:r.SkinCache.CompileShaders='+('0' if a.inline_skinning else '1'))
cmd.append('-VamPoseLightYaw='+str(a.light_yaw))
if a.light_from_below:cmd.append('-VamPoseLightFromBelow')
if a.hair_view:cmd.append('-VamHairView')
if a.hair_rest:cmd.append('-VamHairRest')
if a.preview:cmd.append('-VamLocomotionPreview')
if a.case:cmd.append('-VamPoseCase='+a.case)
if a.streaming_probe:cmd.append('-VamStreamingProbe='+a.streaming_probe)
(a.out/'command.json').write_text(json.dumps(cmd,indent=2));(a.out/'asset-identity.json').write_text(json.dumps({k:bundle[k] for k in ['identity','configuration','blueprint']},indent=2))
with (a.out/'stdout.log').open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
(a.out/'process.json').write_text(json.dumps(dict(exit_code=r.returncode)));raise SystemExit(r.returncode)
