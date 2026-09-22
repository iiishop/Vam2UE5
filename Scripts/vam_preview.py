"""Local stage-03 decode worker and bounded browser/UE preview service."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import zipfile

# Dependencies are isolated from the user's and UE's global Python installs.
SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SCRIPTS.parent / 'Saved/Python'))
from vam_decode import DecodeError, require, finite, decode_vmb, decode_vab, preview_mesh, to_ue, MAX_BYTES
from vam_plan import canonical, strict_json, sha, Planner


def decode_plan(plan, catalog):
    from vam_unity import Bundle, mesh_from_unity, skeleton, validate_skin
    baseline_file=catalog.data/'ImportState/manifest.json'
    baseline=strict_json(baseline_file.read_bytes()) if baseline_file.exists() else None
    check = Planner(catalog, baseline=baseline, locked=plan).generate(plan['selection'])
    if check['plan_id'] != plan['plan_id'] or check['status'] == 'cancelled':
        diagnostics=[i for i in check['items'] if i.get('code','').startswith('locked_') or i.get('code')=='cancelled']
        changed=[k for k in plan if k!='plan_id' and plan[k]!=check.get(k)]
        detail='; '.join(i.get('code','')+': '+i.get('reason','') for i in diagnostics)
        raise DecodeError('plan_changed', (detail or 'Changed fields: '+', '.join(changed))+'; regenerate the import plan for changed sources')
    root = Path(plan['source_root']).resolve()
    items = {i['id']:i for i in plan['items']}
    source_hashes, raw_records, render, errors, warnings = {}, [], [], [], []
    read_items = {}
    bundles = {}

    def source_path(relative):
        p = (root/relative).resolve()
        require(p.is_relative_to(root) and p.is_file(), 'source_path', relative)
        return p

    def fingerprint(relative, expected):
        if relative not in source_hashes:
            p=source_path(relative)
            with p.open('rb') as stream: h=hashlib.file_digest(stream,'sha256').hexdigest()
            require(h == expected, 'source_changed', relative)
            source_hashes[relative] = h

    def read(item):
        if item['source']:
            path=source_path(item['source'])
            with zipfile.ZipFile(path) as z:
                info=z.getinfo(item['path']);require(info.file_size<=MAX_BYTES,'read_limit',item['path'])
                b=z.read(info)
        else:
            path=source_path(item['path']);require(path.stat().st_size<=MAX_BYTES,'read_limit',item['path']);b=path.read_bytes()
        require(sha(b)==item['sha256'],'source_changed',item['path'])
        read_items[item['id']] = item
        return b

    def bundle_for(mapping, name):
        record=mapping['files'][name]; fingerprint(record['path'],record['sha256'])
        if name not in bundles: bundles[name]=Bundle(source_path(record['path']))
        return bundles[name]

    def fail(item, exc):
        errors.append({'source':item.get('source',''), 'path':item.get('path',''),
                       'code':getattr(exc,'code','decode_error'),'message':str(exc),
                       'offset':getattr(exc,'offset',None),
                       'references':[e for e in plan['edges'] if e['to']==item.get('id')]})

    for item in items.values():
        if item['action'] in ('missing','unsupported'):
            fail(item,DecodeError(item.get('code',item['action']),item.get('reason','Unavailable resource')))
    items={k:i for k,i in items.items() if i['action'] not in ('missing','unsupported')}
    characters=[i for i in items.values() if i.get('builtin_mapping',{}).get('entry',{}).get('role')=='character']
    require(len(characters)<=1,'multiple_characters','Decode one person at a time')
    bones=[]; body=None; body_source=None; skin_stats=[]; meshes_by_id={}
    if characters:
        try:
            character=characters[0];mapping=character['builtin_mapping'];entry=mapping['entry'];loc=entry['locator']
            body_bundle=bundle_for(mapping,loc['file'])
            bones=skeleton(bundle_for(mapping,'a_per'),entry['gender'])
            for obj,cls,data in body_bundle.records({'DAZMesh','DAZMergedMesh'}):
                mesh=mesh_from_unity(data);meshes_by_id[obj.path_id]=mesh
                raw_records.append({'kind':'unity_mesh','object':str(obj.path_id),'class':cls,'source':loc['file'],
                                    'mesh':mesh,'parameters':{k:v for k,v in data.items() if k not in ('_baseVertices','_OrigUV','_UVVertices','_basePolyList','_UVPolyList','_baseVerticesToUVVertices')}})
            merged=[(o,d)for o,c,d in body_bundle.records({'DAZMergedMesh'})]
            require(len(merged)==1,'body_layout','Need one verified merged body mesh')
            body_obj,body_data=merged[0];body_source=str(body_obj.path_id)
            body=meshes_by_id[body_obj.path_id]
            target_ref=body_data['targetMesh']
            require(target_ref['m_FileID']==0 and target_ref['m_PathID'] in meshes_by_id,'morph_mesh_reference',str(target_ref))
            morph_mesh=meshes_by_id[target_ref['m_PathID']]
            morph_base_count=len(morph_mesh['vertices']); morph_uv_count=len(morph_mesh['uv'])
            for obj,cls,data in body_bundle.records({'DAZSkinV2','DAZMergedSkinV2'}):
                ref=data['dazMesh'];require(ref['m_FileID']==0 and ref['m_PathID'] in meshes_by_id,'mesh_reference',str(ref))
                stats=validate_skin(data,meshes_by_id[ref['m_PathID']],bones);stats['object']=str(obj.path_id);skin_stats.append(stats)
                raw_records.append({'kind':'skin','object':str(obj.path_id),'source':loc['file'],'statistics':stats,'parameters':data})
            # Morph application targets the body base indices. Graft skinning/physics
            # are retained in the IR, not approximated by guessed bone weights.
            body={'vertices':[v[:]for v in body['vertices']],**{k:v for k,v in body.items() if k!='vertices'}}
        except Exception as exc:
            fail(characters[0],exc);body=None;bones=[];skin_stats=[]

    decoded_morphs={}; dynamic_items=[]
    for item in sorted(items.values(),key=lambda x:x['id']):
        try:
            builtin_entry=item.get('builtin_mapping',{}).get('entry',{})
            if builtin_entry.get('role') in ('clothing','hair'):
                if builtin_entry.get('operation')=='clear_hair':
                    raw_records.append({'kind':'clear_hair','id':item['id'],'mapping':item['builtin_mapping']})
                    continue
                # Source selection is preserved; unsupported builtin component
                # layouts must not silently disappear from a successful preview.
                raise DecodeError('builtin_component_pending', 'This builtin clothing/hair component has not yet been decoded; source mapping is retained in the plan')
            if item['resource_kind']=='vmi':
                meta=strict_json(read(item)); count=int(meta.get('numDeltas',0))
                companions=[i for i in items.values() if i['source']==item['source'] and i['path']==item['path'][:-4]+'.vmb']
                if count:
                    require(len(companions)==1,'missing_vmb',item['path']);result=decode_vmb(read(companions[0]),meta,allow_repeated=True)
                else: result={'layout':'VMI/zero-deltas','parameters':meta,'deltas':[]}
                decoded_morphs[item['id']]=result
                raw_records.append({'kind':'morph','id':item['id'],'source':item['source'],'path':item['path'],'data':result})
            elif item.get('builtin_mapping',{}).get('entry',{}).get('role')=='morph':
                mapping=item['builtin_mapping'];locator=mapping['entry']['locator'];b=bundle_for(mapping,locator['file'])
                obj=next((o for o in b.env.objects if str(o.path_id)==locator['object']),None)
                require(obj is not None,'object_reference',str(locator))
                d=b.read(obj,'DAZMorphSubBank')['_morphs'][locator['morph_index']]
                require(d['morphName']==locator['morph_name'],'morph_reference',str(locator))
                deltas=[[v['vertex'],v['delta']['x'],v['delta']['y'],v['delta']['z']]for v in d['deltas']]
                require(len(deltas)==d['numDeltas'],'delta_count_mismatch',item['path'])
                for i,*delta in deltas: require(i>=0,'vertex_index',str(i))
                result={'layout':'DAZMorphBank','parameters':{k:v for k,v in d.items() if k!='deltas'},'deltas':deltas}
                decoded_morphs[item['id']]=result
                raw_records.append({'kind':'morph','id':item['id'],'source':'builtin','path':item['path'],'data':result})
            elif item['resource_kind']=='vab':
                siblings={i['path'].rsplit('.',1)[-1]:i for i in items.values() if i['source']==item['source'] and i['path'][:-4]==item['path'][:-4]}
                require('vam'in siblings and 'vaj'in siblings,'missing_metadata',item['path'])
                result=decode_vab(read(item),strict_json(read(siblings['vam'])),strict_json(read(siblings['vaj'])))
                raw_records.append({'kind':'dynamic','id':item['id'],'source':item['source'],'path':item['path'],'data':result})
                dynamic_items.append((item,result))
        except Exception as e:fail(item,e)

    applied=[]
    if body:
        for root_id in plan['roots']:
            doc=plan['documents'].get(root_id,{}).get('parameters',{})
            storables=doc.get('storables',[])
            for sidx,storable in enumerate(storables):
                if storable.get('id')!='geometry':continue
                for midx,morph in enumerate(storable.get('morphs',[])):
                    try:
                        value=float(morph.get('value',0));finite(value)
                        if value==0:continue
                        prefix=f'/storables/{sidx}/morphs/{midx}/'
                        targets={e['to']for e in plan['edges']if e['from']==root_id and e['field'].startswith(prefix)}
                        if len(targets)!=1:
                            fail({'id':root_id,'path':str(morph)},DecodeError('morph_target','Expected one resolved Morph target'));continue
                        target=next(iter(targets))
                        if target not in decoded_morphs:continue  # Explicit failed source already reported.
                        decoded=decoded_morphs[target]
                        from vam_fit import apply_morph, apply_bone_centers
                        source_item=items[target]
                        segments=source_item['path'].replace('\\','/').casefold().split('/')
                        offset=0;base_count=morph_base_count;uv_count=morph_uv_count
                        if 'female_genitalia' in segments or 'male_genitalia' in segments:
                            graft_ref=body_data['graftMesh']
                            require(graft_ref['m_FileID']==0 and graft_ref['m_PathID'] in meshes_by_id,'morph_domain','Missing graft mesh')
                            graft=meshes_by_id[graft_ref['m_PathID']]
                            offset=body_data['startGraftVertIndex']
                            base_count=len(graft['vertices']);uv_count=len(graft['uv'])
                        report=apply_morph(body,decoded,value,base_count,uv_count,offset)
                        report['vertex_offset']=offset
                        unresolved=apply_bone_centers(bones,decoded,value)
                        if report['outside_uv'] or report['uv_duplicates']:
                            warnings.append(f"{morph.get('uid',morph.get('name',''))}: VaM compatibility: {report['outside_uv']} out-of-domain deltas ignored; {report['uv_duplicates']} UV-seam deltas overwritten by base vertices. Raw deltas retained.")
                        if unresolved:
                            warnings.append('Morph formulas not executed ('+', '.join(sorted(unresolved))+'): '+morph.get('uid',morph.get('name','')))
                        applied.append({'id':target,'value':value,'deltas':len(decoded['deltas']),**report})
                    except Exception as exc:
                        fail({'id':root_id,'path':str(morph)},exc)
        from vam_fit import finalize_bone_centers
        finalize_bone_centers(bones)
        try:
            from vam_fit import apply_graft_boundary
            graft_id=str(body_data['graftMesh']['m_PathID'])
            graft_parameters=next(r['parameters']for r in raw_records if r.get('kind')=='unity_mesh' and r['object']==graft_id)
            raw_records.append({'kind':'graft_transfer','data':apply_graft_boundary(body,morph_mesh,body_data,graft_parameters)})
        except Exception as exc:fail(characters[0],exc)
        render.insert(0,preview_mesh(body,characters[0]['path']+' + preset morphs',{'source':'builtin','object':body_source}))
        warnings.extend(['Static inspection pose; source triaxial skinning, joint corrections and physics are not executed.',
                         'Clothing/custom scalps use validated static skin wrapping; hair roots translate with validated builtin/custom scalps. Hair rotation, smoothing, thickness and simulation are not yet applied.',
                         'Web preview uses inspection colors. UE source materials are available after material parsing.'])
    from vam_fit import fit_wrap
    scalp_cache={}
    def fit_to_body(mesh,wrap):
        failures=[]
        for candidate in (body,dict(morph_mesh,vertices=body['vertices'][:morph_base_count])):
            try:return fit_wrap(mesh,wrap,candidate)
            except DecodeError as exc:failures.append(str(exc))
        raise DecodeError('wrap_target','; '.join(failures))
    for item,result in dynamic_items:
        try:
            component_render=[];fitted=[]
            for m in result['meshes']:
                fit=m
                if body and result['wraps']:
                    require(len(result['wraps'])==1 and len(result['meshes'])==1,'wrap_ambiguous','Expected one verified skin binding')
                    # Validate triangle identity, not just index bounds, before choosing a target.
                    fit=fit_to_body(m,result['wraps'][0])
                fitted.append(fit)
                component_render.append(preview_mesh(fit,result['vam'].get('displayName',item['path']),{'source':item['source'],'path':item['path']}))
            hair=(result['dynamic'] or {}).get('hair')
            if hair:
                verts,indices=[],[];seg=hair['segments'];width=.00025
                roots=hair['root_to_scalp']
                require(len(roots)==len(hair['vertices'])//seg,'hair_root_count','Root map must cover every strand')
                scalp_map=None;scalp_source=None;scalp_target=None
                if hair['scalp']=='CustomScalp' and len(fitted)==1:
                    scalp_source=result['meshes'][0];scalp_target=fitted[0]
                elif body:
                    name=hair['scalp']
                    if name not in scalp_cache:
                        from vam_unity import builtin_scalp
                        catalog_mapping=strict_json((SCRIPTS.parent/'Config/BuiltinCatalog.json').read_bytes())
                        person=bundle_for(characters[0]['builtin_mapping'],'a_per')
                        shared=bundle_for(catalog_mapping,'h_zzz_mat')
                        base_scalp,wrap=builtin_scalp(person,shared,name)
                        scalp_cache[name]=(base_scalp,fit_to_body(base_scalp,wrap))
                        raw_records.append({'kind':'builtin_scalp','name':name,'mesh':base_scalp,'wrap':wrap,'source':'a_per + h_zzz_mat'})
                    scalp_source,scalp_target=scalp_cache[name]
                if scalp_source:
                    from vam_decode import validate_mesh
                    if len(hair['strands'])==len(scalp_source['uv']):scalp_map=validate_mesh(scalp_source)
                    else:require(len(hair['strands'])==len(scalp_source['vertices']),'hair_scalp_domain','Unknown scalp vertex domain')
                for strand,start in enumerate(range(0,len(hair['vertices']),seg)):
                    shift=[0.,0.,0.]
                    if scalp_source:
                        scalp=roots[strand]
                        if scalp_map is not None:scalp=scalp_map[scalp]
                        require(scalp<len(scalp_target['vertices']),'hair_scalp_index',str(scalp))
                        shift=[scalp_target['vertices'][scalp][a]-scalp_source['vertices'][scalp][a] for a in range(3)]
                    for j in range(seg):
                        p=[v+d for v,d in zip(hair['vertices'][start+j],shift)]
                        verts.extend([to_ue([p[0]-width,p[1],p[2]]),to_ue([p[0]+width,p[1],p[2]])])
                        if j:
                            k=len(verts)-4;indices.extend([k,k+2,k+1,k+1,k+2,k+3])
                component_render.append({'name':'Hair guide ribbons','locator':{'source':item['source'],'path':item['path']},'vertices':verts,
                    'uv':[[0,0]for _ in verts],'sections':[indices],'materials':['Hair guides'],'converted_to_source_vertex':[i//2 for i in range(len(verts))]})
            render.extend(component_render)
        except Exception as exc:fail(item,exc)
    require(render,'no_geometry','No supported renderable geometry in selection')
    # Validate source bytes again before committing a result.
    for relative,expected in source_hashes.items():
        with source_path(relative).open('rb') as stream:require(hashlib.file_digest(stream,'sha256').hexdigest()==expected,'source_changed',relative)
    for item in list(read_items.values()): read(item)
    statistics={'meshes':len(render),'vertices':sum(len(m['vertices'])for m in render),
                'triangles':sum(sum(len(s)//3 for s in m['sections'])for m in render),'bones':len(bones),
                'material_regions':sum(len(m['materials'])for m in render),'morphs_applied':len(applied),'skins':skin_stats}
    ir={'schema':1,'decoder':'vam-decode-2','plan_id':plan['plan_id'],'coordinates':{'source':'VaM metres, Y up, Z forward','target':'UE centimetres, Z up, X forward','position':'100 * (z,x,y)','uv':'(u,1-v)','triangulation':'VaM DAZ winding: c,b,a and a,d,c; index maps retained'},
        'source_hashes':source_hashes,'skeleton':bones,'records':raw_records,'applied_morphs':applied,
        'plan_documents':plan['documents'],'statistics':statistics,'errors':errors,'warnings':sorted(set(warnings))}
    finite(ir);digest=sha(canonical(ir));ir['decode_id']=digest
    preview={'schema':1,'decode_id':digest,'plan_id':plan['plan_id'],'meshes':render,
             'skeleton':[{'name':b['name'],'parent':b['parent'],'position':to_ue(b['position'])}for b in bones],
             'statistics':statistics,'errors':errors,'warnings':ir['warnings'],'status':'partial' if errors or warnings else 'ready'}
    return ir,preview


class DecodeService:
    def __init__(self,catalog,plans):
        self.catalog,self.plans=catalog,plans
        self.directory=catalog.data/'Decoded';self.directory.mkdir(exist_ok=True)
        self.lock=threading.Lock();self.process=None;self.state={'running':False,'status':'idle'}
    def status(self):
        with self.lock:return dict(self.state)
    def start(self,identity):
        plan=self.plans.read_plan(identity)
        require(plan['status']!='cancelled','plan_cancelled','Generate a completed plan first')
        with self.lock:
            require(not self.state['running'],'busy','Decode already running')
            self.state={'running':True,'status':'decoding','plan_id':identity,'error':''}
        threading.Thread(target=self.run,args=(identity,),daemon=True).start()
        return self.status()
    def start_materials(self):
        with self.lock:
            require(not self.state['running'],'busy','Decode already running')
            result=self.result()
            self.state={'running':True,'status':'materials','plan_id':result['plan_id'],'error':''}
        threading.Thread(target=self.run,args=(result['plan_id'],True),daemon=True).start()
        return self.status()
    def run(self,identity,materials=False):
        try:
            args=[sys.executable,'-I',str(SCRIPTS/'vam_preview.py'),'--plan',str(self.plans.directory/(identity+'.json')),'--data',str(self.catalog.data)]
            if materials:args=[sys.executable,'-I',str(SCRIPTS/'vam_material_worker.py'),'--data',str(self.catalog.data)]
            with (self.directory/'worker.log').open('w',encoding='utf8') as log:
                with self.lock:
                    if self.state.get('cancelled'):return
                    self.process=subprocess.Popen(args,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                code=self.process.wait(timeout=600 if materials else 300)
            with self.lock:
                if self.state.get('cancelled'):return
            require(code==0,'worker_failed',(self.directory/'worker.log').read_text(encoding='utf8')[-2500:])
            result=strict_json((self.directory/'latest.json').read_bytes())
            with self.lock:self.state.update(status=result['status'],decode_id=result['decode_id'],statistics=result['statistics'])
        except Exception as e:
            if self.process and self.process.poll() is None:self.process.kill()
            with self.lock:self.state.update(status='failed',error=str(e))
        finally:
            with self.lock:self.state['running']=False;self.process=None
    def cancel(self):
        with self.lock:
            self.state.update(cancelled=True,status='cancelled')
            if self.process and self.process.poll() is None:self.process.terminate()
        return self.status()
    def result(self):
        return strict_json((self.directory/'latest.json').read_bytes())
    def material_result(self):
        result=self.result();path=Path(result.get('source_material_ir','')).resolve()
        require(path.parent==(self.catalog.data/'SourceAppearance').resolve(),'material_result','No material result for this preview')
        return strict_json(path.read_bytes())

    def open_ue(self):
        require(not self.status()['running'],'busy','Wait for decoding to finish')
        result=self.result()
        require(result.get('meshes'),'no_geometry','No usable geometry to preview')
        import time
        heartbeat=self.catalog.data/'editor-alive.txt'
        require(heartbeat.exists() and time.time()-heartbeat.stat().st_mtime<10,'editor_required','请在 UE 当前工程的窗口菜单中打开 VaM 资源浏览器，再预览人物。')
        request=self.directory/result.get('appearance_file',result['decode_id']+'.preview.json')
        require(request.resolve().parent==self.directory.resolve(),'preview_path','Invalid preview cache path')
        self.ue_result=request.with_suffix('.ue-result.json')
        if self.ue_result.exists():self.ue_result.unlink()
        queue=self.catalog.data/'preview-request.json'
        require(not queue.exists() and not (self.catalog.data/'preview-active.json').exists(),'busy','当前编辑器正在加载人物')
        temp=queue.with_suffix('.tmp');temp.write_bytes(canonical({'request':str(request.resolve())}));temp.replace(queue)
        self.ue_queued=True
        return {'status':'starting','message':'正在载入当前 UE 场景'}

    def ue_status(self):
        if getattr(self,'ue_result',None) and self.ue_result.exists():return strict_json(self.ue_result.read_bytes())
        if getattr(self,'ue_queued',False):return {'status':'starting'}
        if getattr(self,'ue_process',None):return {'status':'starting' if self.ue_process.poll() is None else 'closed'}
        return {'status':'idle'}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--plan',type=Path,required=True);parser.add_argument('--data',type=Path,required=True);args=parser.parse_args()
    from vam_index import Catalog
    plan=strict_json(args.plan.read_bytes());ir,preview=decode_plan(plan,Catalog(args.data))
    output=args.data/'Decoded';output.mkdir(exist_ok=True)
    for name,value in [(ir['decode_id']+'.ir.json',ir),(ir['decode_id']+'.preview.json',preview),('latest.json',preview)]:
        target=output/name;temporary=target.with_suffix('.tmp');temporary.write_bytes(canonical(value));temporary.replace(target)
    print(json.dumps(preview['statistics'],ensure_ascii=False))
