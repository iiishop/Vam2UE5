"""Compare exact source strand IDs in measured head frames; no nearest-neighbour fit."""
import argparse
import json
from pathlib import Path
import numpy as np
from vam_hair_prepare import resample


def audit(capture, prepared, contract):
    meta = json.loads((capture/'hair-rest.json').read_text())
    body = json.loads((capture/'hair-rest-body.json').read_text())
    head = np.array(next(b for b in body['bones'] if b['id']=='head')['worldMatrix'],float).reshape(4,4)
    bind = np.array(next(b for b in contract['bones'] if b['name']=='head')['world_bind'])
    rows = []
    for item in meta['hairs']:
        name = item['path'].split(':')[-1]
        groups = [g for g in prepared['groups'] if Path(g['source_path']).stem==name]
        assert groups, name
        count = int(item['provider_count'])//int(item['source_segments'])
        particles = np.fromfile(capture/(item['prefix']+'-particles.f32'),dtype='<f4').reshape(count,-1,3)
        local = (np.concatenate([particles,np.ones((*particles.shape[:2],1))],-1)@np.linalg.inv(head).T)[...,:3]
        local = local[:,:,[2,0,1]]*100
        target = (np.concatenate([local,np.ones((*local.shape[:2],1))],-1)@bind.T)[...,:3]
        errors, roots = [], []
        for group in groups:
            for curve in group['curves']:
                source = target[curve['source_curve']]
                if group['kind']=='anchor':
                    raise ValueError('Split anchors need their source segment interval before comparison')
                source = source[curve.get('anchor_index',0):]
                wanted = np.array(resample(source.tolist(),len(curve['points'])))
                delta = np.linalg.norm(np.array(curve['points'])-wanted,axis=-1)
                errors.extend(delta); roots.append(delta[0])
        errors = np.array(errors)
        rows.append(dict(resource=name,rms_cm=float(np.sqrt(np.mean(errors**2))),
                         p95_cm=float(np.percentile(errors,95)),max_cm=float(errors.max()),
                         root_rms_cm=float(np.sqrt(np.mean(np.array(roots)**2)))))
    return dict(scope='Native physics-off particles vs imported source-ID curves, aligned by actual head matrix; not final density-expanded render-strand validation',
                prepared_identity=prepared['identity'],capture=str(capture),resources=rows)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('prepared',type=Path);p.add_argument('contract',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
    r=audit(a.capture,json.loads(a.prepared.read_text()),json.loads(a.contract.read_text()))
    a.out.write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
