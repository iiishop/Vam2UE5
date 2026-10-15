"""Compare retained VaM raw/smoothed captures against actual A1 actor replays.

No nearest-neighbour matching; duplicate render aliases are checked before use.
"""
import argparse
import csv
import json
from pathlib import Path
import numpy as np


def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def metrics(delta, ids):
    e = np.linalg.norm(delta, axis=1)
    return dict(rms_cm=float(np.sqrt(np.mean(e * e))),
                p95_cm=float(np.percentile(e, 95)), max_cm=float(e.max()),
                worst_source=int(ids[e.argmax()]), count=len(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('baseline', type=Path)
    ap.add_argument('replay', type=Path)
    a = ap.parse_args()
    masks = np.load(a.baseline / 'c2-region-masks.npz')
    source_ids = masks['source_ids'].astype(int)
    old = {(r['pose'], r['mode']): r for r in read(a.baseline / 'ue-replays.json')}
    rows, gates = [], []
    for record in read(a.replay / 'ue-replays.json'):
        key = record['pose'], record['mode']
        assert record['request_id'] == old[key]['request_id'], key
        capture = Path(record['source_capture'])
        meta = read(capture)
        targets = {k: np.fromfile(capture.parent / meta[k]['file'], dtype='<f4')
                   .reshape(-1, 3)[:, [2, 0, 1]].astype(float) * 100
                   for k in ('raw', 'smoothed')}
        bundle = Path(record['audit_bundle'])
        baseline = Path(old[key]['audit_bundle'])
        def bones(p):
            return {b['name']: np.array(b['component_matrix_row_major'])
                    for b in read(p / '00-source-lbs/inputs.json')['bones']}
        before, after = bones(baseline), bones(bundle)
        matrix_error = max(float(np.max(np.abs(before[b] - after[b]))) for b in before)
        assert matrix_error < 1e-6, (key, matrix_error)
        gate = dict(pose=key[0], mode=key[1], matrix_max=matrix_error, aliases={})
        for region in masks.files:
            if region == 'source_ids':
                continue
            indices = np.flatnonzero(masks[region] > .05)
            ids = source_ids[indices]
            rows.append(dict(pose=key[0], mode=key[1], region=region,
                             stage='vam-smoothing', target='raw',
                             **metrics(targets['smoothed'][indices] - targets['raw'][indices], ids)))
        for stage in ('00-source-lbs', '01-final-weights', '02-static-helpers', '03-corrective'):
            mapping = read(bundle / ('source-mapping.json' if stage == '00-source-lbs' else 'mapping.json'))
            vertices = np.array(read(bundle / stage / 'surface.json')['positions'])
            groups = {}
            for i, v in enumerate(mapping['render_to_input']):
                if 0 <= v < len(mapping['input_to_source']):
                    s = mapping['input_to_source'][v]
                    if s >= 0:
                        groups.setdefault(s, []).append(i)
            # First alias, never average away a crack. Report alias spread separately.
            points = {s: vertices[g[0]] for s, g in groups.items()}
            gate['aliases'][stage] = max(float(np.linalg.norm(vertices[g] - vertices[g[0]], axis=1).max()) for g in groups.values())
            for region in masks.files:
                if region == 'source_ids':
                    continue
                indices = np.array([i for i in np.flatnonzero(masks[region] > .05) if source_ids[i] in points])
                ids = source_ids[indices]
                actual = np.array([points[s] for s in ids])
                for target, expected in targets.items():
                    rows.append(dict(pose=key[0], mode=key[1], region=region,
                                     stage=stage, target=target, **metrics(actual - expected[indices], ids)))
        gates.append(gate)
    with (a.replay / 'surface-layers.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (a.replay / 'surface-gates.json').write_text(json.dumps(gates, indent=2))
    print('Compared', len(gates), 'captures;', len(rows), 'regional measurements')


if __name__ == '__main__':
    main()
