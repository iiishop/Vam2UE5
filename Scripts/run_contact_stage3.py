from pathlib import Path
import subprocess,json,sys,hashlib
root=Path(__file__).resolve().parents[1]
out=root/'Saved/ContactStage3'/sys.argv[1];out.mkdir(parents=True,exist_ok=False)
test=sys.argv[2] if len(sys.argv)>2 else 'Vam.Breast.GPUCoupling'
a=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject','-ExecCmds=Automation RunTests '+test,'-TestExit=Automation Test Queue Empty','-unattended','-nosplash','-RenderOffscreen','-dx12','-abslog='+str(out/'runtime.log')]+[x for x in sys.argv[3:] if x!="--installed"]
if "--installed" in sys.argv:a[1]=str(root.parents[1]/"SmartNPC.uproject")
(out/"source-hashes.json").write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in [root/"Source",root/"Shaders"] for p in folder.rglob("*") if p.is_file()},indent=2))
(out/'command.json').write_text(json.dumps(a,indent=2))
with (out/'console.log').open('w') as f:r=subprocess.run(a,stdout=f,stderr=subprocess.STDOUT,timeout=300,creationflags=subprocess.CREATE_NO_WINDOW)
log=(out/'runtime.log').read_text(errors='replace')
print(r.returncode);print('\n'.join(x for x in log.splitlines() if 'COUPLING' in x or 'STAGE3' in x or 'Result={' in x or 'LogAutomationController: Error:' in x))
if r.returncode or 'Result={Fail}' in log or 'Result={Success}' not in log:raise SystemExit(r.returncode or 1)
