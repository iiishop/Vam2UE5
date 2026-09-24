"""Read-only Stage07 transaction reproduction in a separate editor process.
VAM_STAGE07_PROBE_CONFIG is an explicit JSON input; no latest-report lookup.
"""
import hashlib, json, os, time, traceback
from pathlib import Path
import unreal as u

config = json.loads(Path(os.environ['VAM_STAGE07_PROBE_CONFIG']).read_text(encoding='utf8'))
output = Path(os.environ['VAM_STAGE07_PROBE_REPORT'])

def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

plugin = Path(u.Paths.project_plugins_dir())/'VamResourceBrowser'
inputs = {str(p.relative_to(plugin)):fingerprint(p) for base in ('Source','Binaries/Win64')
          for p in sorted((plugin/base).rglob('*')) if p.is_file() and p.suffix in ('.h','.cpp','.cs','.dll')}

u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub = u.get_editor_subsystem(u.EditorActorSubsystem)
bp = u.load_asset(config['blueprint'])
assert bp
actors = [sub.spawn_actor_from_class(bp.generated_class(), u.Vector(0, y, 0)) for y in (0, 220)]
characters = [a.get_editor_property('character') for a in actors]
for c in characters: c.load_character()
started = time.monotonic()

def position(body, bone):
    return body.get_socket_transform(bone, u.RelativeTransformSpace.RTS_WORLD).translation

def tick(dt):
    if not all(c.get_editor_property('body') for c in characters):
        if time.monotonic() - started < 90: return
    result = {'schema': 'vam-stage07-transaction-probe/1', 'config': config, 'stage07_passed': False}
    try:
        a, b = characters
        body, other = [c.get_editor_property('body') for c in characters]
        assert body and other, 'load timeout'
        definition = a.get_editor_property('definition')
        rig = a.get_editor_property('rig_profile')
        bone = rig.bone_for_semantic('left_hand')
        original = position(body, bone)
        isolated = position(other, bone)
        goal = body.get_socket_transform(bone, u.RelativeTransformSpace.RTS_WORLD)
        goal.translation += u.Vector(15, 0, 5)
        assert a.set_ik_goal('left_hand', goal)
        driven = position(body, bone)
        instance = body.get_anim_instance()
        assert a.commit_shape()
        after = position(body, bone)
        result['same_value_commit'] = {
            'same_anim_object': instance == body.get_anim_instance(),
            'ik_before_error_cm': (driven-goal.translation).length(),
            'ik_after_error_cm': (after-goal.translation).length(),
            'hand_change_cm': (after-driven).length(),
            'other_change_cm': (position(other,bone)-isolated).length(),
        }
        clip = u.load_asset(config['animation'])
        assert clip and clip.get_editor_property('skeleton') == body.get_editor_property('skeletal_mesh_asset').get_editor_property('skeleton')
        body.play_animation(clip, True)
        body.set_position(.37, False)
        before_time = body.get_position()
        assert a.commit_shape()
        result['single_node_commit'] = {'before_seconds': before_time, 'after_seconds': body.get_position(),
            'scope': 'single-node preservation only; layered host is verified by the separate composition probe'}
        if config.get('require_fixed', False):
            assert abs(body.get_position()-before_time)<1.e-5, 'shape commit restarted the animation'
            assert result['same_value_commit']['hand_change_cm'] < 1.e-4
            assert result['same_value_commit']['other_change_cm'] < 1.e-4
            # Use transient objects to exercise both import conventions without
            # rewriting the locked source or pretending this is another person.
            runtime = a.get_editor_property('runtime_configuration')
            if runtime:
                original_profile = runtime.get_editor_property('physics_shape')
                neutral_definition = u.new_object(u.VamCharacterDefinition)
                for key in ('body', 'shape', 'parameters'):
                    neutral_definition.set_editor_property(key, definition.get_editor_property(key))
                assert neutral_definition.get_editor_property('shape_convention') == 'neutral_plus_parameters'
                neutral_profile = u.new_object(u.VamPhysicsShapeProfile)
                error = u.VamStage06AssetEditor.build_physics_shape_profile(
                    neutral_profile, neutral_definition, runtime.get_editor_property('physics'))
                assert error is not None and error == '', error
                result['neutral_collision_convention'] = 'transient build accepted; internal fitting values require native regression; no independent neutral character sample'
            parameters = list(definition.get_editor_property('parameters'))
            expression = next(p for p in parameters if str(p.get_editor_property('group')) == 'Expression')
            target = str(expression.get_editor_property('target'))
            shape = next(p for p in parameters if str(p.get_editor_property('group')) == 'Shape' and p.get_editor_property('maximum') > p.get_editor_property('minimum'))
            shape_target = str(shape.get_editor_property('target'))
            baseline = dict(a.get_shape_state(True).values)
            other_values = dict(b.get_shape_state().values)
            assert a.set_expression_weights({target: .8})
            cycles = []
            minimum, maximum = shape.get_editor_property('minimum'), shape.get_editor_property('maximum')
            for value in (minimum, (minimum+maximum)*.5, maximum):
                assert a.set_parameter(shape_target, value)
                assert abs(body.get_morph_target(target)-.8)<1.e-5
                assert a.commit_shape()
                assert abs(body.get_morph_target(target)-.8)<1.e-5
                assert a.set_parameter(shape_target, shape.get_editor_property('default_value'))
                a.cancel_shape()
                assert abs(body.get_morph_target(target)-.8)<1.e-5
                assert dict(b.get_shape_state().values)==other_values
                assert abs(body.get_position()-before_time)<1.e-5
                cycles.append({'value':value,'expression_weight':body.get_morph_target(target),'animation_seconds':body.get_position()})
            assert not a.set_expression_weights({shape_target: .8})
            assert abs(body.get_morph_target(target)-.8)<1.e-5, 'invalid layer was not atomic'
            assert a.set_expression_weights({})
            assert abs(body.get_morph_target(target))<1.e-5
            assert a.preview_parameters(baseline) and a.commit_shape()
            interaction=actors[0].get_editor_property('interaction')
            old_collision=body.get_collision_enabled()
            assert not interaction.set_physical_mode(u.VamPhysicalMode.LOCAL_RESPONSE, 'not_a_body')
            assert body.get_collision_enabled()==old_collision, 'invalid mode mutated collision'
            state=a.get_character_state()
            assert state.animation_pose_time_seconds == -1 and state.rigid_pose_time_seconds == -1
            assert state.collision_proxy_time_seconds == -1 and state.simulation_shape_revision == -1
            result['expression_shape_cycles']=cycles
            result['regression_passed']=True
        result['parameters'] = len(definition.get_editor_property('parameters'))
        result['definition'] = definition.get_path_name()
        native_folder = definition.get_path_name().split('.')[0].rsplit('/',1)[0]
        content = Path(u.Paths.project_content_dir())
        files = list((content/native_folder.removeprefix('/Game/')).rglob('*.uasset'))
        files += list((content/config['blueprint'].rsplit('/',1)[0].removeprefix('/Game/')).rglob('*.uasset'))
        result['asset_sha256'] = {str(p.relative_to(content)):fingerprint(p) for p in sorted(set(files))}
        result['code_and_binary_sha256'] = inputs
        result['environment'] = {'engine':u.SystemLibrary.get_engine_version(),'rendering':'NullRHI; not visual or cooked acceptance'}
        result['status'] = 'observed'
    except Exception:
        result.update(status='error', error=traceback.format_exc())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding='utf8')
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

handle = u.register_slate_post_tick_callback(tick)
