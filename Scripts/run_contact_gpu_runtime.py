"""Build new candidate assets or measure the real two-character viewport. No source assets overwritten."""
from pathlib import Path
import subprocess,sys,json,re,hashlib
root=Path(__file__).resolve().parents[1]
mode=sys.argv[1] if len(sys.argv)>1 else 'gpu48'
run=sys.argv[2] if len(sys.argv)>2 else mode
out=root/'Saved/ContactGPURuntime'/run;out.mkdir(parents=True,exist_ok=True)
asset_root='/Game/VamRuntime/GPUContact_20261009_v2'
if len(sys.argv)>4:asset_root=sys.argv[4]
if mode=='assets-skin':mode='assets';asset_root='/Game/VamRuntime/GPUContact_Skin_20261009'
args=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject']
if mode=='assets':
 args+=['-VamBreastTestConfig=/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime','-VamGPUAssetRoot='+asset_root,'-ExecCmds=Automation RunTests Vam.Breast.GPUAssets','-TestExit=Automation Test Queue Empty']
else:
 gpu=mode.startswith('gpu');n=int(mode[3:]) if gpu else 32
 args+=['/Engine/Maps/Entry','-game','-ForceRes','-ResX=1920','-ResY=1080','-windowed','-NoVSync','-VamBreastTestConfig='+asset_root+'/RC_Runtime','-ExecCmds='+','.join('sg.'+x+'Quality 2' for x in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading'])+',r.ScreenPercentage 100,t.MaxFPS 0,vam.Contact.GPU '+('1' if gpu else '0')+',vam.Contact.GPUIterations '+str(n)+',vam.Contact.GPUBarriers '+(sys.argv[3] if len(sys.argv)>3 else '128')+',vam.ContactBenchmark '+out.as_posix()+' motion']
args+=sys.argv[5:]
args+=['-unattended','-nosplash','-RenderOffscreen','-dx12','-abslog='+str(out/'runtime.log')]
(out/'command.json').write_text(json.dumps(args,indent=2))
shader=root/'Shaders/Private/VamGPUContact.usf'
(out/'solver.usf').write_bytes(shader.read_bytes())
(out/'source-hashes.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in list((root/'Source/VamContactGPU').rglob('*'))+[shader] if p.is_file()},indent=2))
with (out/'console.log').open('w') as f:
 p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 try:code=p.wait(timeout=400)
 except subprocess.TimeoutExpired:p.kill();p.wait();raise
s=(out/'runtime.log').read_text(errors='replace')
r={'exit':code,'tests':re.findall('Test Completed.*',s),'errors':re.findall(r'(?:LogAutomationController: Error:|Fatal error:|Assertion failed:).*',s)}
(out/'result.json').write_text(json.dumps(r,indent=2));print(r)
if code or r['errors']:raise SystemExit(code or 1)
if mode=='assets' and not any('Result={Success}' in x for x in r['tests']):raise SystemExit(1)
if mode!='assets' and not (out/'frames.csv').exists():raise SystemExit('No frame measurements')

if mode!='assets':
 for phase in (2,3):
  for instance in (0,1):
   f=out/f'surface-{phase}-{instance}.bin'
   if not f.exists() or f.stat().st_size==0:raise SystemExit('Missing actual deformed render vertices: '+str(f))
 if mode.startswith('gpu'):
  if 'GPU_SAFETY' in s:raise SystemExit('GPU numerical safety fallback occurred; result is NOT a GPU performance success')
  for phase in (2,3):
   if 'GPU resident' not in (out/f'diagnostics-{phase}.txt').read_text(errors='replace') or 'GPU fallback' in (out/f'diagnostics-{phase}.txt').read_text(errors='replace'):raise SystemExit('GPU backend did not run for both characters')
