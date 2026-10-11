"""Run explicit normal-BP S1/S2 regression; keeps Contact acceptance separate."""
import argparse,json,subprocess,sys,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('recipe',type=Path);p.add_argument('out',type=Path);p.add_argument('--project',default='I:/Document/UE5/SmartNPC/SmartNPC.uproject');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);b=json.loads(a.bundle.read_text());assert b['status']=='committed';config=b['configuration'];editor='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
tests=['Vam.Glute.FinalCorrective','Vam.Glute.G1.CharacterCalibration','Vam.Glute.PoseFunction','Vam.Glute.PoseRefinement','Vam.Glute.RotationBlendReference','Vam.Glute.G1.Mechanics','Vam.Glute.G1.FrameRateStability','Vam.Glute.G1.AttachmentDamping','Vam.Breast.Dynamics','Vam.Material.AffineStreaming','Vam.Leg.PreserveSupportSlots','Vam.Breast.WeightCompression','Vam.Glute.FinalInfluenceBudget','Vam.Glute.MorphNormalOrientation']
cmd=[editor,a.project,'-VamFinalCorrectiveConfig='+config,'-VamGluteTestConfig='+config,'-VamFinalCorrectiveReport='+str((a.out/'authored-targets.json').resolve()),'-ExecCmds=Automation RunTests '+'+'.join(tests),'-TestExit=Automation Test Queue Empty','-NullRHI','-unattended','-nosplash','-abslog='+str((a.out/'tests.log').resolve())]
(a.out/'test-command.json').write_text(json.dumps(cmd,indent=2))
with (a.out/'tests-stdout.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
log=(a.out/'tests.log').read_text(errors='replace');success=re.findall(r'Test Completed\. Result=\{Success\}.*Path=\{([^}]+)\}',log);assert set(tests)<=set(success),success
for world in ['Editor','PIE']:
 subprocess.run([sys.executable,'-X','utf8','Scripts/run_s1s2_pose_suite.py',str(a.bundle),str(a.out/world.lower()),'--recipe',str(a.recipe),'--world',world,'--project',a.project],check=True)
subprocess.run([sys.executable,'-X','utf8','Scripts/run_s1s2_pose_suite.py',str(a.bundle),str(a.out/'lighting'),'--recipe',str(a.recipe),'--case','knee-120','--world','Editor','--project',a.project,'--light-yaw','65'],check=True)
(a.out/'process.json').write_text(json.dumps(dict(status='complete',identity=b['identity'],tests=success,contact='excluded: separately preserved historical rest-volume failure'),indent=2))
