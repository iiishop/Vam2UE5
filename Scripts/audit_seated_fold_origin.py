"""Trace a picked seated fold through retained source IDs and generator evidence.
Source IDs below select diagnostic probes only; they are never production repair masks.
"""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('c15',type=Path);a=p.parse_args()
read=lambda x:json.loads(x.read_text(encoding='utf-8-sig'))
contract=read(a.c15/'contract.json');vam=np.load(a.c15/'source-shape-attribution.npz')['actual'];maps={side:read(a.evidence/(side+'-generation.json')) for side in ['L','R']}
pairs=[(7189,17896),(7840,18520),(8249,18920)];rows=[]
for left,right in pairs:
 row={'source_pair':[left,right],'vam_preskin_right_minus_reflected_left_cm':(vam[right]-vam[left]*[1,-1,1]).tolist(),'aliases':{},'morph_contributions':[]}
 for side,source in [('L',left),('R',right)]:
  aliases=[r for r in maps[side]['vertices'] if r['source']==source and r['area']>0];chosen=max(aliases,key=lambda r:r['area']);row['aliases'][side]=dict(copies=aliases,largest_area_input=chosen['input'],region_spread=max(r['region'] for r in aliases)-min(r['region'] for r in aliases),source_capture_error_cm=float(np.linalg.norm(np.array(chosen['position'])-vam[source])))
 for m in contract['morphs']:
  d=(np.array(m['deltas'][right])-np.array(m['deltas'][left])*[1,-1,1])*m['default']
  if np.linalg.norm(d)>.001:row['morph_contributions'].append(dict(name=m.get('source_parameters',{}).get('displayName',m['name']),target=m['name'],value=m['default'],delta_cm=d.tolist()))
 rows.append(row)
out={'scope':'Source topology IDs from rear-camera fold picking. VaM pre-skin samples from frozen C1.5. Generator rows from fresh UE Glute Build; no nearest-neighbor matching. Largest-area representative is downstream Leg WeldWeights policy.','source_contract_id':contract['contract_id'],'probes':rows}
(a.evidence/'origin-summary.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
