"""Create an opt-in GPU BP beside the isolated candidate RC; never overwrite an asset."""
import unreal as u
root='/Game/VamRuntime/GPUContact_20261009_v2'
path=root+'/BP_VamCharacter_GPU'
if u.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('Candidate BP already exists; refusing overwrite')
rc=u.load_asset(root+'/RC_Runtime')
assert rc and rc.get_editor_property('breast_contact').get_editor_property('gpu_surface_deformer')
f=u.BlueprintFactory();f.set_editor_property('parent_class',u.VamCharacterActor)
bp=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_VamCharacter_GPU',root,u.Blueprint,f)
u.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=u.get_default_object(bp.generated_class())
cdo.get_editor_property('character').set_editor_property('runtime_configuration',rc)
cdo.get_editor_property('breast_contact').set_editor_property('use_gpu',True)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
u.log('GPU_CANDIDATE_SAVED '+path)

