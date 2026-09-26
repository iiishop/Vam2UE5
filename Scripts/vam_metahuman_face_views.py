"""Local geometry-only face comparisons, identical smooth shading and cameras.

This diagnostic never changes an Unreal asset and does not certify likeness.
"""
import json, sys, math
from pathlib import Path
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'Saved/Python'))
import numpy as np
from PIL import Image, ImageDraw
folder = root / 'Saved/MetaHuman/FitRepair'
name = sys.argv[1] if len(sys.argv) > 1 else 'refined_posed'
raw = json.loads((folder / 'old_target.json').read_text())
source = np.asarray(raw['vertices'])[:, [1, 0, 2]] * [-1, 1, 1]
st = np.asarray(raw['triangles']).reshape(-1, 3)[:, [0, 2, 1]]
raw = json.loads((folder / (name + '.json')).read_text())
fit = np.asarray(raw['vertices'])[:, [0, 2, 1]]
ft = np.asarray(raw['triangles']).reshape(-1, 3)
corner_normals=None
if '--skin-only' in sys.argv:
    section_path=folder/(name.removesuffix('_component')+'-sections.json')
    if not section_path.exists():section_path=folder/'face-sections.json'
    sections=json.loads(section_path.read_text(encoding='utf8'))['sections']
    skin=[s for s in sections if s['slot']=='head_shader_shader']
    assert len(skin)==1,'Missing unambiguous head skin section'
    ft=np.asarray(skin[0]['triangles']).reshape(-1,3)
    if 'corner_normals' in skin[0]:
        corner_normals=np.asarray(skin[0]['corner_normals']).reshape(-1,3,3)@np.asarray(raw['normal_rotation'])
    name += '_skin'

def render(v, t, yaw, pitch, corner_normals=None):
    a, b = np.radians([yaw, pitch])
    right = np.array([np.cos(a), -np.sin(a), 0])
    forward = np.array([np.sin(a)*np.cos(b), np.cos(a)*np.cos(b), np.sin(b)])
    up = np.cross(right, forward)
    basis = np.array([right, forward, up]).T
    q = (v - [0, 2, 165]) @ basis
    normals = np.zeros_like(v)
    fn = np.cross(v[t[:, 2]]-v[t[:, 0]], v[t[:, 1]]-v[t[:, 0]])
    for i in range(3): np.add.at(normals, t[:, i], fn)
    normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1e-9)
    light = np.array([-.35, .85, .4]); light /= np.linalg.norm(light)
    # Absolute cosine removes winding as a lighting confound in this comparison.
    colors = 65 + 175 * np.abs((normals @ basis) @ light)
    corner_colors=None if corner_normals is None else 65+175*np.abs((corner_normals@basis)@light)
    size = 420; xy = q[:, [0, 2]] * [13, -13] + [210, 230]
    image = np.full((size, size, 3), [34, 39, 45], dtype=np.uint8)
    depth = np.full((size, size), -np.inf)
    for triangle_index,ids in enumerate(t):
        if v[ids, 2].max() < 149: continue
        p = xy[ids]
        lo = np.maximum(np.floor(p.min(0)).astype(int), 0)
        hi = np.minimum(np.ceil(p.max(0)).astype(int), size-1)
        if np.any(lo > hi): continue
        x,y = np.meshgrid(np.arange(lo[0], hi[0]+1)+.5, np.arange(lo[1], hi[1]+1)+.5)
        den = (p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
        if abs(den) < 1e-10: continue
        a = ((p[1,1]-p[2,1])*(x-p[2,0])+(p[2,0]-p[1,0])*(y-p[2,1]))/den
        b = ((p[2,1]-p[0,1])*(x-p[2,0])+(p[0,0]-p[2,0])*(y-p[2,1]))/den
        c = 1-a-b; z = a*q[ids[0],1]+b*q[ids[1],1]+c*q[ids[2],1]
        old = depth[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
        mask = (a >= 0)&(b >= 0)&(c >= 0)&(z > old)
        shades=colors[ids] if corner_colors is None else corner_colors[triangle_index]
        shade = np.clip(a*shades[0]+b*shades[1]+c*shades[2],0,255).astype(np.uint8)
        old[mask] = z[mask]
        image[lo[1]:hi[1]+1,lo[0]:hi[0]+1][mask] = shade[mask,None]
    return Image.fromarray(image)

views = [(0,0),(-45,0),(45,0),(-90,0),(90,0),(0,25),(0,-20)]
sheet = Image.new('RGB', (840, 420*len(views)))
for row,(yaw,pitch) in enumerate(views):
    for col,(v,t,label) in enumerate([(source,st,'SOURCE'),(fit,ft,name)]):
        tile = render(v,t,yaw,pitch,corner_normals if col==1 else None)
        ImageDraw.Draw(tile).text((10,10),f'{label} yaw={yaw} pitch={pitch}',fill='white')
        sheet.paste(tile,(420*col,420*row))
path = folder / (name + '_face_views.png'); sheet.save(path); print(path)
