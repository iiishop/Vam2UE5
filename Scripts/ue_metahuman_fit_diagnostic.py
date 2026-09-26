"""Local, read-only geometry evidence; no cloud request or existing-asset save."""
import json
from pathlib import Path
import unreal as u

root = Path(__file__).resolve().parents[1]
out = root/'Saved/MetaHuman/FitRepair'
out.mkdir(parents=True, exist_ok=True)
sub = u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
paths = {
    'native': '/Game/VamCharacters/C_b41dfaaabfc8427b0ef4e065/SK_Body',
    'old_target': '/Game/MetaHumans/启梦MH00/Source/SM_ConformTarget',
    'old_body': '/Game/MetaHumans/启梦MH00/AssemblyV3/启梦MH00/Body/SKM_启梦MH00_BodyMesh',
    'old_face': '/Game/MetaHumans/启梦MH00/AssemblyV3/启梦MH00/Face/SKM_启梦MH00_FaceMesh',
    'archetype': '/MetaHumanCharacter/Optional/Clothing/DefaultGarment/SourceBodies/archetype_SkelMesh',
}
report = {}
character_path = '/Game/MetaHumans/启梦MH修复诊断/启梦MH修复诊断'
if u.EditorAssetLibrary.does_asset_exist(character_path):
    character = u.load_asset(character_path)
    assert sub.try_add_object_to_edit(character)
    actor = sub.spawn_meta_human_actor(character, True)
    for component in actor.get_components_by_class(u.SkeletalMeshComponent):
        if component.get_name() in ('Body','Face'):
            paths['rotated_'+component.get_name()] = component.get_editor_property('skeletal_mesh_asset')
for name, path in paths.items():
    mesh = u.load_asset(path) if isinstance(path,str) else path
    if not mesh:
        report[name] = {'error': 'missing', 'path': path}
        continue
    result = sub.get_mesh_data_for_conforming(mesh)
    if result is None:
        report[name] = {'error': 'no geometry', 'path': path}
        continue
    vertices, triangles = result
    vs = [[v.x, v.y, v.z] for v in vertices]
    value = {'path': mesh.get_path_name(), 'vertices': vs, 'triangles': list(triangles)}
    (out/(name+'.json')).write_text(json.dumps(value), encoding='utf8')
    report[name] = {'count': len(vs), 'bounds': [[min(v[i] for v in vs), max(v[i] for v in vs)] for i in range(3)]}
(out/'geometry-report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('FIT_DIAGNOSTIC '+json.dumps(report))
