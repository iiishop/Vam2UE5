import json
from pathlib import Path
import unreal as u
b=u.load_asset('/Game/VamStage06/BP_VamCharacter')
defaults=u.get_default_object(b.generated_class())
c=defaults.get_editor_property('character')
p=c.get_editor_property('material_profile')
result={'profile':p.get_path_name() if p else None,'animation':str(c.get_editor_property('animation_class')),
        'definition':str(c.get_editor_property('definition')),
        'blink_morph_targets':[str(x) for x in defaults.get_editor_property('active_pose').get_editor_property('blink_morph_targets')],
        'body_materials':len(p.get_editor_property('body_materials')) if p else 0}
Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-bp-inspect.json').write_text(json.dumps(result,indent=2),encoding='utf8')
