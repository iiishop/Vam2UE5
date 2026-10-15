import json,numpy as np
from pathlib import Path
r=Path(__file__).resolve().parent;read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));coverage=read(r/'coverage.json');results={}
for case in sorted(list((r/'runtime').glob('*/video.json'))+list((r/'probe').glob('*/video.json'))):
 samples=read(case)['samples'];state=read(case.parent/'state.json');bundle=Path(state['audit_bundle']);inp=read(bundle/'04-jiggle/inputs.json');mapping=read(bundle/'mapping.json');ids=np.array([mapping['input_to_source'][i] for i in mapping['render_to_input']]);weights=inp['render_vertex_influences'];names=[b['name'] for b in inp['bones']]
 times=np.array([x['time_seconds'] for x in samples]);steady=(times>=1)&(times<2.9);late=times>=5
 rows={};matrices={}
 for si in range(4):
  seg=[s['leg_pipeline'][si] for s in samples];nodes=[]
  assert set(s['steps'] for s in seg)=={4} and set(s['samples'] for s in seg)=={2}
  for ni in range(5):
   ns=[s['nodes'][ni] for s in seg];raw=np.array([n['raw_cm'] for n in ns]);final=np.array([n['final_cm'] for n in ns]);mat=np.array([n['matrix'] for n in ns]).reshape(-1,4,4);matrices[ns[0]['bone']]=mat
   assert np.max(abs(mat[:,:3,:3]-mat[0,:3,:3]))<1e-6
   nodes.append(dict(bone=ns[0]['bone'],raw_static_cm=raw[-1].tolist(),final_static_cm=final[-1].tolist(),raw_steady_peak_to_peak_cm=np.ptp(raw[steady],axis=0).tolist(),final_steady_peak_to_peak_cm=np.ptp(final[steady],axis=0).tolist(),max_travel_ratio=max(n['nonlinear_travel'] for n in ns),passive_tension=ns[0]['tension'],frequency_hz=(np.sqrt(np.array(ns[0]['support'])/ns[0]['mass'])/(2*np.pi)).tolist(),effective_damping_ratio=(np.array(ns[0]['damping'])/(2*np.sqrt(ns[0]['mass']*np.array(ns[0]['support'])))).tolist()))
   ratio=abs(raw[-1])/np.array(ns[-1]['travel']);t=np.maximum(0,(ratio-.65)/.35)
   nodes[-1]['static_ratio']=ratio.tolist();nodes[-1]['interior_tangent_stiffness_multiplier']=(1+6*ratio**2+40*(t*t+2*ratio*t/.35)).tolist()
  rows[seg[0]['name']]=dict(steps_per_frame=4,samples=2,limit_corrections=max(s['limits'] for s in seg),gravity_residual=seg[-1]['gravity_residual'],max_input_acceleration=max(float(np.linalg.norm(s['acceleration'])) for s in seg),nodes=nodes)
 disp=np.zeros((len(samples),len(weights),3))
 for name,mat in matrices.items():
  bi=names.index(name);w=np.array([dict(row).get(bi,0) for row in weights]);disp+=(mat[:,3,:3]-mat[0,3,:3])[:,None,:]*w[None,:,None]
 surface={}
 for prefix,members in coverage['source_ids'].items():
  unique={int(s):i for i,s in enumerate(ids) if int(s) in members};ix=list(unique.values());d=disp[:,ix];steadydev=d[steady]-d[steady].mean(0);maxdev=np.linalg.norm(steadydev,axis=2).max(0)
  surface[prefix]=dict(vertex_count=len(ix),steady_rms_cm=float(np.sqrt(np.mean(np.sum(steadydev**2,axis=2)))),steady_peak_deviation_p95_cm=float(np.percentile(maxdev,95)),steady_peak_deviation_max_cm=float(maxdev.max()),late_rms_cm=float(np.sqrt(np.mean(np.sum((d[late]-d[late].mean(0))**2,axis=2)))))
 results[case.parent.name]=dict(nodes=rows,surface=surface,scope='True PIE recorded solver states; surface temporal displacement reconstructed from final quantized skin weights and actual helper translations. Fixed pose/morph, rotation checked constant. Does not claim GPU readback of each video frame.')
(r/'analysis.json').write_text(json.dumps(results,indent=2));print(json.dumps({k:v['surface'] for k,v in results.items()},indent=2))
