"""Record three explicit leg-chain/contact combinations through the existing A1/A2 harness."""
import argparse,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('suite',type=Path);p.add_argument('out',type=Path);p.add_argument('--world',choices=['Editor','PIE'],default='PIE');p.add_argument('--project',default='I:/Document/UE5/SmartNPC/SmartNPC.uproject');a=p.parse_args()
assert not a.out.exists(),'Preserve previous evidence; choose a new output directory'
a.out.mkdir(parents=True)
recipe=json.loads(a.suite.read_text(encoding='utf-8-sig'));results=[]
for group,chain,contact in [('A',True,True),('B',True,False),('C',False,True)]:
 cases=[c for c in recipe['cases'] if c['id'].startswith(group+'-')]
 assert cases and all(c['leg_chain']==chain and c['contact_solver']==contact and c['leg_jiggle'] for c in cases)
 sub=a.out/(group+'-recipe.json');sub.write_text(json.dumps(dict(recipe,cases=cases),indent=2))
 cmd=[sys.executable,'-X','utf8',str(Path(__file__).with_name('run_s1s2_pose_suite.py')),str(a.bundle),str(a.out/group),'--recipe',str(sub),'--world',a.world,'--project',a.project]
 result=subprocess.run(cmd)
 suite=a.out/group/'suite.json'
 states=json.loads(suite.read_text())['states'] if suite.exists() else []
 log=(a.out/group/'runtime.log').read_text(errors='replace')
 valid=result.returncode==0 and 'LEG_CHAIN_CAPTURE_ISOLATION' in log and 'Python script executed with errors' not in log and 'Result={Success} Name={ContactRuntime}' in log and len(states)==len(cases) and all(s['leg_chain_enabled']==chain and s['world_type']==a.world for s in states)
 if a.world=='PIE':
  valid=valid and all(s['contact_solver_active']==contact for s in states)
  for case in cases:
   video=a.out/group/case['id']/'video.json'
   if case.get('video_frames',0):
    samples=json.loads(video.read_text())['samples'] if video.exists() else []
    valid=valid and len(samples)==case['video_frames']//2 and all(s['contact_solver_active']==contact for s in samples)
 results.append(dict(group=group,returncode=result.returncode,valid=valid,states=len(states)))
 (a.out/'validation.json').write_text(json.dumps(results,indent=2))
 print(results[-1],flush=True)
assert all(r['valid'] for r in results),results
