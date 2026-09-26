"""Resumable local Face Fidelity task. No Unreal/cloud/assets are mutated here."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_face_semantics import FidelityError, template, topology, validate_map
from vam_metahuman import write_json, neutral_target, verify_plan


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def annotation_obj(path, mesh, triangle_ids):
    # Retain original indexing, including unused vertices, for repeatable
    # authoring in a local mesh editor. Export is local task evidence only.
    import numpy as np
    triangles = np.asarray(mesh['triangles']).reshape(-1, 3)
    original = mesh.get('input_to_source_vertex', list(range(len(mesh['vertices']))))
    with Path(path).open('w', encoding='utf8') as stream:
        stream.write('# Local annotation reference. OBJ index = source.json index + 1.\n')
        for i, v in enumerate(mesh['vertices']):
            stream.write(f'# vertex {i} source_identity {original[i]}\n')
            stream.write('v '+' '.join(str(x) for x in v)+'\n')
        for i in triangle_ids:
            stream.write('f '+' '.join(str(int(x)+1) for x in triangles[i])+'\n')


def prepare(args):
    job = Path(args.job)
    if job.exists(): raise FidelityError('JobExists: use run to resume')
    ir = read(args.source_ir); plan = read(args.plan); verify_plan(plan, ir)
    source = neutral_target(ir)
    # This pipeline does not accept runtime captures or caller-supplied meshes
    # in place of SourceIR p0. Tagged pose/expression morphs are never enabled.
    source['identity_input'] = {'geometry': 'imported_p0', 'pose': 'reference',
        'runtime_deformations': False, 'expression_policy': 'exclude_tagged_pose_expression',
        'limitation': 'Expressions sculpted into an identity morph require source classification; metadata alone cannot detect them.'}
    target = read(args.head)
    if 'apose_vertices' not in target: raise FidelityError('MissingOfficialAPose')
    job.mkdir(parents=True, exist_ok=False)
    write_json(job/'source.json', source); write_json(job/'head.json', target)
    annotation = template(source, target)
    # Broad face region is supported by source material metadata; it is not
    # sufficient evidence for any individual eyelid/nose/nasolabial curve.
    annotation['source_face_triangles'] = [i for i, material in enumerate(source['triangle_materials'])
                                          if material in ('Face', 'Head', 'Ears', 'Lips', 'Nostrils')]
    annotation['target_face_triangles'] = list(range(len(target['triangles'])//3))
    annotation['source_base_topology_sha256'] = source['source_base_topology_sha256']
    annotation['source_identity_note'] = 'source sample indices address source.json; input_to_source_vertex resolves original SourceIR identity'
    write_json(job/'semantic-map.template.json', annotation)
    annotation_obj(job/'source-annotation.obj', source, annotation['source_face_triangles'])
    annotation_obj(job/'official-head-annotation.obj', target, annotation['target_face_triangles'])
    inputs = {k: {'path': str(Path(getattr(args, k)).resolve()), 'sha256': digest(getattr(args, k))}
              for k in ('source_ir', 'plan', 'head')}
    write_json(job/'task.json', {'schema': 'vam-face-fidelity-task/1', 'inputs': inputs,
        'source_sha256': digest(job/'source.json'), 'head_sha256': digest(job/'head.json'),
        'state': 'AwaitingSemanticAnnotation', 'character': None, 'bp': None,
        'visual_acceptance_passed': False})
    return {'state': 'AwaitingSemanticAnnotation', 'job': str(job.resolve())}


def run(args):
    job = Path(args.job); task = read(job/'task.json')
    for entry in task['inputs'].values():
        if digest(entry['path']) != entry['sha256']: raise FidelityError('InputChanged:'+entry['path'])
    for name in ('source', 'head'):
        if digest(job/(name+'.json')) != task[name+'_sha256']: raise FidelityError('TaskGeometryChanged:'+name)
    if not args.semantic_map or not Path(args.semantic_map).is_file():
        raise FidelityError('AwaitingSemanticAnnotation: supply a reviewed topology map')
    semantic_map = read(args.semantic_map); source = read(job/'source.json'); head = read(job/'head.json')
    validate_map(semantic_map, source, head)
    map_hash = digest(args.semantic_map)
    if task.get('semantic_map_sha256') not in (None, map_hash): raise FidelityError('SemanticMapChanged: create a new task')
    task.update(state='Solving', semantic_map_sha256=map_hash)
    write_json(job/'task.json', task); write_json(job/'semantic-correspondence.json', semantic_map)
    task['semantic_correspondence_sha256'] = digest(job/'semantic-correspondence.json')
    write_json(job/'task.json', task)
    from vam_face_fidelity import Problem
    problem = Problem(source, head, semantic_map)
    def checkpoint(profile, result): write_json(job/(profile+'.json'), result)
    def check_cancel():
        if (job/'cancel').exists(): raise InterruptedError('Cancelled: remove cancel file and resume; checkpoints retained')
    fitted, report = problem.candidates(checkpoint, check_cancel)
    # Only convert through the rigid transform between this same MH head's
    # posed coordinates and A pose. Never align to source again to hide error.
    import numpy as np
    a = np.asarray(head['vertices']); b = np.asarray(head['apose_vertices'])
    ca, cb = a.mean(0), b.mean(0); u, _, vt = np.linalg.svd((a-ca).T@(b-cb)); rotation = u@vt
    if np.linalg.det(rotation) < 0 or np.max(np.linalg.norm((a-ca)@rotation+cb-b, axis=1)) > 1e-3:
        raise FidelityError('NonRigidHeadPose: export a consistent official head')
    write_json(job/'pose-transform.json', {'posed_center': ca.tolist(), 'apose_center': cb.tolist(),
        'posed_to_apose_rotation': rotation.tolist(), 'note': 'MH states only; never aligned to source'})
    task['pose_transform_sha256'] = digest(job/'pose-transform.json')
    write_json(job/'metrics.json', report)
    write_json(job/'residual.json', {'vertices': fitted.tolist(), 'triangles': head['triangles'], 'report': report})
    from vam_face_fidelity_views import comparison
    camera = comparison(source, {'vertices': fitted.tolist(), 'triangles': head['triangles']}, semantic_map, job/'source-residual-seven-views.png')
    write_json(job/'comparison-camera.json', camera)
    write_json(job/'template.json', {'schema': 'vam-face-template/1', 'head_vertices': ((fitted-ca)@rotation+cb).tolist(),
        'topology_sha256': hashlib.sha256(json.dumps(head['triangles'], separators=(',', ':')).encode()).hexdigest(),
        'semantic_map_sha256': map_hash, 'report': {'flipped_triangles': report['candidates'][report['chosen_solver_profile']]['metrics']['flipped_triangles']},
        'fidelity_report': report, 'official_topology_signature': topology(head)})
    task.update(state='AwaitingOfficialTemplateImport', chosen_solver_profile=report['chosen_solver_profile'],
                template_sha256=digest(job/'template.json'))
    write_json(job/'task.json', task)
    return task


def main():
    p = argparse.ArgumentParser(description=__doc__); sub = p.add_subparsers(dest='mode', required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--job', required=True)
    for name in ('source-ir', 'plan', 'head'): prep.add_argument('--'+name, required=True)
    solve = sub.add_parser('run'); solve.add_argument('--job', required=True); solve.add_argument('--semantic-map')
    args = p.parse_args()
    try:
        result = prepare(args) if args.mode == 'prepare' else run(args)
        print(json.dumps(result, ensure_ascii=False)); return 0
    except (FidelityError, ValueError, InterruptedError) as exc:
        job = Path(args.job)
        state = 'Partial'
        if args.mode == 'run' and (job/'task.json').is_file():
            task = read(job/'task.json'); task.update(state='AwaitingSemanticAnnotation' if str(exc).startswith(('AwaitingSemantic', 'MissingSemantics')) else 'Partial', recoverable_error=str(exc))
            write_json(job/'task.json', task)
            state = task['state']
        print(json.dumps({'state': state, 'recoverable_error': str(exc)}, ensure_ascii=False)); return 2


if __name__ == '__main__': sys.exit(main())
