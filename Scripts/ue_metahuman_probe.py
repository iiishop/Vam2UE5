"""Read-only UE capability/reflective API probe. Does not create a Character or call cloud APIs."""
import json
from pathlib import Path
import re
import sys
import unreal as u
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vam_metahuman import write_json
command = u.SystemLibrary.get_command_line()
match = re.search(r'-VamMHReport=(?:"([^"]+)"|(\S+))', command)
assert match, 'Missing -VamMHReport'
path = Path(match.group(1) or match.group(2)); path.parent.mkdir(parents=True, exist_ok=True)
report = json.loads(u.VamMetaHumanEditorAdapter.probe())
report['reflected_methods'] = {name: hasattr(u.MetaHumanCharacterEditorSubsystem, name) for name in
    ('try_add_object_to_edit', 'remove_object_to_edit', 'conform_to_target_meshes', 'can_build_meta_human',
     'build_meta_human', 'request_auto_rigging', 'request_texture_sources')}
report['factory'] = hasattr(u, 'MetaHumanCharacterFactoryNew')
report['autorig_enum'] = str(u.MetaHumanRigType.JOINTS_AND_BLEND_SHAPES)
params = u.MetaHumanCharacterAutoRiggingRequestParams()
params.set_editor_property('blocking', True); params.set_editor_property('report_progress', False)
texture = u.MetaHumanCharacterTextureRequestParams()
texture.set_editor_property('blocking', True); texture.set_editor_property('report_progress', False)
build = u.MetaHumanCharacterEditorBuildParameters()
build.set_editor_property('absolute_build_path', '/Game/MH00ProbeOnly')
report['request_structs_constructed_without_dispatch'] = True
report['visual_acceptance_passed'] = False
write_json(path, report)
assert all(report['reflected_methods'].values()) and report['factory'], 'Required reflective API missing'
