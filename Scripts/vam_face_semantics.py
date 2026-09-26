"""Versioned, topology-bound face annotations. Never infer identity from positions.

Annotations are authored once per base topology, independently of a character.
An unreviewed template is deliberately not a usable semantic map.
"""
import hashlib
import json
import numpy as np
from scipy import sparse

SCHEMA = 'vam-source-face-semantic-map/1'
SIDED = ('upper_eyelid', 'lower_eyelid', 'inner_canthus', 'outer_canthus',
         'nose_ala', 'nostril', 'mouth_corner', 'nasolabial', 'cheek', 'cheekbone', 'ear')
REQUIRED = tuple(f'{n}.{s}' for n in SIDED for s in ('left', 'right')) + (
    'nose_bridge', 'nose_tip', 'columella', 'upper_lip', 'lower_lip',
    'philtrum', 'jaw', 'chin', 'face_silhouette')


class FidelityError(ValueError):
    pass


def topology(mesh):
    t = np.asarray(mesh['triangles'])
    if t.size == 0 or t.size % 3 or not np.issubdtype(t.dtype, np.integer):
        raise FidelityError('InvalidTriangles')
    t = t.reshape(-1, 3)
    count = len(mesh['vertices'])
    if t.min() < 0 or t.max() >= count:
        raise FidelityError('TriangleIndexOutOfRange')
    payload = {'vertex_count': count, 'triangles': t.ravel().tolist()}
    return hashlib.sha256(json.dumps(payload, separators=(',', ':')).encode()).hexdigest()


def geometry(mesh):
    v = np.asarray(mesh['vertices'], float)
    topology(mesh)
    if v.shape != (len(v), 3) or not np.isfinite(v).all():
        raise FidelityError('InvalidVertices')
    return v, np.asarray(mesh['triangles'], int).reshape(-1, 3)


def annotation_matrix(points, mesh):
    """Sparse barycentric evaluation: vertex, edge or triangle identities only."""
    _, triangles = geometry(mesh)
    edges = {tuple(sorted(e)) for t in triangles for e in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0]))}
    rows, columns, weights = [], [], []
    for row, point in enumerate(points):
        ids = point.get('vertices', [])
        w = point.get('weights', [])
        if (not 1 <= len(ids) <= 3 or len(ids) != len(w) or len(set(ids)) != len(ids)
                or any(type(i) is not int or not 0 <= i < len(mesh['vertices']) for i in ids)
                or not np.isfinite(w).all() or min(w) < 0 or abs(sum(w)-1) > 1e-8):
            raise FidelityError('InvalidBarycentricIdentity')
        if len(ids) == 2 and tuple(sorted(ids)) not in edges:
            raise FidelityError('AnnotationNotMeshEdge')
        if len(ids) == 3:
            face = point.get('triangle')
            if type(face) is not int or not 0 <= face < len(triangles) or list(triangles[face]) != ids:
                raise FidelityError('AnnotationNotOrderedTriangle')
        rows.extend([row]*len(ids)); columns.extend(ids); weights.extend(w)
    return sparse.csr_matrix((weights, (rows, columns)), shape=(len(points), len(mesh['vertices'])))


def validate_map(data, source, target):
    if data.get('schema') != SCHEMA or type(data.get('revision')) is not int or data['revision'] < 1:
        raise FidelityError('SemanticMapVersionUnsupported')
    if data.get('status') != 'Reviewed' or not data.get('provenance'):
        raise FidelityError('AwaitingSemanticMapReview')
    if source.get('source_topology_family') and data.get('source_topology_family') != source['source_topology_family']:
        raise FidelityError('UnsupportedTopologyFamily')
    if source.get('source_base_topology_sha256') and data.get('source_base_topology_sha256') != source['source_base_topology_sha256']:
        raise FidelityError('UnsupportedBaseTopology')
    for side, mesh in (('source', source), ('target', target)):
        if data.get(side+'_topology_sha256') != topology(mesh):
            raise FidelityError('UnsupportedTopology:'+side)
        faces = data.get(side+'_face_triangles', [])
        if not faces or len(set(faces)) != len(faces) or any(type(i) is not int or not 0 <= i < len(mesh['triangles'])//3 for i in faces):
            raise FidelityError('MissingOrInvalidFaceRegion:'+side)
    curves = data.get('curves', {})
    missing = sorted(set(REQUIRED)-set(curves))
    if missing:
        raise FidelityError('MissingSemantics:'+','.join(missing))
    resolved = {}
    for name in REQUIRED:
        curve = curves[name]
        if curve.get('verification_state') != 'verified':
            raise FidelityError('UnverifiedSemantic:'+name)
        a, b = curve.get('source', []), curve.get('target', [])
        if not a or len(a) != len(b) or (name not in ('nose_tip', 'chin') and not any(x in name for x in ('canthus', 'corner')) and len(a) < 2):
            raise FidelityError('MissingOrderedCorrespondence:'+name)
        if not curve.get('evidence'):
            raise FidelityError('MissingSemanticEvidence:'+name)
        resolved[name] = (annotation_matrix(a, source), annotation_matrix(b, target))
    return resolved


def template(source, target):
    return {'schema': SCHEMA, 'revision': 1, 'status': 'AwaitingAnnotation',
            'provenance': '', 'source_topology_family': source.get('source_topology_family'), 'source_topology_sha256': topology(source),
            'target_topology_sha256': topology(target), 'source_face_triangles': [],
            'target_face_triangles': [], 'curves': {n: {'source': [], 'target': [], 'evidence': '', 'verification_state': 'unverified'} for n in REQUIRED}}
