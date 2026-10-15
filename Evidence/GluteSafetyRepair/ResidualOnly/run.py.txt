import os,json,subprocess
from pathlib import Path
r=Path(__file__).resolve().parent;plugin=r.parents[3];env=os.environ.copy();env['VAM_RUNTIME_RECIPE']=str(r/'recipe.json');env['VAM_RUNTIME_REPORT']=str(r/'bundle.json')
editor='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe';project='I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject'
script='I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Scripts/ue_runtime_build.py'
for phase in ['build','reload','verify']:
 env['VAM_RUNTIME_PHASE']=phase;cmd=[editor,project,'-ExecutePythonScript='+script,'-VamGluteSafetyTrace=7837,7833,7845,8744,8760,8249,18920,18922,18858,17882,19400,18538,18540','-NullRHI','-unattended','-nosplash','-abslog='+str(r/(phase+'.log'))];(r/(phase+'-command.json')).write_text(json.dumps(cmd,indent=2))
 with (r/(phase+'-stdout.log')).open('w') as f:rc=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 if rc or (r/'bundle.json.error.json').exists():raise SystemExit('Failed '+phase)
 assert (r/'bundle.json').is_file()
print(json.loads((r/'bundle.json').read_text())['status'])
