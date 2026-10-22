"""Measure native source hair motion in head space; never infer motion from a sim toggle."""
import argparse,json
from pathlib import Path
import numpy as np

def audit(root):
    manifest=json.loads((root/'dynamic-manifest.json').read_text())
    frames=json.loads((root/'dynamic-samples.json').read_text())
    times=np.array([float(f['time']) for f in frames])
    if len(frames)<12 or np.any(np.diff(times)<=0):
        raise ValueError('Missing or nonmonotonic dynamic samples')
    result={'complete':(root/'complete.txt').exists(),'samples':len(frames),
            'duration_seconds':float(times[-1]-times[0]),'groups':[],
            'note':'Head-space centimetres. Diagnostic readback affects timing; not a performance benchmark.'}
    for gi,meta in enumerate(manifest['hairs']):
        local=[]
        for f in frames:
            g=f['groups'][gi]
            if str(g['enabled']).lower()!='true':raise ValueError('Hair simulation disabled')
            p=np.fromfile(root/g['particles'],dtype='<f4').reshape(-1,3)
            if not np.isfinite(p).all():raise ValueError('Nonfinite native particles')
            m=np.array(f['head_world'],float).reshape(4,4)
            local.append((np.c_[p,np.ones(len(p))]@np.linalg.inv(m).T)[:,:3]*100)
        local=np.array(local);segments=int(meta['segments'])
        if local.shape[1]%segments:raise ValueError('Particle/segment identity mismatch')
        tips=local[:,segments-1::segments]
        row={'path':meta['path'],'particles':local.shape[1],'strands':tips.shape[1],'phases':{}}
        for name in ('settle','motion','recovery'):
            ids=np.array([i for i,f in enumerate(frames) if f['phase']==name])
            if len(ids)<2:raise ValueError('Missing phase '+name)
            p=tips[ids];dt=np.diff(times[ids]);v=np.linalg.norm(np.diff(p,axis=0),axis=2)/dt[:,None]
            d=np.linalg.norm(p-p[0],axis=2)
            row['phases'][name]={'tip_excursion_max_cm':float(d.max()),
                'tip_speed_p95_cm_s':float(np.percentile(v,95)),
                'tip_speed_final_median_cm_s':float(np.median(v[-min(5,len(v)):]))}
        result['groups'].append(row)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    r=audit(a.capture);a.output.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(r,ensure_ascii=False,indent=2))
