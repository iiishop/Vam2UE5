"""Build an immutable Groom add-on for an existing verified normal BP character.
Set VAM_HAIR_INPUT / VAM_HAIR_BASE_BUNDLE / VAM_HAIR_REPORT. Reload in a second
process with VAM_HAIR_PHASE=reload before publishing the new configuration.
"""
import hashlib,json,os,traceback
from pathlib import Path
import unreal as u

def main():
    report=Path(os.environ['VAM_HAIR_REPORT']);base=json.loads(Path(os.environ['VAM_HAIR_BASE_BUNDLE']).read_text())
    asset_tools=u.AssetToolsHelpers.get_asset_tools()
    def save(o):assert u.EditorAssetLibrary.save_loaded_asset(o,False)
    if os.environ.get('VAM_HAIR_PHASE')=='reload':
        d=json.loads(report.read_text());c=u.load_asset(d['configuration']);p=c.get_editor_property('hair')
        assert p and p.get_editor_property('fine_groom')
        assert p.get_editor_property('build_identity')==d['hair_identity']
        assert c.get_editor_property('build_identity')==d['identity']
        style=c.get_editor_property('hairstyle')
        assert style and style==p.get_editor_property('hairstyle')
        assets=[e.get_editor_property('hair') for e in style.get_editor_property('entries')]
        assert assets and all(a and a.get_editor_property('source_identity') for a in assets)
        assert [a.get_path_name() for a in assets]==d['hair_resources']
        assert u.VamHairBuilder.bind_hairstyle(p,c,style),'Cache composition validation failed on reload'
        # This API only certifies independent reload on this isolated configuration.
        # It does not replace the project's character or latest-asset selection.
        assert u.VamStage06AssetEditor.publish_runtime_configuration(c);save(c)
        d['published']=False
        d.update(status='committed',independent_reload_verified=True,reloader_pid=os.getpid());report.write_text(json.dumps(d,indent=2));return
    inp=Path(os.environ['VAM_HAIR_INPUT']);source=json.loads(inp.read_text());c0=u.load_asset(base['configuration']);definition=c0.get_editor_property('definition');mesh=definition.get_editor_property('body')
    assert source['schema']=='vam-groom-source/1'
    assert source['identity']==hashlib.sha256(json.dumps({k:v for k,v in source.items() if k!='identity'},sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest(),'Hair input content digest mismatch'
    # Source input identity is combined with actual body and solver implementation.
    repo=Path(__file__).resolve().parents[1]
    implementation={f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in ['Scripts/vam_hair_prepare.py','Scripts/vam_hair_catalog.py','Scripts/vam_hair_frames.py','Scripts/prepare_hair_render_reference.py','Scripts/prepare_hair_physics.py','Scripts/prepare_hair_head_collision.py','Scripts/vam_unity.py','Scripts/vam_fit.py','Scripts/ue_hair_build.py','Scripts/prepare_hair_constraints.py','Source/VamResourceBrowser/Private/VamHairSourceConstraints.h','Source/VamResourceBrowser/Private/VamHairBuilder.cpp','Source/VamResourceBrowser/Private/VamHairStyleSolver.h','Source/VamCharacterRuntime/Public/VamHairProfile.h','Source/VamResourceBrowser/Private/VamHairCatalogBuilder.cpp','Source/VamCharacterRuntime/Public/VamHairAsset.h','Source/VamCharacterRuntime/Private/VamHairAsset.cpp','Source/VamResourceBrowser/Public/VamHairBuilder.h','Source/VamCharacterRuntime/Private/VamHairComponent.cpp','Source/VamCharacterRuntime/Public/VamHairComponent.h','Source/VamCharacterRuntime/Public/VamHairProfile.h']}
    identity=hashlib.sha256((source['identity']+base['identity']+json.dumps(implementation,sort_keys=True)).encode()).hexdigest();root='/Game/VamHairRuntime/H_'+identity[:24]
    assert not u.EditorAssetLibrary.does_asset_exist(root+'/HP_Hair')
    result=u.VamHairBuilder.build_hair(str(inp.resolve()),root,mesh,identity,c0.get_editor_property('physics'))
    profile=result[0] if isinstance(result,tuple) else result
    assert profile,result
    config=u.EditorAssetLibrary.duplicate_asset(base['configuration'],root+'/RC_Runtime');assert config
    assert u.VamStage06AssetEditor.set_runtime_configuration_identity(config,definition,identity,'')
    config.set_editor_property('hair',profile)
    if profile.get_editor_property('hairstyle'):
        assert u.VamHairBuilder.bind_hairstyle(profile,config,profile.get_editor_property('hairstyle'))
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.VamCharacterActor)
    bp=asset_tools.create_asset('BP_VamCharacter',root,u.Blueprint,factory);assert bp
    defaults=u.get_default_object(bp.generated_class());defaults.get_editor_property('character').set_editor_property('runtime_configuration',config)
    defaults.get_editor_property('hair').set_editor_property('b_simulate' if hasattr(defaults.get_editor_property('hair'),'b_simulate') else 'simulate',bool(source.get('physics_policy')))
    u.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
    style=profile.get_editor_property('hairstyle')
    resources=[e.get_editor_property('hair').get_path_name() for e in style.get_editor_property('entries')] if style else []
    receipt={'hairstyle':style.get_path_name() if style else None,'hair_resources':resources,'schema':'vam-hair-runtime/1','base_configuration':base['configuration'],'base_identity':base['identity'],'hair_identity':profile.get_editor_property('build_identity'),'hair_profile':profile.get_path_name(),'identity':identity,'blueprint':bp.get_path_name(),'source':source['receipt'],'implementation':implementation}
    assert u.VamStage06AssetEditor.set_runtime_configuration_identity(config,definition,identity,json.dumps(receipt));save(config)
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps(dict(receipt,configuration=config.get_path_name().split('.')[0],status='saved_pending_reload',independent_reload_verified=False,writer_pid=os.getpid(),build_report=json.loads(profile.get_editor_property('build_report_json'))),indent=2))

try:main()
except Exception:
    Path(os.environ['VAM_HAIR_REPORT']+'.error.txt').write_text(traceback.format_exc());raise
