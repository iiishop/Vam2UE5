"""Editor-host regression for source-derived bilateral eyelid Morphs."""
import json
import time
import traceback
from pathlib import Path

import unreal as u

saved=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild')
latest=json.loads((saved/'latest-native-assets.json').read_text(encoding='utf8'))
report=saved/'stage05-blink-check.json'
folder=latest['folder']
definition=u.load_asset(folder+'/CD_Character')
params={p.get_editor_property('display_name'):p for p in definition.get_editor_property('parameters')}
targets=[str(params[name].get_editor_property('target')) for name in ('Eyes Closed Left','Eyes Closed Right')]
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.EditorLoadingAndSavingUtils.new_blank_map(False)
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
blueprint=u.load_asset(folder+'/BP_VamCharacter')
actors=[sub.spawn_actor_from_class(blueprint.generated_class(),u.Vector(0,index*200,0)) for index in range(2)]
characters=[a.get_editor_property('character') for a in actors]
for character in characters:character.load_character()
started=time.monotonic()

def finish(result):
    report.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()

def tick(dt):
    try:
        assert time.monotonic()-started<90,'Load timeout'
        bodies=[c.get_editor_property('body') for c in characters]
        if not all(bodies):return
        mesh=u.load_asset(folder+'/SK_Body')
        available={m.get_name() for m in mesh.get_editor_property('morph_targets')}
        assert set(targets)<=available,(targets,available)
        for target in targets:
            assert characters[0].set_parameter(target,1.)
            assert abs(bodies[0].get_morph_target(target)-1.)<.001,target
            assert abs(bodies[1].get_morph_target(target))<.001,target
            assert characters[0].set_parameter(target,0.)
            assert abs(bodies[0].get_morph_target(target))<.001,target
        finish({'status':'passed','folder':folder,'targets':targets,'instances_isolated':True,
                'both_eyelids_reach_full_weight_and_reset':True})
    except Exception:
        result={'status':'failed','folder':folder,'error':traceback.format_exc()}
        u.log_error(result['error'])
        finish(result)

handle=u.register_slate_post_tick_callback(tick)
