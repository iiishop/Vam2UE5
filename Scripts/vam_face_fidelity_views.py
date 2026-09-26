"""Deterministic geometry-only comparison, shared source-derived camera frame."""
import numpy as np
from PIL import Image, ImageDraw
from vam_face_fidelity import VIEWS, view_matrix
from vam_face_surface import normals


def render(vertices, triangles, rotation, center, radius, size=384):
    q = (vertices-center)@rotation.T
    xy = q[:, :2]*[.43*size/radius, -.43*size/radius]+size/2
    n, _ = normals(vertices, triangles)
    light = np.array([-.35, .4, .85]); light /= np.linalg.norm(light)
    colors = 65+175*np.abs((n@rotation.T)@light)
    image = np.full((size, size, 3), [34, 39, 45], np.uint8)
    depth = np.full((size, size), -np.inf)
    # The same integer bounding-box rejection as below, batched for closeups.
    # Keep full-mesh normals: cropping triangles must not change shading.
    projected=xy[triangles]
    boxes_lo=np.maximum(np.floor(projected.min(axis=1)).astype(int),0)
    boxes_hi=np.minimum(np.ceil(projected.max(axis=1)).astype(int),size-1)
    visible=np.all(boxes_lo<=boxes_hi,axis=1)
    for ids in triangles[visible]:
        p = xy[ids]; lo = np.maximum(np.floor(p.min(0)).astype(int), 0)
        hi = np.minimum(np.ceil(p.max(0)).astype(int), size-1)
        if np.any(lo > hi): continue
        x, y = np.meshgrid(np.arange(lo[0], hi[0]+1)+.5, np.arange(lo[1], hi[1]+1)+.5)
        den = (p[1, 1]-p[2, 1])*(p[0, 0]-p[2, 0])+(p[2, 0]-p[1, 0])*(p[0, 1]-p[2, 1])
        if abs(den) < 1e-12: continue
        a = ((p[1, 1]-p[2, 1])*(x-p[2, 0])+(p[2, 0]-p[1, 0])*(y-p[2, 1]))/den
        b = ((p[2, 1]-p[0, 1])*(x-p[2, 0])+(p[0, 0]-p[2, 0])*(y-p[2, 1]))/den
        c = 1-a-b; z = a*q[ids[0], 2]+b*q[ids[1], 2]+c*q[ids[2], 2]
        old = depth[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        mask = (a >= 0)&(b >= 0)&(c >= 0)&(z > old)
        shades = colors[ids]
        shade = np.clip(a*shades[0]+b*shades[1]+c*shades[2], 0, 255).astype(np.uint8)
        old[mask] = z[mask]; image[lo[1]:hi[1]+1, lo[0]:hi[0]+1][mask] = shade[mask, None]
    return Image.fromarray(image)


def comparison(source, fitted, semantic_map, output, label='RESIDUAL DRAFT', tile_size=384):
    sv = np.asarray(source['vertices']); tv = np.asarray(fitted['vertices'])
    st = np.asarray(source['triangles']).reshape(-1, 3)[semantic_map['source_face_triangles']]
    tt = np.asarray(fitted['triangles']).reshape(-1, 3)[semantic_map['target_face_triangles']]
    points = sv[np.unique(st)]
    center = (points.max(0)+points.min(0))/2
    radius = max(float(np.linalg.norm(points-center, axis=1).max()), 1e-6)
    size = int(tile_size)
    if not 128<=size<=2048:raise ValueError('InvalidComparisonResolution')
    sheet = Image.new('RGB', (size*2, size*len(VIEWS)))
    for row, view in enumerate(VIEWS):
        display_st=np.asarray(source['triangles']).reshape(-1,3) if semantic_map.get('display_full_geometry') else st
        display_tt=np.asarray(fitted['triangles']).reshape(-1,3) if semantic_map.get('display_full_geometry') else tt
        for col, (v, t, title) in enumerate(((sv, display_st, 'SOURCE p0'), (tv, display_tt, label))):
            tile = render(v, t, view_matrix(*view), center, radius, size)
            ImageDraw.Draw(tile).text((8, 8), f'{title} yaw={view[0]} pitch={view[1]}', fill='white')
            sheet.paste(tile, (col*size, row*size))
    sheet.save(output)
    return {'views': VIEWS, 'center': center.tolist(), 'radius': radius,'tile_size':size,
            'projection': 'orthographic', 'scope': 'geometry only, skin regions, no material or rig verification'}
