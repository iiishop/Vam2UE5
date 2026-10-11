import unreal as u,json,os,time,hashlib,traceback
from pathlib import Path
r=Path(os.environ['VAM_S1S2_SHAPE_ROOT']);bundle=json.loads((r.parent/'s1s2-v5/bundle.json').read_text());params=json.loads((r.parent/'supported-shape-parameters.json').read_text(encoding='utf8'));param=next(p for p in params if p['name']=='a07 - Body');u.EditorPythonScripting.set_keep_python_script_alive(True);u.EditorLoadingAndSavingUtils.new_blank_map(False);actor=u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(u.load_class(None,bundle['blueprint']+'_C'),u.Vector());char=actor.get_editor_property('character');char.load_character();cases=[(v,p) for v in [.75,.9,1.] for p in ['Neutral standing','Flexion 60']];index=0;frames=0;rows=[];start=time.monotonic()
def tick(dt):
 global index,frames
 try:
  assert time.monotonic()-start<240,'shape audit timeout'
  body=char.get_editor_property('body')
  if not body:return
  if index==len(cases):
   (r/'status.json').write_text(json.dumps(dict(status='complete',count=len(rows),scope='Real normal BP in Editor; supported parameter bounds; A1 geometry, no alternative-shape VaM fidelity claim'),indent=2));u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor();return
  value,pose=cases[index]
  if frames==0:
   actor.set_breast_contact_enabled(False);actor.set_lower_body_contact_enabled(False)
   for key in ['jiggle_enabled','glute_jiggle_enabled','leg_jiggle_enabled']:body.set_editor_property(key,False)
   actor.reset_deformation_audit_state();body.glute_pose_command('Neutral standing');assert char.set_parameter(param['target'],value),'parameter rejected';body.glute_pose_command(pose)
  frames+=1
  if frames<45:return
  assert not char.get_editor_property('last_shape_error'),char.get_editor_property('last_shape_error')
  actor.export_deformation_audit();path=actor.get_editor_property('deformation_audit_status').removeprefix('Capturing:');assert Path(path,'manifest.json').is_file(),path
  validation=hashlib.sha256(json.dumps([bundle['identity'],param['target'],value,pose]).encode()).hexdigest();rows.append(dict(parameter=param,value=value,pose=pose,validation_id=validation,audit_bundle=path));(r/'variants.json').write_text(json.dumps(rows,indent=2));index+=1;frames=0
 except Exception:
  (r/'status.json').write_text(json.dumps(dict(status='failed',error=traceback.format_exc()),indent=2));u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()
handle=u.register_slate_post_tick_callback(tick)
