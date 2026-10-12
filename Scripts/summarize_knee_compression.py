"""Collect actual knee angles and runtime acceptance from existing UE captures."""
import json,re
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[1];r=root/'Saved/LowerBodyContact/KneeCompression';out=[]
for p in r.glob('candidate-*/*/*/state.json'):
 s=json.loads(p.read_text());bones={v['bone']:np.array(v['component_matrix']).reshape(4,4)[3,:3] for v in s['actual_bones']};angles={}
 for side in ['l','r']:
  a=bones[side+'Shin']-bones[side+'Thigh'];b=bones[side+'Foot']-bones[side+'Shin'];angles[side]=float(np.degrees(np.arccos(np.clip(np.dot(a,b)/np.linalg.norm(a)/np.linalg.norm(b),-1,1))))
 out.append(dict(capture=str(p),pose=s['id'],world=s['world_type'],actual_knee_bend_deg=angles,solver_active=s['contact_solver_active'],acceptance=s['acceptance']))
(r/'capture-summary.json').write_text(json.dumps(out,indent=2));print(len(out),'captures')
for name in ['regression','final']:
 p=r/(name+'-tests.log')
 if p.exists():
  tests=re.findall(r'Test Completed\. Result=\{([^}]+)\} Name=\{([^}]+)\} Path=\{([^}]+)\}',p.read_text(errors='replace'));(r/(name+'-results.json')).write_text(json.dumps(tests,indent=2));print(name,len(tests),'tests',set(v[0] for v in tests))
for v in out:
 if 'candidate-4' in v['capture'] and v['pose']=='knee-120-soft':print(v['world'],v['actual_knee_bend_deg'],v['acceptance'])
