"""Verification fixtures only. Formal characters own every solver and runtime input.
VAM_SOFT_TISSUE_MAP_SPEC names committed build reports and a new destination.
"""
import json, os
from pathlib import Path
import unreal as u

spec=json.loads(Path(os.environ['VAM_SOFT_TISSUE_MAP_SPEC']).read_text(encoding='utf-8-sig'))
root=spec['destination_root']
assert not u.EditorAssetLibrary.does_directory_exist(root), 'Protect existing test assets'
reports=[json.loads(Path(p).read_text(encoding='utf8')) for p in spec['build_reports']]
assert all(r['status']=='committed' for r in reports)
classes=[]
for report in reports:
    bp=u.load_asset(report['blueprint'])
    assert bp and u.get_default_object(bp.generated_class()).get_editor_property('soft_tissue')
    classes.append(bp.generated_class())
legacy=u.load_asset(spec['without_profile_blueprint']).generated_class()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.GameModeBase)
mode=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_ManualGameMode',root,u.Blueprint,factory)
u.get_default_object(mode.generated_class()).set_editor_property('default_pawn_class',None)
u.BlueprintEditorLibrary.compile_blueprint(mode)
assert u.EditorAssetLibrary.save_loaded_asset(mode,False)
maps=[]
for index in range(len(classes)+1):
    world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
    selected=classes if index==0 else [classes[index-1],classes[index-1],legacy]
    subjects=[actors.spawn_actor_from_class(cls,u.Vector(0,(i-(len(selected)-1)/2)*220,0)) for i,cls in enumerate(selected)]
    if index:
        probe=actors.spawn_actor_from_class(u.VamSoftTissueRuntimeProbe,u.Vector())
        probe.set_editor_property('subjects',subjects)
    floor=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6))
    floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(u.Vector(10,12,.1))
    floor.set_actor_label('Ordinary WorldStatic Floor')
    if index==0:
        sphere=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(30,-100,140))
        sphere.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Sphere'))
        sphere.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE)
        sphere.static_mesh_component.set_collision_object_type(u.CollisionChannel.ECC_WORLD_DYNAMIC)
        sphere.set_actor_scale3d(u.Vector(.12,.12,.12))
        sphere.set_actor_label('Ordinary WorldDynamic Collider - Move in Simulate')
    camera=actors.spawn_actor_from_class(u.CameraActor,u.Vector(480,0,155),u.Rotator(pitch=-4,yaw=180,roll=0))
    camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
    camera.camera_component.set_field_of_view(65)
    for pitch,yaw,intensity in [(-35,150,4),(-25,-20,2)]:
        light=actors.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=pitch,yaw=yaw,roll=0))
        light.light_component.set_intensity(intensity)
        light.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    world.get_world_settings().set_editor_property('default_game_mode',mode.generated_class())
    path=root+('/L_Manual' if index==0 else '/L_Audit'+str(index))
    assert u.EditorLoadingAndSavingUtils.save_map(world,path)
    maps.append(path)

# Verify the dependency direction from the published character roots, independent of test-map loading.
registry=u.AssetRegistryHelpers.get_asset_registry()
options=u.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,
    include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
for report in reports:
    pending=[report['blueprint'].split('.')[0],report['configuration'].split('.')[0]];seen=set()
    while pending:
        package=pending.pop()
        if package in seen or package.startswith('/Script/'):continue
        seen.add(package)
        assert not package.startswith(root) and not package.startswith('/Game/VamRuntimeTests/'), 'Production asset points into test fixtures: '+package
        pending.extend(str(p) for p in registry.get_dependencies(package,options))
Path(spec['report']).write_text(json.dumps(dict(maps=maps,production_blueprints=[r['blueprint'] for r in reports],
    dependency_direction_passed=True,manual_map_has_test_probe=False,visual_acceptance_passed=False),indent=2),encoding='utf8')
