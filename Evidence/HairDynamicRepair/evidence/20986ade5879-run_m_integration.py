"""Serial production M integration using the existing real UE tools."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
root=root.parent if root.name=='Saved' else root
assert (root/'Scripts').exists(),root
runs=root/'Saved/HairDynamicRepair'
commands=json.loads((runs/'m-inputs/prepare-commands.json').read_text())['commands']
ref=commands[1]
commands.append(dict(style='bun',build=[*ref['build'][:4],'bun',*ref['build'][5:]],
 prepare=[*ref['prepare'][:7],str(runs/'bun-prediction-m-final/bundle.json'),str(runs/'m-inputs/bun.json')]))
# Bun uses the measured ponytail HEAD motion only; there is no new VaM bun
# dynamic reference and this run must not be labelled a VaM bun comparison.
project='I:/Document/UE5/SmartNPC/SmartNPC.uproject'
def run(args,log):
 print('RUN',args[0:4],flush=True)
 with log.open('w',encoding='utf8') as f:
  r=subprocess.run([sys.executable,'-X','utf8',*map(str,args)],cwd=root,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError((r.returncode,str(log)))
for cmd in commands:
 style=cmd['style'];candidate=runs/(style+'-prediction-m-final');bundle=candidate/'bundle.json'
 if not bundle.exists():run(cmd['build'][3:],runs/('m-'+style+'-build.log'))
 assert json.loads(bundle.read_text())['independent_reload_verified']
 target=Path(cmd['prepare'][-1])
 if not target.exists():run(cmd['prepare'][3:],runs/('m-'+style+'-prepare.log'))
for cmd in commands:
 style=cmd['style'];candidate=runs/(style+'-prediction-m-final');bundle=candidate/'bundle.json'
 target=Path(cmd['prepare'][-1]);orbit=target.with_name(style+'-orbit.json')
 if not orbit.exists():
  d=json.loads(target.read_text());d['use_recorded_camera']=False
  d['camera_override_provenance']={'source_replay':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'reason':'Production M multi-angle regression only; orbit is not the VaM camera comparison.'}
  d.pop('replay_identity');d['replay_identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
  orbit.write_text(json.dumps(d,separators=(',',':')))
 out=candidate/'editor'
 if not (out/'check.json').exists():run(['Scripts/run_hair_editor_check.py',bundle,out],candidate/'editor-runner.log')
 assert json.loads((out/'check.json').read_text())['status']=='passed'
 out=candidate/'pie'
 if not (out/'capture.json').exists():run(['Scripts/run_hair_capture.py',bundle,out,'--source',orbit,'--project',project,'--pie','--orbit'],candidate/'pie-runner.log')
 if not (out/'metrics.json').exists():run(['Scripts/audit_hair_dynamic_comparison.py',out/'capture.json',out/'metrics.json','--replay',orbit],candidate/'pie-metrics.log')
 assert json.loads((out/'metrics.json').read_text())['head_replay_validation']['status']=='PASS'
 out=candidate/'benchmark'
 if not (out/'frames.csv').exists():run(['Scripts/run_hair_capture.py',bundle,out,'--benchmark','--project',project],candidate/'benchmark-runner.log')
 if not (out/'summary.json').exists():run(['Scripts/summarize_hair_benchmark.py',out],candidate/'benchmark-summary.log')
print('M production integration completed',flush=True)
