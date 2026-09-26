"""One-time canonical topology-family calibration, with unverified evidence.

Metadata discovers morph candidates ONLY. Semantic supports come exclusively
from decoded deltas. Coordinates of the current character are never consulted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_plan import canonical
from vam_metahuman import write_json, verify_plan, to_creator
from vam_face_semantics import REQUIRED, FidelityError

SCHEMA = 'vam-source-face-topology-calibration/1'
QUERIES = {
    'upper_eyelid': r'eyelid.*(upper|top)|(upper|top).*eyelid',
    'lower_eyelid': r'eyelid.*(lower|bottom)|(lower|bottom).*eyelid',
    'inner_canthus': r'(eye|tear|lacrimal).*(inner|corner)|canth',
    'outer_canthus': r'eye.*(outer|corner)|canth',
    'nose_bridge': r'nose.*(bridge|root)', 'nose_tip': r'nose.*tip',
    'nose_ala': r'nose.*(wing|ala)|nostril.*(flare|width)',
    'nostril': r'nostril', 'columella': r'columella|nose.*septum',
    'upper_lip': r'lip.*upper|upper.*lip', 'lower_lip': r'lip.*lower|lower.*lip',
    'mouth_corner': r'mouth.*corner', 'philtrum': r'philtrum',
    'nasolabial': r'nasolab|nasal.*fold|smile.*line', 'jaw': r'jaw',
    'chin': r'chin', 'cheek': r'cheek', 'cheekbone': r'cheek.*bone',
    'face_silhouette': r'face.*(width|height)|head.*(width|height)', 'ear': r'\bear(?:s|\b)',
}


def base_mesh(ir):
    candidates = [r for r in ir['records'] if r.get('kind') == 'unity_mesh' and
                  r['mesh'].get('names', {}).get('sceneNodeId') in ('Genesis2Female', 'Genesis2Male') and
                  'Face' in r['mesh']['materials'] and 'Head' in r['mesh']['materials']]
    if len(candidates) != 1: raise FidelityError('AmbiguousCanonicalTopology')
    record = candidates[0]; mesh = record['mesh']
    family = {'Genesis2Female': 'G2F', 'Genesis2Male': 'G2M'}[mesh['names']['sceneNodeId']]
    digest = hashlib.sha256(canonical({'vertex_count': len(mesh['vertices']), 'polygons': [p['vertices'] for p in mesh['polygons']]})).hexdigest()
    return family, digest, record


def connected(vertices, edges):
    todo = set(vertices); adjacency = {v: set() for v in todo}
    for a, b in edges:
        if a in todo and b in todo: adjacency[a].add(b); adjacency[b].add(a)
    result = []
    while todo:
        seed = min(todo); group = {seed}; pending = [seed]; todo.remove(seed)
        while pending:
            for n in adjacency[pending.pop()] & todo:
                todo.remove(n); group.add(n); pending.append(n)
        result.append(sorted(group))
    return sorted(result, key=lambda g: (-len(g), g[0]))


def boundary_chains(edges):
    """Order nonbranching material boundaries using adjacency alone."""
    vertices = {v for e in edges for v in e}; result = []
    for group in connected(vertices, edges):
        adjacency = {v: set() for v in group}
        for a, b in edges:
            if a in adjacency and b in adjacency: adjacency[a].add(b); adjacency[b].add(a)
        endpoints = sorted(v for v, neighbors in adjacency.items() if len(neighbors) == 1)
        if any(len(n) > 2 for n in adjacency.values()) or len(endpoints) not in (0, 2):
            result.append({'vertex_ids': group, 'ordered': False, 'reason': 'BranchingBoundary'}); continue
        start = endpoints[0] if endpoints else min(group); chain = [start]
        while True:
            remaining = sorted(adjacency[chain[-1]]-set(chain))
            if not remaining: break
            chain.append(remaining[0])
        result.append({'vertex_ids': chain, 'ordered': len(chain) == len(group), 'closed': not endpoints})
    return result


def discover(catalog, gender, limit=32):
    """Deterministic bounded metadata search; does not label any vertex."""
    selected = {}; by_semantic = {}; discovered = {}
    for semantic, query in QUERIES.items():
        candidates = []
        for entry in catalog['entries']:
            if entry['role'] != 'morph' or entry['gender'] not in (gender, 'any'): continue
            meta = entry.get('metadata', {}); count = meta.get('numDeltas', 0)
            name = str(meta.get('displayName', ''))
            if count and re.search(query, name, re.I):
                # Prefer bounded local supports and official-style catalog
                # aliases, but neither is proof of an anatomical rim.
                candidates.append((0 if any(n.startswith('PHM') for n in entry['names']) else 1,
                                   int(count), name, entry))
        candidates.sort(key=lambda v: (v[0], v[1], v[2]))
        by_semantic[semantic] = []; discovered[semantic] = candidates[:2]
    # Give every region its first candidate before allocating second choices.
    for rank in range(2):
      for semantic, candidates in discovered.items():
        for _, _, _, entry in candidates[rank:rank+1]:
            name = next((n for n in entry['names'] if n.startswith('PHM')), entry['names'][0])
            if name not in selected and len(selected) >= limit: continue
            selected[name] = {'name': name}; by_semantic[semantic].append(name)
    return {'schema': 'vam-editable-morph-selection/1', 'builtin': list(selected.values()),
            'catalog_resources': [], 'discovery_only': by_semantic}


def build(ir, supplemental=None):
    family, digest, record = base_mesh(ir); mesh = record['mesh']; count = len(mesh['vertices'])
    edge_materials = {}; face_vertices = set()
    for polygon in mesh['polygons']:
        ids = polygon['vertices']; material = mesh['materials'][polygon['materialNum']]
        if material in ('Face', 'Head', 'Ears', 'Lips', 'Nostrils', 'Lacrimals', 'Tear'): face_vertices.update(ids)
        for a, b in zip(ids, ids[1:]+ids[:1]):
            edge_materials.setdefault(tuple(sorted((a, b))), set()).add(material)
    boundaries = {}
    for edge, materials in edge_materials.items():
        if len(materials) > 1 and set(edge) <= face_vertices:
            boundaries.setdefault('|'.join(sorted(materials)), []).append(list(edge))
    records = list(ir['records'])+list((supplemental or {}).get('records', []))
    evidence = {}; rejected = []; seen = set()
    graft_ids = {a['id'] for a in ir.get('applied_morphs', []) if a.get('vertex_offset', 0)}
    for morph in records:
        if morph.get('kind') != 'morph' or morph['id'] in seen: continue
        if morph['id'] in graft_ids:
            rejected.append({'id': morph['id'], 'reason': 'GraftVertexDomain'}); continue
        seen.add(morph['id']); data = morph['data']; meta = data['parameters']
        found = [s for s, query in QUERIES.items() if re.search(query, str(meta.get('displayName', '')), re.I)]
        if not found: continue
        # Aggregate repeated delta indices exactly as the source decoder does;
        # support means nonzero net delta, not metadata count or a guess.
        summed = {}; invalid = []
        for index, *delta in data['deltas']:
            if not 0 <= index < count: invalid.append(index); continue
            accum = summed.setdefault(index, [0., 0., 0.])
            for axis in range(3): accum[axis] += delta[axis]
        support = sorted(i for i, delta in summed.items() if any(x != 0 for x in delta))
        local = sorted(set(support)&face_vertices)
        if not local:
            rejected.append({'id': morph['id'], 'reason': 'NoCanonicalFaceSupport'}); continue
        evidence[morph['id']] = {'display_name': meta.get('displayName'),
            'discovery_semantics': found, 'metadata_is_not_semantics': True,
            'region': meta.get('region'), 'group': meta.get('group'), 'is_pose_control': meta.get('isPoseControl'),
            'source_vertex_ids': support, 'face_support_vertex_ids': local,
            'connected_components': connected(local, edge_materials),
            'excluded_nonbase_indices': sorted(set(invalid)),
            'delta_payload_sha256': hashlib.sha256(canonical(data['deltas'])).hexdigest(),
            'source_record_id': morph['id'], 'verification_state': 'unverified',
            'note': 'True delta support. Not an exact semantic curve; never applied to identity geometry.'}
    semantics = {}
    for name in REQUIRED:
        category = name.split('.')[0]
        sources = [key for key, entry in evidence.items() if category in entry['discovery_semantics']]
        semantics[name] = {'semantic_id': name, 'vertex_ids': [], 'edge_chain': [], 'barycentric_locations': [],
            'candidate_evidence_ids': sources, 'evidence': [], 'provenance': [],
            'confidence': None, 'verification_state': 'unverified', 'topology_digest': digest,
            'note': 'Side and ordered rim/curve require canonical topology confirmation. Support is a region, not a rim.'}
    return {'schema': SCHEMA, 'revision': 1, 'topology_family': family, 'topology_digest': digest,
        'vertex_count': count, 'source_mesh_identity': record['object'],
        'canonical_geometry': 'unmorphed SourceIR unity_mesh; no applied_morphs',
        'uv_topology_digest': hashlib.sha256(canonical({'uv_map': mesh['uv_map'], 'uv_polygons': mesh['uv_polygons']})).hexdigest(),
        'material_boundary_edges': boundaries,
        'material_boundary_chains': {name: boundary_chains(edges) for name, edges in boundaries.items()},
        'morph_support_evidence': evidence,
        'rejected_evidence': rejected, 'semantics': semantics,
        'state': 'AwaitingCanonicalReview', 'visual_acceptance_passed': False}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--ir', required=True); p.add_argument('--plan', required=True)
    p.add_argument('--out', required=True); p.add_argument('--data', required=True); p.add_argument('--catalog', required=True)
    args = p.parse_args(); out = Path(args.out)
    if out.exists(): raise FidelityError('CalibrationDestinationExists')
    ir = json.loads(Path(args.ir).read_text(encoding='utf8')); plan = json.loads(Path(args.plan).read_text(encoding='utf8'))
    verify_plan(plan, ir); family, _, record = base_mesh(ir)
    catalog = json.loads(Path(args.catalog).read_text(encoding='utf8'))
    selection = discover(catalog, 'female' if family == 'G2F' else 'male')
    out.mkdir(parents=True); write_json(out/'morph-selection.json', selection)
    # Reuse the existing bounded, source-locked decoder; no duplicate importer.
    from vam_editable_morphs import extend
    _, supplemental = extend(ir, plan, args.data, out/'morph-selection.json')
    result = build(ir, supplemental)
    result['provenance'] = {k: {'path': str(Path(v).resolve()), 'sha256': hashlib.sha256(Path(v).read_bytes()).hexdigest()}
                            for k, v in [('source_ir', args.ir), ('plan', args.plan), ('catalog', args.catalog)]}
    result['supplemental_lock_id'] = supplemental['lock_id']
    write_json(out/(family+'_'+result['topology_digest']+'.json'), result)
    mesh = record['mesh']; triangles = []
    for polygon in mesh['polygons']:
        if mesh['materials'][polygon['materialNum']] not in ('Face', 'Head', 'Ears', 'Lips', 'Nostrils', 'Lacrimals', 'Tear'): continue
        ids = polygon['vertices']
        for i in range(1, len(ids)-1): triangles.extend([ids[0], ids[i], ids[i+1]])
    canonical_mesh = {'vertices': [to_creator(v) for v in mesh['vertices']], 'triangles': triangles,
                      'topology_digest': result['topology_digest'], 'note': 'Unmorphed base; original source vertex indexing'}
    write_json(out/'canonical-face.json', canonical_mesh)
    from vam_face_fidelity_cli import annotation_obj
    annotation_obj(out/'canonical-face.obj', canonical_mesh, range(len(triangles)//3))
    print(json.dumps({'family': family, 'state': result['state'], 'morph_evidence': len(result['morph_support_evidence']),
                      'material_boundaries': len(result['material_boundary_edges']), 'out': str(out.resolve())}))


if __name__ == '__main__': main()
