"""Exercise a real native animation clip on the character and follower components."""
import json,os,time,traceback
from pathlib import Path
import unreal as u

saved=Path(__file__).resolve().parents[1]/'Saved'
report=json.loads(Path(os.environ['VAM_NATIVE_REPORT_FILE']).read_text(encoding='utf8'))
contract=json.loads((saved/'NativeBuild'/(report['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset(report['blueprint'])
actors=[sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,i*180,0)) for i in range(2)]
characters=[a.get_editor_property('character') for a in actors]
for c in characters:c.load_character()
started=time.monotonic();phase=0;changed=0

def location(body,name):
    return body.get_socket_transform(name,u.RelativeTransformSpace.RTS_COMPONENT).translation

def tick(dt):
    global phase,changed
    try:
        assert time.monotonic()-started<120,'animation check timed out'
        if not all(c.get_editor_property('body') for c in characters):return
        a,b=[c.get_editor_property('body') for c in characters]
        if phase==0:
            # This clip explicitly declares lShldr in its authoring script.
            # Test descendants selected from the saved source parent hierarchy.
            shoulder=next(i for i,x in enumerate(contract['bones']) if x['name']=='lShldr')
            descendants={shoulder}
            for i,x in enumerate(contract['bones']):
                if x['parent'] in descendants:descendants.add(i)
            tick.names=[contract['bones'][i]['name'] for i in sorted(descendants)]
            tick.before={n:location(a,n) for n in tick.names}
            tick.other={n:location(b,n) for n in tick.names}
            clip=u.load_asset(report['validation_animation']);assert clip
            a.override_animation_data(clip,False,True,.5,0.)
            a.set_update_animation_in_editor(True)
            phase=1;changed=time.monotonic();return
        if time.monotonic()-changed<2:return
        movement=max((location(a,n)-tick.before[n]).length() for n in tick.names)
        other=max((location(b,n)-tick.other[n]).length() for n in tick.names)
        assert movement>5,(movement,other)
        assert other<.001,(movement,other)
        followers=[c for c in actors[0].get_components_by_class(u.SkeletalMeshComponent) if c!=a]
        assert len(followers)==report['parts']
        error=max((location(c,n)-location(a,n)).length() for c in followers for n in tick.names)
        assert error<.001,error
        result={'status':'passed','folder':report['folder'],'animation':report['validation_animation'],
                'descendant_motion_cm':movement,'other_instance_motion_cm':other,
                'follower_pose_error_cm':error,'followers':len(followers)}
    except Exception:
        result={'status':'failed','error':traceback.format_exc()};u.log_error(result['error'])
    (saved/'NativeBuild/shape-animation-check.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()

handle=u.register_slate_post_tick_callback(tick)
