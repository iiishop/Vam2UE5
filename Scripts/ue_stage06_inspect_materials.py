import json
from pathlib import Path
import unreal as u
root='/Game/VamCharacters/C_c6e9958caee9598900461d95'
d=u.load_asset(root+'/CD_Character');m=d.get_editor_property('body');g=u.load_asset(root+'/GD_Bindings')
regions=g.get_editor_property('regions')
def slots(mesh):
    return [{'slot':i,'name':str(s.get_editor_property('material_slot_name')),'material':s.get_editor_property('material_interface').get_path_name() if s.get_editor_property('material_interface') else None} for i,s in enumerate(mesh.get_editor_property('materials'))]
result={'body':slots(m),'regions':[{'id':str(x.get_editor_property('id')),'semantic':str(x.get_editor_property('anatomical_semantic')),'evidence':str(x.get_editor_property('source_evidence'))[:180]} for x in regions],
        'parts':[{'path':p.get_path_name(),'slots':slots(p)} for p in d.get_editor_property('parts')]}
Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-material-inspect.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
