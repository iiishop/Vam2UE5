"""Replay frozen LBS snapshots without UE or solver history. Requires numpy.
Usage: python replay_deformation_audit.py <bundle-directory>
Exports stage OBJ files and a report with per-stage replay errors and SHA256 evidence.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np

def run(root):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8-sig'))
    result={'schema':'vam-audit-replay/1','actor':manifest['actor'],'configuration':manifest['configuration'],'stages':{}}
    for stage, meta in manifest['stages'].items():
        folder=root/stage
        if not (folder/'inputs.json').exists():
            result['stages'][stage]={'status':'unavailable','detail':meta};continue
        data=json.loads((folder/'inputs.json').read_text(encoding='utf-8-sig'))
        v=np.array(json.loads((folder/'preskin.json').read_text())['positions'],dtype=float)
        matrices=[np.array(b['inverse_bind_matrix_row_major']).reshape(4,4)@np.array(b['component_matrix_row_major']).reshape(4,4) for b in data['bones']]
        output=np.zeros_like(v)
        for i,row in enumerate(data['render_vertex_influences']):
            point=np.r_[v[i],1.]
            for bone,weight in row: output[i]+=(point@matrices[int(bone)])[:3]*weight
        expected=np.array(json.loads((folder/'surface.json').read_text())['positions'])
        errors=np.linalg.norm(output-expected,axis=1)
        result['stages'][stage]={'status':'replayed','max_error_cm':float(errors.max()),'rms_cm':float(np.sqrt(np.mean(errors**2))),'vertices':len(v)}
        with (folder/'replay.obj').open('w') as f:
            f.writelines('v %.9g %.9g %.9g\n'%tuple(p) for p in output)
            tri=data['triangle_indices']
            f.writelines('f %d %d %d\n'%tuple(int(x)+1 for x in tri[i:i+3]) for i in range(0,len(tri),3))
    surface=root/'05-gpu-surface.json'
    if surface.exists() and (root/'04-jiggle/surface.json').exists():
        rendered=np.array(json.loads(surface.read_text())['positions'])
        expected=np.array(json.loads((root/'04-jiggle/surface.json').read_text())['positions'])
        if (root/'cage-current.json').exists():
            topology=json.loads((root/'cage-topology.json').read_text())
            residual=np.array(json.loads((root/'cage-current.json').read_text())['positions'])-np.array(json.loads((root/'cage-animated-target.json').read_text())['positions'])
            for i,(parents,weights,mask) in enumerate(zip(topology['surface_parents'],topology['surface_weights'],topology['surface_mask'])):
                if mask>0:
                    for parent,weight in zip(parents,weights):
                        if parent>=0:expected[i]+=residual[int(parent)]*weight*mask
        if rendered.shape==expected.shape:
            errors=np.linalg.norm(rendered-expected,axis=1)
            result['gpu_reconstruction']={'max_error_cm':float(errors.max()),'p95_cm':float(np.percentile(errors,95)),'cage_same_frame':manifest['cage_matches_capture_frame']}
        else:result['gpu_reconstruction']={'error':'render LOD/vertex count mismatch'}
    result['files']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*.json')) if p.name!='replay-report.json'}
    (root/'replay-report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result['stages'],indent=2))
    if any(s.get('max_error_cm',0)>1e-3 for s in result['stages'].values()):raise SystemExit('Replay mismatch')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('bundle',type=Path);run(parser.parse_args().bundle)
