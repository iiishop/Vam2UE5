"""Run explicit chain-operator experiments without overwriting production assets."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('out',type=Path);p.add_argument('--world',choices=['Editor','PIE'],default='PIE');a=p.parse_args()
assert not a.out.exists();a.out.mkdir(parents=True)
recipe=json.loads(Path('Config/LegChainVariants/'+('motion' if a.world=='PIE' else 'editor')+'.json').read_text())
bundle=Path('Saved/LegChainComparison/candidate-5/bundle.json');asset=json.loads(bundle.read_text())
sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in Path('Source').rglob('*') if f.suffix in ('.h','.cpp')}
manifest=dict(asset_identity=asset['identity'],configuration=asset['configuration'],source_hashes=sources,recipe=recipe,world=a.world,scope='Runtime operator experiment; existing mesh weights/bind/morph/topology unchanged. Not a regenerated or newly certified corrective asset.')
manifest['validation_identity']=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
(a.out/'identity.json').write_text(json.dumps(manifest,indent=2));results=[]
editor_ret=None
if a.world=='Editor':
 sub=a.out/'all-recipe.json';sub.write_text(json.dumps(recipe,indent=2))
 editor_ret=subprocess.run([sys.executable,'-X','utf8','Scripts/run_s1s2_pose_suite.py',str(bundle),str(a.out/'all'),'--world','Editor','--recipe',str(sub),'--project','I:/Document/UE5/SmartNPC/SmartNPC.uproject']).returncode
for group in ['R','L','T','V','S','C']:
 cases=[c for c in recipe['cases'] if c['id'].startswith(group+'-')];sub=a.out/(group+'-recipe.json');sub.write_text(json.dumps(dict(recipe,cases=cases),indent=2))
 cmd=[sys.executable,'-X','utf8','Scripts/run_s1s2_pose_suite.py',str(bundle),str(a.out/group),'--world',a.world,'--recipe',str(sub),'--project','I:/Document/UE5/SmartNPC/SmartNPC.uproject']
 ret=subprocess.run(cmd).returncode if editor_ret is None else editor_ret
 runroot=a.out/group if editor_ret is None else a.out/'all'
 states=[]
 for c in cases:
  root=runroot/c['id'];state=json.loads((root/'state.json').read_text()) if (root/'state.json').exists() else {}
  valid=state.get('leg_chain_method')==c['leg_chain_method'] and state.get('leg_chain_enabled')==c['leg_chain'] and state.get('world_type')==a.world
  active=None
  if a.world=='PIE':
   frames=json.loads((root/'video.json').read_text())['samples'] if (root/'video.json').exists() else []
   valid=valid and len(frames)==210 and all(f['leg_chain_method']==c['leg_chain_method'] for f in frames)
   active=sum(f['contact_solver_active'] for f in frames)
   if group!='C':valid=valid and active==0
  states.append(dict(id=c['id'],valid=valid,contact_active_frames=active))
 log=(runroot/'runtime.log').read_text(errors='replace')
 valid=ret==0 and all(s['valid'] for s in states) and 'Result={Success} Name={ContactRuntime}' in log
 results.append(dict(group=group,valid=valid,process_exit=ret,states=states))
 (a.out/'validation.json').write_text(json.dumps(results,indent=2));print(results[-1],flush=True)
 assert valid,'Incomplete or mismatched run; preserve failure and investigate'
