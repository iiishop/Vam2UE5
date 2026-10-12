"""Measure knee changes using existing A1 exports and retained C1.5 source IDs."""
import argparse, csv, json
from pathlib import Path
import numpy as np
from compare_c15_asset_replay import read, surface


def stats(v):
    e = np.asarray(v)
    return dict(rms_cm=float(np.sqrt(np.mean(e*e))), p95_cm=float(np.percentile(e,95)), max_cm=float(e.max()))


def run(before, after, masks_path, out):
    out.mkdir(parents=True, exist_ok=True)
    masks = np.load(masks_path)
    ids = masks['source_ids']
    regions = {k: {int(ids[i]): int(i) for i in np.flatnonzero(masks[k]>.05)} for k in masks.files if k!='source_ids'}
    old, new = [{(r['pose'],r['mode']): r for r in read(p/'ue-replays.json')} for p in (before,after)]
    assert old.keys()==new.keys(), 'Do not silently omit failed replays'
    rows, changes, gates, coverage = [], [], [], []
    for key in sorted(old):
        assert old[key]['request_id']==new[key]['request_id']
        capture = Path(old[key]['source_capture']); meta=read(capture)
        target = np.fromfile(capture.parent/meta['raw']['file'],dtype='<f4').reshape(-1,3)[:,[2,0,1]].astype(float)*100
        bundles = [Path(r[key]['audit_bundle']) for r in (old,new)]
        bones = [{b['name']: np.array(b['component_matrix_row_major']) for b in read(p/'00-source-lbs/inputs.json')['bones']} for p in bundles]
        matrix_error = max(float(np.max(np.abs(bones[0][b]-bones[1][b]))) for b in bones[0])
        assert matrix_error<1.e-7, 'Actual source bone matrices differ'
        gates.append(dict(pose=key[0],mode=key[1],request_id=old[key]['request_id'],matrix_max=matrix_error))
        for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective']:
            points = [surface(p,stage) for p in bundles]
            for region,mapping in regions.items():
                pairs = [(s,i) for s,i in mapping.items() if all(s in p for p in points)]
                missing = [sorted(s for s in mapping if s not in p) for p in points]
                assert missing[0]==missing[1], f'Changed source coverage: {region}'
                coverage.append(dict(pose=key[0],mode=key[1],stage=stage,region=region,used=len(pairs),mask_count=len(mapping),missing_source_ids=missing[0]))
                arrays = [np.array([p[s] for s,i in pairs]) for p in points]
                for label,u in zip(['before','after'],arrays):
                    err=np.linalg.norm(u-np.array([target[i] for s,i in pairs]),axis=1)
                    rows.append(dict(version=label,pose=key[0],mode=key[1],stage=stage,region=region,count=len(err),**stats(err),worst_source=pairs[int(err.argmax())][0]))
                err=np.linalg.norm(arrays[1]-arrays[0],axis=1)
                changes.append(dict(pose=key[0],mode=key[1],stage=stage,region=region,**stats(err)))
    for name,values in [('source-errors',rows),('stage-changes',changes)]:
        with (out/(name+'.csv')).open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(values[0]));w.writeheader();w.writerows(values)
    (out/'gates.json').write_text(json.dumps(dict(gates=gates,scope='Actual VaM matrices, original core on/off states kept separate. Source ID mapping only; raw VaM output is not an anatomical ground truth.'),indent=2))
    (out/'coverage.json').write_text(json.dumps(coverage,indent=2))
    print('Matched replays',len(gates),'metric rows',len(rows))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('masks',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();run(a.before,a.after,a.masks,a.out)
