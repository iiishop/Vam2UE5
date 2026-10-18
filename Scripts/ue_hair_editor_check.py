"""Instantiate the ordinary BP in the actual Editor world and verify the maintained Fine solver.

This checks editor attachment/lifecycle, not a substitute for GPU geometry audits
or the game/PIE motion recordings.
"""
import json, math, os, time, traceback
from pathlib import Path
import unreal as u

bundle = json.loads(Path(os.environ['VAM_HAIR_REPORT']).read_text())
output = Path(os.environ['VAM_HAIR_EDITOR_OUTPUT'])
u.EditorPythonScripting.set_keep_python_script_alive(True)
# Session-only: the editor otherwise overrides realtime when automation is backgrounded.
u.EditorLoadingAndSavingUtils.new_blank_map(False)
u.SystemLibrary.execute_console_command(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),'vam.HairEditorRealtime')
actor = u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(
    u.load_class(None, bundle['blueprint']+'_C'), u.Vector())
actor.get_editor_property('character').load_character()
hair = actor.get_editor_property('hair')
qualities = [u.VamHairQuality.FINE]
hair.set_hair_simulation_enabled(True)
hair.set_hair_quality(qualities[0])
started = changed = time.monotonic()
phase = 0
rows = []
errors = []
last_progress = 0

def tick(dt):
    global phase, changed, last_progress
    try:
        now = time.monotonic()
        if now-last_progress>5:
            last_progress=now
            u.SystemLibrary.execute_console_command(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),'vam.HairEditorRealtime')
            u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(250,0,140),u.Rotator(0,180,0))
            u.log("HAIR_EDITOR_PROGRESS "+str(dict(status=hair.get_editor_property("status"),tick_enabled=hair.is_component_tick_enabled(),groom=str(hair.get_editor_property("groom")))))
        assert now-started < 600, 'Editor attachment check timed out'
        actor.set_actor_rotation(u.Rotator(0, math.sin(now-started)*20, 0), False)
        groom = hair.get_editor_property('groom')
        if not groom or now-changed < 15:
            return
        body = actor.get_editor_property('character').get_editor_property('body')
        error = (groom.get_world_location()-body.get_socket_location('head')).length()
        assert math.isfinite(error) and error < .01, error
        physics_groups=groom.get_editor_property('groom_asset').get_editor_property('hair_groups_physics')
        niagara = [groom.get_niagara_component(i) for i in range(len(physics_groups))]
        active = sum(bool(n and n.is_active()) for n in niagara)
        u.log('HAIR_EDITOR_NIAGARA '+str([(str(n),n.is_active() if n else None,str(n.get_asset()) if n else None) for n in niagara]))
        if active != len(physics_groups) or not all(n and n.get_asset() for n in niagara):
            return  # Wait for native systems to bind; Editor activation is recorded separately.
        parameters=[]
        for physics in groom.get_editor_property('groom_asset').get_editor_property('hair_groups_physics'):
            contact=physics.get_editor_property('material_constraints').get_editor_property('collision_constraint')
            viscosity=float(contact.get_editor_property('strands_viscosity'))
            assert viscosity==0, 'Hair self collision remained enabled'
            parameters.append(dict(collision_radius_cm=float(contact.get_editor_property('collision_radius')),self_collision_viscosity=viscosity))
        rows.append(dict(quality=str(qualities[phase]), actor=actor.get_path_name(),parameters=parameters,
                         groom=groom.get_editor_property('groom_asset').get_path_name(),
                         head_attachment_error_cm=error, active_niagara=active, bound_niagara=len(niagara),
                         status=hair.get_editor_property('status')))
        phase += 1
        if phase < len(qualities):
            hair.set_hair_quality(qualities[phase]); changed = now; return
        result = dict(status='passed', world='Editor', identity=bundle['identity'],
                      scope='Normal BP Editor attachment, Fine solver and Niagara asset binding; active simulation recorded separately', rows=rows)
    except Exception:
        result = dict(status='failed', rows=rows, error=traceback.format_exc())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2))
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

handle = u.register_slate_post_tick_callback(tick)
