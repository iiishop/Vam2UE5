"""Compare actual Niagara GPU nodes with the published Groom guide buffer.

Capture stalls are intentional; this is not a performance measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from audit_hair_node_reconstruction import interpolation

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture', type=Path)
p.add_argument('out', type=Path)
a = p.parse_args()
raw = a.capture.read_bytes()
d = json.loads(raw)
rows = []
for frame in d['gpu_samples']:
    world = np.array(frame['groom_world_matrix']).reshape(4, 4)
    inv = np.linalg.inv(world)
    for component in frame['niagara_physics_di']['components']:
        g = component['niagara_component_index']
        published = next(x for x in frame['groups'] if x['group'] == g)
        particles = component.get('raw_gpu_particles', [])
        if len(particles) != 1 or 'Particles.NodePosition' not in particles[0]:
            raise ValueError(('Missing raw GPU data', frame['time'], g))
        data = particles[0]
        pos = np.array(data['Particles.NodePosition']).reshape(-1, 32, 3)
        rest = np.array(data['Particles.RestPosition']).reshape(-1, 32, 3)
        local = (np.concatenate([pos, np.ones((*pos.shape[:2], 1))], axis=2) @ inv)[..., :3]
        guide = np.array(published['guide_points']).reshape(-1, 32, 3)
        guide_rest = np.array(published['guide_rest_points']).reshape(-1, 32, 3)
        expected = guide_rest + np.einsum('ij,cjk->cik', interpolation(32, 31), local[:, 1:] - rest[:, 1:])
        error = np.linalg.norm(expected - guide, axis=2)
        lengths = np.linalg.norm(np.diff(local[:, 1:], axis=1), axis=2)
        rest_lengths = np.linalg.norm(np.diff(rest[:, 1:], axis=1), axis=2)
        ratios = lengths.sum(axis=1) / rest_lengths.sum(axis=1)
        head = [x for x in frame['collision_proxies_head_local'] if x['bone'] == 'head']
        depth = np.max(np.stack([x['radius_cm'] - np.linalg.norm(local - x['a'], axis=2) for x in head]), axis=0)
        depth[:, :2] = 0
        ci, ni = np.unravel_index(np.argmax(depth), depth.shape)
        ri = int(np.argmax(ratios))
        rows.append(dict(time=frame['time'], group=g, count=len(pos)*32,
                         guide_reprojection_max_cm=float(error.max()),
                         raw_head_depth_max_cm=float(max(0,depth.max())),
                         worst_head_curve=int(ci), worst_head_slot=int(ni),
                         worst_head_local_cm=local[ci,ni].tolist(),
                         raw_length_ratio_max=float(ratios.max()),
                         worst_length_curve=ri,
                         worst_length_rest_cm=float(rest_lengths[ri].sum()),
                         worst_length_current_cm=float(lengths[ri].sum())))
out = dict(schema='vam-hair-raw-particle-audit/1',capture=str(a.capture.resolve()),
           sha256=hashlib.sha256(raw).hexdigest(),rows=rows,
           scope='Direct Niagara float-buffer readback; head proxy depth excludes virtual node and root; published guide comparison checks same-frame mapping. Not a skin test.')
a.out.parent.mkdir(parents=True, exist_ok=True)
with a.out.open('x') as f:
    json.dump(out,f,indent=2,allow_nan=False)
for group in sorted({r['group'] for r in rows}):
    selected=[r for r in rows if r['group']==group]
    print(group,{k:max(r[k] for r in selected) for k in ('guide_reprojection_max_cm','raw_head_depth_max_cm','raw_length_ratio_max')})
