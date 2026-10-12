"""Recover DAZSkinV2.InitMesh's distance-normalized bulge weights from IR.

The source asset stores dimensionless bulge masks. InitMesh divides them by
distance in Unity metres using the initial mesh and original bone positions.
Never use the morphed reference pose or apply this normalization twice.
"""
import copy
import math


def runtime_skin_nodes(ir, skin):
    if not skin['nodes']:
        return []
    mesh_id = str(skin['dazMesh']['m_PathID'])
    mesh = next(r['mesh'] for r in ir['records']
                if r.get('kind') == 'unity_mesh' and str(r['object']) == mesh_id)
    bones = {b['name']: b.get('base_position', b['position']) for b in ir['skeleton']}
    mapping = list(range(len(mesh['vertices']))) + [-1] * (len(mesh['uv']) - len(mesh['vertices']))
    for alias in mesh['uv_map']:
        mapping[alias['tovert']] = alias['fromvert']
    result = copy.deepcopy(skin['nodes'])
    for node in result:
        center = bones[node['name']]
        for row in node['weights']:
            source = mapping[row['vertex']]
            if source < 0:
                raise ValueError('Missing source UV mapping')
            position = mesh['vertices'][source]
            distance = math.sqrt(sum((float(a) - float(b)) ** 2 for a, b in zip(position, center)))
            if not math.isfinite(distance) or distance <= 1e-12:
                raise ValueError('Invalid source bulge distance: %s vertex %d' % (node['name'], row['vertex']))
            for axis in 'xyz':
                for side in ('left', 'right'):
                    row[axis + side + 'bulge'] /= distance
    return result
