"""Create/reload an isolated lower-body contact candidate without modifying source assets."""
import os,json,hashlib
from pathlib import Path
import unreal as u
root=os.environ.get('VAM_LOWER_ROOT','/Game/VamRuntime/LowerContact_20261009_v1')
report=Path(os.environ['VAM_LOWER_REPORT'])
phase=os.environ.get('VAM_LOWER_PHASE','build')
if phase=='build':
    assert not u.EditorAssetLibrary.does_directory_exist(root),'Destination already exists'
    source=u.load_asset(os.environ.get('VAM_LOWER_SOURCE','/Game/VamRuntime/GPUContact_20261009_v2/RC_Runtime'));assert source
    rc=u.EditorAssetLibrary.duplicate_asset(source.get_path_name(),root+'/RC_Runtime')
    profile=u.EditorAssetLibrary.duplicate_asset(source.get_editor_property('breast_contact').get_path_name(),root+'/DA_BodyContact')
    if os.environ.get('VAM_LOWER_FILTER_SHAPE')=='1':
        allowed={str(p.get_editor_property('target')) for p in source.get_editor_property('definition').get_editor_property('parameters') if str(p.get_editor_property('group'))!='Expression'}
        before=list(profile.get_editor_property('morphs')); after=[m for m in before if str(m.get_editor_property('parameter')) in allowed]
        profile.set_editor_property('morphs',after)
        u.log('LOWER_SHAPE_FILTER '+str(len(before))+' -> '+str(len(after)))
    else:
        error=u.VamLowerBodyContactBuilder.append(source.get_editor_property('definition'),source.get_editor_property('glute_structure'),source.get_editor_property('leg_jiggle'),profile)
        assert not error,error
    error=u.VamBreastContactBuilder.build_deformer(profile,root+'/DG_BodyContact');assert not error,error
    rc.set_editor_property('breast_contact',profile)
    identity=hashlib.sha256((source.get_editor_property('build_identity')+root+'lower-connected-ftetwild-v1').encode()).hexdigest()
    receipt={'schema':'lower-contact-candidate/1','source_configuration':source.get_path_name(),'profile':profile.get_path_name(),'identity':identity,'regions':list(map(str,profile.get_editor_property('region_names')))}
    assert u.VamStage06AssetEditor.set_runtime_configuration_identity(rc,source.get_editor_property('definition'),identity,json.dumps(receipt))
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.VamCharacterActor)
    bp=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_VamCharacter',root,u.Blueprint,factory)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo=u.get_default_object(bp.generated_class());cdo.get_editor_property('character').set_editor_property('runtime_configuration',rc)
    cdo.get_editor_property('breast_contact').set_editor_property('use_gpu',True)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    for path in u.EditorAssetLibrary.list_assets(root,True,False):assert u.EditorAssetLibrary.save_loaded_asset(u.load_asset(path),False),path
    report.write_text(json.dumps({'status':'pending_independent_reload','root':root,'profile':profile.get_path_name(),'particles':len(profile.get_editor_property('particles')),'volumes':list(profile.get_editor_property('effective_volume_cm3'))},indent=2))
else:
    rc=u.load_asset(root+'/RC_Runtime');assert rc
    profile=rc.get_editor_property('breast_contact');assert not profile.validate_data(),profile.validate_data()
    assert len(profile.get_editor_property('effective_volume_cm3'))==4
    assert profile.get_editor_property('gpu_surface_deformer')
    assert u.VamStage06AssetEditor.publish_runtime_configuration(rc)
    assert u.EditorAssetLibrary.save_loaded_asset(rc,False)
    report.write_text(json.dumps({'status':'reloaded_and_published','root':root,'profile':profile.get_path_name(),'blueprint':root+'/BP_VamCharacter'},indent=2))
u.log('LOWER_CONTACT_CANDIDATE '+root+' '+phase)
