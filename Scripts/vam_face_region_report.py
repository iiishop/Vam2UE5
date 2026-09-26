"""Report actual Morph-support region errors; never call them verified curves."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
import numpy as np
from vam_face_adaptive import AdaptiveFit
from vam_face_fidelity import Surface
from vam_face_semantics import REQUIRED
from vam_metahuman import write_json,fingerprint
from vam_face_topology_equivalence import equivalent


def read(p):return json.loads(Path(p).read_text(encoding='utf8'))


def report(experiment,exported,calibration,views_size=0):
    job=Path(experiment);task=read(job/'task.json');source=read(job/'source.json');head=read(job/'head.json')
    mapping=read(calibration)
    if mapping['topology_digest']!=source['source_base_topology_sha256'] or mapping['topology_family']!=source['source_topology_family']:
        raise ValueError('UnsupportedSourceTopologyFamily')
    actual=read(Path(exported)/'actual-head.json');pose=read(job/'pose-transform.json')
    topology_check=equivalent(head['triangles'],actual['triangles'])
    head=dict(head,triangles=actual['triangles'])
    v=(np.array(actual['vertices'])-pose['apose_center'])@np.array(pose['posed_to_apose_rotation']).T+pose['posed_center']
    problem=AdaptiveFit(source,head,read(task['landmarks']['path']))
    before=Surface(problem.base,problem.tf).query(problem.s)[2]*problem.scale
    after=Surface((v-problem.origin)/problem.scale,problem.tf).query(problem.s)[2]*problem.scale
    remap={raw:i for i,raw in enumerate(source['input_to_source_vertex'])}
    regions={}
    for name in REQUIRED:
        entry=mapping['semantics'][name];evidence=entry['candidate_evidence_ids']
        original=sorted({i for key in evidence for i in mapping['morph_support_evidence'][key]['face_support_vertex_ids'] if i in remap})
        ids=[remap[i] for i in original]
        value={'verification_state':entry['verification_state'],'metric_kind':'morph_delta_support_region_distance_not_semantic_curve_error',
               'morph_evidence_ids':evidence,'source_vertex_ids':original,'vertex_count':len(ids)}
        if ids:
            value.update(baseline_mean_cm=float(before[ids].mean()),actual_mean_cm=float(after[ids].mean()),
                         actual_p95_cm=float(np.quantile(after[ids],.95)))
        else:value.update(state='NoIndependentMorphSupport',baseline_mean_cm=None,actual_mean_cm=None,actual_p95_cm=None)
        regions[name]=value
    result={'schema':'vam-face-region-diagnostics/1','calibration_sha256':fingerprint(calibration),
            'regions':regions,'topology_check':topology_check,'visual_acceptance_passed':False,
            'official_export_directory':str(Path(exported).resolve()),
            'actual_head_sha256':fingerprint(Path(exported)/'actual-head.json'),
            'character_sha256':read(Path(exported)/'reload.json')['character_sha256'],
            'scope':'Morph support regions can overlap. Metadata discovers candidates; actual decoded vertex sets define these measurements.'}
    write_json(job/'semantic-region-diagnostics.json',result)
    if views_size:
        from vam_face_fidelity_views import comparison
        camera=comparison(source,{'vertices':v.tolist(),'triangles':actual['triangles']},problem.metric_map,
            job/'official-high-resolution-seven-views.png','OFFICIAL OUTPUT',views_size)
        write_json(job/'official-high-resolution-camera.json',camera)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--experiment',required=True);p.add_argument('--export',dest='exported',required=True);p.add_argument('--calibration',required=True)
    p.add_argument('--views-size',type=int,default=0)
    a=p.parse_args();r=report(a.experiment,a.exported,a.calibration,a.views_size);print(json.dumps({'regions':len(r['regions']),'visual_acceptance_passed':False}))
