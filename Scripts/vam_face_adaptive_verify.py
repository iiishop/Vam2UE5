"""Verify official output of provisional fitting without declaring semantics reviewed."""
import json
from pathlib import Path
import numpy as np
from vam_metahuman import fingerprint,write_json,verify_plan,neutral_target
from vam_face_adaptive import AdaptiveFit,PROFILES
from vam_face_fidelity_views import comparison
from vam_face_topology_equivalence import equivalent
from vam_face_template_contract import check as check_template_topology,triangle_digest


def read(path):return json.loads(Path(path).read_text(encoding='utf8'))


def attach_task(experiment,source_task):
    out=Path(experiment);old=read(Path(source_task)/'task.json');lock=read(out/'input-lock.json');report=read(out/'metrics.json')
    # Rendering diagnostics is independent of official import. Numerical
    # candidates and the committed template must be complete before dispatch.
    if not set(PROFILES).issubset(report.get('candidates',{})) or not (out/'template.json').exists():
        raise ValueError('CandidateSolveIncomplete')
    template=read(out/'template.json')
    if template['fidelity_report']['chosen_solver_profile']!=report['chosen_solver_profile']:
        raise ValueError('CandidateTemplateMismatch')
    for value in old['inputs'].values():
        if fingerprint(value['path'])!=value['sha256']:raise ValueError('OriginalSourceLockChanged')
    ir=read(old['inputs']['source_ir']['path']);plan=read(old['inputs']['plan']['path']);verify_plan(plan,ir)
    fresh=neutral_target(ir);source=read(lock['source']['path'])
    for key in ('vertices','triangles','input_to_source_vertex'):
        if fresh[key]!=source[key]:raise ValueError('ExperimentInputIsNotLockedNeutralP0:'+key)
    for key in ('source','head'):
        ref=lock[key]
        if fingerprint(ref['path'])!=ref['sha256']:raise ValueError('ExperimentInputChanged:'+key)
        dest=out/(key+'.json')
        if dest.resolve()!=Path(ref['path']).resolve():
            if dest.exists() and fingerprint(dest)!=ref['sha256']:raise ValueError('OutputInputCollision')
            dest.write_bytes(Path(ref['path']).read_bytes())
    inputs=dict(old['inputs']);inputs['head']=dict(lock['head'])
    task={'schema':'vam-face-fidelity-task/1','solver_kind':'adaptive-provisional','state':'AwaitingOfficialTemplateImport',
          'inputs':inputs,'parent_inputs':old['inputs'],'source_sha256':fingerprint(out/'source.json'),'head_sha256':fingerprint(out/'head.json'),
          'pose_transform_sha256':fingerprint(out/'pose-transform.json'),'template_sha256':fingerprint(out/'template.json'),
          'landmarks':lock['landmarks'],'chosen_solver_profile':report['chosen_solver_profile'],
          'source_semantics_reviewed':False,'visual_acceptance_passed':False,'character':None,'bp':None}
    if (out/'task.json').exists():raise ValueError('TaskAlreadyAttached')
    write_json(out/'task.json',task);return task


def verify(job,exported,report_name='post-template-metrics.json'):
    job=Path(job);exported=Path(exported);task=read(job/'task.json')
    for name,field in [('source','source_sha256'),('head','head_sha256'),('pose-transform','pose_transform_sha256')]:
        if fingerprint(job/(name+'.json'))!=task[field]:raise ValueError('ExperimentGeometryChanged:'+name)
    lm=task['landmarks']
    if fingerprint(lm['path'])!=lm['sha256']:raise ValueError('OfficialLandmarksChanged')
    source=read(job/'source.json');head=read(job/'head.json');actual=read(exported/'actual-head.json');reload=read(exported/'reload.json')
    if reload.get('max_reload_delta_cm') is None or reload['max_reload_delta_cm']>1e-5:raise ValueError('IndependentReloadNotVerified')
    if report_name=='post-rig-metrics.json' and not reload.get('full_rig'):raise ValueError('FullRigMissing')
    if len(actual['vertices'])!=len(head['vertices']):raise ValueError('OfficialHeadVertexIdentityChanged')
    topology_check=equivalent(head['triangles'],actual['triangles'])
    if topology_check['quad_flips']:
        if head.get('official_quads'):
            # AutoRig/RemoveFaceRig may retain the other legal diagonal. Prove
            # coverage of the locked official quads, never infer from positions.
            topology_check=check_template_topology(head,actual['triangles'],triangle_digest(head['triangles']))
        elif report_name!='post-rig-metrics.json':
            raise ValueError('UnexpectedPreRigTriangulationChange')
        if report_name=='post-rig-metrics.json':
            prior=read(job/'Official/TemplateReload/actual-head.json')
            if not head.get('official_quads') and prior['triangles']!=head['triangles']:
                raise ValueError('UnexpectedPreRigTriangulationChange')
            delta=float(np.max(np.linalg.norm(np.asarray(prior['vertices'])-actual['vertices'],axis=1)))
            if delta>1e-4:raise ValueError('RigVertexIdentityNotStable')
            topology_check['max_pre_rig_vertex_delta_cm']=delta
    # Score and render actual official triangles, including their diagonals.
    head=dict(head,triangles=actual['triangles'])
    pose=read(job/'pose-transform.json');r=np.array(pose['posed_to_apose_rotation'])
    world=(np.array(actual['vertices'])-pose['apose_center'])@r.T+pose['posed_center']
    problem=AdaptiveFit(source,head,read(lm['path']));baseline=problem.metrics(problem.base);metrics=problem.metrics((world-problem.origin)/problem.scale)
    eligible=(metrics['flipped_triangles']==0 and metrics.get('eye_skin_proper_crossing_pairs') in (None,0) and metrics['score']<=baseline['score']*1.02
        and metrics['surface_mean_cm']<=baseline['surface_mean_cm']*1.02 and metrics['normal_one_minus_cosine']<=baseline['normal_one_minus_cosine']*1.05)
    report={'state':'OfficialTemplateGeometryVerified' if eligible else 'OfficialTemplateRegression','eligible_for_rig':bool(eligible),
        'actual_metrics':metrics,'baseline_metrics':baseline,'topology_check':topology_check,'character':reload['character'],'character_sha256':reload['character_sha256'],
        'verification_scope':'Provisional geometric regression checks only; no reviewed source semantic correspondence',
        'source_semantics_reviewed':False,'visual_acceptance_passed':False}
    write_json(job/report_name,report)
    camera=comparison(source,{'vertices':world.tolist(),'triangles':head['triangles']},problem.metric_map,job/(Path(report_name).stem+'-seven-views.png'),'OFFICIAL PROVISIONAL')
    write_json(job/(Path(report_name).stem+'-camera.json'),camera)
    if not eligible:raise ValueError('OfficialTemplateRegression: experimental Draft retained')
    return report


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--experiment',required=True);p.add_argument('--source-task',required=True)
    a=p.parse_args();print(json.dumps(attach_task(a.experiment,a.source_task)))
