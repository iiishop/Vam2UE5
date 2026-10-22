"""Exercise new catalog presets with the accepted solver and identical M-body motion.

Uses the existing VaM/native import, immutable UE build, Editor and PIE tools.
This is a UE generalization survey, not a new per-style VaM dynamics comparison.
Selection JSON must point to separately exported hair-only presets.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT/'Saved/LowerBodyContact/C1.5/run-20261009-224219/contract.json'
BASE = ROOT/'Evidence/MKneeOperator/asset-bundle.json'
PROXY = ROOT/'Saved/HairDynamicRepair/long-prediction-m-final/source.json'
AUDIT = Path('I:/Document/UE5/SmartNPC/Saved/VamDeformationAudit/20261022-150339-E7DA082641FF201D31BB66B5863EC41E')
PROJECT = ROOT.parents[1]/'SmartNPC.uproject'
HOST = ROOT.parents[1]/'Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject'
EDITOR = 'I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'


def read(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def write(p,d): Path(p).write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf8')
def run(cmd, log, env=None):
    print('RUN',log,flush=True)
    if log.exists():
        archive=log.with_name(log.stem+'-'+sha(log)[:12]+log.suffix)
        if not archive.exists(): archive.write_bytes(log.read_bytes())
    with log.open('w',encoding='utf8') as f:
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1800)
    if r.returncode: raise RuntimeError(f'{r.returncode}: {log}')
def py(script,args,log): run([sys.executable,'-X','utf8',ROOT/'Scripts'/script,*args],log)


def process(row,out):
    key=row['key'];p=out/key;p.mkdir(exist_ok=True)
    capture=ROOT/'Saved/HairRestoration'/('diversity-'+key+'-01')
    capture.mkdir(exist_ok=True)
    if not (capture/'runtime/complete.txt').exists():
        run(['pwsh','-NoProfile','-ExecutionPolicy','Bypass','-File',ROOT/'Scripts/run_vam_hair_rest_capture.ps1',
             '-OutputRoot',capture,'-HairPresetPath',out/(key+'.vap')],p/'vam-runner.log')
    assert (capture/'restored.txt').exists(),'VaM plugin restoration not verified'
    prep=p/'import'
    if not (prep/'prepared.json').exists():
        py('prepare_hair_preset.py',[row['id'],ROOT/'Saved/HairRestoration/source-ir.json',CONTRACT,prep],p/'prepare.log')
    steps=[('prepare_hair_native_roots.py',[capture/'runtime',prep/'prepared.json',CONTRACT,p/'native.json'],'native.json'),
           ('prepare_hair_render_reference.py',[capture/'runtime',p/'native.json',CONTRACT,p/'render.json'],'render.json'),
           ('prepare_hair_physics.py',[p/'render.json',CONTRACT,p/'physics.json'],'physics.json')]
    for script,args,output in steps:
        if not (p/output).exists(): py(script,args,p/(script+'.log'))
    source=p/'source.json'
    if not source.exists():
        d=read(p/'physics.json');reference=read(PROXY);base=read(BASE)
        assert d['body_contract']==reference['body_contract']==read(CONTRACT)['contract_id']
        proof=read(ROOT/'Saved/HairDynamicRepair/m-base-compatibility.json')
        assert proof['m_base_identity']==base['identity']
        for path,digest in proof['source_files'].items(): assert sha(path)==digest,(path,'changed proof')
        d['head_collision']=copy.deepcopy(reference['head_collision'])
        assert d['head_collision']['reference_body']==read(AUDIT/'manifest.json')['stages']['04-jiggle']['mesh']
        d['survey_provenance']=dict(selection=row,accepted_proxy=str(PROXY),proxy_sha256=sha(PROXY),
                                  base_identity=base['identity'],scope='New source hair; unchanged solver and verified same body/head proxy')
        d.pop('identity');d['identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
        source.write_text(json.dumps(d,separators=(',',':')),encoding='utf8')
    bundle=p/'bundle.json'
    env=dict(os.environ,VAM_HAIR_INPUT=str(source),VAM_HAIR_BASE_BUNDLE=str(BASE),VAM_HAIR_REPORT=str(bundle))
    for phase in ['build','reload']:
        if bundle.exists() and (phase=='build' or read(bundle).get('independent_reload_verified')): continue
        env['VAM_HAIR_PHASE']=phase
        run([EDITOR,HOST,'-run=pythonscript','-script='+str(ROOT/'Scripts/ue_hair_build.py'),'-NullRHI','-unattended','-nosplash','-abslog='+str(p/(phase+'.log'))],p/(phase+'-stdout.log'),env)
    assert read(bundle)['independent_reload_verified']
    replay=p/'replay.json'
    if not replay.exists():
        py('prepare_hair_dynamic_replay.py',[ROOT/'Evidence/HairDynamicReference/ponytail/dynamic-samples.json',
           ROOT/'Saved/HairRestoration/variants-ponytail-01/runtime/hair-rest-body.json',AUDIT,bundle,replay],p/'replay-prepare.log')
    orbit=p/'orbit.json'
    if not orbit.exists():
        d=read(replay);d['use_recorded_camera']=False
        d['camera_override_provenance']=dict(source_replay=str(replay),sha256=sha(replay),reason='Identical orbit survey across new styles; head motion only reused, no new VaM style dynamics reference')
        d.pop('replay_identity');d['replay_identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest();write(orbit,d)
    if not (p/'editor/check.json').exists():py('run_hair_editor_check.py',[bundle,p/'editor'],p/'editor-runner.log')
    assert read(p/'editor/check.json')['status']=='passed'
    if not (p/'pie/capture.json').exists():py('run_hair_capture.py',[bundle,p/'pie','--source',orbit,'--project',PROJECT,'--pie','--orbit'],p/'pie-runner.log')
    if not (p/'pie/metrics.json').exists():py('audit_hair_dynamic_comparison.py',[p/'pie/capture.json',p/'pie/metrics.json','--replay',orbit],p/'metrics.log')
    assert read(p/'pie/metrics.json')['head_replay_validation']['status']=='PASS'
    if not (p/'benchmark/frames.csv').exists():py('run_hair_capture.py',[bundle,p/'benchmark','--benchmark','--project',PROJECT],p/'benchmark-runner.log')
    if not (p/'benchmark/summary.json').exists():py('summarize_hair_benchmark.py',[p/'benchmark'],p/'benchmark-summary.log')
    return dict(status='MEASURED',bundle=str(bundle),capture=str(p/'pie/capture.json'),source_capture=str(capture),
                metrics=str(p/'pie/metrics.json'),benchmark=str(p/'benchmark/summary.json'))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('selection',type=Path);args=parser.parse_args()
    out=args.selection.resolve().parent;results={}
    for row in read(args.selection):
        try:results[row['key']]=process(row,out)
        except Exception:
            results[row['key']]=dict(status='FAILED',error=traceback.format_exc());print(results[row['key']]['error'],flush=True)
        write(out/'results.json',results)
    print(json.dumps(results,indent=2),flush=True)
    sys.exit(0 if all(r['status']=='MEASURED' for r in results.values()) else 1)
