"""Explicit deep-flexion input on normal BP; production Rig assets remain unchanged."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('out',type=Path);p.add_argument('--world',default='PIE',choices=['PIE','Editor']);p.add_argument('--groups',nargs='+',default=['M','C']);a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
recipe=read(Path('Config/LegDeepKneel/motion.json'));bundle=Path('Saved/LegChainComparison/candidate-5/bundle.json')
manifest=dict(bundle=read(bundle),recipe=recipe,world=a.world,source_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in Path('Source').rglob('*') if p.suffix in ('.h','.cpp')},scope='Capture-only input bypasses pose limits explicitly; no production RigProfile changes. Actual normal character/skin/jiggle/deformer. Not a production kneeling controller or certified contact simulation.')
manifest['validation_identity']=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest();(a.out/'identity.json').write_text(json.dumps(manifest,indent=2));checks=[]
for g in a.groups:
 cases=[c for c in recipe['cases'] if c['id'].startswith(g+'-') and (a.world=='PIE' or c['id'].endswith('deep-kneel'))];r=dict(recipe,cases=cases);rp=a.out/(g+'-recipe.json');rp.write_text(json.dumps(r,indent=2));run=a.out/g
 ret=subprocess.run([sys.executable,'-X','utf8','Scripts/run_s1s2_pose_suite.py',str(bundle),str(run),'--world',a.world,'--recipe',str(rp),'--project','I:/Document/UE5/SmartNPC/SmartNPC.uproject']).returncode
 assert ret==0,('UE failed',g,ret)
 assert 'Result={Success} Name={ContactRuntime}' in (run/'runtime.log').read_text(errors='replace')
 for c in cases:
  state=read(run/c['id']/'state.json');v=read(run/c['id']/'video.json');frames=v['samples'];assert state['world_type']==a.world and len(frames)==c['video_frames']//2
  assert v['diagnostic_deep_flexion'] and all(f['diagnostic_deep_flexion'] for f in frames)
  peak=frames[165]['actual_knee_deg'];expected=[155,0] if c.get('deep_single_leg') else [155,155]
  assert all(abs(x-y)<.05 for x,y in zip(peak,expected)),(c['id'],peak)
  checks.append(dict(case=c['id'],world=a.world,frames=len(frames),actual_knee_at_hold=peak,production_clamp_difference_deg=frames[165]['production_clamp_difference_deg'],contact_active_frames=sum(f['contact_solver_active'] for f in frames),capture_valid=True))
 (a.out/'validation.json').write_text(json.dumps(checks,indent=2));print(checks,flush=True)
