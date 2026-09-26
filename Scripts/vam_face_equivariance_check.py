"""Real-input single-profile geometry regression; not a visual acceptance test."""
import argparse,copy,json
from pathlib import Path
import numpy as np
from vam_face_adaptive import AdaptiveFit,PROFILES
from vam_metahuman import fingerprint,write_json


def check(request_file,profile,output):
    def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
    request=read(request_file)
    source,head,landmarks=[read(request[k]) for k in ('source','head','landmarks')]
    problem=AdaptiveFit(source,head,landmarks)
    baseline_path=Path(request['output'])/(profile+'.json')
    baseline=np.asarray(read(baseline_path)['normalized_vertices'])*problem.scale+problem.origin
    axis=np.array([.2,-.4,.7]);axis/=np.linalg.norm(axis);angle=.63
    x,y,z=axis;k=np.array([[0,-z,y],[z,0,-x],[-y,x,0]])
    rotation=np.eye(3)+np.sin(angle)*k+(1-np.cos(angle))*(k@k)
    scale=1.7;offset=np.array([7.,-5,3])
    moved_source=copy.deepcopy(source);moved_head=copy.deepcopy(head)
    for mesh in (moved_source,moved_head):
        for name in ('vertices','apose_vertices'):
            if name in mesh:mesh[name]=(np.asarray(mesh[name])@rotation.T*scale+offset).tolist()
    moved=AdaptiveFit(moved_source,moved_head,landmarks)
    result,history=moved.solve(profile)
    restored=((result*moved.scale+moved.origin-offset)/scale)@rotation
    error=np.linalg.norm(restored-baseline,axis=1)
    same_regions=np.array_equal(problem.ids,moved.ids)
    result={'schema':'vam-face-equivariance/1','request_sha256':fingerprint(request_file),
            'baseline_candidate_sha256':fingerprint(baseline_path),'profile':profile,
            'max_delta_cm':float(error.max()),'mean_delta_cm':float(error.mean()),
            'same_face_vertex_identity':bool(same_regions),'quad_diagonal_protection':bool(head.get('official_quads')),
            'passed':bool(error.max()<1e-6 and same_regions),'visual_acceptance_passed':False,
            'scope':'One profile geometry under rigid transform and scale; not cross-identity or projected-view score equivalence'}
    write_json(output,result)
    if not result['passed']:raise ValueError('GeometryEquivarianceRegression')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);p.add_argument('--profile',choices=PROFILES,default='Conservative');p.add_argument('--output',required=True)
    a=p.parse_args();print(json.dumps(check(a.request,a.profile,a.output)))
