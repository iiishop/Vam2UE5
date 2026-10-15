"""Instantiate the ordinary BP in the actual Editor world and switch both tiers.

This checks editor attachment/lifecycle, not a substitute for GPU geometry audits
or the game/PIE motion recordings.
"""
import json, math, os, time, traceback
from pathlib import Path
import unreal as u

bundle = json.loads(Path(os.environ['VAM_HAIR_REPORT']).read_text())
output = Path(os.environ['VAM_HAIR_EDITOR_OUTPUT'])
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
actor = u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(
    u.load_class(None, bundle['blueprint']+'_C'), u.Vector())
actor.get_editor_property('character').load_character()
hair = actor.get_editor_property('hair')
qualities = [u.VamHairQuality.FINE, u.VamHairQuality.COARSE]
hair.set_hair_simulation_enabled(True)
hair.set_hair_quality(qualities[0])
started = changed = time.monotonic()
phase = 0
rows = []
errors = []

def tick(dt):
    global phase, changed
    try:
        now = time.monotonic()
        assert now-started < 600, 'Editor attachment check timed out'
        actor.set_actor_rotation(u.Rotator(0, math.sin(now-started)*20, 0), False)
        groom = hair.get_editor_property('groom')
        if not groom or now-changed < 15:
            return
        body = actor.get_editor_property('character').get_editor_property('body')
        error = (groom.get_world_location()-body.get_socket_location('head')).length()
        assert math.isfinite(error) and error < .01, error
        niagara = groom.get_editor_property('niagara_components')
        active = sum(bool(n and n.is_active()) for n in niagara)
        assert active > 0, 'Editor has no active hair simulation components'
        rows.append(dict(quality=str(qualities[phase]), actor=actor.get_path_name(),
                         groom=groom.get_editor_property('groom_asset').get_path_name(),
                         head_attachment_error_cm=error, active_niagara=active,
                         status=hair.get_editor_property('status')))
        phase += 1
        if phase < len(qualities):
            hair.set_hair_quality(qualities[phase]); changed = now; return
        result = dict(status='passed', world='Editor', identity=bundle['identity'],
                      scope='Normal BP editor attachment, tier switching and active Niagara lifecycle', rows=rows)
    except Exception:
        result = dict(status='failed', rows=rows, error=traceback.format_exc())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2))
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

handle = u.register_slate_post_tick_callback(tick)
