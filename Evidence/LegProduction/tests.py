import json,os,subprocess,sys,re
from pathlib import Path
r=Path(__file__).resolve().parent;repo=r.parents[3]
b=json.loads((r/'bundle.json').read_text());assert b['status']=='committed' and b['independent_reload_verified']
editor='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe';project='I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject'
tests=['Vam.Glute.ResidualOnlySafety','Vam.Glute.FinalCorrective','Vam.Glute.G1.CharacterCalibration','Vam.Glute.PoseFunction','Vam.Glute.PoseRefinement','Vam.Glute.RotationBlendReference','Vam.Glute.G1.Mechanics','Vam.Glute.G1.FrameRateStability','Vam.Glute.G1.AttachmentDamping','Vam.Breast.Dynamics','Vam.Material.AffineStreaming','Vam.Leg.PreserveSupportSlots','Vam.Breast.WeightCompression','Vam.Glute.FinalInfluenceBudget','Vam.Glute.MorphNormalOrientation']
cmd=[editor,project,'-VamFinalCorrectiveConfig='+b['configuration'],'-VamGluteTestConfig='+b['configuration'],'-VamFinalCorrectiveReport='+str(r/'authored-targets.json'),'-ExecCmds=Automation RunTests '+'+'.join(tests),'-TestExit=Automation Test Queue Empty','-NullRHI','-unattended','-nosplash','-abslog='+str(r/'tests.log')]
(r/'test-command.json').write_text(json.dumps(cmd,indent=2))
with (r/'tests-stdout.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
log=(r/'tests.log').read_text(errors='replace');success=re.findall(r'Test Completed. Result=\{Success\}.*Path=\{([^}]+)\}',log);assert set(tests)<=set(success),success
(r/'tests.json').write_text(json.dumps(success,indent=2))
