"""Build a versioned Play-world composition map from explicitly committed bundles."""
import hashlib,json,os,sys
from pathlib import Path
import unreal as u
sys.path.insert(0,str(Path(__file__).parent))
from vam_runtime_recipe import digest,require
from ue_runtime_build import load,fingerprint,write_json
spec_path=Path(os.environ['VAM_COMPOSITION_SPEC'])
spec=json.loads(spec_path.read_text(encoding='utf8'))
quality=json.loads((spec_path.parent/spec['quality']).resolve().read_text(encoding='utf8'))
require(quality['schema']=='vam-composition-quality/1','Invalid composition quality profile')
reports=[json.loads((spec_path.parent/p).resolve().read_text(encoding='utf8')) for p in spec['build_reports']]
require(len(reports)>=2,'Two independent builds required')
require(len({r['configuration'] for r in reports})>=2,'Two copies of one build are not independent proportions')
for report in reports:
    require(report['status']=='committed' and report['independent_reload_verified'],'Bundle has not passed independent reload')
    for package,expected in report['output_files'].items():require(fingerprint(package)==expected,'Bundle changed after commit: '+package)
shader_statistics={}
if spec.get('warm_material_shaders',False):
    # UE 5.8 GetStatistics submits missing jobs and waits for FinishCompilation.
    # This warms the saved material identity without PostEditChange or resaving
    # source assets, before timed physics tests or screenshots begin.
    materials={}
    for report in reports:
        profile=load(report['configuration'],u.VamRuntimeConfiguration).get_editor_property('materials')
        for m in profile.get_editor_property('body_materials'):
            if m:materials[m.get_path_name()]=m
        for part in profile.get_editor_property('part_materials'):
            for m in part.get_editor_property('materials'):
                if m:materials[m.get_path_name()]=m
    for name,material in sorted(materials.items()):
        stats=u.MaterialEditingLibrary.get_statistics(material)
        vertex=stats.get_editor_property('num_vertex_shader_instructions')
        pixel=stats.get_editor_property('num_pixel_shader_instructions')
        require(vertex>0 and pixel>0,'Material produced no compiled render shader: '+name)
        shader_statistics[name]={'vertex_instructions':vertex,'pixel_instructions':pixel}
    for report in reports:
        for package,expected in report['output_files'].items():require(fingerprint(package)==expected,'Shader warm-up changed a saved asset: '+package)
if spec.get('shared_asset_instance',False): reports.append(reports[0])
identity=digest({'builds':[r['identity'] for r in reports],'quality':quality,
    'map_builder':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'probe':hashlib.sha256((Path(__file__).parents[1]/'Source/VamCharacterRuntime/Private/VamStage07CompositionProbe.cpp').read_bytes()).hexdigest()})
path=spec['map_root'].rstrip('/')+'/L_Composition_'+identity[:16]
require(not u.EditorAssetLibrary.does_asset_exist(path),'Existing map protected; inspect or use an explicitly different map_root')
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem);subjects=[]
for i,report in enumerate(reports):
    bp=load(report['blueprint'],u.Blueprint)
    actor=sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,(i-(len(reports)-1)*.5)*220,0))
    actor.set_actor_label('Composition '+report['identity'][:8]);subjects.append(actor)
probe=sub.spawn_actor_from_class(u.VamStage07CompositionProbe,u.Vector())
probe.set_editor_property('subjects',subjects)
for key in ('final_ik_tolerance_cm','minimum_base_motion_cm','minimum_grab_motion_cm','final_foot_tolerance_cm','settled_speed_tolerance_cm_s','settled_drift_tolerance_cm','joint_limit_tolerance_degrees'):probe.set_editor_property(key,quality[key])
floor=sub.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6))
floor.static_mesh_component.set_static_mesh(load('/Engine/BasicShapes/Cube',u.StaticMesh));floor.set_actor_scale3d(u.Vector(8,8,.1))
camera=sub.spawn_actor_from_class(u.CameraActor,u.Vector(480,0,155),u.Rotator(pitch=-4,yaw=180,roll=0))
camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0);camera.camera_component.set_field_of_view(65)
for pitch,yaw,intensity in [(-35,150,4),(-25,-20,2)]:
    light=sub.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,250),u.Rotator(pitch=pitch,yaw=yaw,roll=0))
    light.light_component.set_intensity(intensity);light.light_component.set_mobility(u.ComponentMobility.MOVABLE)
factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.GameModeBase)
game_mode=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_LabGameMode',path+'_Support',u.Blueprint,factory)
require(game_mode is not None,'Could not create isolated lab GameMode')
u.get_default_object(game_mode.generated_class()).set_editor_property('default_pawn_class',None)
u.BlueprintEditorLibrary.compile_blueprint(game_mode)
require(u.EditorAssetLibrary.save_loaded_asset(game_mode,False),'Could not save lab GameMode')
world.get_world_settings().set_editor_property('default_game_mode',game_mode.generated_class())
world.get_world_settings().set_editor_property('force_no_precomputed_lighting',True)
require(u.EditorLoadingAndSavingUtils.save_map(world,path),'Failed to save composition map')
write_json(os.environ['VAM_COMPOSITION_MAP_REPORT'],{'map':path,'identity':identity,'quality':quality,'builds':[r['identity'] for r in reports],'shader_statistics':shader_statistics})
