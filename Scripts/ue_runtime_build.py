"""Immutable runtime bundle build/reload. Inputs are explicit recipes and native assets.
Run in an isolated editor with VAM_RUNTIME_RECIPE and VAM_RUNTIME_REPORT. A second
process runs this script with VAM_RUNTIME_PHASE=reload before publication.
"""
import hashlib, json, os, sys, traceback
from pathlib import Path
import unreal as u
SCRIPTS=Path(__file__).resolve().parent
sys.path.insert(0,str(SCRIPTS))
from vam_runtime_recipe import digest,require,validate_recipe,validate_family,verify_native_binding,build_identity


def write_json(path, value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temp=path.with_suffix('.tmp');temp.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf8');temp.replace(path)

def load(path,cls=None):
    obj=u.load_asset(path)
    require(obj is not None and (cls is None or isinstance(obj,cls)),'Asset missing or wrong class: '+path)
    return obj

def asset_path(obj):
    require(obj is not None,'Missing asset reference')
    return obj.get_path_name()

def package_file(package):
    package=package.split('.')[0]
    if package.startswith('/Game/'): root=Path(u.Paths.project_content_dir());tail=package[6:]
    elif package.startswith('/Engine/'): root=Path(u.Paths.engine_content_dir());tail=package[8:]
    elif package.startswith('/VamResourceBrowser/'): root=Path(u.Paths.project_plugins_dir())/'VamResourceBrowser/Content';tail=package[len('/VamResourceBrowser/'):]
    else: raise ValueError('Unregistered dependency mount; explicit resolver required: '+package)
    return root/(tail+'.uasset')

def fingerprint(package):
    file=package_file(package)
    require(file.is_file(),'Asset must be persisted before upgrading: '+package)
    return hashlib.sha256(file.read_bytes()).hexdigest()

def source_closure(objects):
    registry=u.AssetRegistryHelpers.get_asset_registry()
    options=u.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,
        include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
    pending=[asset_path(o).split('.')[0] for o in objects if o]
    visited=set(); result={}
    while pending:
        package=pending.pop()
        if package in visited or package.startswith('/Script/'):continue
        visited.add(package)
        result[package]=fingerprint(package)
        pending.extend(str(p) for p in registry.get_dependencies(package,options))
    return result

def inputs(recipe_path):
    recipe=json.loads(Path(recipe_path).read_text(encoding='utf8'));validate_recipe(recipe)
    family_path=(Path(recipe_path).resolve().parent/recipe['family']).resolve()
    family=json.loads(family_path.read_text(encoding='utf8'))
    definition=load(recipe['definition'],u.VamCharacterDefinition)
    mapping=load(recipe['source_mapping'],u.VamSourceMapping)
    require(definition.get_editor_property('build_verified'),'Unverified Stage05 definition')
    payload=json.loads(mapping.get_editor_property('native_contract_json'));contract=payload['contract']
    require(contract['contract_id']==digest({k:v for k,v in contract.items() if k!='contract_id'}),'Persisted native contract digest mismatch')
    for field,key in [('source_digest','decode_id'),('bind_signature','bind_signature')]:
        require(definition.get_editor_property(field)==mapping.get_editor_property(field)==contract[key],'Definition/source mapping mismatch: '+field)
    shape=definition.get_editor_property('shape');mesh=definition.get_editor_property('body');skeleton=definition.get_editor_property('skeleton')
    require(shape and mesh and skeleton and mesh.get_editor_property('skeleton')==skeleton,'Missing or incompatible native dependencies')
    require(shape.get_editor_property('morph_set_lock_digest')==contract['editable_morph_lock']['lock_id'],'Shape MorphSet lock mismatch')
    verify_native_binding(json.loads(u.VamStage06AssetEditor.describe_mesh_binding(mesh)),contract['bones'])
    validate_family(contract['neutral_bones'],family)
    for part in definition.get_editor_property('parts'):
        require(part.get_editor_property('skeleton')==skeleton,'Part skeleton mismatch')
        verify_native_binding(json.loads(u.VamStage06AssetEditor.describe_mesh_binding(part)),contract['bones'])
    base=load(recipe['base_animation'],u.AnimSequence) if recipe.get('base_animation') else None
    require(u.VamShapeAnimInstance.supports_base_animation(mesh,base),'Base animation must use exact skeleton and be an in-place non-additive clip')
    files=source_closure([definition,mapping,base])
    algorithms={}
    for base in ('Source/VamCharacterRuntime','Source/VamResourceBrowser'):
        for file in sorted((SCRIPTS.parent/base).rglob('*')):
            if file.is_file() and file.suffix in ('.h','.cpp','.cs'):
                algorithms[str(file.relative_to(SCRIPTS.parent))]=hashlib.sha256(file.read_bytes()).hexdigest()
    for name in ('vam_runtime_recipe.py','ue_runtime_build.py','ue_stage06_materials.py'):
        algorithms['Scripts/'+name]=hashlib.sha256((SCRIPTS/name).read_bytes()).hexdigest()
    identity=build_identity(recipe,family,files,algorithms,u.SystemLibrary.get_engine_version())
    return recipe,family,definition,contract,files,identity,algorithms

def create(name,root,cls,factory=None):
    if factory is None:
        factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
    result=u.AssetToolsHelpers.get_asset_tools().create_asset(name,root,cls,factory)
    require(result is not None,'Could not create '+root+'/'+name)
    return result

def save(obj):
    require(u.EditorAssetLibrary.save_loaded_asset(obj,False),'Save failed; transaction remains uncommitted, close the editor holding this package and retry explicit recipe: '+asset_path(obj))

def rotator(values):return u.Rotator(pitch=values[0],yaw=values[1],roll=values[2])

def rig_snapshot(rig):
    def rot(v):return [v.pitch,v.yaw,v.roll]
    return {'skeleton':asset_path(rig.get_editor_property('skeleton')),'root':str(rig.get_editor_property('solver_root_semantic')),
        'effectors':[str(x) for x in rig.get_editor_property('effectors')],'iterations':rig.get_editor_property('iterations'),
        'joints':[{**{k:str(j.get_editor_property(k)) for k in ('semantic','bone')},
                   **{k:rot(j.get_editor_property(k)) for k in ('minimum','maximum','preferred_bend')},
                   **{k:bool(j.get_editor_property(k)) for k in ('pose_control','limit_rotation')}} for j in rig.get_editor_property('joints')]}

def build(recipe_path,report_path):
    recipe,family,definition,contract,source_files,identity,algorithms=inputs(recipe_path)
    root=recipe['destination_root'].rstrip('/')+'/R_'+identity[:24]
    config_path=root+'/RC_Runtime'
    if u.EditorAssetLibrary.does_asset_exist(config_path):
        config=load(config_path,u.VamRuntimeConfiguration)
        receipt=json.loads(config.get_editor_property('receipt_json'))
        require(receipt['identity']==identity,'Existing runtime configuration identity mismatch')
        # Reuse still requires the independent reload phase, even with no Saved cache.
        write_json(report_path,{'writer_pid':os.getpid(),'status':'saved_pending_reload','configuration':config_path,'identity':identity,'reused':True})
        return
    require(not u.EditorAssetLibrary.does_directory_exist(root),'Uncommitted/user-owned destination protected: '+root+'; inspect failed output, then use an explicit new recipe revision to retry')
    mesh=definition.get_editor_property('body');skeleton=definition.get_editor_property('skeleton')
    rig=create('DA_Rig',root,u.VamRigProfile);joints=[]
    for row in family['joints']:
        joint=u.VamRigJoint()
        for key in ('semantic','bone','pose_control'):joint.set_editor_property(key,row[key])
        joint.set_editor_property('limit_rotation',row['pose_control'])
        for key in ('minimum','maximum','preferred_bend'):joint.set_editor_property(key,rotator(row[key]))
        joints.append(joint)
    for key,value in [('skeleton',skeleton),('joints',joints),('solver_root_semantic',family['solver_root']),('effectors',family['effectors']),('iterations',recipe.get('ik_iterations',24))]:rig.set_editor_property(key,value)
    save(rig)
    physics,error=u.VamStage06AssetEditor.build_physics_asset(root+'/PA_Body',mesh,recipe.get('minimum_bone_size_cm',8))
    require(physics is not None,str(error))
    physics_summary=u.VamStage06AssetEditor.configure_runtime_physics_asset(physics,mesh,rig)
    require(not physics_summary.startswith('ERROR:'),physics_summary);save(physics)
    physics_shape=create('DA_PhysicsShape',root,u.VamPhysicsShapeProfile)
    error=u.VamStage06AssetEditor.build_physics_shape_profile(physics_shape,definition,physics)
    require(error is not None and not error,'Collision fitting build rejected: '+str(error));save(physics_shape)
    factory=u.AnimBlueprintFactory();factory.set_editor_property('parent_class',u.VamShapeAnimInstance)
    factory.set_editor_property('target_skeleton',skeleton);factory.set_editor_property('preview_skeletal_mesh',mesh)
    animation=create('ABP_Character',root,u.AnimBlueprint,factory);u.BlueprintEditorLibrary.compile_blueprint(animation);save(animation)
    from ue_stage06_materials import build_material_profile
    materials=build_material_profile(definition,root,recipe.get('part_material_category','reference'),recipe.get('skin_shading','source'));save(materials)
    config=create('RC_Runtime',root,u.VamRuntimeConfiguration)
    require(u.VamStage06AssetEditor.set_runtime_configuration_identity(config,definition,identity,''),'Could not initialize runtime identity')
    for key,value in [('definition',definition),('rig',rig),('physics',physics),('physics_shape',physics_shape),('animation_class',animation.generated_class()),('materials',materials)]:config.set_editor_property(key,value)
    tissue=recipe.get('soft_tissue')
    if tissue:
        profile=create('DA_SoftTissue',root,u.VamSoftTissueProfile)
        regions=[]
        for row in tissue['regions']:
            region=u.VamSoftTissueRegion()
            for key,value in row.items():region.set_editor_property(key,value)
            regions.append(region)
        error=u.VamSoftTissueBuilder.build(profile,definition,regions)
        require(not error,'Soft tissue bake failed: '+str(error))
        profile.set_editor_property('gravity',tissue.get('gravity',True));save(profile)
        config.set_editor_property('soft_tissue_profile',profile)
        config.set_editor_property('soft_tissue_quality',getattr(u.VamSoftTissueQuality,tissue.get('quality','Balanced').upper()))
        config.set_editor_property('enabled_regions',tissue['enabled_regions'])
        config.set_editor_property('soft_tissue_backend_version',profile.get_editor_property('backend_version'))
    if recipe.get('base_animation'):config.set_editor_property('base_animation',load(recipe['base_animation'],u.AnimSequence))
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.VamCharacterActor)
    host=create('BP_VamCharacter',root,u.Blueprint,factory)
    defaults=u.get_default_object(host.generated_class());character=defaults.get_editor_property('character')
    character.set_editor_property('runtime_configuration',config)
    # Explicit source IDs select expressions; display-name/localization heuristics are not used.
    parameters={p.get_editor_property('source_id'):p for p in definition.get_editor_property('parameters')}
    targets=[]
    for source_id in recipe.get('blink_source_ids',[]):
        require(source_id in parameters,'Blink source ID not present in this MorphSet: '+source_id)
        p=parameters[source_id];require(str(p.get_editor_property('group'))=='Expression' and not p.get_editor_property('bone_centers'),'Blink must be a vertex Expression')
        targets.append(str(p.get_editor_property('target')))
    defaults.get_editor_property('active_pose').set_editor_property('blink_morph_targets',targets)
    defaults.get_editor_property('active_pose').set_editor_property('blink_morph_target','')
    u.BlueprintEditorLibrary.compile_blueprint(host);save(host)
    assets=[str(x).split('.')[0] for x in u.EditorAssetLibrary.list_assets(root,True,False) if str(x).split('.')[0]!=config_path]
    receipt={'schema':'vam-runtime-receipt/2','algorithms':algorithms,'identity':identity,'recipe':recipe,'family':family,'source_files':source_files,
        'rig':asset_path(rig),'physics':asset_path(physics),'animation':asset_path(animation),'materials':asset_path(materials),'blueprint':asset_path(host),
        'physics_shape':asset_path(physics_shape),'definition':asset_path(definition),'rig_snapshot':rig_snapshot(rig),'physics_summary':physics_summary,'blink_targets':targets,
        'body_slots':len(mesh.get_editor_property('materials')),'part_slots':[len(p.get_editor_property('materials')) for p in definition.get_editor_property('parts')],
        'body_shading_models':[str(m.get_editor_property('shading_model')) if isinstance(m,u.Material) else None for m in materials.get_editor_property('body_materials')],
        'output_files':{p:fingerprint(p) for p in assets}}
    require(u.VamStage06AssetEditor.set_runtime_configuration_identity(config,definition,identity,json.dumps(receipt,ensure_ascii=False,separators=(',',':'))),'Could not persist runtime receipt');save(config)
    write_json(report_path,{'writer_pid':os.getpid(),'status':'saved_pending_reload','identity':identity,'configuration':config_path,'reused':False})

def reload_and_publish(recipe_path,report_path):
    report=json.loads(Path(report_path).read_text(encoding='utf8'))
    require(report['status'] in ('saved_pending_reload','committed'),'No saved runtime transaction to reload')
    require(report['writer_pid']!=os.getpid(),'Reload must run in a separate editor process')
    recipe,family,definition,contract,source_files,identity,algorithms=inputs(recipe_path)
    require(report['identity']==identity,'Recipe/source/algorithm changed after save; do not commit stale output')
    config=load(report['configuration'],u.VamRuntimeConfiguration)
    require(config.get_editor_property('build_identity')==identity,'Runtime bundle identity mismatch')
    receipt=json.loads(config.get_editor_property('receipt_json'))
    require(receipt['identity']==identity and receipt['recipe']==recipe and receipt['family']==family,'Persisted runtime receipt mismatch')
    require(receipt['source_files']==source_files and receipt['algorithms']==algorithms,'Upstream native dependencies or algorithms changed during build')
    for path,expected in receipt['output_files'].items():
        require(fingerprint(path)==expected,'User-modified or corrupt derived package protected: '+path)
        load(path)
    rig=load(receipt['rig'],u.VamRigProfile)
    require(rig_snapshot(rig)==receipt['rig_snapshot'],'Rig limits/mapping did not survive independent reload; no fallback accepted')
    require(config.get_editor_property('definition')==definition and config.get_editor_property('rig')==rig,'Configuration native references mismatch')
    require(config.get_editor_property('source_digest')==definition.get_editor_property('source_digest') and
        config.get_editor_property('bind_signature')==definition.get_editor_property('bind_signature') and
        config.get_editor_property('morph_set_lock_digest')==contract['editable_morph_lock']['lock_id'],'Configuration source identities mismatch')
    expected_base=load(recipe['base_animation'],u.AnimSequence) if recipe.get('base_animation') else None
    require(config.get_editor_property('base_animation')==expected_base,'Base animation reference mismatch')
    tissue=recipe.get('soft_tissue')
    if tissue:
        profile=config.get_editor_property('soft_tissue_profile')
        require(profile and profile.get_editor_property('body')==definition.get_editor_property('body'),'Soft tissue body mismatch')
        require(profile.get_editor_property('bind_signature')==config.get_editor_property('bind_signature') and profile.get_editor_property('morph_set_lock_digest')==config.get_editor_property('morph_set_lock_digest'),'Soft tissue identity mismatch')
        require(profile.get_editor_property('backend_version')==config.get_editor_property('soft_tissue_backend_version'),'Soft tissue backend mismatch')
        require([str(n) for n in config.get_editor_property('enabled_regions')]==tissue['enabled_regions'],'Soft tissue region selection did not persist')
        require(bool(profile.get_editor_property('gravity'))==tissue.get('gravity',True),'Soft tissue gravity did not persist')
        require(config.get_editor_property('soft_tissue_quality')==getattr(u.VamSoftTissueQuality,tissue.get('quality','Balanced').upper()),'Soft tissue quality did not persist')
    else:require(not config.get_editor_property('soft_tissue_profile'),'Unrequested soft tissue profile')
    physics=load(receipt['physics'],u.PhysicsAsset)
    require(config.get_editor_property('physics')==physics and len(physics.get_constraints(False))>0,'Physics reference/constraints missing')
    physics_shape=load(receipt['physics_shape'],u.VamPhysicsShapeProfile)
    require(config.get_editor_property('physics_shape')==physics_shape and physics_shape.get_editor_property('physics')==physics,'Collision profile dependency mismatch')
    require(physics_shape.get_editor_property('bind_signature')==definition.get_editor_property('bind_signature') and
        physics_shape.get_editor_property('morph_set_lock_digest')==contract['editable_morph_lock']['lock_id'],'Collision profile identity mismatch')
    animation=load(receipt['animation'],u.AnimBlueprint)
    require(animation.get_editor_property('target_skeleton')==definition.get_editor_property('skeleton'),'Animation skeleton mismatch')
    require(config.get_editor_property('animation_class')==animation.generated_class(),'Animation class reference mismatch')
    materials=load(receipt['materials'],u.VamMaterialProfile)
    require(config.get_editor_property('materials')==materials,'Material profile reference mismatch')
    require(len(materials.get_editor_property('body_materials'))==receipt['body_slots'],'Material body slot mismatch')
    require([str(m.get_editor_property('shading_model')) if isinstance(m,u.Material) else None for m in materials.get_editor_property('body_materials')]==receipt['body_shading_models'],'Material shading did not persist')
    require([len(p.get_editor_property('materials')) for p in materials.get_editor_property('part_materials')]==receipt['part_slots'],'Part material slot mismatch')
    host=load(receipt['blueprint'],u.Blueprint);defaults=u.get_default_object(host.generated_class())
    require(defaults.get_editor_property('character').get_editor_property('runtime_configuration')==config,'Host runtime configuration reference missing')
    require([str(x) for x in defaults.get_editor_property('active_pose').get_editor_property('blink_morph_targets')]==receipt['blink_targets'],'Host blink mapping changed')
    require(u.VamStage06AssetEditor.publish_runtime_configuration(config),'Failed to publish verified runtime configuration')
    save(config)
    output=dict(receipt['output_files']);output[report['configuration']]=fingerprint(report['configuration'])
    write_json(report_path,dict(report,status='published_pending_verification',independent_reload_verified=True,reloader_pid=os.getpid(),blueprint=receipt['blueprint'],
        source_files=source_files,algorithms=algorithms,output_files=output,physics_summary=receipt['physics_summary']))


def verify_publication(recipe_path,report_path):
    report=json.loads(Path(report_path).read_text(encoding='utf8'))
    require(report['status']=='published_pending_verification' and report['reloader_pid']!=os.getpid(),'Publication needs a fresh verification process')
    recipe,family,definition,contract,source_files,identity,algorithms=inputs(recipe_path)
    require(report['identity']==identity and report['algorithms']==algorithms and report['source_files']==source_files,'Publication inputs changed')
    for path,expected in report['output_files'].items():require(fingerprint(path)==expected,'Published output changed: '+path)
    config=load(report['configuration'],u.VamRuntimeConfiguration)
    require(config.get_editor_property('schema_version')==2 and config.get_editor_property('independent_reload_verified') and config.get_editor_property('build_identity')==identity,'Native publication marker did not persist')
    write_json(report_path,dict(report,status='committed',publication_verifier_pid=os.getpid()))
    u.log('VAM_RUNTIME_BUNDLE_COMMITTED '+report['configuration'])

def main():
    recipe=os.environ.get('VAM_RUNTIME_RECIPE');report=os.environ.get('VAM_RUNTIME_REPORT')
    require(recipe and report,'Set VAM_RUNTIME_RECIPE and VAM_RUNTIME_REPORT explicitly; see Config/Examples. No default character or latest-cache fallback.')
    phase=os.environ.get('VAM_RUNTIME_PHASE','build')
    require(phase in ('build','reload','verify'),'Unknown runtime build phase')
    from vam_native_job_state import exclusive_build
    try:
        with exclusive_build(Path(u.Paths.project_saved_dir())/'VamRuntimeBuild'):
            {'build':build,'reload':reload_and_publish,'verify':verify_publication}[phase](recipe,report)
    except Exception:
        # Preserve a pending transaction for explicit diagnosis/retry; never replace
        # its identity with a success flag when a save or reload fails.
        write_json(str(report)+'.error.json',{'status':'failed','phase':phase,'error':traceback.format_exc()})
        raise

if __name__=='__main__':main()
