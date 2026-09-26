"""Run recoverable, explicitly provisional real-mesh candidate experiments."""
import argparse,json,hashlib,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
import numpy as np
from vam_face_adaptive import AdaptiveFit, PROFILES
from vam_metahuman import write_json,fingerprint
from vam_face_fidelity_views import comparison


def algorithm_fingerprints():
    return {name:fingerprint(Path(__file__).with_name(name)) for name in (
        'vam_face_adaptive.py','vam_face_adaptive_cli.py','vam_face_phong.py',
        'vam_face_fidelity.py','vam_face_surface.py','vam_face_topology_calibrate.py',
        'vam_face_topology_equivalence.py','vam_face_eye_correspondence.py','vam_face_eye_intersections.py','vam_face_eye_regions.py')}


def run(request_file):
    request=json.loads(Path(request_file).read_text(encoding='utf8'));out=Path(request['output']).resolve();out.mkdir(parents=True,exist_ok=True)
    def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
    code=algorithm_fingerprints();code_path=out/'algorithm-lock.json'
    if code_path.exists() and read(code_path)!=code:raise ValueError('AlgorithmChanged: use a new experiment')
    if not code_path.exists() and any((out/(name+'.json')).exists() for name in PROFILES):
        raise ValueError('LegacyCandidateCacheWithoutAlgorithmLock: preserve evidence and use a new experiment')
    write_json(code_path,code)
    lock={key:{'path':str(Path(request[key]).resolve()),'sha256':fingerprint(request[key])} for key in ('source','head','landmarks')}
    if (out/'input-lock.json').exists() and read(out/'input-lock.json')!=lock:raise ValueError('InputsChanged: use a new experiment')
    write_json(out/'input-lock.json',lock)
    source,head,landmarks=[read(request[key]) for key in ('source','head','landmarks')]
    problem=AdaptiveFit(source,head,landmarks)
    write_json(out/'eye-surface-ownership.json',problem.eye_region_diagnostics)
    write_json(out/'correspondence.json',{'state':'ProvisionalAutomaticCandidates','candidates':problem.correspondence,
        'unverified_semantics':'No source nasolabial/nose/lip/ear semantic truth is inferred from nearest surface',
        'metric_region':problem.metric_map})
    report={'schema':'vam-adaptive-fit-report/1','state':'Draft','candidates':{},'chosen_solver_profile':'Baseline','visual_acceptance_passed':False}
    best=problem.base;baseline=problem.metrics(best);best_score=baseline['score'];report['candidates']['Baseline']={'metrics':baseline}
    for name in PROFILES:
        if (out/'cancel').exists():raise InterruptedError('Cancelled: all saved candidates retained')
        path=out/(name+'.json')
        if path.exists():
            saved=read(path);v=np.asarray(saved['normalized_vertices']);history=saved['history']
        else:
            def progress(value):
                if (out/'cancel').exists():raise InterruptedError('Cancelled')
                print(json.dumps({'profile':name,**value}),flush=True)
            v,history=problem.solve(name,progress)
            write_json(path,{'normalized_vertices':v.tolist(),'history':history})
        metrics=problem.metrics(v)
        eligible=(metrics['flipped_triangles']==0 and metrics.get('eye_skin_proper_crossing_pairs') in (None,0) and metrics['surface_mean_cm']<=baseline['surface_mean_cm']*1.02
                  and metrics['normal_one_minus_cosine']<=baseline['normal_one_minus_cosine']*1.05)
        report['candidates'][name]={'metrics':metrics,'eligible_as_experimental_candidate':eligible,'history':history}
        if eligible and metrics['score']<best_score:best=v;best_score=metrics['score'];report['chosen_solver_profile']=name
        write_json(out/'metrics.json',report)
        print(json.dumps({'profile':name,'metrics':{k:metrics[k] for k in ('score','surface_mean_cm','normal_one_minus_cosine','boundary_candidate_mean_cm')},'eligible':eligible}),flush=True)
    world=best*problem.scale+problem.origin
    a=np.asarray(head['vertices']);b=np.asarray(head['apose_vertices']);ca=a.mean(0);cb=b.mean(0)
    u,_,vt=np.linalg.svd((a-ca).T@(b-cb));rotation=u@vt
    if np.linalg.det(rotation)<0 or np.max(np.linalg.norm((a-ca)@rotation+cb-b,axis=1))>1e-3:raise ValueError('NonRigidHeadPose')
    write_json(out/'pose-transform.json',{'posed_center':ca.tolist(),'apose_center':cb.tolist(),'posed_to_apose_rotation':rotation.tolist()})
    write_json(out/'template.json',{'head_vertices':((world-ca)@rotation+cb).tolist(),
        'topology_sha256':hashlib.sha256(json.dumps(head['triangles'],separators=(',',':')).encode()).hexdigest(),
        'report':{'flipped_triangles':report['candidates'][report['chosen_solver_profile']]['metrics']['flipped_triangles']},
        'provisional_semantics':True,'fidelity_report':report})
    camera=comparison(source,{'vertices':world.tolist(),'triangles':head['triangles']},problem.metric_map,out/'source-candidate-seven-views.png','PROVISIONAL RESIDUAL')
    write_json(out/'comparison-camera.json',camera)
    comparison(source,head,problem.metric_map,out/'source-baseline-seven-views.png','BASELINE')
    report.update(state='AwaitingOfficialTemplateExperiment',template_sha256=fingerprint(out/'template.json'),full_semantic_verification=False)
    write_json(out/'metrics.json',report)
    return {'state':report['state'],'chosen':report['chosen_solver_profile'],'output':str(out)}


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);a=p.parse_args();print(json.dumps(run(a.request)))
