from pathlib import Path
import subprocess,json,sys
root=Path(__file__).resolve().parents[1]
out=root/'Saved/ContactTwoCharacters'/ (sys.argv[1] if len(sys.argv)>1 else 'run1');out.mkdir(parents=True,exist_ok=True)
args=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject','/Engine/Maps/Entry','-game','-unattended','-nosplash','-RenderOffscreen','-ForceRes','-ResX=1920','-ResY=1080','-windowed','-NoVSync','-dx12','-abslog='+str(out/'runtime.log'),'-ExecCmds=sg.ViewDistanceQuality 2,sg.AntiAliasingQuality 2,sg.ShadowQuality 2,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.PostProcessQuality 2,sg.TextureQuality 2,sg.EffectsQuality 2,sg.FoliageQuality 2,sg.ShadingQuality 2,r.ScreenPercentage 100,t.MaxFPS 0,vam.ContactBenchmark '+out.as_posix()]
(out/'command.json').write_text(json.dumps(args,indent=2))
if len(sys.argv)>2:
 args=[a.replace(',vam.ContactBenchmark ', ',vam.Contact.MaterialCache '+sys.argv[2]+',vam.ContactBenchmark ')+(' motion' if a.startswith('-ExecCmds=') else '') for a in args]
 (out/'command.json').write_text(json.dumps(args,indent=2))
with (out/'console.log').open('w') as f:
 p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 try: code=p.wait(timeout=300)
 except subprocess.TimeoutExpired:p.kill();p.wait();raise
print('Exit',code,'CSV exists',(out/'frames.csv').exists(),flush=True)
if code or not (out/'frames.csv').exists():raise SystemExit(1)
