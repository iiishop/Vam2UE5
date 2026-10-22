"""Build/reload immutable hair variants; preserve the body and rendered source curves."""
import argparse,os,json,hashlib,subprocess
from pathlib import Path
from prepare_hair_constraints import collision_radii
p=argparse.ArgumentParser();p.add_argument('style',choices=['long','ponytail','bun']);p.add_argument('mode',choices=['no-extra','source-clock','prediction']);p.add_argument('--tag',default='');p.add_argument('--base',type=Path,help='Verified body bundle; default preserves the historical LegResearch base');a=p.parse_args()
assert not a.tag or all(c.isalnum() or c=='-' for c in a.tag)
root=Path(__file__).resolve().parents[1];out=root/'Saved/HairDynamicRepair'/(f'{a.style}-{a.mode}'+('-'+a.tag if a.tag else ''))
out.mkdir(parents=True,exist_ok=False)
inp=root/'Saved/HairGeneric'/a.style/('source-v5.json' if a.style=='ponytail' else 'source-v4-budget.json')
# The bun source-v4-budget identity reproduces the recorded bun-r8 asset identity.
# It is the same imported four-group hair, not a replacement hairstyle.
d=json.loads(inp.read_text())
base=a.base.resolve() if a.base else root/'Evidence/LegResearch/bundle.json'
base_data=json.loads(base.read_text())
assert base_data['status']=='committed' and base_data['independent_reload_verified'], 'Unverified body bundle'
head_rebind=None
if a.base and base.resolve()!=(root/'Evidence/LegResearch/bundle.json').resolve():
    # A source cache's head collision provenance is body-specific. Only the
    # independently measured zero-difference M compatibility proof permits
    # relabeling that geometry; arbitrary other bodies still require refitting.
    proof_path=root/'Saved/HairDynamicRepair/m-base-compatibility.json'
    proof=json.loads(proof_path.read_text())
    assert proof['m_base_identity']==base_data['identity'], 'No compatibility proof for requested body'
    for key in ('common_inverse_bind_max_abs','head_preskin_max_cm','head_influence_set_mismatch_count','head_knee_operator_max_cm'):
        assert proof[key]==0, ('Head compatibility differs',key)
    assert proof['head_input_set_equal'] and proof['input_to_source_equal'] and proof['refinement_parents_equal']
    for path,digest in proof['source_files'].items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest, ('Compatibility evidence changed',path)
    assert hashlib.sha256(base.read_bytes()).hexdigest()==proof['source_files'][str(base.resolve())]
    old_manifest=json.loads((Path(proof['old_audit'])/'manifest.json').read_text())
    new_manifest=json.loads((Path(proof['m_audit'])/'manifest.json').read_text())
    old_body=old_manifest['stages']['04-jiggle']['mesh'];new_body=new_manifest['stages']['04-jiggle']['mesh']
    assert d['head_collision']['reference_body']==old_body, 'Source collision is not from the proven old body'
    head_rebind=dict(old_reference_body=old_body,new_reference_body=new_body,compatibility_proof=str(proof_path),compatibility_sha256=hashlib.sha256(proof_path.read_bytes()).hexdigest(),scope='Identical head bind, canonical pre-skin positions and influences; source sphere geometry unchanged')
    d['head_collision']['reference_body']=new_body
    d['head_collision']['body_compatibility']=head_rebind
for g in d['groups']:
    s=g['source_constraints'];s['source_owns_shape']=True
    if a.mode in ('source-clock','prediction'):
        # Reference-rate adapter, not a claim of native VaM GPU-kernel equivalence.
        s['support_passes_per_second']=60*max(1,int(float(g['native_physics']['Iterations'])))
    if a.mode=='prediction':s['prediction_support']=True
    hair=next(x['source']['dynamic']['hair'] for x in d['catalog']['assets'] if x['source_path']==g['source_path'])
    s['source_collision']=collision_radii(g,hair,s['guide_source_ids'])
    s['version']='source-point-support-links-3-'+a.mode
d.pop('identity');d['identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
source=out/'source.json';source.write_text(json.dumps(d,separators=(',',':')))
(out/'preparation.json').write_text(json.dumps(dict(source_path=str(inp.resolve()),source_sha256=hashlib.sha256(inp.read_bytes()).hexdigest(),base_bundle=str(base),base_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),base_identity=base_data['identity'],base_configuration=base_data['configuration'],style=a.style,mode=a.mode,head_collision_rebind=head_rebind),indent=2))
env=dict(os.environ,VAM_HAIR_INPUT=str(source),VAM_HAIR_BASE_BUNDLE=str(base),VAM_HAIR_REPORT=str(out/'bundle.json'))
editor='I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
project='I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject'
for phase in ['build','reload']:
    env['VAM_HAIR_PHASE']=phase
    with (out/(phase+'-stdout.log')).open('w') as f:
        r=subprocess.run([editor,project,'-run=pythonscript','-script='+str(root/'Scripts/ue_hair_build.py'),'-NullRHI','-unattended','-nosplash','-abslog='+str(out/(phase+'.log'))],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
    (out/(phase+'-process.json')).write_text(json.dumps({'exit_code':r.returncode}))
    assert r.returncode==0,(phase,r.returncode)
    print(a.style,a.mode,phase,flush=True)
assert json.loads((out/'bundle.json').read_text())['independent_reload_verified']
