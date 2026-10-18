"""Dynamic body-proxy penetration, separate from actual skin-surface validation.

The proxies are read from the actual character at each GPU capture frame. This
does not substitute a static head mesh for the moving torso/shoulders.
"""
import argparse,json
from pathlib import Path
import numpy as np


def measure(capture,source,reference=None):
    rows=[]
    reference_frames={f['time']:f for f in reference['gpu_samples']} if reference else {}
    for frame in capture['gpu_samples']:
        proxy_frame=reference_frames.get(frame['time'],frame)
        for g in frame['groups']:
            for kind in ['guide','render']:
                p=np.asarray(g[kind+'_points'],float)
                if not np.isfinite(p).all():raise ValueError('Nonfinite GPU position')
                if kind=='guide':ids=np.arange(len(p));nodes=len(p)//g['guide_curves']
                else:
                    ids=np.asarray(g['render_point_ids']);counts=set(map(len,source['groups'][g['group']]['render_curves']))
                    if len(counts)!=1:raise ValueError('Variable render layout needs explicit offsets')
                    nodes=counts.pop()
                p=p[ids%nodes>=max(2,int(nodes*.1))]
                for region in ['head','body']:
                    proxies=[x for x in proxy_frame['collision_proxies_head_local'] if (x['bone']=='head')==(region=='head')]
                    if not proxies:continue
                    a=np.asarray([x['a'] for x in proxies]);b=np.asarray([x['b'] for x in proxies]);r=np.asarray([x['radius_cm'] for x in proxies])
                    edge=b-a;d=p[:,None,:]-a
                    t=np.clip((d*edge).sum(2)/np.maximum((edge*edge).sum(1),1e-20),0,1)
                    depth=(r-np.linalg.norm(d-t[:,:,None]*edge,axis=2)).max(1)
                    rows.append(dict(time=frame['time'],group=g['group'],kind=kind,region=region,
                                     count_over_02cm=int((depth>.2).sum()),max_depth_cm=float(max(0,depth.max())),samples=len(p)))
    summary={f'{kind}_{region}':dict(max_depth_cm=max(r['max_depth_cm'] for r in rows if r['kind']==kind and r['region']==region),
                 max_count_over_02cm=max(r['count_over_02cm'] for r in rows if r['kind']==kind and r['region']==region))
                 for kind in ['guide','render'] for region in ['head','body']}
    return dict(scope='Actual animated PhysicsAsset sphere/capsule union; GPU readback once/sec; root prefix excluded. Proxy penetration is not skin penetration.',common_reference_proxies=reference is not None,summary=summary,rows=rows)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--reference-proxies',type=Path,help='Same motion/time capture with full-volume body proxies for fair baseline comparison')
    a=p.parse_args();r=measure(json.loads(a.capture.read_text()),json.loads(a.source.read_text()),json.loads(a.reference_proxies.read_text()) if a.reference_proxies else None);a.output.write_text(json.dumps(r,indent=2));print(json.dumps(r['summary'],indent=2))
