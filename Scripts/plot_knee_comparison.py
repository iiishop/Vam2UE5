"""Scientific sections from captured surfaces, explicitly not UE screenshots."""
import json, sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from compare_c15_asset_replay import read, surface

r=Path(sys.argv[1]); masks=np.load(r/'baseline/c2-region-masks.npz')
records=[{(x['pose'],x['mode']):x for x in read(r/p/'ue-replays.json')} for p in ['baseline','replay']]
ids=masks['source_ids']; region=set(int(v) for v in ids[np.maximum(masks['0_knee_anterior'],masks['0_knee_posterior'])>.05])
fig,axes=plt.subplots(1,4,figsize=(14,4.5),constrained_layout=True)
for ax,pose in zip(axes,['knee30','knee60','knee90','knee120']):
    rows=[p[(pose,'off')] for p in records]; bundles=[Path(x['audit_bundle']) for x in rows]
    capture=Path(rows[0]['source_capture']); meta=read(capture)
    raw=np.fromfile(capture.parent/meta['raw']['file'],dtype='<f4').reshape(-1,3)[:,[2,0,1]]*100
    target={int(s):raw[i] for i,s in enumerate(ids)}
    mapping=read(bundles[1]/'mapping.json');source=[mapping['input_to_source'][i] for i in mapping['render_to_input']]
    triangles=np.asarray(read(bundles[1]/'03-corrective/inputs.json')['triangle_indices']).reshape(-1,3)
    faces=[tuple(source[i] for i in t) for t in triangles if all(source[i] in region for i in t)]
    plane=float(np.median([target[s][1] for s in region]))
    for points,label,color in zip([target]+[surface(p,'03-corrective') for p in bundles],['VaM core-off raw','UE before','UE source-operator fix'],['#122b40','#e65b45','#0c9d90']):
        segments=[]
        for f in faces:
            if not all(s in points for s in f):continue
            p=np.array([points[s] for s in f]);cross=[]
            for a,b in [(0,1),(1,2),(2,0)]:
                u,v=p[a],p[b]
                if (u[1]-plane)*(v[1]-plane)<0:
                    t=(plane-u[1])/(v[1]-u[1]);cross.append((u+(v-u)*t)[[0,2]])
            if len(cross)==2:segments.append(cross)
        ax.add_collection(LineCollection(segments,colors=color,linewidths=1.5,label=label))
    ax.autoscale();ax.set_aspect('equal');ax.set_title(pose+' / same actual matrices');ax.set_xlabel('Component X (cm)');ax.grid(alpha=.18)
axes[0].set_ylabel('Component Z (cm)');axes[0].legend(fontsize=8,loc='best')
fig.suptitle('Left knee sagittal sections: retained source IDs, core off. Geometry plot, NOT a UE screenshot.',fontsize=11)
fig.savefig(r/'source-sections.png',dpi=160)
