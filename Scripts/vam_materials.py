"""Source appearance interpretation from locked plans/Source IR, never directory search.

Source values stay separate from the explicitly approximate UE reference adapter.
"""
import colorsys
import copy
import hashlib
import io
import json
from pathlib import Path
import zipfile
from vam_decode import require,finite,MAX_BYTES
from vam_plan import canonical,sha
from vam_unity import Bundle

SEMANTICS={'_MainTex':('base_color','sRGB'),'_SpecTex':('specular','linear'),
 '_GlossTex':('gloss','linear'),'_BumpMap':('normal','linear'),
 '_AlphaTex':('opacity','linear'),'_DecalTex':('decal','sRGB'),
 '_DetailNormalMap':('detail_normal','linear')}
PARAMS={'Diffuse Color':'_Color','Skin Color':'_Color','Specular Color':'_SpecColor',
 'Subsurface Color':'_SubdermisColor','Alpha Adjust':'_AlphaAdjust','Alpha Cutoff':'_Cutoff',
 'Diffuse Texture Offset':'_DiffOffset','Gloss':'_Shininess','Gloss Texture Offset':'_GlossOffset',
 'Specular Intensity':'_SpecInt','Specular Texture Offset':'_SpecOffset','Specular Fresnel':'_Fresnel',
 'Diffuse Bumpiness':'_DiffuseBumpiness','Specular Bumpiness':'_SpecularBumpiness','Global Illumination Filter':'_IBLFilter'}
DYNAMIC_SHADER='Custom/Subsurface/TransparentGlossNMNoCullSeparateAlpha'


def reference_blend(m):
    """Keep source state; use depth-writing coverage for binary reference surfaces."""
    source=m['render_state']['blend']
    if source!='translucent' or 'SeparateAlpha' not in (m['source_shader'].get('name') or ''):return source
    if float(m['parameters'].get('_AlphaAdjust',0))<0:return source
    asset=(m['textures'].get('_AlphaTex') or {}).get('asset')
    if not asset:return 'opaque'
    from PIL import Image
    with Image.open(asset['file']) as image:
        channel=image.convert('RGBA').getchannel('A') if asset['encoding']=='unity_decoded_pixels' else image.convert('RGB').convert('L',(1/3,1/3,1/3,0))
        h=channel.histogram();total=sum(h)
    if sum(h[:248])==0:return 'opaque'
    return 'masked' if sum(h[8:248])/max(1,total)<.01 else source


def color(value):
    if isinstance(value,dict) and all(k in value for k in ('h','s','v')):
        return list(colorsys.hsv_to_rgb(*(float(value[k])for k in ('h','s','v'))))+[1.]
    if isinstance(value,dict):return [float(value.get(k,1)) for k in 'rgba']
    return value


class Sources:
    def __init__(self,plan,ir,out):
        self.plan=plan;self.root=Path(plan['source_root']).resolve();self.out=out
        self.files={};self.bundles={};self.cabs={};self.checked={};self.textures={}
        for item in plan['items']:self.files.update(item.get('builtin_mapping',{}).get('files',{}))
        self.items={i['id']:i for i in plan['items']}
        self.edges={(e['from'],e['field']):e for e in plan['edges']}
        self.blobs=out/'blobs';self.blobs.mkdir(parents=True,exist_ok=True)
        self.blob_bytes=sum(p.stat().st_size for p in self.blobs.iterdir() if p.is_file())
        settings_path=out.parent/'settings.json'
        settings=json.loads(settings_path.read_text(encoding='utf8')) if settings_path.exists() else {}
        self.blob_limit=int(settings.get('material_cache_mb',8192))*1024**2

    def blob(self,data,suffix):
        digest=sha(data);path=self.blobs/(digest+suffix)
        if not path.exists():
            require(self.blob_bytes+len(data)<=self.blob_limit,'material_cache_limit',f'Source material cache requires more than {self.blob_limit//1024**2} MiB; increase material_cache_mb in Saved/settings.json')
            temp=path.with_suffix(path.suffix+'.tmp');temp.write_bytes(data);temp.replace(path)
            self.blob_bytes+=len(data)
        return {'sha256':digest,'file':str(path.resolve())}

    def path(self,relative):
        p=(self.root/relative).resolve();require(p.is_relative_to(self.root) and p.is_file(),'material_source',relative);return p

    def bundle(self,name):
        if name not in self.bundles:
            record=self.files[name];p=self.path(record['path'])
            with p.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            require(digest==record['sha256'],'source_changed',record['path']);self.checked[record['path']]=digest
            self.bundles[name]=Bundle(p)
            for a in self.bundles[name].env.assets:self.cabs[a.name.casefold()]=(name,a)
        return self.bundles[name]

    def resolve(self,owner,ref):
        if not ref['m_PathID']:return None
        if ref['m_FileID']==0:return owner.assets_file.objects[ref['m_PathID']]
        cab=owner.assets_file.externals[ref['m_FileID']-1].path.rsplit('/',1)[-1].casefold()
        if cab not in self.cabs:
            for name in sorted(self.files):
                if name=='StandaloneWindows64' or name.endswith('_mb'):continue
                self.bundle(name)
                if cab in self.cabs:break
        require(cab in self.cabs,'unlocked_bundle',cab)
        return self.cabs[cab][1].objects[ref['m_PathID']]

    def shader(self,obj):
        data=obj.read_typetree();form=data.get('m_ParsedForm',{})
        return {'name':form.get('m_Name',data.get('m_Name')),'object':str(obj.path_id),
            'raw':self.blob(obj.get_raw_data(),'.unity-shader'),
            'properties':form.get('m_PropInfo',{}),'subshaders':[{'tags':s.get('m_Tags',{}),
                'passes':[p.get('m_State',{})for p in s.get('m_Passes',[])]}for s in form.get('m_SubShaders',[])]}

    def unity_texture(self,owner,ref,prop):
        obj=self.resolve(owner,ref)
        if obj is None:return None
        require(obj.type.name=='Texture2D','texture_type',obj.type.name)
        key='unity:'+obj.assets_file.name+':'+str(obj.path_id)
        if key not in self.textures:
            texture=obj.read();require(texture.m_Width*texture.m_Height<=64*1024**2,'texture_limit',key)
            raw=self.blob(obj.get_raw_data(),'.unity-texture')
            image=texture.image;buffer=io.BytesIO();image.save(buffer,format='PNG')
            self.textures[key]={**self.blob(buffer.getvalue(),'.png'),'source':key,'source_raw':raw,
                'width':texture.m_Width,'height':texture.m_Height,'format':str(texture.m_TextureFormat),
                'unity_color_space':getattr(texture,'m_ColorSpace',None),'encoding':'unity_decoded_pixels',
                'sampler':str(getattr(texture,'m_TextureSettings',None))}
        return copy.deepcopy(self.textures[key])

    def reference(self,doc,field,raw):
        edge=self.edges.get((doc,field));require(edge is not None,'unlocked_texture',field)
        item=self.items[edge['to']]
        require(item['action'] not in ('missing','unsupported') and item.get('sha256'),'missing_texture',raw)
        key=item['sha256']
        if key not in self.textures:
            if item['source']:
                with zipfile.ZipFile(self.path(item['source'])) as z:
                    info=z.getinfo(item['path']);require(info.file_size<=MAX_BYTES,'texture_limit',raw);data=z.read(info)
            else:
                path=self.path(item['path']);require(path.stat().st_size<=MAX_BYTES,'texture_limit',raw);data=path.read_bytes()
            require(sha(data)==key,'source_changed',raw)
            from PIL import Image
            with Image.open(io.BytesIO(data)) as image:
                require(image.width*image.height<=64*1024**2,'texture_limit',raw)
                image.load();buffer=io.BytesIO();image.save(buffer,format='PNG')
                self.textures[key]={**self.blob(buffer.getvalue(),'.png'),'source':{'item_id':item['id'],'source':item['source'],'path':item['path']},
                    'source_raw':self.blob(data,Path(item['path']).suffix.lower()),'width':image.width,'height':image.height,'encoding':'source_image_pixels'}
        return copy.deepcopy(self.textures[key])


def build_material_ir(plan,ir,preview,out):
    require(ir['plan_id']==plan['plan_id']==preview['plan_id'],'material_plan','Mismatched source inputs')
    sources=Sources(plan,ir,out);materials=[];diagnostics=[];controllers={};source_configs=[]
    def issue(code,location,scope,detail):diagnostics.append({'code':code,'source':location,'affected_bindings':scope,'impact':detail})
    def fresh(mesh,slot):
        return {'binding':{'mesh':mesh,'slot':slot,'region':preview['meshes'][mesh]['materials'][slot]},
          'source_shader':{'name':None},'parameters':{},'textures':{},'layers':[],
          'render_state':{'hidden':False,'blend':'unknown','two_sided':None},'unknown_fields':{}}
    for mi,mesh in enumerate(preview['meshes']):
        for slot in range(len(mesh['materials'])):materials.append(fresh(mi,slot))
    def slots(mi,numbers):return [m for m in materials if m['binding']['mesh']==mi and m['binding']['slot'] in numbers]
    def texture(m,prop,value,location,scale=(1.,1.),offset=(0.,0.)):
        semantic,space=SEMANTICS.get(prop,('unknown','unknown'))
        m['textures'][prop]={'semantic':semantic,'color_space':space,'uv_set':0,'source_scale':list(scale),'source_offset':list(offset),
            'ue_scale':list(scale),'ue_offset':[offset[0],1-scale[1]-offset[1]],'asset':value,'source':location,
            'normal_convention':('unverified_packed_unity' if value and value['encoding']=='unity_decoded_pixels' else 'Unity tangent RGB; UE V-flipped UV requires matching tangent basis') if semantic=='normal' else None}
    character=next((i for i in plan['items'] if i.get('builtin_mapping',{}).get('entry',{}).get('role')=='character'),None)
    body_index=next((i for i,m in enumerate(preview['meshes']) if m['locator'].get('source')=='builtin'),None)
    regions={};defs=[]
    if character and body_index is not None:
        entry=character['builtin_mapping']['entry'];name=entry['locator']['file']
        try:
            body_bundle=sources.bundle(name)
            mesh_obj=next(o for o in body_bundle.env.objects if str(o.path_id)==preview['meshes'][body_index]['locator']['object'])
            mesh=body_bundle.read(mesh_obj,'DAZMergedMesh')
            for slot,ref in enumerate(mesh['materials']):
                m=slots(body_index,[slot])[0]
                try:
                    obj=sources.resolve(mesh_obj,ref);d=obj.read_typetree();props=d['m_SavedProperties']
                    m['source_material']={'object':str(obj.path_id),'container':obj.assets_file.name,'raw':sources.blob(obj.get_raw_data(),'.unity-material'),'metadata':d}
                    shader=sources.resolve(obj,d['m_Shader'])
                    if shader:m['source_shader']=sources.shader(shader)
                    m['parameters'].update(dict(props.get('m_Floats',[])));m['parameters'].update({k:color(v)for k,v in props.get('m_Colors',[])})
                    m['render_state']['hidden']=not bool(mesh['materialsEnabled'][slot])
                    for prop,env in props.get('m_TexEnvs',[]):
                        try:texture(m,prop,sources.unity_texture(obj,env['m_Texture'],prop),{'container':obj.assets_file.name,'object':str(obj.path_id),'field':prop},[env['m_Scale'][k]for k in 'xy'],[env['m_Offset'][k]for k in 'xy'])
                        except Exception as exc:issue('texture_decode',m['binding'],[m['binding']],str(exc))
                except Exception as exc:issue('material_decode',m['binding'],[m['binding']],str(exc))
            for bundle_name in ('a_per',name):
                b=sources.bundle(bundle_name)
                for obj,cls,d in b.records({'DAZCharacterMaterialOptions','DAZCharacterTextureControl'}):
                    defs.append({'container':bundle_name,'object':str(obj.path_id),'class':cls,'parameters':d})
                    if cls=='DAZCharacterTextureControl':regions.update({r:d.get(key,[])for r,key in [('face','faceMaterialNums'),('torso','torsoMaterialNums'),('limbs','limbMaterialNums'),('genitals','genitalMaterialNums')]})
                    elif d.get('overrideId') and d.get('paramMaterialSlots'):
                        if d['overrideId'].startswith('Male') and entry['gender']=='female':continue
                        if d['overrideId'].startswith('Female') and entry['gender']=='male':continue
                        controllers[d['overrideId']]=(slots(body_index,d['paramMaterialSlots']),obj,d)
        except Exception as exc:issue('body_material_binding',character['path'],[{'mesh':body_index}],str(exc))
    dynamic_shader={'name':DYNAMIC_SHADER,'evidence':'DAZMesh.shaderNameForDynamicLoad / LoadFromBinaryReader'}
    if 'z_sha' in sources.files:
        for obj in sources.bundle('z_sha').env.objects:
            if obj.type.name=='Shader' and obj.read_typetree().get('m_ParsedForm',{}).get('m_Name')==DYNAMIC_SHADER:
                dynamic_shader=sources.shader(obj);break
    # Per-item storable IDs and slot lists come from the actual decoded VAB.
    for record in ir['records']:
        if record['kind']!='dynamic':continue
        matches=[i for i,m in enumerate(preview['meshes']) if m['locator'].get('source')==record['source'] and m['locator'].get('path')==record['path']]
        uid=record['data']['vam'].get('uid','')
        for mi in matches:
            if preview['meshes'][mi]['name']=='Hair guide ribbons':
                controllers[uid+'Sim']=(slots(mi,[0]),None,{'hair':True});continue
            for m in slots(mi,range(len(preview['meshes'][mi]['materials']))):
                m['source_shader']=dynamic_shader
                for prop in dynamic_shader.get('properties',{}).get('m_Props',[]):
                    if prop['m_Type'] in (0,1):m['parameters'][prop['m_Name']]=[prop[f'm_DefValue[{i}]']for i in range(4)]
                    elif prop['m_Type'] in (2,3):m['parameters'][prop['m_Name']]=prop['m_DefValue[0]']
            for option in record['data']['material_options']:
                identity=uid+option['id'][1:] if option['id'].startswith('+') else option['id']
                if option['id'].startswith('+parent+') and record['path'].startswith('Custom/Hair/'):
                    candidate=uid+'CustomScalp'+option['id'][len('+parent+'):]
                    if any(s.get('id')==candidate for s in record['data'].get('vaj',{}).get('storables',[])):
                        identity=candidate
                controllers[identity]=(slots(mi,option['slots']),None,{})

    defaults={id(m):copy.deepcopy(m['textures'])for m in materials}
    def restore(m,prop,location):
        if prop in defaults[id(m)]:m['textures'][prop]=copy.deepcopy(defaults[id(m)][prop])
        else:m['textures'].pop(prop,None)
        m['layers'].append(dict(location,operation='restore_source_default'))

    def apply(doc,index,storable):
        sid=storable.get('id');location={'document':doc,'field':f'/storables/{index}','storable':sid}
        if sid=='textures' and body_index is not None:
            for region,numbers in regions.items():
                for suffix,prop in [('Diffuse','_MainTex'),('Specular','_SpecTex'),('Gloss','_GlossTex'),('Normal','_BumpMap'),('Decal','_DecalTex'),('Detail','_DetailNormalMap')]:
                    field=region+suffix+'Url';raw=storable.get(field)
                    if field not in storable:continue
                    affected=slots(body_index,numbers)
                    if not raw:
                        for m in affected:restore(m,prop,dict(location,parameter=field))
                        continue
                    try:
                        value=None if raw.upper()=='NULL' else sources.reference(doc,location['field']+'/'+field,raw)
                        for m in affected:texture(m,prop,value,dict(location,parameter=field));m['layers'].append(location)
                    except Exception as exc:issue('skin_texture',dict(location,parameter=field),[m['binding']for m in affected],str(exc))
            for key,value in storable.items():
                if key!='id' and not key.endswith('Url'):issue('unsupported_skin_parameter',dict(location,parameter=key),[{'mesh':body_index}],f'{key}={value!r}; preserved, not evaluated by reference shader')
            return
        if sid not in controllers:
            if any(k.startswith('customTexture_') or k in PARAMS for k in storable):issue('unbound_material_config',location,[], 'No evidenced material-slot binding; configuration retained')
            return
        affected,owner,definition=controllers[sid]
        for m in affected:m['layers'].append(location)
        used={'id'}
        # Builtin texture group selections are exact set names on the source component.
        if owner:
            for gi in range(1,6):
                group=definition.get('textureGroup'+str(gi),{});field=group.get('name')
                if not field or field not in storable:continue
                used.add(field);selected=next((x for x in group.get('sets',[])if x['name']==storable[field]),None)
                if not selected:issue('texture_set',dict(location,parameter=field),[m['binding']for m in affected], 'Unknown source texture-set selection');continue
                names=[group.get(k)for k in ('textureName','secondaryTextureName','thirdTextureName','fourthTextureName','fifthTextureName','sixthTextureName')]
                targets=slots(body_index,group.get('materialSlots',[]))
                assignments=[(prop,ref,targets)for prop,ref in zip(names,selected['textures'])]
                if not group.get('mapTexturesToTextureNames'):
                    require(len(group.get('materialSlots',[]))==len(selected['textures']),'texture_group_count',field)
                    assignments=[(prop,ref,slots(body_index,[slot])) for slot,ref in zip(group['materialSlots'],selected['textures'])for prop in names[:2] if prop]
                for prop,ref,targets in assignments:
                    if not prop:continue
                    try:
                        value=sources.unity_texture(owner,ref,prop)
                        for m in targets:
                            texture(m,prop,value,dict(location,parameter=field))
                            defaults[id(m)][prop]=copy.deepcopy(m['textures'][prop])
                    except Exception as exc:issue('builtin_texture',dict(location,parameter=field),[m['binding']for m in targets],str(exc))
        for key,value in storable.items():
            if key in PARAMS:
                used.add(key)
                for m in affected:m['parameters'][PARAMS[key]]=color(value) if isinstance(value,dict) else float(value)
            elif key in ('hideMaterial','renderQueue'):
                used.add(key)
                for m in affected:m['render_state']['hidden' if key=='hideMaterial' else 'render_queue']=str(value).lower()=='true' if key=='hideMaterial' else float(value)
            elif key.startswith('customTexture_'):
                used.add(key);prop=key[len('customTexture'):]
                if not value:
                    for m in affected:restore(m,prop,dict(location,parameter=key))
                    continue
                try:
                    asset=None if value.upper()=='NULL' else sources.reference(doc,location['field']+'/'+key,value)
                    for m in affected:texture(m,prop,asset,dict(location,parameter=key))
                except Exception as exc:issue('custom_texture',dict(location,parameter=key),[m['binding']for m in affected],str(exc))
        names=[definition.get('textureGroup1',{}).get(k)for k in ('textureName','secondaryTextureName','thirdTextureName','fourthTextureName','fifthTextureName','sixthTextureName')]
        if not owner:names=['_MainTex','_SpecTex','_GlossTex','_AlphaTex','_BumpMap','_DecalTex']
        for n,prop in enumerate(names,1):
            keys=[f'customTexture{n}'+k for k in ('TileX','TileY','OffsetX','OffsetY')]
            if any(k in storable for k in keys):
                used.update(keys);sx,sy,ox,oy=[float(storable.get(k,default))for k,default in zip(keys,[1,1,0,0])]
                for m in affected:
                    if prop in m['textures']:m['textures'][prop].update(source_scale=[sx,sy],source_offset=[ox,oy],ue_scale=[sx,sy],ue_offset=[ox,1-sy-oy])
        if definition.get('hair'):
            for m in affected:
                m['source_shader']={'name':'GPUTools Hair / '+str(storable.get('shaderType','unspecified'))}
                m['hair_parameters']=copy.deepcopy(storable)
                if 'rootColor' in storable:m['parameters']['_Color']=color(storable['rootColor']);used.add('rootColor')
            issue('hair_reference_geometry',location,[m['binding']for m in affected], 'Stage03 guide ribbons lack generated strand density, width, curl and source anisotropic shading')
        for key in sorted(set(storable)-used):
            for m in affected:m['unknown_fields'][doc+location['field']+'/'+key]=storable[key]
            issue('unsupported_source_parameter',dict(location,parameter=key),[m['binding']for m in affected], 'Value preserved; effect is not evaluated in UE source reference')

    # Dependency documents first; selected Appearance overrides last. No new scan or path guessing.
    visited=set();order=[]
    def visit(identity):
        if identity in visited:return
        visited.add(identity)
        for e in plan['edges']:
            if e['from']==identity and e['to'] in plan['documents']:visit(e['to'])
        if identity in plan['documents']:order.append(identity)
    for root in plan['roots']:visit(root)
    for doc in order:
        params=plan['documents'][doc]['parameters'];source_configs.append({'id':doc,'raw_sha256':plan['documents'][doc]['raw_sha256'],'parameters':params})
        for index,s in enumerate(params.get('storables',[])):
            try:apply(doc,index,s)
            except Exception as exc:issue('material_config',{'document':doc,'field':f'/storables/{index}'},[],str(exc))
    for m in materials:
        name=m['source_shader'].get('name') or ''
        # Source shader names denote known families; preserve unknown states explicitly.
        m['render_state']['blend']='masked' if 'Cutout' in name else 'translucent' if 'Transparent' in name else 'opaque' if name else 'unknown'
        m['render_state']['two_sided']='NoCull' in name if name else None
        subshaders=m['source_shader'].get('subshaders',[])
        if subshaders:
            tags=dict(subshaders[0]['tags'].get('tags',[]));passes=subshaders[0]['passes']
            m['render_state']['source_tags']=tags
            if passes:
                state=passes[0];m['render_state']['source_first_pass']=state
                m['render_state']['two_sided']=state.get('culling',{}).get('val')==0
                blend=state.get('rtBlend0',{});pair=(blend.get('srcBlend',{}).get('val'),blend.get('destBlend',{}).get('val'))
                m['render_state']['blend']='masked' if tags.get('RenderType')=='TransparentCutout' else 'translucent' if pair in ((5.,10.),(1.,10.)) else 'opaque' if pair==(1.,0.) else 'unknown'
            if len(passes)>1:issue('multipass_shader',m['source_shader']['name'],[m['binding']], 'Pass states preserved; UE reference combines source multipass behavior into one approximate material')
        m['reference_adapter']={'fidelity':'approximate','shading':'UE default lit; source BRDF/SSS is not equivalent','roughness':.55}
        try:
            m['reference_adapter']['blend']=reference_blend(m)
            if m['reference_adapter']['blend']!=m['render_state']['blend']:
                m['reference_adapter']['blend_reason']='Opaque/binary separate alpha uses depth-writing UE coverage to avoid triangle sorting; source state preserved. Binary threshold: under 1% intermediate pixels.'
        except Exception as exc:issue('reference_alpha_analysis',m['binding'],[m['binding']],str(exc))
        issue('source_shader_approximation',{'shader':m['source_shader'].get('name'),'object':m['source_shader'].get('object')},[m['binding']], 'UE reference preserves base color/opacity; VaM BRDF, subsurface scattering, specular lobes and render queue require further shader adapters')
        for prop,value in m['parameters'].items():
            if prop not in ('_Color','_AlphaAdjust','_Cutoff'):
                issue('unsupported_shader_parameter',{'shader':name,'parameter':prop,'value':value,'layers':m['layers']},[m['binding']], 'Preserved in SourceMaterialIR; not applied by UE reference adapter')
        for prop,value in m['textures'].items():
            if value.get('asset') and (prop not in ('_MainTex','_AlphaTex','_DecalTex','_BumpMap') or (prop=='_BumpMap' and value['asset']['encoding']=='unity_decoded_pixels')):
                issue('unsupported_reference_texture',value['source'],[m['binding']],prop+': source pixels retained; packed normals/specular/gloss/detail are not yet evaluated by reference adapter')
        m['id']=sha(canonical(m))
    result={'schema':1,'interpreter':'vam-source-material-1','plan_id':plan['plan_id'],'source_decode_id':ir['decode_id'],
        'materials':materials,'source_configs':source_configs,'source_component_definitions':defs,'source_hashes':sources.checked,
        'diagnostics':diagnostics,'status':'partial' if diagnostics else 'ready',
        'conventions':{'uv':'Source UV retained; Stage03 reference uses (u,1-v). Offset transforms to (ox,1-sy-oy).','color':'Texture semantic is separate from pixel bytes; source HSV/RGBA preserved in configs.','normal':'Normal vs bump semantics retained. Packed Unity normal channels are not silently treated as RGB normals.','precedence':'locked dependency configs, then selected Appearance; source default material remains underneath'},
        'validation_scene':{'camera_location_cm':[290,-290,160],'camera_rotation_deg':[-9,135,0],'fov':40,'exposure_ev100':0,'key_intensity':3.14159,'fill_intensity':1.,'background':[.18,.18,.18]}}
    finite(result);result['material_id']=sha(canonical(result));return result
