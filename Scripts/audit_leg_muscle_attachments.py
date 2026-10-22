"""Same-frame attachment, topology seam and geometric fold audit using A1 exports."""
import argparse,json
from pathlib import Path
import numpy as np
from collections import defaultdict
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--groups',nargs='+',default=['R','S','M','C']);p.add_argument('--actions',nargs='+',default=['flexion','knee-flexion','seated-stop']);p.add_argument('--prefix',default='');p.add_argument('--reference',default='R');p.add_argument('--frames',nargs='+',type=int,default=[45,135]);p.add_argument('--primary-leg-bones',nargs='*',default=[]);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
rows=[];paths=[];checks=[]
for action in a.actions:
 videos={g:read(a.run/g/(g+'-'+action)/'video.json')['samples'] for g in a.groups}
 state=read(a.run/a.reference/(a.reference+'-'+action)/'state.json');ids=[i for i,b in enumerate(state['actual_bones']) if not b['bone'].startswith(('L_','R_'))]
 for g,frames in videos.items():
  gs=read(a.run/g/(g+'-'+action)/'state.json')
  for key in ['cameras','body_to_world','light_from_below','light_yaw_offset','exposure_bias','width','height','temporal_aa','motion_blur','fixed_dt','settle_frames']:assert gs[key]==state[key],(g,action,key)
  assert len(frames)==len(videos[a.reference])
  assert all(x['root_offset_cm']==y['root_offset_cm'] and x['time_seconds']==y['time_seconds'] for x,y in zip(frames,videos[a.reference]))
  if 'camera_matrix' in frames[0]:assert all(x['camera_matrix']==y['camera_matrix'] for x,y in zip(frames,videos[a.reference]))
  error=max(float(np.max(np.abs(np.array(x['bone_matrices'])[ids]-np.array(y['bone_matrices'])[ids]))) for x,y in zip(frames,videos[a.reference]))
  assert error<1e-6;checks.append(dict(group=g,action=action,primary_error=error,contact_frames=sum(f['contact_solver_active'] for f in frames)))
 for frame in a.frames:
  rd=Path(videos[a.reference][frame]['audit_bundle']);data=read(rd/'04-jiggle/inputs.json');mapping=read(rd/'mapping.json');tri=np.array(data['triangle_indices']).reshape(-1,3)
  baseline=np.array(read(rd/'04-jiggle/surface.json')['positions']);boneids={i for i,b in enumerate(data['bones']) if '_Thigh_' in b['name'] or '_Calf_' in b['name'] or b['name'] in a.primary_leg_bones}
  mask=np.array([sum(w for b,w in inf if b in boneids)>.05 for inf in data['render_vertex_influences']]);tm=mask[tri].any(axis=1)
  keys=[('source',mapping['input_to_source'][i]) if mapping['input_to_source'][i]>=0 else ('input',i) for i in mapping['render_to_input']]
  groups=defaultdict(list)
  for i,k in enumerate(keys):
   if mask[i]:groups[k].append(i)
  pairs=np.array([(v[0],q) for v in groups.values() for q in v[1:]],dtype=int)
  edge=defaultdict(list)
  for t,(x,y,z) in enumerate(tri):
   if not tm[t]:continue
   for u,v in [(x,y),(y,z),(z,x)]:edge[tuple(sorted((keys[u],keys[v])))].append(t)
  adjacent=np.array([v for v in edge.values() if len(v)==2],dtype=int)
  def geometry(pos):
   cross=np.cross(pos[tri[:,1]]-pos[tri[:,0]],pos[tri[:,2]]-pos[tri[:,0]]);area=np.linalg.norm(cross,axis=1);norm=cross/np.maximum(area[:,None],1e-12)
   ang=np.degrees(np.arccos(np.clip((norm[adjacent[:,0]]*norm[adjacent[:,1]]).sum(axis=1),-1,1)))
   return area,norm,ang
  ra,rn,re=geometry(baseline)
  s_surface=np.array(read(Path(videos['S'][frame]['audit_bundle'])/'04-jiggle/surface.json')['positions']) if 'S' in videos else None
  for g,video in videos.items():
   d=Path(video[frame]['audit_bundle']);pos=np.array(read(d/'04-jiggle/surface.json')['positions']);assert np.isfinite(pos).all()
   inputs=read(d/'04-jiggle/inputs.json');assert inputs['triangle_indices']==data['triangle_indices'] and inputs['render_vertex_influences']==data['render_vertex_influences']
   assert [b['name'] for b in inputs['bones']]==[b['bone'] for b in state['actual_bones']]
   snapshot_error=float(np.max(np.abs(np.array([b['component_matrix_row_major'] for b in inputs['bones']])-np.array(video[frame]['bone_matrices']))))
   assert snapshot_error<1e-6,('A1 snapshot not same video pose',g,action,frame,snapshot_error)
   ar,norm,angles=geometry(pos);delta=np.linalg.norm(pos-baseline,axis=1)[mask];seam=np.linalg.norm(pos[pairs[:,0]]-pos[pairs[:,1]],axis=1)
   rows.append(dict(group=g,action=action,frame=frame,leg_vertices=int(mask.sum()),rms_vs_R_cm=float(np.sqrt(np.mean(delta**2))),max_vs_R_cm=float(delta.max()),seam_pairs=len(pairs),seam_max_cm=float(seam.max()),seam_over_001cm=int((seam>.01).sum()),edge_p95_deg=float(np.percentile(angles,95)),edge_max_deg=float(angles.max()),new_sharp_edges=int(((angles>45)&(angles-re>15)).sum()),opposed_normals_vs_R=int(((norm*rn).sum(axis=1)[tm]<0).sum()),area_below_tenth_R=int((ar[tm]/np.maximum(ra[tm],1e-12)<.1).sum()),audit=str(d)))
   flags=np.flatnonzero((angles>45)&(angles-re>15))
   rows[-1]['new_sharp_edge_details']=[dict(triangles=adjacent[e].tolist(),render_vertices=tri[adjacent[e]].tolist(),source_keys=[[keys[int(i)] for i in face] for face in tri[adjacent[e]]],reference_angle_deg=float(re[e]),current_angle_deg=float(angles[e]),center_cm=pos[tri[adjacent[e]]].mean(axis=(0,1)).tolist()) for e in flags]
   if a.reference!='R':
    for old,new in [('rms_vs_R_cm','rms_vs_reference_cm'),('max_vs_R_cm','max_vs_reference_cm'),('opposed_normals_vs_R','opposed_normals_vs_reference')]:rows[-1][new]=rows[-1].pop(old)
   if s_surface is not None:
    sd=np.linalg.norm(pos-s_surface,axis=1)[mask];rows[-1].update(rms_vs_S_cm=float(np.sqrt(np.mean(sd**2))),max_vs_S_cm=float(sd.max()))
 for frame in videos['M']:
  for seg in frame['leg_pipeline']:
   for node in seg['nodes']:
    m=node['muscle_path'];paths.append(dict(action=action,frame=frame['frame'],segment=seg['name'],bone=node['bone'],**m))
result=dict(reference_group=a.reference,comparability=checks,surfaces=rows,scope='A1 bone/morph surface, not GPU contact surface or rendered shading normals. Source-ID seams only; no nearest-neighbor matching. Sharp-edge flags require visual review, not anatomical truth.')
(a.run/(a.prefix+'analysis.json')).write_text(json.dumps(result,indent=2));(a.run/(a.prefix+'muscle-paths.json')).write_text(json.dumps(paths,indent=2))
print(json.dumps(rows,indent=2))
