import unreal as u,os,json
from pathlib import Path
out=Path(os.environ['VAM_B2_OUTPUT']);out.mkdir(parents=True,exist_ok=True)
c=u.load_object(None,os.environ['VAM_B2_CONFIG'])
args=[c.get_editor_property(n) for n in ['definition','glute_structure','leg_jiggle','breast_jiggle','glute_corrective']]
error=u.VamGluteStructureBuilder.export_corrective_diagnostics(*args,str(out/'fields'))
(out/'export-status.json').write_text(json.dumps({'error':error,'configuration':c.get_path_name()},indent=2))
if error:raise RuntimeError(error)
