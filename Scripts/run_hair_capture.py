"""Real viewport hair recording/benchmark. Never treats encoded FPS as runtime FPS."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('bundle',type=Path);p.add_argument('out',type=Path)
p.add_argument('--source',type=Path,help='Existing actual-bone locomotion recording')
p.add_argument('--quality',choices=['fine','coarse','rest'],default='fine')
p.add_argument('--benchmark',action='store_true');p.add_argument('--characters',type=int,default=1)
p.add_argument('--pie',action='store_true');p.add_argument('--diagnostic',choices=['no-collision','no-gravity','both','springs','dense']);
p.add_argument('--orbit',action='store_true');p.add_argument('--local-frame',action='store_true')
p.add_argument('--preview',action='store_true');p.add_argument('--full-body',action='store_true')
p.add_argument('--project',default='I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject')
p.add_argument('--editor',default='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe')
a=p.parse_args();d=json.loads(a.bundle.read_text());assert d['status']=='committed' and d['independent_reload_verified']
a.out.mkdir(parents=True,exist_ok=False);out=a.out.resolve().as_posix();bp=d['blueprint']+'_C'
assert ' ' not in out and (not a.source or ' ' not in str(a.source.resolve())), 'UE ExecCmds recorder currently requires paths without spaces'
if a.benchmark:command=f'vam.HairBenchmark {out} {bp} {a.characters}'
else:
    assert a.source and a.source.is_file()
    command=f'vam.HairCapture {out} {bp} {a.source.resolve().as_posix()} {a.quality}'
    if a.preview:command+=' preview'
    if a.full_body:command+=' full-body'
    if a.orbit:command+=' orbit'
    if a.local_frame:command+=' local-frame'
if a.diagnostic:command+=' '+('no-collision no-gravity' if a.diagnostic=='both' else a.diagnostic)
if a.pie:command=command.replace('vam.HairCapture','vam.HairPIECapture')
if a.quality=='rest' and not a.benchmark:command='r.HairStrands.Simulation 0,'+command
cmd=[a.editor,a.project,'/Engine/Maps/Entry','-game','-RenderOffscreen','-unattended','-nosplash','-dx12','-windowed','-ResX=1280','-ResY=720','-ExecCmds='+command,'-abslog='+str(a.out.resolve()/'runtime.log')]
if a.pie:cmd.remove('-game')
(a.out/'command.json').write_text(json.dumps(cmd,indent=2));(a.out/'bundle.json').write_text(json.dumps(d,indent=2))
try:
    with (a.out/'stdout.log').open('w') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=1200)
    (a.out/'process.json').write_text(json.dumps({'exit_code':r.returncode}))
    assert r.returncode==0
    assert (a.out/('frames.csv' if a.benchmark else 'capture.json')).exists(),'Missing completion evidence'
except subprocess.TimeoutExpired:
    (a.out/'process.json').write_text(json.dumps({'timeout':True,'limit_seconds':1200}));raise


