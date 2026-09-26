"""Generate reusable calibration from explicit mesh/camera/template files."""
import argparse,json,sys,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
try:
    from vam_face_calibration import generate
except ModuleNotFoundError as exc:
    raise SystemExit('EditorCalibrationDependencyMissing: install local NumPy for the calibration Python interpreter; no assets modified') from exc
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--fitted',required=True)
p.add_argument('--landmarks',required=True);p.add_argument('--camera',required=True);p.add_argument('--output',required=True)
p.add_argument('--fitted-space',required=True,choices=('creator','dna'),help='InspectFitGeometry reports DNA coordinates; never infer orientation')
p.add_argument('--experimental-surface-samples',action='store_true',help='Diagnostic only: dense point constraints showed regressions; never auto-promote')
a=p.parse_args();read=lambda f:json.loads(Path(f).read_text(encoding='utf8'))
s,f=read(a.source),read(a.fitted)
if a.fitted_space=='dna':f['vertices']=[[v[0],v[2],v[1]] for v in f['vertices']]
r=generate(s['vertices'],s['triangles'],f['vertices'],read(a.landmarks),read(a.camera),a.experimental_surface_samples)
r['coordinate_frame']='creator-centimeters-x-lateral-y-forward-z-up/v2'
r['inputs']={k:{'path':str(Path(getattr(a,k)).resolve()),'sha256':hashlib.sha256(Path(getattr(a,k)).read_bytes()).hexdigest()} for k in ('source','fitted','landmarks','camera')}
out=Path(a.output)
if out.exists():raise FileExistsError('Calibration output exists; choose an explicit new revision')
out.write_text(json.dumps(r,indent=2),encoding='utf8');print('Saved',len(r['keypoints']),'anchors to',out)
