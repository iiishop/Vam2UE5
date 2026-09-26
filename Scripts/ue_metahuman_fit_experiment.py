"""Isolated local fit experiment. Never uploads, assembles, or edits the reference."""
import json
from pathlib import Path
import unreal as u

root = Path(__file__).resolve().parents[1]
out = root/'Saved/MetaHuman/FitRepair'
folder = '/Game/MetaHumans/启梦MH方向法线诊断'
assert not u.EditorAssetLibrary.does_directory_exist(folder), 'Existing experiment retained; choose explicit new version'
u.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
old = json.loads((root/'Saved/MetaHuman/Jobs/MH00-local-proof/target.json').read_text(encoding='utf8'))
# Native UE +X forward -> Creator mesh +Y forward. Proper rotation, not a reflection.
verts = [u.Vector(-v[1], v[0], v[2]) for v in old['vertices']]
triangles = list(old['triangles'])
for i in range(0,len(triangles),3): triangles[i+1],triangles[i+2] = triangles[i+2],triangles[i+1]
target, error = u.VamMetaHumanEditorAdapter.create_target(folder+'/SM_Target', verts, triangles)
assert target, error
u.EditorAssetLibrary.save_loaded_asset(target)
character = u.AssetToolsHelpers.get_asset_tools().create_asset('启梦MH方向法线诊断', folder, u.MetaHumanCharacter, u.MetaHumanCharacterFactoryNew())
sub = u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
assert sub.try_add_object_to_edit(character)
points = {str(k): v for k,v in sub.get_preset_body_key_points(character).items()}
(out/'preset-keypoints.json').write_text(json.dumps(points, indent=2), encoding='utf8')
error = u.VamMetaHumanEditorAdapter.conform(character, target, {}, '')
assert error == '', str(error)
u.EditorAssetLibrary.save_loaded_asset(character)
actor = sub.spawn_meta_human_actor(character, True)
for component in actor.get_components_by_class(u.SkeletalMeshComponent):
    if component.get_name() not in ('Body','Face'): continue
    mesh = component.get_editor_property('skeletal_mesh_asset')
    result = sub.get_mesh_data_for_conforming(mesh)
    if result:
        vertices, triangles = result
        (out/('fixed_'+component.get_name()+'.json')).write_text(json.dumps({'vertices': [[v.x,v.y,v.z] for v in vertices], 'triangles': list(triangles)}), encoding='utf8')
sub.remove_object_to_edit(character)
u.log('FIT_EXPERIMENT_COMPLETE local geometry only; no publication')
