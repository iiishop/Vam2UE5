"""Create a new isolated native-only runtime validation map."""
import json,os
from pathlib import Path
import unreal as u
data=Path(__file__).resolve().parents[1]/'Saved'
report_path=Path(os.environ.get('VAM_NATIVE_REPORT_FILE',str(data/'NativeBuild/latest-native-assets.json')))
report=json.loads(report_path.read_text(encoding='utf8'))
assert report['independent_reload_verified']
path=report['folder']+'/L_NativeValidation'
assert not u.EditorAssetLibrary.does_asset_exist(path),'Validation map exists; do not overwrite'
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(report['blueprint']);actor=sub.spawn_actor_from_class(bp.generated_class(),u.Vector())
actor.set_actor_label('Native VaM Character - source reference')
camera=sub.spawn_actor_from_class(u.CameraActor,u.Vector(300,-300,155),u.Rotator(pitch=-8,yaw=135,roll=0))
camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
light=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=-35,yaw=-30,roll=0));light.light_component.set_intensity(5)
fill=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=-25,yaw=150,roll=0));fill.light_component.set_intensity(2)
floor=sub.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6));floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(u.Vector(6,6,.1))
world.get_world_settings().set_editor_property('default_game_mode',u.GameModeBase)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
report['validation_map']=path
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_NATIVE_MAP_SAVED '+path)
