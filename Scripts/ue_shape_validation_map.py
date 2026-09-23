"""Create a disposable native-only two-instance shape acceptance scene."""
import json,os
from pathlib import Path
import unreal as u

receipt=os.environ.get('VAM_NATIVE_REPORT_FILE')
report=json.loads(Path(receipt).read_text(encoding='utf8')) if receipt else None
folder=report['folder'] if report else '/VamResourceBrowser/Examples/Stage05'
bp_path=report['blueprint'] if report else folder+'/BP_TestPanel'
path=folder+'/L_ShapeValidation'
assert not u.EditorAssetLibrary.does_asset_exist(path),'Existing validation scene protected: '+path
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(bp_path);assert bp
for i in range(2):
    actor=sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,(i-.5)*170,0))
    actor.set_actor_label('Shape instance '+str(i+1))
camera=sub.spawn_actor_from_class(u.CameraActor,u.Vector(520,100,115),u.Rotator(pitch=-3,yaw=180,roll=0))
camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
camera.camera_component.set_field_of_view(60.)
light=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=-35,yaw=150,roll=0));light.light_component.set_intensity(4)
fill=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=-25,yaw=-20,roll=0));fill.light_component.set_intensity(2)
for actor in (light,fill):actor.light_component.set_mobility(u.ComponentMobility.MOVABLE)
light.light_component.set_editor_property('forward_shading_priority',1)
fill.light_component.set_editor_property('forward_shading_priority',0)
floor=sub.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6));floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(u.Vector(8,8,.1))
world.get_world_settings().set_editor_property('default_game_mode',u.VamShapeTestGameMode)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
if report:
    report['shape_validation_map']=path
    Path(receipt).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_SHAPE_MAP_SAVED '+path)
