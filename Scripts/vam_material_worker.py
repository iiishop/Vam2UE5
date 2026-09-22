"""Stage04 isolated worker. Consumes immutable Stage02/03 files only."""
import argparse
import hashlib
from pathlib import Path
import sys
SCRIPTS=Path(__file__).resolve().parent
sys.path[:0]=[str(SCRIPTS),str(SCRIPTS.parent/'Saved/Python')]
from vam_plan import strict_json,canonical,sha
from vam_decode import require
from vam_materials import build_material_ir


def main(data):
    decoded=data/'Decoded';preview=strict_json((decoded/'latest.json').read_bytes())
    ir=strict_json((decoded/(preview['decode_id']+'.ir.json')).read_bytes())
    plan=strict_json((data/'Plans'/(ir['plan_id']+'.json')).read_bytes())
    require(sha(canonical({k:v for k,v in ir.items() if k!='decode_id'}))==ir['decode_id'],'ir_integrity','Source IR hash mismatch')
    require(sha(canonical({k:v for k,v in plan.items() if k!='plan_id'}))==plan['plan_id'],'plan_integrity','Plan hash mismatch')
    root=Path(plan['source_root']).resolve()
    for relative,expected in ir['source_hashes'].items():
        path=(root/relative).resolve();require(path.is_relative_to(root),'source_path',relative)
        with path.open('rb') as stream:require(hashlib.file_digest(stream,'sha256').hexdigest()==expected,'source_changed',relative)
    out=data/'SourceAppearance';out.mkdir(exist_ok=True)
    result=build_material_ir(plan,ir,preview,out)
    target=out/(result['material_id']+'.materials.json')
    temporary=target.with_suffix('.tmp');temporary.write_bytes(canonical(result));temporary.replace(target)
    preview['source_material_ir']=str(target.resolve());preview['material_id']=result['material_id']
    preview['material_status']=result['status'];preview['material_diagnostic_count']=len(result['diagnostics'])
    preview['appearance_file']=preview['decode_id']+'.appearance.preview.json'
    current=strict_json((decoded/'latest.json').read_bytes())
    require(current['decode_id']==preview['decode_id'],'preview_changed','Geometry selection changed while materials were being decoded; retry for the current selection')
    for name in (preview['appearance_file'],'latest.json'):
        path=decoded/name;temporary=path.with_suffix('.tmp');temporary.write_bytes(canonical(preview));temporary.replace(path)
    print('SourceMaterialIR:',target)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--data',type=Path,required=True);args=parser.parse_args();main(args.data)
