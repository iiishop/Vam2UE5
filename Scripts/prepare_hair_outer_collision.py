"""Native sphere proxy candidate for the closed outer head envelope.

Optimizes surface clearance rather than interior volume. This intentionally
bridges facial cavities; it is not an exact skin SDF or an animated-jaw solution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.spatial import ConvexHull, distance

p = argparse.ArgumentParser()
p.add_argument('mesh', type=Path)
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
m = np.load(a.mesh)
v = m['vertices'][m['head_mask']]
h = ConvexHull(v)
axes = [np.arange(v[:, k].min()+.4, v[:, k].max(), .8) for k in range(3)]
grid = np.array(np.meshgrid(*axes, indexing='ij')).reshape(3, -1).T
r = np.concatenate([np.min(-(q @ h.equations[:, :3].T + h.equations[:, 3]), axis=1)
                    for q in np.array_split(grid, 40)])
grid, r = grid[r > .4], r[r > .4]
t = v[h.simplices]
surface = np.unique(np.round(np.concatenate([
    t[:, 0]+(t[:, 1]-t[:, 0])*i/4+(t[:, 2]-t[:, 0])*j/4
    for i in range(5) for j in range(5-i)]), 4), axis=0)
# cdist avoids an N x M x 3 temporary allocation.
clearance = distance.cdist(grid, surface).astype(np.float32)-r[:, None].astype(np.float32)-.2
best = np.full(len(surface), 10., dtype=np.float32)
chosen = []
for _ in range(64):
    score = np.maximum(np.minimum(best, 2.)[None, :]-np.maximum(clearance, 0), 0).sum(1)
    score[chosen] = -1
    i = int(score.argmax())
    chosen.append(i)
    best = np.minimum(best, clearance[i])
s = json.loads(a.source.read_text())
c = s['head_collision']
c.update(version='outer-head-convex-envelope-spheres-1',
         spheres=[dict(center=grid[i].tolist(), radius_cm=float(r[i]+.2)) for i in chosen],
         outer_envelope_clearance_cm=dict(max=float(best.max()), p95=float(np.percentile(best, 95))),
         generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
         note='Closed outer-head envelope with 0.2 cm margin; animated jaw requires separate validation.')
s.pop('identity')
s['identity'] = hashlib.sha256(json.dumps(s, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(s, separators=(',', ':')))
print(json.dumps(c['outer_envelope_clearance_cm']))
