"""Read actual limb ratios from explicitly published configurations; no sample paths."""
import json,math,os
from pathlib import Path
import unreal as u
spec=json.loads(Path(os.environ['VAM_BINDING_PROBE_SPEC']).read_text(encoding='utf8'));rows=[]
for path in spec['build_reports']:
 report=json.loads(Path(path).read_text(encoding='utf8'));assert report['status']=='committed'
 config=u.load_asset(report['configuration']);definition=config.get_editor_property('definition');rig=config.get_editor_property('rig')
 bones=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(definition.get_editor_property('body')))
 indices={b['name']:i for i,b in enumerate(bones)}
 def length(root,tip):
  parent=indices[str(rig.bone_for_semantic(root))];index=indices[str(rig.bone_for_semantic(tip))];value=0
  while index!=parent:
   assert index>=0,(root,tip)
   b=bones[index];value+=math.sqrt(sum(v*v for v in b['translation']));index=b['parent']
  return value
 arm=length('left_shoulder','left_hand');leg=length('left_hip','left_foot');torso=length('root','head')
 rows.append({'configuration':report['configuration'],'build_identity':report['identity'],'definition':definition.get_path_name(),'arm_chain_cm':arm,'leg_chain_cm':leg,'torso_head_chain_cm':torso,'leg_to_arm':leg/arm,'torso_to_leg':torso/leg})
assert len(rows)>=2 and len({r['definition'] for r in rows})>=2
assert any(abs(rows[0][key]-rows[1][key])>1.e-4 for key in ('leg_to_arm','torso_to_leg')),'Independent definitions do not demonstrate different proportions'
Path(spec['output']).write_text(json.dumps({'different_proportions_verified':True,'family_scope':'configured rig family only','subjects':rows},indent=2),encoding='utf8')
