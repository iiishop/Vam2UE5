"""Verify InitMesh bulge preparation and isolated hip-X against real VaM buffers.

This is a diagnostic mathematical replay, not an Unreal screenshot or a
compound-pose solver. Raw source IDs/UV aliases and actual bone matrices only.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from vam_skin_runtime import runtime_skin_nodes
from vam_triax_lbs import axis_response


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    ap = argparse.ArgumentParser()
    for name in ('ir', 'contract', 'runtime', 'masks', 'output'):
        ap.add_argument(name, type=Path)
    a = ap.parse_args()
    ir, contract = read(a.ir), read(a.contract)
    skin = next(r['parameters'] for r in ir['records'] if r.get('kind') == 'skin'
                and str(r['object']) == str(contract['skin']['source_object']))
    prepared = runtime_skin_nodes(ir, skin)
    observed = read(a.runtime / 'runtime-skin-operator.json')
    assert len(prepared) == len(observed['nodes'])
    weight_errors = []
    for predicted, actual in zip(prepared, observed['nodes']):
        assert predicted['name'] == actual['name']
        if predicted['name'] not in ('lThigh', 'rThigh', 'lShin', 'rShin'):
            continue
        assert len(predicted['weights']) == len(actual['weights'])
        for p, q in zip(predicted['weights'], actual['weights']):
            assert p['vertex'] == int(q['vertex'])
            for axis in 'xyz':
                for side in ('left', 'right'):
                    key = axis + side + 'bulge'
                    weight_errors.append(abs(p[key] - float(q[key])))
    assert max(weight_errors) < 1e-4
    masks = np.load(a.masks)
    C = np.eye(4)
    C[:3, :3] = [[0, 0, 1], [1, 0, 0], [0, 1, 0]]

    def converted(matrix):
        result = C @ np.array(matrix, float).reshape(4, 4) @ C.T
        result[:3, 3] *= 100
        return result

    rows = []
    for pose in ('hip30', 'hip60', 'hip90', 'hip110'):
        for mode in ('off', 'on'):
            path = a.runtime / ('core-' + mode + '-' + pose + '.json')
            meta = read(path)
            bones = {b['id']: b for b in meta['bones']}
            for b in bones.values():
                angles = np.array(b['actualAnglesRadians'], float)
                assert np.max(np.abs(angles[1:])) < 1e-5
                if b['id'] not in ('lThigh', 'rThigh'):
                    assert np.max(np.abs(angles)) < 1e-5
            positions = np.fromfile(path.with_name(path.stem + '-morphed.f32'), '<f4').reshape(-1, 3)[:, [2, 0, 1]].astype(float) * 100
            target = np.fromfile(a.runtime / meta['raw']['file'], '<f4').reshape(-1, 3)[:, [2, 0, 1]].astype(float) * 100
            for label, nodes in (('serialized-bulge', skin['nodes']), ('initialized-bulge', prepared)):
                out = positions.copy()
                for node in nodes:
                    b = bones[node['name']]
                    bind = converted(b['morphedBind'])
                    local = (np.c_[out, np.ones(len(out))] @ np.linalg.inv(bind).T)[:, :3]
                    weight = np.zeros(len(out)); left = weight.copy(); right = weight.copy()
                    active = np.zeros(len(out), bool); full = active.copy()
                    for w in node['weights']:
                        v = w['vertex']; active[v] = True
                        weight[v], left[v], right[v] = w['xweight'], w['xleftbulge'], w['xrightbulge']
                        full[v] = all(w[k] > .99999 for k in ('xweight', 'yweight', 'zweight'))
                    for v in node['fullyWeightedVertices']:
                        active[v] = full[v] = True
                    factors = {s + t: node['bulgeFactors'].get('x' + s + t, 0)
                               for s in ('pos', 'neg') for t in ('left', 'right')}
                    delta = axis_response(local, 1, float(b['actualAnglesRadians'][0]), weight, left, right, factors, float(skin['bulgeScale']))
                    rigid = (np.c_[out, np.ones(len(out))] @ converted(b['changeFromOriginal']).T)[:, :3]
                    out[active] += (delta @ bind[:3, :3].T)[active]
                    out[full] = rigid[full]
                out = out[masks['source_ids'].astype(int)]
                for region in ('0_lateral_glute', '1_lateral_glute', '0_glute_thigh', '1_glute_thigh'):
                    indices = np.flatnonzero(masks[region] > .05)
                    e = np.linalg.norm(out[indices] - target[indices], axis=1)
                    rows.append(dict(pose=pose, mode=mode, preparation=label, region=region,
                                     rms_cm=float(np.sqrt(np.mean(e * e))), p95_cm=float(np.percentile(e, 95)),
                                     max_cm=float(e.max()), worst_source=int(masks['source_ids'][indices[e.argmax()]])))
    result = dict(scope='Isolated bilateral hip-X mathematical replay against actual VaM GPU raw buffers; not UE visual acceptance',
                  runtime_bulge_max_error=max(weight_errors), rows=rows)
    a.output.write_text(json.dumps(result, indent=2))
    maximum = max(r['max_cm'] for r in rows if r['preparation'] == 'initialized-bulge')
    print('Initialized replay maximum cm:', maximum)
    assert maximum < .001


if __name__ == '__main__':
    main()
