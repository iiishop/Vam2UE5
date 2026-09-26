"""Read-only p0 ocular material evidence omitted from the current skin target.

Original vertex/edge identity is retained. Material ownership is direct evidence;
homology to MH plica/eyelid loops remains unverified. No cloud dispatch occurs.
"""
import argparse,copy,json
from pathlib import Path
from vam_metahuman import to_creator,write_json,fingerprint
from vam_fit import apply_morph


def extract(request):
    r=json.loads(Path(request).read_text(encoding='utf8'));out=Path(r['output']);job=out/'Initial'
    recipe=json.loads((job/'recipe.json').read_text(encoding='utf8'));target=json.loads((job/'target.json').read_text(encoding='utf8'))
    ref=recipe['inputs']['source_ir']
    if fingerprint(ref['path'])!=ref['sha256']:raise ValueError('SourceIRChanged')
    ir=json.loads(Path(ref['path']).read_text(encoding='utf8'))
    record=next(x for x in ir['records'] if x.get('kind')=='unity_mesh' and x['object']==target['source_object'])
    mesh=copy.deepcopy(record['mesh']);morphs={x['id']:x for x in ir['records'] if x.get('kind')=='morph'}
    for a in target['included_morphs']:
        apply_morph(mesh,morphs[a['id']]['data'],a['value'],len(mesh['vertices']),len(mesh['uv']))
    edges={};materials={}
    for polygon in mesh['polygons']:
        material=mesh['materials'][polygon['materialNum']];ids=polygon['vertices']
        materials.setdefault(material,[]).append(ids)
        for a,b in zip(ids,ids[1:]+ids[:1]):edges.setdefault(tuple(sorted((a,b))),set()).add(material)
    regions={}
    for name in ('Lacrimals','Tear','Sclera','Cornea'):
        polygons=materials.get(name,[]);ids=sorted({i for p in polygons for i in p})
        regions[name]={'source_vertex_ids':ids,'p0_creator_vertices':[to_creator(mesh['vertices'][i]) for i in ids],
            'source_polygons':polygons,'shared_material_boundaries':[{'source_edge':list(e),'materials':sorted(m)} for e,m in edges.items() if name in m and len(m)>1],
            'excluded_from_current_fit':name in target['excluded_materials'],'evidence':'original SourceIR material polygons and source vertex/edge identity'}
    weights={x['id']:x['value'] for x in target['included_morphs']}
    eye_formulas=[{'morph_id':row['id'],'morph_value':weights[row['id']],**formula}
        for row in target['formula_provenance'] for formula in row['formulas'] if 'eye' in str(formula.get('target','')).casefold()]
    write_json(out/'ocular-source-evidence.json',{'source_ir':ref,'topology_digest':target['source_base_topology_sha256'],
        'regions':regions,'mh_semantic_homology_verified':False,'scope':'local p0 diagnostic only; original source unchanged; no new upload',
        'identity_eye_bone_formulas_not_evaluated_by_current_target':eye_formulas,
        'eye_geometry_limitation':'p0 vertex deltas only; source eye bone center/orientation/scale formulas are recorded, not evaluated. Ocular diagnostic vertices do not certify native neutral eye placement.'})
    print(json.dumps({'preset':r['preset'],'regions':{n:{'vertices':len(v['source_vertex_ids']),'polygons':len(v['source_polygons']),
        'shared_material_edges':len(v['shared_material_boundaries']),'excluded_from_fit':v['excluded_from_current_fit']} for n,v in regions.items()}}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);a=p.parse_args();extract(a.request)
