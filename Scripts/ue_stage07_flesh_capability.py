"""Explicit, isolated Chaos Flesh capability build. Not a production region bake."""
import json
import os
from pathlib import Path
import unreal as u

spec = json.loads(Path(os.environ['VAM_FLESH_CAPABILITY_SPEC']).read_text(encoding='utf-8-sig'))
root = spec['destination_root']
report = Path(spec['report'])
runtime = u.load_asset(spec['runtime_configuration'])
assert runtime and runtime.get_editor_property('independent_reload_verified')
definition = runtime.get_editor_property('definition')
profile_path = root + '/DA_Capability'
if spec.get('phase', 'build') == 'reload':
    asset = u.load_asset(profile_path)
    saved = json.loads(report.read_text(encoding='utf8'))
    assert asset and asset.get_editor_property('flesh')
    assert asset.get_editor_property('body') == definition.get_editor_property('body')
    assert asset.get_editor_property('bound_render_vertices') == saved['bound_render_vertices']
    for material in asset.get_editor_property('surface_materials'):
        assert material.get_editor_property('used_with_mesh_deformer'), 'Saved material lacks the required cooked permutation'
    assert saved['writer_pid'] != os.getpid()
    saved.update(reloader_pid=os.getpid(), independent_reload=True)
    report.write_text(json.dumps(saved, indent=2), encoding='utf8')
else:
    assert not u.EditorAssetLibrary.does_asset_exist(profile_path), 'Existing experiment is protected'
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', u.VamFleshCapabilityAsset)
    asset = u.AssetToolsHelpers.get_asset_tools().create_asset('DA_Capability', root, u.VamFleshCapabilityAsset, factory)
    error = u.VamFleshCapabilityBuilder.build(asset, definition, spec['source_bone'], spec.get('cells', 4))
    assert not error, str(error)
    # A separate experiment owns these shader permutations; baseline published assets stay immutable.
    source_materials = runtime.get_editor_property('materials').get_editor_property('body_materials')
    surface_materials, cache = [], {}
    for source in source_materials:
        key = source.get_path_name()
        if key not in cache:
            material = u.AssetToolsHelpers.get_asset_tools().duplicate_asset('M_Surface_'+str(len(cache)), root, source)
            assert material
            u.MaterialEditingLibrary.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_MESH_DEFORMER)
            u.MaterialEditingLibrary.recompile_material(material)
            assert u.EditorAssetLibrary.save_loaded_asset(material, False)
            cache[key] = material
        surface_materials.append(cache[key])
    asset.set_editor_property('surface_materials', surface_materials)
    assert u.EditorAssetLibrary.save_loaded_asset(asset, False)
    world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(u.VamFleshCapabilityActor, u.Vector())
    actor.set_editor_property('capability', asset)
    # A native CDO's soft default path alone was not discovered by the cooker.
    # Persist an explicit map reference to a private, derived graph package.
    source_deformer = u.load_asset('/ChaosFlesh/Deformers/DG_FleshDeformer')
    deformer = u.AssetToolsHelpers.get_asset_tools().duplicate_asset('DG_Surface', root, source_deformer)
    assert deformer and u.EditorAssetLibrary.save_loaded_asset(deformer, False)
    report.with_suffix('.surface-graph.txt').write_text(u.VamFleshCapabilityBuilder.describe_surface_graph(deformer), encoding='utf8')
    actor.set_editor_property('surface_deformer', deformer)
    if spec.get('hide_contact_sphere', False):
        actor.get_editor_property('probe_sphere').set_editor_property('hidden_in_game', True)
    actor.get_editor_property('character').set_editor_property('runtime_configuration', runtime)
    camera = actors.spawn_actor_from_class(u.CameraActor, u.Vector(230, 0, 140), u.Rotator(pitch=-3, yaw=180, roll=0))
    camera.set_editor_property('auto_activate_for_player', u.AutoReceiveInput.PLAYER0)
    camera.camera_component.set_field_of_view(45)
    for pitch, yaw, intensity in [(-35, 150, 3), (-25, -20, 1)]:
        light = actors.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 250), u.Rotator(pitch=pitch, yaw=yaw, roll=0))
        light.light_component.set_intensity(intensity)
        light.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', u.GameModeBase)
    mode = u.AssetToolsHelpers.get_asset_tools().create_asset('BP_NoPawn', root, u.Blueprint, factory)
    u.get_default_object(mode.generated_class()).set_editor_property('default_pawn_class', None)
    u.BlueprintEditorLibrary.compile_blueprint(mode)
    assert u.EditorAssetLibrary.save_loaded_asset(mode, False)
    world.get_world_settings().set_editor_property('default_game_mode', mode.generated_class())
    assert u.EditorLoadingAndSavingUtils.save_map(world, root+'/L_Capability')
    report.write_text(json.dumps(dict(writer_pid=os.getpid(), asset=profile_path, map=root+'/L_Capability',
        bound_render_vertices=asset.get_editor_property('bound_render_vertices'),
        rest_volume_cm3=asset.get_editor_property('rest_volume_cm3'),
        limitation=asset.get_editor_property('limitation'), independent_reload=False, stage07_passed=False), indent=2), encoding='utf8')
