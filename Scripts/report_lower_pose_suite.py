"""Local extrema, exact-pose repeat checks, and a browsable real-UE screenshot index."""

import json,sys,html,hashlib

from pathlib import Path

import numpy as np
from PIL import Image

root=Path(sys.argv[1]).resolve();suite=json.loads((root/'suite.json').read_text(encoding='utf-8-sig'));result={'states':[],'orientation_checks':[]};cached={}

def points(p):return np.array(json.loads(p.read_text(encoding='utf-8-sig'))['positions'],dtype=float)

for state in suite['states']:

    bundle=Path(state['audit_bundle']);final=points(bundle/'04-jiggle/surface.json');cached[state['id']]=final

    bones={b['bone']:np.array(b['component_matrix']).reshape(4,4)[3,:3] for b in state['actual_bones']}

    rois={}

    for side,prefix in [('left','l'),('right','r')]:

        if prefix+'Thigh' in bones:rois[side+'-hip']=(bones[prefix+'Thigh'],22)

        if prefix+'Shin' in bones:rois[side+'-knee']=(bones[prefix+'Shin'],15)

        if prefix+'Shin' in bones and prefix+'Foot' in bones:rois[side+'-calf']=((bones[prefix+'Shin']+bones[prefix+'Foot'])/2,18)

    record={'id':state['id'],'acceptance':state['acceptance'],'limited':state['limited'],'regions':{},'audit_bundle':str(bundle),'image_count':len(list((root/state['id']).glob('*.png')))}

    for name,(center,radius) in rois.items():

        ids=np.flatnonzero(np.linalg.norm(final-center,axis=1)<=radius)

        if not len(ids):continue

        item={'vertices':len(ids),'bounds_cm':{'min':final[ids].min(0).tolist(),'max':final[ids].max(0).tolist()},'radius_cm':radius,'stage_deltas':{}}

        previous=None

        for stage in ['01-final-weights','02-static-helpers','03-corrective','04-jiggle']:

            current=points(bundle/stage/'surface.json')

            if previous is not None:

                d=np.linalg.norm(current[ids]-previous[ids],axis=1);worst=np.argsort(d)[-8:][::-1]

                item['stage_deltas'][stage]={'maximum_cm':float(d.max()),'p95_cm':float(np.percentile(d,95)),'worst_render_vertices':[{'index':int(ids[i]),'delta_cm':float(d[i])} for i in worst]}

            previous=current

        record['regions'][name]=item

    record['silhouette_rows']={}
    for view in ['side','rear','front','oblique']:
        pixels=np.array(Image.open(root/state['id']/(view+'-silhouette.png')).convert('RGB'))
        mask=pixels.min(axis=2)>64
        record['silhouette_rows'][view]=[{'row':int(y),'left':int(np.flatnonzero(row)[0]),'right':int(np.flatnonzero(row)[-1])} for y,row in enumerate(mask) if row.any()]
    result['states'].append(record)

for name in ['seat-sideways','seat-prone']:

    if name in cached and 'suspended-seat' in cached:

        d=np.linalg.norm(cached[name]-cached['suspended-seat'],axis=1)

        result['orientation_checks'].append({'case':name,'reference':'suspended-seat','space':'body_component_cm','maximum_cm':float(d.max()),'p95_cm':float(np.percentile(d,95)),'note':'No dynamics: orientation should not change local surface. This does not validate gravity dynamics.'})

result['image_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.glob('*/*.png'))}

(root/'metrics.json').write_text(json.dumps(result,indent=2),encoding='utf-8')

parts=['<!doctype html><meta charset="utf-8"><title>A2 Pose Suite</title><style>body{background:#171b20;color:#eee;font:16px system-ui;margin:28px}img{width:24%;min-width:180px}section{border-top:1px solid #555;margin:28px 0}a{color:#8cd}code{color:#fd9}</style><h1>A2 · 姿态与截图规范</h1><p>真实 UE 截图。每行依次为最终材质、灰模、轮廓、线框；请求角度不代表实际角度。限位和求解失败均保留。</p><p><a href="metrics.json">局部极值与朝向一致性</a> · <a href="suite.json">实际骨骼矩阵与相机</a></p>']

blocked=[s['id'] for s in result['states'] if s['acceptance'].startswith('blocked')]
parts.append('<p><strong>Captured '+str(len(result['states']))+' states / '+str(len(result['image_sha256']))+' images. Contact acceptance blocked: '+html.escape(', '.join(blocked) or 'none')+'</strong></p>')
if (root/'replay-check.json').exists():parts.append('<p><a href="replay-check.json">Independent replay comparison</a></p>')
for s in result['states']:

    name=s['id'];parts.append('<section><h2>'+html.escape(name)+'</h2><code>'+s['acceptance']+'</code> · <a href="'+name+'/state.json">状态/关节限位</a>')

    for view in ['side','rear','front','oblique']:

        parts.append('<h3>'+view+'</h3>')

        for mode in ['material','gray','silhouette','wireframe']:

            path=name+'/'+view+'-'+mode+'.png';parts.append('<a href="'+path+'"><img loading="lazy" src="'+path+'" alt="'+view+' '+mode+'"></a>')

    parts.append('</section>')

(root/'compare.html').write_text(''.join(parts),encoding='utf-8');print(json.dumps({'states':len(result['states']),'images':len(result['image_sha256']),'acceptance':{s['id']:s['acceptance'] for s in result['states']}},indent=2))

