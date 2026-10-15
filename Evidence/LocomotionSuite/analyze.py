import json,sys
from pathlib import Path
import numpy as np
r=Path(__file__).resolve().parent;take=sys.argv[1] if len(sys.argv)>1 else 'take-1';root=r/take/'continuous'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
d=read(root/'locomotion.json');state=read(root/'state.json');b=read(Path(state['audit_bundle'])/'04-jiggle/inputs.json')['bones'];names=[x['name'] for x in b]
m=np.array([s['bones'] for s in d['samples']]).reshape(-1,len(names),4,4);p=m[:,:,3,:3];t=np.array([s['time'] for s in d['samples']]);actor=np.array([s['actor'] for s in d['samples']]).reshape(-1,4,4)
front=np.array(d['target_front']);front[2]=0;front/=np.linalg.norm(front);up=np.array([0,0,1.]);right=np.cross(up,front)
lr=p[:,names.index('rThigh')]-p[:,names.index('lThigh')];lr[:,2]=0;lr/=np.linalg.norm(lr,axis=1)[:,None];hf=np.cross(lr,up);hf*=np.sign(np.mean(hf@front));yaw=np.degrees(np.arctan2(hf@right,hf@front))
torso=p[:,names.index('neck')]-p[:,names.index('hip')];lean=np.degrees(np.arctan2(torso@right,torso@up));head=p[:,names.index('head')]-p[:,names.index('neck')];hl=np.degrees(np.arctan2(head@right,head@up))
stats={}
for name,lo,hi in [('idle',0,1),('walk',2,5),('turn',5,7),('run',8,11),('jump',12.45,13.25),('crouch',16.3,17.5),('final',22,24)]:
 sel=(t>=lo)&(t<hi)
 if not sel.any():continue
 stats[name]={'pelvis_yaw_mean_deg':float(yaw[sel].mean()),'pelvis_yaw_min_deg':float(yaw[sel].min()),'pelvis_yaw_max_deg':float(yaw[sel].max()),'torso_lateral_mean_deg':float(lean[sel].mean()),'head_neck_lateral_mean_deg':float(hl[sel].mean())}
result={'take':take,'frames':len(t),'duration':len(t)/30,'world':state['world_type'],'finite_matrices':bool(np.isfinite(m).all()),'heading_metrics':stats,'source_front':d['source_front'],'target_front':d['target_front'],'distance_cm':d['samples'][-1]['distance_cm'],'jump_height_cm':max(s['jump_height_cm'] for s in d['samples']),'turn_degrees':max(s['heading_degrees'] for s in d['samples']),'crouch_reached':max(s['crouch_alpha'] for s in d['samples']),'retarget_clamp_count':d['retarget_clamp_count'],'max_clamp_degrees':d['max_clamp_degrees'],'glute_jiggle':d['glute_jiggle'],'leg_jiggle':d['leg_jiggle']}
result['node_peak_cm']={k:float(max(np.linalg.norm(n) for s in d['samples'] for a in s[k] for n in a['nodes_cm'])) for k in ['leg','glute']}
result['limit_corrections']={k:max(a['limits'] for s in d['samples'] for a in s[k]) for k in ['leg','glute']}
(r/(take+'-analysis.json')).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
