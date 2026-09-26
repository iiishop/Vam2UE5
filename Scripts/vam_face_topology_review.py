"""Validate one-time canonical annotations and compile a source/MH bridge.

Human-authored input is data, not executable instructions. This never promotes
morph support automatically. Complete verified curves and provenance required.
"""
from vam_face_semantics import FidelityError, REQUIRED, template, validate_map, topology
from vam_face_topology_calibrate import base_mesh


def review(calibration, annotation, ir):
    family, digest, record = base_mesh(ir)
    if calibration.get('topology_family') != family or annotation.get('topology_family') != family:
        raise FidelityError('ReviewTopologyFamilyMismatch')
    if calibration['topology_digest'] != digest or annotation.get('topology_digest') != digest:
        raise FidelityError('ReviewTopologyMismatch')
    if not annotation.get('reviewer') or not annotation.get('reviewed_at'):
        raise FidelityError('MissingCanonicalReviewProvenance')
    mesh = record['mesh']; count = len(mesh['vertices']); edges = set()
    for polygon in mesh['polygons']:
        ids = polygon['vertices']
        edges.update(tuple(sorted(e)) for e in zip(ids, ids[1:]+ids[:1]))
    import copy
    output = copy.deepcopy(calibration)
    for name, entry in annotation.get('semantics', {}).items():
        if name not in REQUIRED: raise FidelityError('UnknownSemantic:'+name)
        ids = entry.get('vertex_ids', [])
        if not ids or len(set(ids)) != len(ids) or any(type(i) is not int or not 0 <= i < count for i in ids):
            raise FidelityError('InvalidCanonicalVertexIdentity:'+name)
        chain = entry.get('edge_chain', [])
        if chain:
            if len(set(chain)) != len(chain) or any(type(i) is not int for i in chain): raise FidelityError('RepeatedOrInvalidChainVertex:'+name)
            if any(tuple(sorted(e)) not in edges for e in zip(chain, chain[1:])): raise FidelityError('DisconnectedCanonicalEdgeChain:'+name)
            if set(chain) != set(ids): raise FidelityError('ChainVertexSetMismatch:'+name)
        elif len(ids) > 1:
            raise FidelityError('OrderedEdgeChainRequired:'+name)
        if entry.get('verification_state') != 'verified' or not entry.get('evidence') or not entry.get('provenance'):
            raise FidelityError('UnverifiedCanonicalAnnotation:'+name)
        # Verified means explicit canonical author review, not visual likeness.
        output['semantics'][name].update(entry)
    output['revision'] += 1
    output['review'] = {k: annotation[k] for k in ('reviewer', 'reviewed_at')}
    output['state'] = 'CanonicalReviewed' if all(e['verification_state'] == 'verified' for e in output['semantics'].values()) else 'AwaitingCanonicalReview'
    return output


def compile_bridge(calibration, source, head, target_annotations):
    """Explicit source-ID pairs; no arclength/spatial guess fills missing pairs."""
    if calibration.get('state') != 'CanonicalReviewed': raise FidelityError('AwaitingCanonicalReview')
    if calibration.get('topology_family') != source.get('source_topology_family'):
        raise FidelityError('UnsupportedTopologyFamily: prepare a new family-bound source input')
    if calibration['topology_digest'] != source.get('source_base_topology_sha256'): raise FidelityError('UnsupportedBaseTopology')
    if target_annotations.get('target_topology_sha256') != topology(head):
        raise FidelityError('TargetAnnotationTopologyMismatch')
    remap = {original: i for i, original in enumerate(source['input_to_source_vertex'])}
    result = template(source, head)
    result.update(status='Reviewed', source_base_topology_sha256=calibration['topology_digest'],
                  provenance={'canonical_review': calibration['review'], 'target_review': target_annotations.get('provenance')})
    if not target_annotations.get('provenance'): raise FidelityError('MissingTargetCorrespondenceReview')
    result['source_face_triangles'] = [i for i, m in enumerate(source['triangle_materials']) if m in ('Face', 'Head', 'Ears', 'Lips', 'Nostrils')]
    # The official head includes neck triangles. A reviewed face region must
    # match the source material region; never silently score neck against chin.
    result['target_face_triangles'] = target_annotations.get('face_triangles', [])
    for name in REQUIRED:
        pairs = target_annotations.get('curves', {}).get(name, [])
        verified_ids = set(calibration['semantics'][name]['vertex_ids'])
        if not pairs: raise FidelityError('MissingTargetCorrespondence:'+name)
        source_points, target_points = [], []
        for pair in pairs:
            original = pair['source_vertex']
            if original not in verified_ids or original not in remap: raise FidelityError('UnverifiedSourceCorrespondence:'+name)
            source_points.append({'vertices': [remap[original]], 'weights': [1.]})
            target_points.append(pair['target'])
        result['curves'][name] = {'source': source_points, 'target': target_points,
            'evidence': calibration['semantics'][name]['evidence'], 'source_vertex_ids': [p['source_vertex'] for p in pairs],
            'verification_state': 'verified'}
    validate_map(result, source, head)
    return result


if __name__ == '__main__':
    import argparse, json
    from pathlib import Path
    from vam_metahuman import write_json
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['review', 'bridge'])
    p.add_argument('--calibration', required=True); p.add_argument('--annotations', required=True)
    p.add_argument('--ir'); p.add_argument('--source'); p.add_argument('--head'); p.add_argument('--out', required=True)
    args = p.parse_args()
    if Path(args.out).exists(): raise FidelityError('OutputExists: write a new revision')
    def read(path): return json.loads(Path(path).read_text(encoding='utf8'))
    c, a = read(args.calibration), read(args.annotations)
    if a.get('schema') == 'vam-topology-review-bundle/1':
        a = a['source_review' if args.mode == 'review' else 'target_review']
    result = review(c, a, read(args.ir)) if args.mode == 'review' else compile_bridge(c, read(args.source), read(args.head), a)
    write_json(args.out, result)
