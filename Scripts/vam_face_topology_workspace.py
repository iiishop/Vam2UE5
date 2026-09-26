"""Build an offline canonical annotation workspace, no external JS or uploads."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_face_topology_calibrate import base_mesh
from vam_metahuman import fingerprint
from vam_face_semantics import topology


def build(calibration_path, canonical_path, head_path, output):
    calibration = json.loads(Path(calibration_path).read_text(encoding='utf8'))
    canonical = json.loads(Path(canonical_path).read_text(encoding='utf8'))
    head = json.loads(Path(head_path).read_text(encoding='utf8'))
    if canonical['topology_digest'] != calibration['topology_digest']: raise ValueError('CanonicalTopologyMismatch')
    ir_ref = calibration['provenance']['source_ir']
    if fingerprint(ir_ref['path']) != ir_ref['sha256']: raise ValueError('SourceIRChanged')
    ir = json.loads(Path(ir_ref['path']).read_text(encoding='utf8'))
    _, digest, record = base_mesh(ir)
    if digest != calibration['topology_digest']: raise ValueError('BaseTopologyChanged')
    source_edges = set()
    for polygon in record['mesh']['polygons']:
        ids = polygon['vertices']
        source_edges.update(tuple(sorted(e)) for e in zip(ids, ids[1:]+ids[:1]))
    payload = {'calibration': calibration, 'source': canonical, 'target': head, 'target_topology_sha256': topology(head),
               'source_edges': sorted(source_edges), 'reference_note': 'Canonical source; official MH topology reference, not a likeness verdict'}
    html = Path(__file__).with_name('face_topology_workspace.html').read_text(encoding='utf8')
    html = html.replace('__PAYLOAD__', json.dumps(payload, ensure_ascii=False, separators=(',', ':')).replace('</', '<\/'))
    output = Path(output)
    if output.exists(): raise ValueError('WorkspaceExists: preserve annotations and use a new output')
    output.write_text(html, encoding='utf8')
    return output


if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('--calibration',required=True); p.add_argument('--canonical',required=True)
    p.add_argument('--head',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); print(build(a.calibration,a.canonical,a.head,a.out))
