import json, hashlib, shutil, subprocess, re, gc
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parents[1];O=R/'Evidence/HairStructureRepair';O.mkdir(exist_ok=True)
refs={'wavy':'HairDiversity/wavy','long':'HairDynamicRepair/long-prediction-m-final','ponytail':'HairDynamicRepair/ponytail-prediction-m-structured','bun':'HairDynamicRepair/bun-prediction-m-structured'}
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def header(p):
 with p.open(encoding='utf-8-sig') as f:s=f.read(8192)
 return dict(world=re.search(r'"world"\s*:\s*"([^"]+)"',s)[1],frames=int(re.search(r'"frames"\s*:\s*(\d+)',s)[1]))
def first_sample(p):
 s='';start=None
 with p.open(encoding='utf-8-sig') as f:
  while True:
   chunk=f.read(1024*1024)
   if not chunk:raise ValueError('Incomplete GPU capture')
   s+=chunk
   if start is None:
    m=re.search(r'"gpu_samples"\s*:\s*\[\s*',s)
    if not m:continue
    start=m.end()
   try:return json.JSONDecoder().raw_decode(s,start)[0]
   except json.JSONDecodeError:pass
results={}
for key,oldref in refs.items():
 n=R/'Saved/HairStructureRepair'/(key+'-buffered');old=R/'Saved'/oldref
 fresh=R/'Saved/HairStructureRepair'/(key+'-baseline')
 before=fresh if (fresh/'pie/capture.json').exists() else old
 if not (n/'pie/metrics.json').exists():continue
 a=read(old/'source.json');b=read(n/'source.json');assert a['groups']==b['groups'],'Changed source geometry/constraints'
 metric=read(n/'pie/metrics.json');data=header(n/'pie/capture.json');original=header(before/'pie/capture.json')
 assert metric['head_replay_validation']['status']=='PASS'
 assert data['world']=='PIE' and data['frames']==original['frames']
 groups=[]
 for ph,rows in metric['phases'].items():
  for g in rows:
   groups.append(dict(phase=ph,group=g['group'],tip_max_cm=g['tip_excursion_from_phase_first_cm']['max'],length_p95=g['polyline_length_ratio_to_rest']['p95'],length_max=g['polyline_length_ratio_to_rest']['max'],head_proxy_max_cm=g['proxy_penetration']['head']['depth_cm']['max'],body_proxy_max_cm=g['proxy_penetration']['body']['depth_cm']['max'],render_common_points=g['render_matched_points']))
 bundle=read(n/'bundle.json');shutil.copyfile(n/'bundle.json',O/(key+'-bundle.json'))
 row=dict(identity=bundle['identity'],blueprint=bundle['blueprint'],configuration=bundle['configuration'],frames=data['frames'],matrix_validation=metric['head_replay_validation']['status'],source_groups_unchanged=True,groups=groups,capture=str(n/'pie/capture.json'),capture_sha256=digest(n/'pie/capture.json'),baseline_capture=str(before/'pie/capture.json'),baseline_is_fresh=before==fresh,shader_validation=read(n/'pie/shader-validation.json'))
 if key=='wavy':
  x=np.array(first_sample(before/'pie/capture.json')['groups'][0]['guide_points']).reshape(-1,32,3);y=np.array(first_sample(n/'pie/capture.json')['groups'][0]['guide_points']).reshape(-1,32,3)
  row['guide_boundary']=dict(before_tip_above_origin=int(sum(x[:,-1,2]>0)),after_tip_above_origin=int(sum(y[:,-1,2]>0)),first_64_rms_change_cm=float(np.sqrt(np.mean(np.sum((x[:64]-y[:64])**2,axis=-1)))),last_64_rms_change_cm=float(np.sqrt(np.mean(np.sum((x[64:]-y[64:])**2,axis=-1)))))
 ep=n/'editor/check.json'
 if (n/'editor-retry/check.json').exists():ep=n/'editor-retry/check.json'
 if (n/'editor-final/check.json').exists():ep=n/'editor-final/check.json'
 if ep.exists():row['editor']=read(ep)
 if (n/'benchmark/summary.json').exists():row['performance']=read(n/'benchmark/summary.json')
 results[key]=row
 del a,b;gc.collect()
(O/'metrics.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:dict(identity=v['identity'],boundary=v.get('guide_boundary'),editor=v.get('editor',{}).get('status'),performance=v.get('performance',{}).get('hair_delta_ms')) for k,v in results.items()},indent=2))
