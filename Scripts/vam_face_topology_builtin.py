"""Extract a locked canonical calibration input using existing source adapters.

This is not a character importer: no textures, outfits, morph application,
runtime asset generation or source writes. Builtin name selects source data,
never a person-specific fitting rule.
"""
import argparse
import hashlib
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_index import Catalog
from vam_plan import Planner, canonical
from vam_unity import Bundle, mesh_from_unity
from vam_metahuman import write_json
from vam_face_topology_calibrate import base_mesh


def extract(data, builtin_name, output):
    output=Path(output)
    if output.exists(): raise ValueError('CanonicalInputDestinationExists')
    resolver=Planner(Catalog(data)); identity=resolver.builtin('character', builtin_name, '')
    item=resolver.items[identity]; mapping=item['builtin_mapping']; loc=mapping['entry']['locator']
    bundle=Bundle(resolver.root/mapping['files'][loc['file']]['path'])
    records=[]
    for obj, cls, raw in bundle.records({'DAZMesh','DAZMergedMesh'}):
        records.append({'kind':'unity_mesh','class':cls,'object':str(obj.path_id),'source':loc['file'],
                        'mesh':mesh_from_unity(raw),
                        'parameters':{k:v for k,v in raw.items() if k in ('targetMesh','graftMesh','startGraftVertIndex')}})
    plan={'schema':'vam-face-canonical-plan/1','source_root':str(resolver.root),'items':[item],
          'roots':[identity],'documents':{},'edges':[]}
    plan['plan_id']=hashlib.sha256(canonical(plan)).hexdigest()
    ir={'schema':'vam-face-canonical-ir/1','plan_id':plan['plan_id'],'decode_id':plan['plan_id'],
        'records':records,'applied_morphs':[],
        'source_hashes':{value['path']:value['sha256'] for value in mapping['files'].values()},
        'note':'Canonical source adapters only; not a complete appearance SourceIR'}
    family,digest,_=base_mesh(ir)
    output.mkdir(parents=True);write_json(output/'plan.json',plan);write_json(output/'canonical.ir.json',ir)
    return {'family':family,'topology_digest':digest,'output':str(output.resolve())}


if __name__=='__main__':
    import json
    p=argparse.ArgumentParser();p.add_argument('--data',required=True);p.add_argument('--builtin',required=True);p.add_argument('--out',required=True)
    a=p.parse_args();print(json.dumps(extract(a.data,a.builtin,a.out)))
