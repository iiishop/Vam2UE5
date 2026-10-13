"""Analyze recorded UE solver states; no synthetic or offline-rendered evidence."""
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
result={'scope':'Actual PIE matrices and solver states. Dynamic statistics are helper-local displacement, not per-frame GPU surface readback. Static surface geometry comes from A1 exports.','runs':{},'paired':{}}
cache={}
for label,folder in [('before','before-v3'),('after','after-v3')]:
    rows={}
    for path in sorted((ROOT/folder).glob('*/video.json')):
        data=read(path);samples=data['samples'];t=np.array([s['time_seconds'] for s in samples])
        state=read(path.parent/'state.json');bundle=Path(state['audit_bundle'])
        inputs=read(bundle/'04-jiggle/inputs.json');names=[b['name'] for b in inputs['bones']]
        matrices=np.array([s['bone_matrices'] for s in samples]);assert np.isfinite(matrices).all()
        stop=4 if path.parent.name in ['flexion','step'] else 3
        segments={}
        for si in range(4):
            seg=[s['leg_pipeline'][si] for s in samples]
            raw=np.array([[n['raw_cm'] for n in s['nodes']] for s in seg])
            final=np.array([[n['final_cm'] for n in s['nodes']] for s in seg])
            g=np.array([s['gravity_residual'] for s in seg]);assert np.isfinite(raw).all() and np.isfinite(final).all()
            equilibrium=final[-15:].mean(0);dev=np.linalg.norm(final-equilibrium,axis=2)
            segments[seg[0]['name']]={'max_raw_cm':float(np.linalg.norm(raw,axis=2).max()),'max_final_cm':float(np.linalg.norm(final,axis=2).max()),'moving_peak_deviation_cm':float(dev[(t>1)&(t<stop)].max()),'late_peak_deviation_cm':float(dev[t>=6].max()),'static_final_cm':equilibrium.tolist(),'max_gravity_residual_cm_s2':float(np.linalg.norm(g,axis=1).max()),'steps':sorted(set(s['steps'] for s in seg)),'input_samples':sorted(set(s['samples'] for s in seg)),'max_limit_corrections':max(s['limits'] for s in seg),'max_nonlinear_travel':max(n['nonlinear_travel'] for s in seg for n in s['nodes'])}
        rows[path.parent.name]={'frames':len(samples),'last_time':float(t[-1]),'world':state['world_type'],'actor_class':state['actor_class'],'diagnostic_preload':data['diagnostic_pose_preload'],'segments':segments}
        cache[label,path.parent.name]=(matrices,names,state,t)
    result['runs'][label]=rows
for case in result['runs']['before'].keys() & result['runs']['after'].keys():
    a,an,sa,ta=cache['before',case];b,bn,sb,tb=cache['after',case]
    bones=['hip','pelvis','lThigh','rThigh','lShin','rShin','lFoot','rFoot']
    delta=max(float(np.max(abs(a[:,an.index(n)]-b[:,bn.index(n)]))) for n in bones)
    result['paired'][case]={'actual_primary_matrix_max_abs_difference':delta,'times_identical':bool(np.array_equal(ta,tb)),'cameras_identical':sa['cameras']==sb['cameras'],'lighting_identical':all(sa[k]==sb[k] for k in ['light_from_below','light_yaw_offset','exposure_bias']),'note':'Matrix elements include centimeters and dimensionless rotation; max is an identity check, not a geometric RMS.'}
(ROOT/'motion-analysis.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result['paired'],indent=2))
