from pathlib import Path
import subprocess,sys,json,hashlib
root=Path(__file__).resolve().parents[1];out=root/'Saved/ContactSkinResearch'/sys.argv[1];
if out.exists(): raise SystemExit('Refusing to overwrite capture: '+str(out))
out.mkdir(parents=True)
a=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject','-VamBreastTestConfig=/Game/VamRuntime/GPUContact_20261009_v2/RC_Runtime','-VamResidentGPUVideo','-VamContactEvidence='+str(out),'-ExecCmds=Automation RunTests Vam.Breast.ContactRuntime','-TestExit=Automation Test Queue Empty','-unattended','-nosplash','-RenderOffscreen','-dx12','-abslog='+str(out/'runtime.log')]
if len(sys.argv)>2 and sys.argv[2]=='cpu':a+=['-VamSkinCPU','-VamContactTrialBending']
if len(sys.argv)>2 and sys.argv[2]=='skin':a=[x.replace('/Game/VamRuntime/GPUContact_20261009_v2/RC_Runtime','/Game/VamRuntime/GPUContact_Skin_20261009/RC_Runtime') for x in a]
if len(sys.argv)>2 and sys.argv[2].startswith('gpu'):
 n=int(sys.argv[2][3:]);a=[x.replace('-ExecCmds=Automation','-ExecCmds=vam.Contact.GPUIterations '+str(n)+',Automation') for x in a]
if 'installed' in sys.argv:a[1]='I:/Document/UE5/SmartNPC/SmartNPC.uproject'
(out/'command.json').write_text(json.dumps(a,indent=2))
(out/'solver.usf').write_bytes((root/'Shaders/Private/VamGPUContact.usf').read_bytes())
with (out/'console.log').open('w') as f:r=subprocess.run(a,stdout=f,stderr=subprocess.STDOUT,timeout=240,creationflags=subprocess.CREATE_NO_WINDOW)
print(r.returncode);print('\n'.join(x for x in (out/'runtime.log').read_text(errors='replace').splitlines() if any(k in x for k in ['WORLD_GLASS_GPU','GPU_SAFETY','Result={','Side 0 cage'])))

log=(out/'runtime.log').read_text(errors='replace')
if r.returncode or 'Result={Fail}' in log or 'GPU_SAFETY' in log or 'Result={Success}' not in log: raise SystemExit(r.returncode or 1)
