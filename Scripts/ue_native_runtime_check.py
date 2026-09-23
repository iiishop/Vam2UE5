"""Exercise asynchronous native assembly and absolute Morph API in an editor host.

Pins an immutable build report; writes separate evidence without republishing latest.
Packaged runtime loading is verified separately, without this Python script.
"""
import json,os,time,traceback
from pathlib import Path
import unreal as u

saved=Path(__file__).resolve().parents[1]/'Saved'
report=json.loads(Path(os.environ['VAM_NATIVE_REPORT_FILE']).read_text(encoding='utf8'))
output=saved/'NativeBuild/stage05-final-runtime-check.json'
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(report['blueprint'])
actor=sub.spawn_actor_from_class(bp.generated_class(),u.Vector())
component=actor.get_editor_property('character')
definition=u.load_asset(report['definition'])
parameters=definition.get_editor_property('parameters')
expected=1+len(definition.get_editor_property('parts'))
started=time.monotonic();phase=0;checks=[]
component.load_character()
component.load_character()  # Cancel a pending request before its completion.

def meshes():return actor.get_components_by_class(u.SkeletalMeshComponent)

def tick(dt):
    global phase
    try:
        assert time.monotonic()-started<90,'Native load timed out'
        body=component.get_editor_property('body')
        if not body:return
        assert len(meshes())==expected,(len(meshes()),expected)
        if phase==0:
            first_load=time.monotonic()-started
            for p in parameters:
                name=p.get_editor_property('target');default=p.get_editor_property('default_value')
                low=p.get_editor_property('minimum');high=p.get_editor_property('maximum')
                assert abs(body.get_morph_target(name))<1e-5,'p0 was applied twice'
                for value in (low,high,high+1,low-1,default):
                    assert component.set_parameter(name,value)
                    wanted=max(low,min(high,value))-default
                    for mesh in meshes():
                        names=[str(m.get_name()) for m in mesh.get_editor_property('skeletal_mesh_asset').get_editor_property('morph_targets')]
                        if str(name) in names:assert abs(mesh.get_morph_target(name)-wanted)<1e-5
                assert not component.set_parameter(name,float('nan'))
            assert not component.set_parameter('UnknownParameter',0.5)
            checks.extend(['no_double_p0','absolute_parameters_and_clamping','part_morph_propagation','reject_nonfinite_and_unknown','cancel_pending_load'])
            phase=1
            component.unload_character()
            assert not component.get_editor_property('body') and not meshes()
            checks.append('unload_destroys_components')
            component.load_character()
            tick.first_load=first_load
            return
        checks.extend(['reload_without_duplicate_components','default_offsets_restored'])
        for p in parameters:assert abs(body.get_morph_target(p.get_editor_property('target')))<1e-5
        result={'status':'passed','folder':report['folder'],'components':expected,'parameters':len(parameters),
                'checks':checks,'first_load_seconds':tick.first_load,'host':'UE editor native runtime component; packaged load tested separately'}
    except Exception:
        result={'status':'failed','folder':report['folder'],'error':traceback.format_exc(),'checks':checks}
        u.log_error(result['error'])
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

handle=u.register_slate_post_tick_callback(tick)
