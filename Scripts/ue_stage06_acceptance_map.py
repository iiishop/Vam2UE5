"""Create a cooked-compatible runtime acceptance map, separate from the interactive preview."""
import json
from pathlib import Path
import unreal as u
root='/Game/VamStage06';path=root+'/L_Stage06Automated'
assert not u.EditorAssetLibrary.does_asset_exist(path),path
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(root+'/BP_VamCharacter');assert bp
for y in (-100,100):sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,y,0))
sub.spawn_actor_from_class(u.VamStage06AcceptanceActor,u.Vector())
floor=sub.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6))
floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(u.Vector(8,8,.1))
world.get_world_settings().set_editor_property('default_game_mode',u.GameModeBase)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
report=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-build.json')
data=json.loads(report.read_text(encoding='utf8'));data['automated_map']=path
report.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
