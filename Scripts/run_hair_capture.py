"""Real viewport hair recording/benchmark. Never treats encoded FPS as runtime FPS."""
import argparse,json,subprocess,time
from hair_runtime_validation import shader_failures
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('bundle',type=Path);p.add_argument('out',type=Path)
p.add_argument('--source',type=Path,help='Existing actual-bone locomotion recording')
p.add_argument('--quality',choices=['fine','rest'],default='fine')
p.add_argument('--benchmark',action='store_true');p.add_argument('--characters',type=int,default=1)
p.add_argument('--pie',action='store_true');p.add_argument('--diagnostic',choices=['no-collision','no-gravity','both','springs','dense','native-loose','soft-loose','no-viscosity']);
p.add_argument('--temporal-audit',action='store_true')
p.add_argument('--body-audit',action='store_true')
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
    if a.body_audit:command+=' body-audit'
    if a.temporal_audit:command+=' temporal-audit'
    if a.local_frame:command+=' local-frame'
if a.diagnostic:command+=' '+('no-collision no-gravity' if a.diagnostic=='both' else a.diagnostic)
if a.pie:command=command.replace('vam.HairCapture','vam.HairPIECapture')
if a.quality=='rest' and not a.benchmark:command='r.HairStrands.Simulation 0,'+command
cmd=[a.editor,a.project,'/Engine/Maps/Entry','-game','-RenderOffscreen','-unattended','-nosplash','-dx12','-windowed','-ResX=1280','-ResY=720','-ExecCmds='+command,'-abslog='+str(a.out.resolve()/'runtime.log')]
if a.pie:cmd.remove('-game')
(a.out/'command.json').write_text(json.dumps(cmd,indent=2));(a.out/'bundle.json').write_text(json.dumps(d,indent=2))
try:
    cleanup=False
    with (a.out/'stdout.log').open('w') as log:
        process=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
        started=time.monotonic()
        while process.poll() is None:
            runtime=a.out/'runtime.log'
            failures=shader_failures(runtime.read_text(encoding='utf8',errors='replace')) if runtime.exists() else []
            if failures:
                (a.out/'shader-validation.json').write_text(json.dumps({'passed':False,'errors':failures},indent=2))
                process.terminate();process.wait(timeout=30)
                raise RuntimeError('GPU shader compilation failed; capture is invalid. See shader-validation.json')
            capture=a.out/'capture.json'
            if capture.exists():
                try:
                    complete=json.loads(capture.read_text())
                    expected=180 if a.preview else len(json.loads(a.source.read_text())['samples'])
                    if a.pie and complete.get('world')=='PIE' and complete.get('frames')==expected:
                        # The in-world quit ends PIE, not its isolated editor.
                        # Only terminate this runner's own child after evidence is complete.
                        time.sleep(2)
                        if process.poll() is None:process.terminate();cleanup=True
                        break
                except (ValueError,OSError):pass
            if time.monotonic()-started>1200:
                process.kill();process.wait();raise subprocess.TimeoutExpired(cmd,1200)
            time.sleep(1)
        code=process.wait(timeout=30)
    failures=shader_failures((a.out/'runtime.log').read_text(encoding='utf8',errors='replace'))
    (a.out/'shader-validation.json').write_text(json.dumps({'passed':not failures,'errors':failures},indent=2))
    assert not failures,'GPU shader failed; process exit alone is not sufficient'
    (a.out/'process.json').write_text(json.dumps({'exit_code':code,'owned_pie_editor_cleanup_after_complete_capture':cleanup}))
    assert code==0 or cleanup
    assert (a.out/('frames.csv' if a.benchmark else 'capture.json')).exists(),'Missing completion evidence'
except subprocess.TimeoutExpired:
    (a.out/'process.json').write_text(json.dumps({'timeout':True,'limit_seconds':1200}));raise


