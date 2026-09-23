"""Small original, non-human fixture for native asset save/reload validation.

Run only in the disposable build host. Does not import source assets or commit
ImportState. A second process reload is selected with VAM_NATIVE_RELOAD=1.
"""
import os
import unreal as u

folder=os.environ.get('VAM_NATIVE_FIXTURE_FOLDER','/Game/VamStage05Tests')
mesh_path=folder+'/SK_TestPanel'
definition_path=folder+'/CD_TestPanel'
bp_path=folder+'/BP_TestPanel'


def property_set(obj, **properties):
    for key,value in properties.items():obj.set_editor_property(key,value)
    return obj


def run():
    if os.environ.get('VAM_NATIVE_RELOAD'):
        mesh=u.load_asset(mesh_path)
        definition=u.load_asset(definition_path)
        assert mesh and definition and u.load_asset(bp_path)
        assert list(mesh.get_editor_property('morph_targets'))
        assert definition.get_editor_property('build_verified')
        assert len(u.VamNativeBuilder.get_render_to_input_map(mesh))>=4
        u.log('VAM_NATIVE_RELOAD_OK')
        return
    vertices=[u.Vector(0,-10,0),u.Vector(0,10,0),u.Vector(0,-10,100),u.Vector(0,10,100)]
    root=property_set(u.VamBuildBone(),name='root',parent=-1,local_bind=u.Transform())
    tip=property_set(u.VamBuildBone(),name='tip',parent=0,local_bind=u.Transform(location=u.Vector(0,0,50)))
    influence=[property_set(u.VamBuildInfluence(),vertex=i,bone=0 if i<2 else 1,weight=1.) for i in range(4)]
    morph=property_set(u.VamBuildMorph(),name='Bend',deltas=[u.Vector(),u.Vector(),u.Vector(20,0,0),u.Vector(20,0,0)])
    source=property_set(u.VamNativeMeshInput(),vertices=vertices,normals=[u.Vector(1,0,0)]*4,
                        uv=[u.Vector2D(0,0),u.Vector2D(1,0),u.Vector2D(0,1),u.Vector2D(1,1)],
                        triangles=[0,2,1,1,2,3],triangle_materials=[0,0],
                        materials=[u.load_asset('/Engine/EngineMaterials/DefaultMaterial')],
                        source_vertices=[0,1,2,3],bones=[root,tip],influences=influence,morphs=[morph])
    mesh,error=u.VamNativeBuilder.build_mesh(mesh_path,source)
    assert mesh,error
    assert set(u.VamNativeBuilder.get_render_to_input_map(mesh))=={0,1,2,3}
    skeleton=mesh.get_editor_property('skeleton')
    parameter=property_set(u.VamMorphParameter(),target='Bend',source_id='synthetic:Bend',default_value=.25,minimum=0.,maximum=1.)
    definition=u.VamNativeBuilder.create_definition(definition_path,mesh,[parameter],
                                                    'synthetic:two-bone-panel','original-test-data','synthetic-v1')
    assert definition
    factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.VamCharacterActor)
    bp=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_TestPanel',folder,u.Blueprint,factory)
    cdo=u.get_default_object(bp.generated_class())
    cdo.get_editor_property('character').set_editor_property('definition',definition)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    for asset in (skeleton,mesh,definition,bp):assert u.EditorAssetLibrary.save_loaded_asset(asset,False)
    # Empty, reusable native host ships in plugin content; no test geometry dependency.
    host=u.load_asset('/VamResourceBrowser/Blueprints/BP_VamCharacter')
    if not host:host=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_VamCharacter','/VamResourceBrowser/Blueprints',u.Blueprint,factory)
    u.BlueprintEditorLibrary.compile_blueprint(host)
    assert u.EditorAssetLibrary.save_loaded_asset(host,False)
    u.log('VAM_NATIVE_BUILD_OK')


try:run()
except Exception:
    import traceback
    u.log_error('VAM_NATIVE_TEST_FAILED: '+traceback.format_exc())
    raise
