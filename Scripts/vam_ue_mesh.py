"""Adapt the WebGL inspection topology to UE clockwise geometry conventions."""
import math


def normals_for_ue(mesh):
    # Accumulate across material boundaries and UV duplicates, but never weld
    # unrelated coincident vertices (mouth, eyelids, clothing, etc.).
    vertices = mesh['vertices']
    mapping = mesh.get('converted_to_source_vertex', list(range(len(vertices))))
    sums = {}
    for indices in mesh['sections']:
        for start in range(0, len(indices), 3):
            ids = indices[start:start+3]
            a, b, c = (vertices[i] for i in ids)
            u = [b[k]-a[k] for k in range(3)]
            v = [c[k]-a[k] for k in range(3)]
            # UE uses edge2.Cross(edge1); reversing the preview triangles below
            # therefore gives this outward normal.
            n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            for i in ids:
                total = sums.setdefault(mapping[i], [0., 0., 0.])
                for k in range(3): total[k] += n[k]
    result = []
    for key in mapping:
        n = sums.get(key, [0., 0., 1.])
        length = math.sqrt(sum(x*x for x in n))
        result.append([x/length for x in n] if length > 1e-20 else [0., 0., 1.])
    return result


def ue_triangles(indices, remap):
    return [(remap[indices[i]], remap[indices[i+2]], remap[indices[i+1]])
            for i in range(0, len(indices), 3)]
