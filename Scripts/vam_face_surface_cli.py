"""Produce official-order head vertices for the Editor template-fit adapter.

Head input: posed Creator-space vertices, matching apose_vertices, triangles.
Source input: neutral Creator-space skin mesh with correct winding.
"""
import argparse,json,hashlib,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
try:
    import numpy as np
    from vam_face_surface import transfer
except ModuleNotFoundError as exc:
    raise SystemExit('EditorSurfaceDependencyMissing: local NumPy and SciPy are required; no assets modified') from exc
p=argparse.ArgumentParser()
for name in ('source','head','camera','output'):p.add_argument('--'+name,required=True)
a=p.parse_args();out=Path(a.output)
if out.exists():raise FileExistsError('Choose an explicit new output revision')
read=lambda name:json.loads(Path(name).read_text(encoding='utf8'))
s,h,c=read(a.source),read(a.head),read(a.camera)
base=np.asarray(h['vertices']);apose=np.asarray(h['apose_vertices'])
if base.shape!=apose.shape:raise ValueError('OfficialHeadTopologyMismatch')
ac=apose.mean(0);bc=base.mean(0);u,_,vt=np.linalg.svd((apose-ac).T@(base-bc));rotation=u@vt
if np.linalg.det(rotation)<0 or np.max(np.linalg.norm((apose-ac)@rotation+bc-base,axis=1))>1e-3:
    raise ValueError('HeadStatesAreNotRigidlyRelated')
vertices,report=transfer(s['vertices'],s['triangles'],base,h['triangles'],c)
result={'head_vertices':((vertices-bc)@rotation.T+ac).tolist(),'report':report,
        'topology_sha256':hashlib.sha256(json.dumps(h['triangles'],separators=(',',':')).encode()).hexdigest(),
        'inputs':{k:{'path':str(Path(getattr(a,k)).resolve()),'sha256':hashlib.sha256(Path(getattr(a,k)).read_bytes()).hexdigest()} for k in ('source','head','camera')}}
out.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(report))
