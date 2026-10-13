import json,subprocess,sys
from pathlib import Path
import numpy as np
r=Path(__file__).resolve().parent;repo=r.parents[2]
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
recipe=read(r/'v3/editor-recipe.json');recipe['cases']+=read(r/'v3/seated-recipe.json')['cases'];(r/'v3/pie-static-recipe.json').write_text(json.dumps(recipe,indent=2))
for world,recipe in [('Editor','editor-recipe.json'),('PIE','pie-static-recipe.json')]:
 out=r/('main-'+world.lower())
 cmd=[sys.executable,'-X','utf8',str(repo/'Scripts/run_s1s2_pose_suite.py'),str(r/'v3/bundle.json'),str(out),'--world',world,'--recipe',str(r/'v3'/recipe),'--light-from-below','--project','I:/Document/UE5/SmartNPC/SmartNPC.uproject']
 subprocess.run(cmd,cwd=repo,check=True)
 assert 'Result={Success} Name={ContactRuntime}' in (out/'runtime.log').read_text(errors='replace')
rows=[]
for state in sorted((r/'main-editor').glob('*/state.json')):
 a=read(state);b=read(r/'main-pie'/state.parent.name/'state.json');assert a['world_type']=='Editor' and b['world_type']=='PIE'
 assert a['cameras']==b['cameras']
 bone_error=max(float(np.max(abs(np.array(x['component_matrix'])-y['component_matrix']))) for x,y in zip(a['actual_bones'],b['actual_bones']))
 points=lambda s:np.array(read(Path(s['audit_bundle'])/'03-corrective/surface.json')['positions'])
 error=float(np.linalg.norm(points(a)-points(b),axis=1).max());assert error<1e-6 and bone_error<1e-9
 rows.append(dict(pose=state.parent.name,max_surface_error_cm=error,max_bone_matrix_error=bone_error,editor_actor=a['actor_class'],pie_actor=b['actor_class'],screenshots_per_world=len(list(state.parent.glob('*.png')))))
(r/'main-validation.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'matched Editor/PIE poses plus seated jiggle in main project')
