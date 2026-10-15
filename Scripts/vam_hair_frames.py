"""Scalp tangent frames for transporting authored hair through a fitted scalp.

The domain is the retained source UV topology; no spatial nearest-neighbour weld.
"""
import math
from vam_decode import validate_mesh
from vam_fit import cross


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def unit(v):
    length = math.sqrt(dot(v, v))
    if length < 1e-12:
        raise ValueError('Degenerate scalp tangent frame')
    return [x/length for x in v]


def frames(mesh, positions):
    mapping = validate_mesh(mesh)
    if len(positions) != len(mapping):
        positions = [positions[s] for s in mapping]
    ns = {}; nc = {}; tc = [0]*len(positions); ts = [[0., 0., 0.] for _ in positions]
    uv = mesh['uv']
    for poly in sorted(mesh['uv_polygons'],key=lambda p:p['materialNum']):
        v = poly['vertices']
        tris = [(v[2], v[1], v[0])]
        if len(v) == 4:
            tris.append((v[0], v[3], v[2]))
        for a, b, c in tris:
            e1, e2 = sub(positions[b], positions[a]), sub(positions[c], positions[a])
            n = unit(cross(e1, e2))
            d1, d2 = sub(uv[b], uv[a]), sub(uv[c], uv[a])
            det = d1[0]*d2[1]-d1[1]*d2[0]
            t = [(e1[k]*d2[1]-e2[k]*d1[1])/det for k in range(3)] if abs(det)>1e-12 else [0.,0.,0.]
            for i in (a,b,c):
                total = ns.setdefault(mapping[i], [0.,0.,0.])
                # VaM's source GPU adjacency stores the first six incident faces.
                # Reproduce that source operator, including high-valence vertices.
                for k in range(3):
                    if nc.get(mapping[i],0)<6:total[k] += n[k]
                    if tc[i]<6:ts[i][k] += t[k]
                nc[mapping[i]]=nc.get(mapping[i],0)+1
                tc[i]+=1
    result = []
    for i in range(len(positions)):
        n = unit(ns[mapping[i]])
        t = unit([ts[i][k]-n[k]*dot(n,ts[i]) for k in range(3)])
        result.append((t,cross(n,t),n))
    return result


def transport(point, source_root, target_root, source_frame, target_frame):
    local = [dot(sub(point,source_root), axis) for axis in source_frame]
    return [target_root[k]+sum(local[j]*target_frame[j][k] for j in range(3)) for k in range(3)]
