import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];runs=root/'Saved/HairDynamicRepair'
project='I:/Document/UE5/SmartNPC/SmartNPC.uproject'
def run(args,log):
 print('RUN',args[:4],flush=True)
 with log.open('w',encoding='utf8') as f:r=subprocess.run([sys.executable,'-X','utf8',*map(str,args)],cwd=root,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError((r.returncode,str(log)))
for style,tag in [('ponytail','structured'),('ponytail','m-structured'),('bun','m-structured')]:
 p=runs/(style+'-prediction-'+tag);bundle=p/'bundle.json'
 if not bundle.exists():
  args=['Scripts/run_hair_dynamic_candidate.py',style,'prediction','--tag',tag]
  if tag.startswith('m-'):args+=['--base','Evidence/MKneeOperator/asset-bundle.json']
  run(args,runs/(style+'-'+tag+'-build.log'))
 assert json.loads(bundle.read_text())['independent_reload_verified']
 if tag=='structured':
  replay=root/'Saved/HairDynamicUE/inputs-v2/ponytail.json';out=p/'capture'
  if not (out/'capture.json').exists():run(['Scripts/run_hair_capture.py',bundle,out,'--source',replay,'--project',project],p/'capture-runner.log')
  if not (out/'metrics.json').exists():run(['Scripts/audit_hair_dynamic_comparison.py',out/'capture.json',out/'metrics.json','--replay',replay],p/'capture-metrics.log')
  assert json.loads((out/'metrics.json').read_text())['head_replay_validation']['status']=='PASS'
  continue
 replay=runs/'m-inputs'/(style+'-structured.json')
 if not replay.exists():
  run(['Scripts/prepare_hair_dynamic_replay.py','Evidence/HairDynamicReference/ponytail/dynamic-samples.json','Saved/HairRestoration/variants-ponytail-01/runtime/hair-rest-body.json','I:/Document/UE5/SmartNPC/Saved/VamDeformationAudit/20261022-150339-E7DA082641FF201D31BB66B5863EC41E',bundle,replay],p/'prepare.log')
 orbit=replay.with_name(style+'-structured-orbit.json')
 if not orbit.exists():
  d=json.loads(replay.read_text());d['use_recorded_camera']=False
  d['camera_override_provenance']={'source_replay':str(replay),'sha256':hashlib.sha256(replay.read_bytes()).hexdigest(),'reason':'Production M orbit regression; same measured head motion, not the source camera.'}
  d.pop('replay_identity');d['replay_identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
  orbit.write_text(json.dumps(d,separators=(',',':')))
 out=p/'editor'
 if not (out/'check.json').exists():run(['Scripts/run_hair_editor_check.py',bundle,out],p/'editor-runner.log')
 assert json.loads((out/'check.json').read_text())['status']=='passed'
 out=p/'pie'
 if not (out/'capture.json').exists():run(['Scripts/run_hair_capture.py',bundle,out,'--source',orbit,'--project',project,'--pie','--orbit'],p/'pie-runner.log')
 if not (out/'metrics.json').exists():run(['Scripts/audit_hair_dynamic_comparison.py',out/'capture.json',out/'metrics.json','--replay',orbit],p/'pie-metrics.log')
 assert json.loads((out/'metrics.json').read_text())['head_replay_validation']['status']=='PASS'
 out=p/'benchmark'
 if not (out/'frames.csv').exists():run(['Scripts/run_hair_capture.py',bundle,out,'--benchmark','--project',project],p/'benchmark-runner.log')
 if not (out/'summary.json').exists():run(['Scripts/summarize_hair_benchmark.py',out],p/'benchmark-summary.log')
print('Structured group regression completed',flush=True)
