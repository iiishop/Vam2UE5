"""Build native source-reference assets from a calibrated immutable contract.

Each build is validated separately. Source calibration/formula limitations
are persisted on the CharacterDefinition and in the transaction report.
"""
import hashlib
import json
import os,re
from pathlib import Path
import unreal as u


def props(obj,**values):
    for k,v in values.items():obj.set_editor_property(k,v)
    return obj


def build(data):
    from vam_native_job_state import check_cancel,progress
    check_cancel()
    from ue_source_materials import Appearance
    from vam_native_source import digest
    from vam_ue_mesh import normals_for_ue
    from vam_native_input import load_preview
    preview=load_preview(data)
    path=data/'NativeBuild'/(preview['decode_id']+'.calibrated.json')
    contract=json.loads(path.read_text(encoding='utf8'))
    assert contract.get('native_reference_ready'),'Run source calibration first'
    assert contract['decode_id']==preview['decode_id']
    assert contract['contract_id']==digest({k:v for k,v in contract.items() if k!='contract_id'}),'Contract integrity mismatch'
    source_path=Path(contract['source_ir']['path'])
    assert hashlib.sha256(source_path.read_bytes()).hexdigest()==contract['source_ir']['sha256']
    material_path=Path(preview['source_material_ir'])
    material_data=json.loads(material_path.read_text(encoding='utf8'))
    assert material_data['source_decode_id']==preview['decode_id']
    parts_path=data/'NativeBuild'/(preview['decode_id']+'.parts.json')
    parts=json.loads(parts_path.read_text(encoding='utf8')) if parts_path.exists() else {'parts':[],'missing':[{'error':'Parts not prepared'}]}
    if parts['parts']:assert parts['contract_id']==contract['contract_id'],'Stale part correspondence'
    # Plan actions change after manifest publication; they must not rename identical assets.
    target=os.environ.get('VAM_NATIVE_TARGET_ROOT','/Game/VamCharacters').rstrip('/')
    assert re.fullmatch(r'/Game(?:/[A-Za-z_][A-Za-z0-9_]*)+',target),'Target must be a project Content path, e.g. /Game/VamCharacters'
    stable_key=digest([{k:contract[k] for k in ('body','bones','influences','morphs','formulas')},
        {k:v for k,v in parts.items() if k not in ('contract_id','adapter_version')},material_data['materials'],target,'native-character-shape-kernel-v7-morph-material-usage'])
    index_path=data/'NativeBuild/native-destinations.json'
    index=json.loads(index_path.read_text(encoding='utf8')) if index_path.exists() else {}
    destination=index.get(stable_key,{})
    identity=destination.get('commit_id',stable_key)
    folder=destination.get('folder',target+'/C_'+identity[:24])
    bp_path=folder+'/BP_VamCharacter';definition_path=folder+'/CD_Character'
    marker=data/'NativeBuild'/(identity+'.commit.json')
    if marker.exists():
        previous=json.loads(marker.read_text(encoding='utf8'))
        latest=data/'NativeBuild/latest-native-assets.json'
        verified=json.loads(latest.read_text(encoding='utf8')) if latest.exists() else {}
        if verified.get('folder')==folder:previous=verified
        for package,expected in previous.get('asset_file_sha256',{}).items():
            filename=Path(u.Paths.project_content_dir())/(package[len('/Game/'):]+'.uasset')
            with filename.open('rb') as stream:
                assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,'User-modified native asset protected: '+package
        assert all(u.load_asset(p) for p in previous['assets']),'Saved build is incomplete; protected against overwriting'
        latest.write_text(json.dumps(previous,ensure_ascii=False,indent=2),encoding='utf8')
        if '-run=' not in u.SystemLibrary.get_command_line().lower():u.EditorAssetLibrary.sync_browser_to_objects([bp_path])
        return previous
    assert not u.EditorAssetLibrary.does_directory_exist(folder),'Uncommitted or user-owned destination; refusing overwrite'
    appearance=Appearance(material_path);appearance.folder=folder+'/Materials'
    default=u.load_asset('/Engine/EngineMaterials/DefaultMaterial')
    body_data=dict(contract['body'])
    body_index=next(i for i,m in enumerate(preview['meshes']) if m['locator'].get('object')==body_data['locator']['object'])
    body_preview=preview['meshes'][body_index]
    assert body_data['uv']==body_preview['uv'] and body_data['materials']==body_preview['materials'],'Body UV/material domain mismatch'
    body_data['vertices']=[[v[k]+sum(m['default']*m['deltas'][i][k] for m in contract['morphs']) for k in range(3)] for i,v in enumerate(body_data['vertices'])]
    assert max(abs(a-b) for v,w in zip(body_data['vertices'],body_preview['vertices']) for a,b in zip(v,w))<1e-4,'Native default appearance differs from decoded body'
    body_data['normals']=normals_for_ue(body_preview)
    mats=[];triangles=[];triangle_slots=[];removed_triangles=[]
    for slot,section in enumerate(body_data['sections']):
        mat,hidden=appearance.material(body_index,slot)
        mats.append(mat or default)
        if not hidden:
            for offset in range(0,len(section),3):
                ids=section[offset:offset+3];a,b,c=[body_data['vertices'][i] for i in ids]
                ab=[b[k]-a[k] for k in range(3)];ac=[c[k]-a[k] for k in range(3)]
                cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
                if all(abs(x)<=.0001 for x in cross):
                    removed_triangles.append({'slot':slot,'triangle':offset//3,'vertices':ids,'reason':'zero or near-zero neutral area; source polygon retained in provenance'})
                    continue
                triangles.extend(ids);triangle_slots.append(slot)
    assert not appearance.errors,'Material construction failed: '+str(appearance.errors)
    bones=[props(u.VamBuildBone(),name=b['name'],parent=b['parent'],local_bind=u.Transform(
        location=u.Vector(*b['translation']),rotation=u.Quat(*b['quaternion_xyzw']).rotator())) for b in contract['bones']]
    vertex_morphs=[m for m in contract['morphs'] if any(sum(x*x for x in v)>1e-12 for v in m['deltas'])]
    morphs=[props(u.VamBuildMorph(),name=m['name'],deltas=[u.Vector(*v) for v in m['deltas']]) for m in vertex_morphs]
    native=props(u.VamNativeMeshInput(),vertices=[u.Vector(*v) for v in body_data['vertices']],
        normals=[u.Vector(*v) for v in body_data['normals']],uv=[u.Vector2D(*v) for v in body_data['uv']],
        triangles=triangles,triangle_materials=triangle_slots,materials=mats,
        source_vertices=body_data['converted_to_source_vertex'],bones=bones,morphs=morphs,
        influences=[props(u.VamBuildInfluence(),vertex=v,bone=b,weight=w) for v,b,w in contract['influences']])
    mesh,error=u.VamNativeBuilder.build_mesh(folder+'/SK_Body',native)
    assert mesh,error
    supported=[m for m in contract['morphs'] if m in vertex_morphs or m.get('bone_centers')]
    parameters=[props(u.VamMorphParameter(),target=m['name'],source_id=m['source_id'],default_value=float(m['default']),
        minimum=min(0.,float(m['minimum']),m['default']),maximum=max(0.,float(m['maximum']),m['default']),
        display_name=m['display_name'],group=m['group'],unit=m['unit'],affected_regions=m['affected_regions'],
        bone_centers=[props(u.VamBoneCenterDelta(),bone_index=b['bone_index'],local_translation=u.Vector(*b['local_translation'])) for b in m['bone_centers']],
        formula_diagnostics_json=json.dumps(m['formula_diagnostics'],ensure_ascii=False)) for m in supported]
    definition=u.VamNativeBuilder.create_definition(definition_path,mesh,parameters,contract['plan_id'],contract['decode_id'],contract['bind_signature'])
    assert definition,'Definition validation failed'
    u.VamNativeBuilder.set_appearance_baseline(definition)
    def data_asset(name,cls):
        factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
        asset=u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,cls,factory)
        assert asset,'Failed to create '+name
        return asset
    shape=data_asset('SD_Shape',u.VamShapeDefinition)
    shape.set_editor_property('neutral_local_bind',[u.Transform(location=u.Vector(*b['translation']),rotation=u.Quat(*b['quaternion_xyzw']).rotator()) for b in contract['neutral_bones']])
    shape.set_editor_property('morph_set_lock_digest',contract['editable_morph_lock']['lock_id'])
    shape.set_editor_property('formula_diagnostics_json',json.dumps(contract['formula_classification'],ensure_ascii=False))
    preset=data_asset('AP_Imported',u.VamAppearancePreset)
    preset.set_editor_property('source_identity',contract['plan_id'])
    preset.set_editor_property('parameters',{m['name']:float(m['default']) for m in supported})
    binding=data_asset('GD_Bindings',u.VamGeometryBinding)
    binding.set_editor_property('topology_digest',digest([body_data['converted_to_source_vertex'],triangles]))
    binding.set_editor_property('render_to_input',list(u.VamNativeBuilder.get_render_to_input_map(mesh)))
    binding.set_editor_property('input_to_source',body_data['converted_to_source_vertex'])
    binding.set_editor_property('input_triangles',triangles)
    regions=[]
    region_semantics={}
    for source_definition in material_data.get('source_component_definitions',[]):
        if source_definition.get('class')!='DAZCharacterTextureControl':continue
        for field,semantic in [('faceMaterialNums','face'),('torsoMaterialNums','torso'),('limbMaterialNums','limbs'),('genitalMaterialNums','genitals')]:
            for slot in source_definition['parameters'].get(field,[]):
                region_semantics[slot]=(semantic,'DAZCharacterTextureControl '+str(source_definition['object'])+' '+field)
    for slot,section in enumerate(body_data['sections']):
        check_cancel()
        regions.append(props(u.VamSurfaceRegion(),id='source_material_'+str(slot),
            anatomical_semantic=region_semantics.get(slot,('None',''))[0],
            source_evidence='Source mesh '+str(body_data['locator'])+' material slot '+str(slot)+': '+str(body_data['materials'][slot])+'; '+region_semantics.get(slot,('','unassigned anatomy'))[1],
            source_vertices=sorted({body_data['converted_to_source_vertex'][v] for v in section}),
            input_triangles=[i for i,s in enumerate(triangle_slots) if s==slot]))
    binding.set_editor_property('regions',regions)
    source_ir=json.loads(source_path.read_text(encoding='utf8'))
    binding.set_editor_property('cloth_geometry_data_json',json.dumps({'schema':'vam-cloth-geometry/1','parts':parts['parts'],
        'source_records':[r for r in source_ir['records'] if r.get('kind')=='dynamic' and r.get('path','').startswith('Custom/Clothing/')]},ensure_ascii=False,separators=(',',':')))
    binding.set_editor_property('hair_source_data_json',json.dumps({'schema':'vam-hair-source/1','records':[r for r in source_ir['records'] if r.get('kind')=='builtin_scalp' or r.get('kind')=='dynamic' and (r.get('data',{}).get('dynamic') or {}).get('hair')]},ensure_ascii=False,separators=(',',':')))
    shape.set_editor_property('geometry',binding)
    definition.set_editor_property('shape',shape)
    definition.set_editor_property('imported_appearance',preset)
    part_assets=[];part_maps=[];part_failures=list(parts['missing'])
    for part in parts['parts']:
        check_cancel()
        try:
            d=dict(part['mesh']);pmats=[];ptris=[];pslots=[]
            defaults={m['name']:m['default'] for m in contract['morphs']}
            d['vertices']=[[v[k]+sum(defaults[m['name']]*m['deltas'][i][k] for m in part['morphs']) for k in range(3)] for i,v in enumerate(d['vertices'])]
            normal_sections=[[x for j in range(0,len(s),3) for x in (s[j],s[j+2],s[j+1])] for s in d['sections']]
            d['normals']=normals_for_ue(dict(d,sections=normal_sections))
            for slot,section in enumerate(d['sections']):
                mat,hidden=appearance.material(part['preview_mesh_index'],slot);pmats.append(mat or default)
                if hidden:continue
                for j in range(0,len(section),3):
                    ids=section[j:j+3];a,b,c=[u.Vector(*d['vertices'][i]) for i in ids]
                    cross=(b-a).cross(c-a)
                    if max(abs(cross.x),abs(cross.y),abs(cross.z))<=.0001:continue
                    ptris.extend(ids);pslots.append(slot)
            if not ptris:continue
            native_part=props(u.VamNativeMeshInput(),vertices=[u.Vector(*v) for v in d['vertices']],
                normals=[u.Vector(*v) for v in d['normals']],uv=[u.Vector2D(*v) for v in d['uv']],
                triangles=ptris,triangle_materials=pslots,materials=pmats,source_vertices=d['converted_to_source_vertex'],bones=bones,
                influences=[props(u.VamBuildInfluence(),vertex=v,bone=b,weight=w) for v,b,w in part['influences']],
                morphs=[props(u.VamBuildMorph(),name=m['name'],deltas=[u.Vector(*v) for v in m['deltas']]) for m in part['morphs'] if any(sum(x*x for x in v)>1e-12 for v in m['deltas'])])
            asset,error=u.VamNativeBuilder.build_mesh(folder+'/SK_Part_'+hashlib.sha256(part['path'].encode()).hexdigest()[:12],native_part)
            assert asset,error
            # Both were built from the identical parent-first bind array; no merge modifies the body skeleton.
            assert u.VamNativeBuilder.share_compatible_skeleton(asset,mesh),'Incompatible part reference bind'
            part_assets.append(asset)
            part_maps.append({'path':part['path'],'asset':asset.get_path_name(),'render_to_input':list(u.VamNativeBuilder.get_render_to_input_map(asset)),
                'input_to_source':d['converted_to_source_vertex'],'correspondence':part['correspondence'],'p0_wrap_error_cm':part['p0_wrap_error_cm']})
        except Exception as exc:part_failures.append({'path':part['path'],'error':str(exc)})
    definition.set_editor_property('parts',part_assets)
    u.VamNativeBuilder.set_build_limitations(definition,[json.dumps(x,ensure_ascii=False) for x in contract['blockers']]+[
        json.dumps(contract['skin'].get('fit',{}),ensure_ascii=False),
        'Source-reference LBS: compound pose calibration pending. BoneCenter is executable; other formula kinds explicitly retained. Hair retained for Stage10.',json.dumps(part_failures,ensure_ascii=False)]+[
        p['path']+': '+p['shape_limitation'] for p in parts['parts'] if p.get('shape_limitation')])
    mapping=u.VamNativeBuilder.create_source_mapping(folder+'/DA_SourceMapping',contract['decode_id'],contract['bind_signature'],
          source_path.read_text(encoding='utf8'),material_path.read_text(encoding='utf8'),
          json.dumps({'contract':contract,'parts':part_maps,'part_source_baselines':parts,'output_shape_convention':'appearance_plus_parameter_offsets','missing_parts':part_failures},ensure_ascii=False),u.VamNativeBuilder.get_render_to_input_map(mesh),
          body_data['converted_to_source_vertex'])
    assert mapping,'Source correspondence could not be saved'
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.VamCharacterActor)
    bp=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_VamCharacter',folder,u.Blueprint,factory)
    u.get_default_object(bp.generated_class()).get_editor_property('character').set_editor_property('definition',definition)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    import ue_native_retarget
    adapter_assets,adapter_report=ue_native_retarget.build(folder,mesh,contract['bones'])
    assets=[*appearance.textures.values(),*appearance.materials.values(),mesh.get_editor_property('skeleton'),mesh,*part_assets,definition,mapping,shape,preset,binding,bp,*adapter_assets]
    for material in appearance.materials.values():
        u.MaterialEditingLibrary.set_material_usage(material,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        u.MaterialEditingLibrary.set_material_usage(material,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    check_cancel();progress('saving','Atomic publication phase; cancellation is no longer accepted',False)
    for asset in assets:assert u.EditorAssetLibrary.save_loaded_asset(asset,False),'Save failed: '+asset.get_path_name()
    report={'status':'native_character_saved','acceptance_status':'pending_per_build_validation','folder':folder,'blueprint':bp_path,
            'shape_kernel_version':1,'render_domain_validation':{'morph_component_tolerance_cm':0.0001,'position_component_tolerance_cm':0.00002,'uv_tolerance':0.000001,'skin_quantization_tolerance':8/65535},
            'definition':definition_path,'assets':sorted({a.get_path_name() for a in assets}),
            'source_identity':contract['plan_id'],'source_digest':contract['decode_id'],
            'animation_adapter':adapter_report,
            'fit':contract['skin'].get('fit',{}),'limitations':list(definition.get_editor_property('limitations')),
            'removed_triangles':removed_triangles,'parts':len(part_assets),'missing_parts':part_failures,'manifest_updated':False,'independent_reload_verified':False}
    temporary=marker.with_suffix('.tmp');temporary.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');temporary.replace(marker)
    index[stable_key]={'folder':folder,'commit_id':identity}
    temporary=index_path.with_suffix('.tmp');temporary.write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf8');temporary.replace(index_path)
    (data/'NativeBuild/latest-native-assets.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    if '-run=' not in u.SystemLibrary.get_command_line().lower():u.EditorAssetLibrary.sync_browser_to_objects([bp_path])
    u.log('VAM_NATIVE_CHARACTER_SAVED '+folder)
    return report
