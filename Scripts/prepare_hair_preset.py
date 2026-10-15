"""Resolve a browser hair preset and fit its complete source guides to an existing body.
The browser planner/decoder is reused; native-render sampling remains a separate step.
"""
import argparse,hashlib,json,sys,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_index import Catalog
from vam_plan import Planner,resource_json
from vam_decode import decode_vab
from vam_hair_prepare import prepare
from vam_unity import Bundle,builtin_scalp
p=argparse.ArgumentParser();p.add_argument('selection');p.add_argument('ir',type=Path);p.add_argument('contract',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
cat=Catalog(Path(__file__).resolve().parents[1]/'Saved');plan=Planner(cat).generate([a.selection]);assert plan['status']=='ready',plan['counts']
(a.output/'plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2));rows=[]
for item in plan['items']:
 if item['resource_kind']!='vab':continue
 with zipfile.ZipFile(Path(plan['source_root'])/item['source']) as z:
  stem=item['path'][:-4];vam=resource_json(z.read(stem+'.vam'),stem+'.vam')[0];vaj=resource_json(z.read(stem+'.vaj'),stem+'.vaj')[0];data=decode_vab(z.read(item['path']),vam,vaj)
 assert data.get('dynamic',{}).get('hair'),'Selected preset contains a non-hair dynamic item'
 rows.append(dict(kind='dynamic',source=item['source'],path=item['path'],data=data))
ir=json.loads(a.ir.read_text());contract=json.loads(a.contract.read_text());known={r['name'] for r in ir['records'] if r.get('kind')=='builtin_scalp'}
missing={r['data']['dynamic']['hair']['scalp'] for r in rows}-known-{'CustomScalp'}
if missing:
 root=Path(plan['source_root'])/'VaM_Data/StreamingAssets';person=Bundle(root/'a_per');shared=Bundle(root/'h_zzz_mat')
 for name in sorted(missing):
  mesh,wrap=builtin_scalp(person,shared,name);ir['records'].append(dict(kind='builtin_scalp',name=name,mesh=mesh,wrap=wrap,source='a_per + h_zzz_mat'))
ir['plan_documents']=plan['documents']
result=prepare(ir,contract,rows,native_full_guides=True)
assert sum(len(g['curves']) for g in result['groups'])==sum(len(r['data']['dynamic']['hair']['root_to_scalp']) for r in rows)
(a.output/'prepared.json').write_text(json.dumps(result,separators=(',',':')))
(a.output/'receipt.json').write_text(json.dumps(dict(selection=a.selection,plan_id=plan['plan_id'],body_contract=contract['contract_id'],prepared_identity=result['identity'],groups=[dict(name=g['name'],curves=len(g['curves'])) for g in result['groups']],source_guide_policy='Whole guides and painted rigidity profiles preserved for native render binding; no anchor triangle loss'),indent=2))
print((a.output/'receipt.json').read_text())
