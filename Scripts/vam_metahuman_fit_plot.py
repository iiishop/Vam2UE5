"""Render local mesh diagnostic projections, without editing user screenshots."""
import json
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'Saved/Python'))
import numpy as np
from PIL import Image, ImageDraw

folder = root/'Saved/MetaHuman/FitRepair'
names = sys.argv[1:] or ['old_target', 'old_body', 'old_face', 'archetype']
image = Image.new('RGB', (660*len(names), 840), '#20252b')
draw = ImageDraw.Draw(image)
for panel, name in enumerate(names):
    parts = [json.loads((folder/(part+'.json')).read_text(encoding='utf8')) for part in name.split('+')]
    vertices, triangles, offset = [], [], 0
    for data in parts:
        vertices.extend(data['vertices']); triangles.extend(i+offset for i in data['triangles'])
        offset += len(data['vertices'])
    v = np.asarray(vertices)
    if name in ('native','old_target'): v = v[:,[1,0,2]] * [-1,1,1]
    tris = np.asarray(triangles).reshape(-1,3)
    coords = v[tris]
    normal = np.cross(coords[:,1]-coords[:,0], coords[:,2]-coords[:,0])
    norm = np.linalg.norm(normal,axis=1)
    shade = np.abs(normal @ np.array([-.35,.8,.45]))/np.maximum(norm,1e-9)
    for i in np.argsort(coords[:,:,1].mean(axis=1)):
        # Same horizontal/vertical scale for proportions.
        points = [(panel*660+330+p[0]*4.0, 790-p[2]*4.0) for p in coords[i]]
        c = int(65+160*min(shade[i],1))
        draw.polygon(points, fill=(c,c,c))
    draw.text((panel*660+12,12), name, fill='white')
image.save(folder/'comparison.png')
print(folder/'comparison.png')
