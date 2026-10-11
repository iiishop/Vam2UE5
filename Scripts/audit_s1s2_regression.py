"""Summarize normal-BP UE captures without substituting offline rendering for screenshots."""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from audit_surface_seams import run as seams
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();rows=[];paired=[]
for world in ['editor','pie']:
 for state in sorted((a.root/world).glob('*/state.json')):
  s=json.loads(state.read_text());b=Path(s['audit_bundle']);report=seams(b);stats={k:v['statistics'] for k,v in report['stages'].items()};finite={}
  for stage in report['stages']:
   positions=np.array(json.loads((b/stage/'surface.json').read_text())['positions']);finite[stage]=bool(np.isfinite(positions).all())
  rows.append(dict(world=world,pose=state.parent.name,actor=s['actor_class'],audit=str(b),statistics=stats,finite=finite,images=[dict(path=str(f),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(state.parent.glob('*.png'))]))
for state in sorted((a.root/'editor').glob('*/state.json')):
 other=a.root/'pie'/state.parent.name/'state.json'
 if not other.exists():continue
 s,t=[json.loads(x.read_text()) for x in [state,other]]
 if s['jiggle_enabled']:continue
 diffs={}
 for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective']:
  v,w=[np.array(json.loads((Path(x['audit_bundle'])/stage/'surface.json').read_text())['positions']) for x in [s,t]]
  e=np.linalg.norm(v-w,axis=1);diffs[stage]=dict(rms_cm=float(np.sqrt(np.mean(e*e))),max_cm=float(e.max()))
 paired.append(dict(pose=state.parent.name,stages=diffs))
result=dict(captures=rows,editor_pie=paired,position_tolerance_cm=.01,scope='Source-ID pairs only. Normal statistics are exported bind attributes, not proof of runtime shading; actual World Normal/Lit images must also be inspected. Contact historical failures tracked separately.')
(a.root/'surface-validation.json').write_text(json.dumps(result,indent=2))
assert all(all(x['finite'].values()) for x in rows),'Nonfinite surface'
assert all(v['position_gap_cm']['max']<=.01 for x in rows for v in x['statistics'].values()),'Seam gap'
print('Captures',len(rows),'static paired worlds',len(paired),'max Editor/PIE cm',max(v['max_cm'] for p in paired for v in p['stages'].values()))
