"""Read saved fit state in source pose; never saves a Character or level."""
import json
from pathlib import Path
import unreal as u
root=Path(__file__).resolve().parents[1]
out=root/'Saved/MetaHuman/FitRepair'
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
folder='/Game/MetaHumans/启梦MH方向法线诊断'
character=u.load_asset(folder+'/启梦MH方向法线诊断')
target=u.load_asset(folder+'/SM_Target')
assert sub.try_add_object_to_edit(character)
for posed,name in [(True,'posed_state'),(False,'apose_state')]:
    raw=u.VamMetaHumanEditorAdapter.inspect_fit_geometry(character,target,posed)
    assert raw
    (out/(name+'.json')).write_text(raw,encoding='utf8')
sub.remove_object_to_edit(character)
