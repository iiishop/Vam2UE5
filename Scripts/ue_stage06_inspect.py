import json
from pathlib import Path
import unreal as u
p=u.load_asset('/Game/VamStage06/PA_Stage05Accepted')
result={'class':p.get_class().get_name(),'methods':[x for x in dir(p) if any(k in x.lower() for k in ('body','constraint','physics','bone'))]}
result['constraints']=len(p.get_constraints(False))
result['constraint_names']=[str(x) for x in p.get_constraints(False)]
if p.get_constraints(False):
    c=p.get_constraints(False)[0]
    result['constraint_methods']=[x for x in dir(c) if any(k in x.lower() for k in ('profile','default','instance','limit','bone'))]
for name in ('get_num_bodies','get_num_constraints','get_body_setup_array','get_editor_property_names'):
    try:result[name]=str(getattr(p,name)())
    except Exception as e:result[name+'_error']=str(e)
Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-physics-inspect.json').write_text(json.dumps(result,indent=2),encoding='utf8')
