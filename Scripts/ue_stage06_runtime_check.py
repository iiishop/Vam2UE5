"""Editor-host smoke check for the Stage06 reusable actor and isolated state."""
import json, time, traceback
from pathlib import Path
import unreal as u

report=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-runtime-check.json')
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
bp=u.load_asset('/Game/VamStage06/BP_VamCharacter')
assert bp
actors=[sub.spawn_actor_from_class(bp.generated_class(),u.Vector(0,i*220,0)) for i in range(2)]
characters=[actor.get_editor_property('character') for actor in actors]
motions=[actor.get_editor_property('motion') for actor in actors]
for character in characters:character.load_character()
start=time.monotonic();phase=0;before=None

def location(body,name):
    return body.get_socket_transform(name,u.RelativeTransformSpace.RTS_WORLD).translation

def finish(result):
    report.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

def tick(dt):
    global phase,before
    try:
        assert time.monotonic()-start<90, 'Stage06 runtime load timed out'
        if not all(c.get_editor_property('body') for c in characters):return
        a,b=[c.get_editor_property('body') for c in characters]
        rig=characters[0].get_editor_property('rig_profile')
        physics=characters[0].get_editor_property('physics_asset')
        assert rig and physics
        assert len(physics.get_constraints(False))>=20
        materials=[body.get_material(0) for body in (a,b)]
        assert all(mat and mat.get_class().get_name()=='MaterialInstanceDynamic' for mat in materials),[
            (str(mat.get_class().get_name()),mat.get_path_name()) if mat else None for mat in materials]
        assert materials[0]!=materials[1], 'Material instances leaked between characters'
        if phase==0:
            assert characters[0].set_appearance_color('Tint',u.LinearColor(.8,.7,.6,1))
            tint0=materials[0].get_vector_parameter_value('Tint')
            tint1=materials[1].get_vector_parameter_value('Tint')
            assert abs(tint0.r-.8)<.001 and abs(tint1.r-1)<.001,(tint0,tint1)
            before=(location(a,'lHand'),location(b,'lHand'))
            goal=u.Transform(location=before[0]+u.Vector(15,0,5),rotation=u.Rotator())
            assert characters[0].set_ik_goal('left_hand',goal)
            phase=1;tick.changed=time.monotonic();return
        if time.monotonic()-tick.changed<1:return
        moved=(location(a,'lHand')-before[0]).length()
        isolated=(location(b,'lHand')-before[1]).length()
        if moved<=1:
            index=a.get_bone_index('lHand')
            debug_ok=characters[0].set_debug_bone_offset(index,u.Transform(location=u.Vector(10,0,0)))
            debug_moved=(location(a,'lHand')-before[0]).length()
            raise AssertionError({'ik_moved':moved,'debug_ok':debug_ok,'debug_moved':debug_moved,
                                  'index':index,'before':str(before[0]),'after':str(location(a,'lHand')),
                                  'anim':str(a.get_anim_instance().get_class().get_name())})
        assert moved>1,(moved,isolated)
        assert isolated<.1,(moved,isolated)
        m0,m1=motions
        m0.set_preview_paused(True)
        root=actors[0].get_actor_transform()
        t=u.SystemLibrary.get_game_time_in_seconds(actors[0])
        m0.move_continuously(root,t+1)
        moved_root=u.Transform(location=root.translation+u.Vector(30,0,0),rotation=root.rotation.rotator())
        m0.move_continuously(moved_root,t+1.1)
        sample=m0.get_motion()
        assert sample.linear_velocity.length()>100,sample.linear_velocity
        m0.step_preview()
        displaced=max(r.local_displacement.length() for r in m0.get_inertia_regions())
        assert displaced>0,displaced
        assert max(r.local_displacement.length() for r in m1.get_inertia_regions())==0
        m0.teleport_to(u.Transform(location=moved_root.translation+u.Vector(500,0,0),rotation=root.rotation.rotator()),t+1.2)
        assert m0.get_motion().teleported
        assert max(r.local_displacement.length() for r in m0.get_inertia_regions())==0
        finish({'status':'passed','ik_hand_motion_cm':moved,'other_character_motion_cm':isolated,
                'motion_velocity_cm_s':sample.linear_velocity.length(),'witness_displacement_cm':displaced,
                'constraints':len(physics.get_constraints(False)),'material_instances_isolated':True})
    except Exception:
        result={'status':'failed','error':traceback.format_exc()}
        u.log_error(result['error']);finish(result)

handle=u.register_slate_post_tick_callback(tick)
