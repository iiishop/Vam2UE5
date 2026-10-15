import unreal as u,json
from pathlib import Path
r=Path('I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/LowerBodyContact/ThighJiggleFold')
b=json.loads((r/'bundle.json').read_text());root=b['configuration'].rsplit('/',1)[0]
source=u.load_asset(root+'/Glute/CD_Character');assert source
shape=source.get_editor_property('shape');geometry=shape.get_editor_property('geometry')
ids=list(geometry.get_editor_property('input_to_source'))
binding=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(source.get_editor_property('body')))
(r/'slots/mapping.json').write_text(json.dumps(dict(source=source.get_path_name(),identity=b['identity'],input_to_source=ids,bones=binding),indent=2))
