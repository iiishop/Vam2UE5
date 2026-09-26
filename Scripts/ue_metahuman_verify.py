"""Independent process verifier. Source verification is not an Assembly success receipt."""
import json
from pathlib import Path
import re
import sys
import unreal as u
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vam_metahuman import verify_recipe, write_json, fingerprint

command = u.SystemLibrary.get_command_line()
match = re.search(r'-VamMHJob=(?:"([^"]+)"|(\S+))', command)
assert match, 'Missing job'
job = Path(match.group(1) or match.group(2))
recipe = verify_recipe(job)
u.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
character = u.load_asset(recipe['assets']['character'])
assert u.VamMetaHumanEditorAdapter.is_character_source_valid(character), 'Invalid editable Character source'
for key, expected in recipe['owned'].items():
    path = Path(u.Paths.project_content_dir())/(recipe['assets'][key][6:]+'.uasset')
    assert fingerprint(path) == expected, 'Saved source asset changed: '+key
    assert u.load_asset(recipe['assets'][key]), 'Cannot reload source asset: '+key
report = {'schema': 'vam-metahuman-verification/1', 'source_valid': True,
    'visual_acceptance_passed': False, 'cooked_lifecycle_verified': False}
if '-VamMHVerify=assembly' in command:
    assert u.VamMetaHumanEditorAdapter.has_full_rig(character), 'Full correctives DNA missing'
    blueprint = u.load_asset(recipe['blueprint'])
    assert isinstance(blueprint, u.Blueprint), 'Missing Assembly Blueprint'
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(blueprint.generated_class(), u.Vector(0,0,0))
    assert actor, 'Ordinary world dynamic spawn failed'
    try:
        report['assembly'] = json.loads(u.VamMetaHumanEditorAdapter.inspect_assembly(actor))
        report['components'] = [{'name': c.get_name(), 'class': c.get_class().get_name()} for c in actor.get_components_by_class(u.ActorComponent)]
        if recipe.get('fidelity_post_rig'):
            # Inspect the generated BP's saved mesh. A valid preview and DNA
            # alone do not prove Assembly retained the selected identity.
            from vam_face_assembly_check import compare_head
            mesh=next(c for c in actor.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face').get_editor_property('skeletal_mesh_asset')
            subsystem=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
            vertices,triangles=subsystem.get_mesh_data_for_conforming(mesh)
            sections=json.loads(u.VamMetaHumanEditorAdapter.inspect_mesh_sections(mesh))['sections']
            skin=next(s for s in sections if s['slot']=='head_shader_shader')
            count=max(skin['triangles'])+1
            actual={'vertices':[[v.x,v.y,v.z] for v in vertices[:count]],'triangles':skin['triangles']}
            reference_path=Path(recipe['fidelity_post_rig']['path']).parent/'Official/RigReload/actual-head.json'
            reference=json.loads(reference_path.read_text(encoding='utf8'))
            write_json(job/'assembled-head.json',actual)
            report['fidelity_geometry']=compare_head(reference,actual)
            report['fidelity_geometry'].update(mesh=mesh.get_path_name(),reference_sha256=fingerprint(reference_path),
                assembled_head_sha256=fingerprint(job/'assembled-head.json'))
        write_json(job/'assembly-inspection.json', report)
        assert report['assembly']['valid'], 'Assembly DNA/LOD/animation/components incomplete'
    finally: actors.destroy_actor(actor)
    registry = u.AssetRegistryHelpers.get_asset_registry()
    options = u.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
        include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False)
    pending = [recipe['blueprint'].split('.')[0]]; seen = set(); closure = []; game_hashes = {}; textures = {}
    forbidden = ('/Script/MetaHumanCharacterEditor', '/Script/PythonScriptPlugin', '/Game/VamRuntimeTests', '/Game/VamCharacters')
    while pending:
        package = pending.pop()
        if package in seen: continue
        seen.add(package)
        assert not package.startswith(forbidden), 'Forbidden cooked dependency: '+package
        editor_sources = {recipe['assets'][key].split('.')[0] for key in ('character', 'target', 'mapping')}
        assert package not in editor_sources, 'Assembly references editor source assets'
        closure.append(package)
        if package.startswith('/Game/'):
            game_hashes[package] = fingerprint(Path(u.Paths.project_content_dir())/(package[6:]+'.uasset'))
        if not package.startswith('/Script/'):
            assets = registry.get_assets_by_package_name(package)
            assert assets, 'Missing closure package: '+package
            for data in assets:
                asset = data.get_asset()
                assert asset, 'Cannot load dependency: '+package
                if isinstance(asset, u.Blueprint):
                    assert u.BlueprintEditorLibrary.compile_blueprint(asset), 'Blueprint compilation rejected: '+package
                if isinstance(asset, u.Texture2D):
                    size = [asset.blueprint_get_size_x(), asset.blueprint_get_size_y()]
                    assert min(size) > 0, 'Texture has no dimensions: '+package
                    textures[asset.get_path_name()] = size
            pending.extend(str(x) for x in registry.get_dependencies(package, options))
    report['dependency_closure'] = sorted(closure)
    report['game_package_sha256'] = game_hashes
    report['texture_dimensions'] = textures
    write_json(job/'assembly-verified.json', report)
else: write_json(job/'source-verified.json', report)
u.log('MH00 verified within scope: '+str(job))
