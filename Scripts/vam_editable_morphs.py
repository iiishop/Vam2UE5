"""Explicit, separately locked supplemental Morph set. Never decode the library."""
import copy,hashlib,json,os,zipfile
from pathlib import Path
from vam_plan import Planner,canonical,strict_json
from vam_decode import require,decode_vmb,MAX_BYTES
from vam_unity import Bundle


def extend(ir,plan,data,selection_path):
    from vam_index import Catalog
    selection=strict_json(Path(selection_path).read_bytes())
    require(selection.get('schema')=='vam-editable-morph-selection/1','morph_set_schema',str(selection_path))
    picks=selection.get('builtin',[]);custom=selection.get('catalog_resources',[])
    require(0<len(picks)+len(custom)<=32,'morph_set_limit','Select 1..32 Morph resources explicitly')
    require(len({p['name'] for p in picks})==len(picks) and len({p['catalog_id'] for p in custom})==len(custom),'morph_set_duplicate','Each selected Morph must be unique')
    character=next(i for i in plan['items'] if i.get('builtin_mapping',{}).get('entry',{}).get('role')=='character')
    gender=character['builtin_mapping']['entry']['gender']
    resolver=Planner(Catalog(data));items=[];bundles={};records=[];applications=[]
    require(resolver.root==Path(plan['source_root']).resolve(),'morph_set_root','Catalog root differs from locked character source')
    existing={r['id'] for r in ir['records'] if r.get('kind')=='morph'}
    for pick in picks:
        from vam_native_job_state import check_cancel
        check_cancel()
        identity=resolver.builtin('morph',pick['name'],gender)
        item=resolver.items[identity];mapping=item['builtin_mapping'];loc=mapping['entry']['locator']
        key=loc['file']
        if key not in bundles:bundles[key]=Bundle(Path(plan['source_root'])/mapping['files'][key]['path'])
        bundle=bundles[key];obj=next(o for o in bundle.env.objects if str(o.path_id)==loc['object'])
        morph=bundle.read(obj,'DAZMorphSubBank')['_morphs'][loc['morph_index']]
        require(morph['morphName']==loc['morph_name'],'morph_set_identity',pick['name'])
        decoded={'layout':'DAZMorphBank','parameters':{k:v for k,v in morph.items() if k!='deltas'},
                 'deltas':[[v['vertex'],*[v['delta'][a] for a in 'xyz']] for v in morph['deltas']]}
        records.append({'kind':'morph','id':identity,'source':'builtin','path':pick['name'],'data':decoded,'editable_metadata':pick})
        items.append(item)
    if custom:
        supplemental=Planner(Catalog(data)).generate([x['catalog_id'] for x in custom])
        require(supplemental['status']=='ready','morph_set_plan','Supplemental dependencies not ready')
        def read(item):
            root=Path(plan['source_root']).resolve();path=(root/(item['source'] or item['path'])).resolve()
            require(path.is_relative_to(root),'source_path',str(path))
            if item['source']:
                with zipfile.ZipFile(path) as z:
                    require(z.getinfo(item['path']).file_size<=MAX_BYTES,'read_limit',item['path']);raw=z.read(item['path'])
            else:
                require(path.stat().st_size<=MAX_BYTES,'read_limit',item['path']);raw=path.read_bytes()
            require(hashlib.sha256(raw).hexdigest()==item['sha256'],'source_changed',item['path'])
            return raw
        for item in supplemental['items']:
            if item['resource_kind']!='vmi':continue
            meta=strict_json(read(item));vmb=next((i for i in supplemental['items'] if i['source']==item['source'] and i['path']==item['path'][:-4]+'.vmb'),None)
            require(not int(meta.get('numDeltas',0)) or vmb is not None,'missing_vmb',item['path'])
            segments=item['path'].lower().split('/')
            require(not ('male' in segments and gender=='female' or 'female' in segments and gender=='male'),'morph_gender',item['path'])
            decoded=decode_vmb(read(vmb),meta,allow_repeated=True) if int(meta.get('numDeltas',0)) else {'parameters':meta,'deltas':[]}
            metadata=next((p for p in custom if p['catalog_id']==item['id']),{})
            records.append({'kind':'morph','id':item['id'],'source':item['source'],'path':item['path'],'data':decoded,'editable_metadata':metadata})
        items.extend(supplemental['items'])
    for filename in resolver.source_stamps:resolver.stamp(Path(filename))
    merged=next(r for r in ir['records'] if r.get('class')=='DAZMergedMesh')
    result=copy.deepcopy(ir)
    for r in records:
        if r['id'] in existing:continue
        require(len(r['data']['deltas'])>0 or r['data']['parameters'].get('formulas'),'empty_morph',r['path'])
        offset=merged['parameters']['startGraftVertIndex'] if any(x in r['path'].lower() for x in ('female_genitalia','male_genitalia')) else 0
        applications.append({'id':r['id'],'value':0.,'vertex_offset':offset,'editable_supplement':True})
        result['records'].append(r)
        existing.add(r['id'])
    result['applied_morphs'].extend(applications)
    lock={'schema':'vam-editable-morph-lock/1','source_decode_id':ir['decode_id'],'gender':gender,'selection':selection,
          'items':sorted(items,key=lambda x:x['id']),'records':records,'applications':applications}
    lock['lock_id']=hashlib.sha256(canonical(lock)).hexdigest()
    directory=Path(data)/'MorphSets';directory.mkdir(exist_ok=True)
    path=directory/(lock['lock_id']+'.json')
    if path.exists():require(path.read_bytes()==canonical(lock),'morph_lock_changed',str(path))
    else:path.write_bytes(canonical(lock))
    result['editable_morph_lock']=lock
    return result,lock
