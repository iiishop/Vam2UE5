"""Save a standalone Stage06 test level with two isolated characters."""
import json
from pathlib import Path
import unreal as u

root='/Game/VamStage06'
path=root+'/L_Stage06Preview'
assert not u.EditorAssetLibrary.does_asset_exist(path), 'Existing preview level protected: '+path
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(root+'/BP_VamCharacter')
assert bp
first=sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,-85,0))
second=sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,85,0))
first.set_actor_label('Stage06 Character A')
second.set_actor_label('Stage06 Character B')
tool=sub.spawn_actor_from_class(u.VamPreviewTool,u.Vector())
tool.set_editor_property('target',first)
camera=sub.spawn_actor_from_class(u.CameraActor,u.Vector(480,0,155),u.Rotator(pitch=-4,yaw=180,roll=0))
camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
camera.camera_component.set_field_of_view(65.)
for pitch,yaw,intensity in [(-35,150,4),(-25,-20,2)]:
    light=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=pitch,yaw=yaw,roll=0))
    light.light_component.set_intensity(intensity)
    light.light_component.set_mobility(u.ComponentMobility.MOVABLE)
floor=sub.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6))
floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(u.Vector(8,8,.1))
world.get_world_settings().set_editor_property('default_game_mode',u.GameModeBase)
world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
report=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-build.json')
data=json.loads(report.read_text(encoding='utf8'))
data['preview_map']=path
report.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
