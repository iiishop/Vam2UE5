"""Re-score the actual independently reloaded official Template output."""
import json
from pathlib import Path
import numpy as np
from vam_face_fidelity import Problem
from vam_face_semantics import FidelityError
from vam_metahuman import write_json, fingerprint


def verify(job, exported, report_name='post-template-metrics.json'):
    job, exported = Path(job), Path(exported)
    def read(path): return json.loads(path.read_text(encoding='utf8'))
    task = read(job/'task.json')
    for name in ('source', 'head'):
        if fingerprint(job/(name+'.json')) != task[name+'_sha256']: raise FidelityError('TaskGeometryChanged:'+name)
    if fingerprint(job/'semantic-correspondence.json') != task['semantic_correspondence_sha256']: raise FidelityError('SemanticMapChanged')
    source, original = read(job/'source.json'), read(job/'head.json')
    semantic = read(job/'semantic-correspondence.json'); actual = read(exported/'actual-head.json')
    if original['triangles'] != actual['triangles'] or len(original['vertices']) != len(actual['vertices']):
        raise FidelityError('OfficialTemplateTopologyChanged')
    transform = read(job/'pose-transform.json')
    if fingerprint(job/'pose-transform.json') != task.get('pose_transform_sha256'):
        raise FidelityError('PoseTransformChanged')
    r = np.asarray(transform['posed_to_apose_rotation'])
    v = (np.asarray(actual['vertices'])-transform['apose_center'])@r.T+transform['posed_center']
    problem = Problem(source, original, semantic)
    result = problem.metrics((v-problem.origin)/problem.scale)
    baseline = problem.metrics(problem.base)
    numerical_pass = result['flipped_triangles'] == 0 and result['score'] <= baseline['score']+1e-5 and all(
        value <= baseline['semantics_normalized'][name]*1.05+1e-5 for name, value in result['semantics_normalized'].items())
    expected = read(job/'template.json')['head_vertices']
    error = np.linalg.norm(np.asarray(actual['vertices'])-expected, axis=1)
    reload_report = read(exported/'reload.json')
    if reload_report['max_reload_delta_cm'] is None: raise FidelityError('MissingIndependentReloadComparison')
    if report_name == 'post-rig-metrics.json' and not reload_report.get('full_rig'):
        raise FidelityError('MissingFullRigAfterAutoRig')
    report = {'state': 'OfficialTemplateGeometryVerified' if numerical_pass else 'OfficialTemplateRegression',
              'actual_metrics': result, 'baseline_metrics': baseline, 'eligible_for_rig': bool(numerical_pass),
              'template_max_error_cm': float(error.max()), 'character': reload_report['character'],
              'character_sha256': reload_report['character_sha256'], 'visual_acceptance_passed': False}
    write_json(job/report_name, report)
    from vam_face_fidelity_views import comparison
    comparison(source, {'vertices': v.tolist(), 'triangles': actual['triangles']}, semantic,
               job/(Path(report_name).stem+'-seven-views.png'), label='OFFICIAL OUTPUT DRAFT')
    if not numerical_pass: raise FidelityError('OfficialTemplateRegression: Draft retained; no cloud promotion')
    return report


if __name__ == '__main__':
    import argparse
    p=argparse.ArgumentParser(); p.add_argument('--job', required=True); p.add_argument('--export', dest='exported', required=True)
    a=p.parse_args(); print(json.dumps(verify(a.job, a.exported)))
