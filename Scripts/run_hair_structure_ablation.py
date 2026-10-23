"""Build isolated, identity-bearing hair ablations; never overwrite source assets."""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT.parents[1]/'Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject'
EDITOR='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'

def run(cmd,log,env=None):
    print('RUN',log,flush=True)
    with log.open('w',encoding='utf8') as f:
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1800)
    if r.returncode:raise RuntimeError((r.returncode,str(log)))

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--variant',choices=['unchanged','no-point-support','multi-guide'],default='unchanged')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    source=json.loads(a.source.read_text());baseline=source.pop('identity')
    if a.variant=='no-point-support':
        for g in source['groups']:
            for row in g['source_constraints']['rigidity']:row[:]=[0.0]*len(row)
    if a.variant=='multi-guide':source['guide_interpolation']='multi'
    source['structure_experiment']=dict(variant=a.variant,source=str(a.source.resolve()),source_identity=baseline,
        source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),scope='Isolated ablation; not a production acceptance')
    source['identity']=hashlib.sha256(json.dumps(source,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    inp=a.output/'source.json';inp.write_text(json.dumps(source,separators=(',',':')),encoding='utf8')
    bundle=a.output/'bundle.json';env=dict(os.environ,VAM_HAIR_INPUT=str(inp.resolve()),
        VAM_HAIR_BASE_BUNDLE=str(ROOT/'Evidence/MKneeOperator/asset-bundle.json'),VAM_HAIR_REPORT=str(bundle.resolve()))
    for phase in ['build','reload']:
        env['VAM_HAIR_PHASE']=phase
        run([EDITOR,HOST,'-run=pythonscript','-script='+str(ROOT/'Scripts/ue_hair_build.py'),'-NullRHI','-unattended','-nosplash',
             '-abslog='+str((a.output/(phase+'.log')).resolve())],a.output/(phase+'-stdout.log'),env)
    assert json.loads(bundle.read_text())['independent_reload_verified']
    print(bundle.resolve(),flush=True)

if __name__=='__main__':main()
