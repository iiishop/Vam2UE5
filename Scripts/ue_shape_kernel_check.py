"""Two-instance Shape transaction, BoneCenter and binding acceptance in real UE."""
import json,os,time,traceback
from pathlib import Path
import unreal as u
saved=Path(__file__).resolve().parents[1]/'Saved'
report=json.loads(Path(os.environ['VAM_NATIVE_REPORT_FILE']).read_text(encoding='utf8'))
output=saved/'NativeBuild/shape-kernel-runtime-check.json'
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem);bp=u.load_asset(report['blueprint'])
actors=[sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,i*180,0)) for i in range(2)]
characters=[a.get_editor_property('character') for a in actors]
for c in characters:c.load_character()
started=time.monotonic();phase=0;checks=[];changed_at=0

def values(c):return {str(k):v for k,v in c.get_shape_state().values.items()}
def almost(a,b):return len(a)==len(b) and all(abs(a[k]-b[k])<1e-5 for k in a)

def tick(dt):
    global phase,changed_at
    try:
        assert time.monotonic()-started<120,'Timed out'
        if not all(c.get_editor_property('body') for c in characters):return
        a,b=characters;body=a.get_editor_property('body')
        definition=u.load_asset(report['definition']);parameters=definition.get_editor_property('parameters')
        defaults={str(p.get_editor_property('target')):p.get_editor_property('default_value') for p in parameters}
        if phase==0:
            assert almost(values(a),defaults) and almost(values(b),defaults)
            assert len([p for p in parameters if p.get_editor_property('default_value')==0])>=3
            assert body.get_editor_property('skeletal_mesh_asset')==b.get_editor_property('body').get_editor_property('skeletal_mesh_asset')
            baseline=a.get_shape_reference_pose();tick.baseline=baseline;tick.before=values(a)
            updates={str(p.get_editor_property('target')):p.get_editor_property('default_value')+(-.25 if p.get_editor_property('default_value')>=p.get_editor_property('maximum') else .25) for p in parameters}
            assert a.preview_parameters(updates);assert almost(values(b),defaults)
            assert any((x.translation-y.translation).length()>1e-4 for x,y in zip(baseline,a.get_shape_reference_pose()))
            tick.revision=a.get_shape_state().revision;changed_at=time.monotonic();phase=1;return
        if time.monotonic()-changed_at<1:return
        if phase==1:
            # Check real evaluated skeleton positions, not just stored parameter arrays.
            local=a.get_shape_reference_pose();world=[];maximum=0.
            for i,t in enumerate(local):
                name=body.get_bone_name(i);parent=body.get_bone_index(body.get_parent_bone(name))
                composed=u.MathLibrary.compose_transforms(t,world[parent]) if parent>=0 else t
                world.append(composed)
                actual=body.get_socket_transform(name,u.RelativeTransformSpace.RTS_COMPONENT).translation
                maximum=max(maximum,(actual-composed.translation).length())
            assert maximum<1e-3,('BoneCenter pose was not evaluated',maximum)
            tick.bone_error=maximum;checks.extend(['zero_weight_editable_set','two_instance_isolation','evaluated_shape_reference_pose'])
            a.cancel_shape();assert almost(values(a),defaults)
            first=parameters[0];name=str(first.get_editor_property('target'))
            assert a.set_parameter(name,first.get_editor_property('minimum'));assert a.commit_shape()
            committed=values(a)
            assert a.set_parameter(name,first.get_editor_property('maximum'));a.cancel_shape();assert almost(values(a),committed)
            before=values(a);assert not a.preview_parameters({name:0.,'invalid':0.});assert almost(values(a),before)
            assert not a.set_parameter(name,float('nan'))
            a.reset_to_base_shape();assert all(abs(v)<1e-6 for v in values(a).values())
            tick.zero_pose=a.get_shape_reference_pose();changed_at=time.monotonic();phase=2;return
        shape=u.load_asset(report['folder']+'/SD_Shape')
        neutral=shape.get_editor_property('neutral_local_bind')
        assert max((x.translation-y.translation).length() for x,y in zip(neutral,tick.zero_pose))<1e-5
        a.reset_to_imported_appearance();assert almost(values(a),defaults)
        assert a.get_shape_state().revision>tick.revision
        assert almost(values(b),defaults)
        snapshot=a.get_character_state()
        assert not snapshot.has_simulation and snapshot.simulation_shape_revision==-1 and not snapshot.simulated_surface
        assert len(snapshot.pose_component_space)==len(neutral)
        checks.extend(['batch_atomic_rejection','commit_cancel','zero_shape_and_neutral_bind','restore_imported','monotonic_revision'])
        checks.append('shape_pose_simulation_separation')
        result={'status':'passed','folder':report['folder'],'parameters':len(parameters),'checks':checks,'maximum_evaluated_bone_error_cm':tick.bone_error}
    except Exception:
        result={'status':'failed','error':traceback.format_exc(),'checks':checks};u.log_error(result['error'])
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()

handle=u.register_slate_post_tick_callback(tick)
