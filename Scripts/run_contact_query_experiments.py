import subprocess,sys,re,json
from pathlib import Path
r=Path(__file__).resolve().parents[1]/'Saved/ContactPerformance/broadphase';r.mkdir(exist_ok=True)
for name in sys.argv[1:]:
 b,c={'baseline':(0,0),'bounds':(1,0),'cache':(0,1),'both':(1,1),'baseline2':(0,0),'bounds2':(1,0),'dirty':(0,0),'boundsdirty':(1,0),'final':(1,0),'ab':(0,0),'xpbd':(1,0),'legacy':(1,0)}[name]
 d=r/name;d.mkdir(exist_ok=True)
 args=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject','-unattended','-nosplash','-VamBreastTestConfig=/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime','-ExecCmds=vam.Contact.NativeXPBD '+str(int(name=='xpbd'))+',vam.Contact.DirtyConstraints '+str(int(name in ['dirty','boundsdirty','final']))+',vam.Contact.Broadphase '+str(b)+',vam.Contact.ExactQueryCache '+str(c)+',Automation RunTests Vam.Breast.ContactRuntime','-TestExit=Automation Test Queue Empty','-abslog='+str(d/'runtime.log'),'-VamContactGPU','-RenderOffscreen','-VamContactSnapshots='+str(d)]
 if name=='ab':args.append('-VamContactPerformanceAB')
 if name in ['final','legacy']:
  args=[('-ExecCmds=Automation RunTests Vam.Breast.ContactRuntime' if a.startswith('-ExecCmds=') else a) for a in args]
 if name=='legacy':
  args=[a.replace('R_619448199d803edf1ab83af9','R_05393c71101a04b1d7f0ed32') for a in args if a not in ['-VamContactGPU','-RenderOffscreen'] and not a.startswith('-VamContactSnapshots=')]
  args.append('-NullRHI')
 with (d/'console.log').open('w') as f:code=subprocess.call(args,stdout=f,stderr=subprocess.STDOUT)
 s=(d/'runtime.log').read_text(errors='replace')
 result={'exit':code,'tests':re.findall(r'Test Completed.*',s),'phases':dict(re.findall(r'CONTACT_PHASE (\w+) frame_ms=([\d.]+)',s)),'errors':re.findall(r'LogAutomationController: Error: .*',s)}
 (d/'summary.json').write_text(json.dumps(result,indent=2));print(name,result,flush=True)
