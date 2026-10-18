"""Derive physical rest guides and constraint classes from source data, never names."""
import argparse,copy,hashlib,json
from pathlib import Path
import numpy as np
from vam_hair_prepare import resample

def classify(curves):
    lengths=[];chords=[];rigidity=[]
    for c in curves:
        p=np.array(c['points']);length=np.linalg.norm(np.diff(p,axis=0),axis=1).sum()
        lengths.append(length);chords.append(np.linalg.norm(p[-1]-p[0])/max(length,1e-6));rigidity.append(c['rigidity'])
    ratio=float(np.median(chords));rig=float(np.median(rigidity))
    # Compact wound strands require a much stronger intrinsic-curvature constraint.
    # Straight hanging strands with the same length remain freely bending.
    kind='compact' if ratio<.6 and rig>.15 else ('supported' if rig>.15 else 'loose')
    return dict(kind=kind,median_length_cm=float(np.median(lengths)),median_chord_ratio=ratio,median_rigidity=rig)

def main(source,contract,out):
    data=json.loads(source.read_text());original=copy.deepcopy(data)
    capture=Path(data['native_render_evaluation']['capture']);manifest=json.loads((capture/'hair-rest.json').read_text());body=json.loads((capture/'hair-rest-body.json').read_text());bind=json.loads(contract.read_text())
    assert bind['contract_id']==data['body_contract']
    head=np.array(next(b for b in body['bones'] if b['id']=='head')['worldMatrix'],float).reshape(4,4)
    target=np.array(next(b for b in bind['bones'] if b['name']=='head')['world_bind'])
    report=[]
    for g in data['groups']:
        item=next(x for x in manifest['hairs'] if Path(g['source_path']).stem==x['path'].split(':')[-1])
        g['native_physics']=item['physics_settings']
        n=len(item['root_to_scalp']);detail=int(float(item['tess_factor'][1]))
        points=np.fromfile(capture/(item['prefix']+'-tess.f32'),dtype='<f4').reshape(-1,3)[:n*detail].reshape(n,detail,3)
        p=(np.concatenate([points,np.ones((*points.shape[:2],1))],-1)@np.linalg.inv(head).T)[...,:3]
        p=p[:,:,[2,0,1]]*100;p=(np.concatenate([p,np.ones((*p.shape[:2],1))],-1)@target.T)[...,:3]
        for c in g['curves']:
            assert c['anchor_index']==0,'Split anchor requires matching native sample intervals'
            c['points']=resample(p[c['source_curve']].tolist(),32)
        g['physics_policy']=classify(g['curves']);report.append(dict(source=g['source_path'],**g['physics_policy']))
    from prepare_hair_constraints import bake_group, fit_visible_guides, ensure_render_bindings
    assets={a['source_path']:a['source']['dynamic']['hair'] for a in data['catalog']['assets']}
    for g in data['groups']:
        item=next(x for x in manifest['hairs'] if Path(g['source_path']).stem==x['path'].split(':')[-1])
        ensure_render_bindings(g,assets[g['source_path']],item,capture)
        g['visible_guide_fit']=fit_visible_guides(g,assets[g['source_path']])
        g['source_constraints']=bake_group(g,assets[g['source_path']])
    assert all(a['render_curves']==b['render_curves'] and a['render_width_cm']==b['render_width_cm'] for a,b in zip(data['groups'],original['groups']))
    data['physics_policy']={'version':'native-rest-source-constraints-2','render_unchanged':True,'groups':report}
    data.pop('identity');data['identity']=hashlib.sha256(json.dumps(data,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    out.write_text(json.dumps(data,separators=(',',':')));print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('contract',type=Path);p.add_argument('output',type=Path);a=p.parse_args();main(a.source,a.contract,a.output)
