"""Stage04 isolated worker. Consumes immutable Stage02/03 files only."""
import argparse
import hashlib
from pathlib import Path
import sys
import time
SCRIPTS=Path(__file__).resolve().parent
sys.path[:0]=[str(SCRIPTS),str(SCRIPTS.parent/'Saved/Python')]
from vam_plan import strict_json,canonical,sha
from vam_decode import require
from vam_materials import build_material_ir
from vam_job_progress import publish


def main(data,preview_path=None):
    started=time.perf_counter()
    publish('验证锁定来源','读取预览、计划与来源 IR')
    decoded=data/'Decoded';preview=strict_json((preview_path or decoded/'latest.json').read_bytes())
    ir=strict_json((decoded/(preview['decode_id']+'.ir.json')).read_bytes())
    plan=strict_json((data/'Plans'/(ir['plan_id']+'.json')).read_bytes())
    require(sha(canonical({k:v for k,v in ir.items() if k!='decode_id'}))==ir['decode_id'],'ir_integrity','Source IR hash mismatch')
    require(sha(canonical({k:v for k,v in plan.items() if k!='plan_id'}))==plan['plan_id'],'plan_integrity','Plan hash mismatch')
    root=Path(plan['source_root']).resolve()
    verified={}
    def verify(relative,expected):
        if verified.get(relative)==expected:return
        path=(root/relative).resolve();require(path.is_relative_to(root),'source_path',relative)
        with path.open('rb') as stream:require(hashlib.file_digest(stream,'sha256').hexdigest()==expected,'source_changed',relative)
        verified[relative]=expected
    locked_sources=list(ir['source_hashes'].items())
    for number,(relative,expected) in enumerate(locked_sources,1):
        publish('验证锁定来源',relative,number-1,len(locked_sources))
        verify(relative,expected)
    publish('验证锁定来源','来源文件核验完成',len(locked_sources),len(locked_sources))
    out=data/'SourceAppearance';out.mkdir(exist_ok=True)
    version=sha(b''.join((SCRIPTS/name).read_bytes() for name in ('vam_material_worker.py','vam_materials.py','vam_unity.py','vam_decode.py','vam_plan.py','vam_zip_compat.py')))
    cache=out/('cache-'+sha(canonical([ir['decode_id'],plan['plan_id'],version]))+'.json')
    result=None
    if cache.exists():
        try:
            publish('检查材质缓存','验证缓存结果与已保存贴图')
            candidate=strict_json(cache.read_bytes())
            require(not any('material_cache_limit' in str(d.get('impact','')) for d in candidate.get('diagnostics',[])),'cache_incomplete','Retry textures previously blocked by cache capacity')
            require(sha(canonical({k:v for k,v in candidate.items() if k!='material_id'}))==candidate['material_id'],'cache_integrity','Material cache hash mismatch')
            for relative,expected in candidate['source_hashes'].items():verify(relative,expected)
            checked_blobs=set()
            def check_files(value):
                if isinstance(value,dict):
                    if 'file' in value and 'sha256' in value:
                        path=Path(value['file']).resolve()
                        require(path.is_relative_to(out.resolve()) and path.is_file(),'cache_blob','Missing material blob')
                        key=(str(path),value['sha256'])
                        if key not in checked_blobs:
                            with path.open('rb') as stream:require(hashlib.file_digest(stream,'sha256').hexdigest()==value['sha256'],'cache_blob','Changed material blob')
                            checked_blobs.add(key)
                    for child in value.values():check_files(child)
                elif isinstance(value,list):
                    for child in value:check_files(child)
            check_files(candidate)
            result=candidate
        except Exception as exc:print('Material cache invalidated:',exc)
    cache_hit=result is not None
    if result is None:
        result=build_material_ir(plan,ir,preview,out,publish)
        if not any('material_cache_limit' in str(d.get('impact','')) for d in result.get('diagnostics',[])):
            temporary=cache.with_suffix('.tmp');temporary.write_bytes(canonical(result));temporary.replace(cache)
    target=out/(result['material_id']+'.materials.json')
    publish('保存材质结果','写入 SourceMaterialIR 与外观预览')
    temporary=target.with_suffix('.tmp');temporary.write_bytes(canonical(result));temporary.replace(target)
    preview['source_material_ir']=str(target.resolve());preview['material_id']=result['material_id']
    preview['material_status']=result['status'];preview['material_diagnostic_count']=len(result['diagnostics'])
    preview['appearance_file']=preview['decode_id']+'.appearance.preview.json'
    if preview_path is None:
        current=strict_json((decoded/'latest.json').read_bytes())
        require(current['decode_id']==preview['decode_id'],'preview_changed','Geometry selection changed while materials were being decoded; retry for the current selection')
    for name in ((preview['appearance_file'],) if preview_path else (preview['appearance_file'],'latest.json')):
        path=decoded/name;temporary=path.with_suffix('.tmp');temporary.write_bytes(canonical(preview));temporary.replace(path)
    print('SourceMaterialIR:',target)
    print('Material parse seconds:',round(time.perf_counter()-started,2),'cache_hit:',cache_hit)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--data',type=Path,required=True);parser.add_argument('--preview',type=Path);args=parser.parse_args();main(args.data,args.preview)
