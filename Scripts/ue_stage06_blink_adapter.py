"""Attach the source-derived Stage05 eyelid morphs to the Stage06 runtime actor."""
import json
import traceback
from pathlib import Path

import unreal as u

project=Path(u.Paths.project_dir())
native=project/'Plugins/VamResourceBrowser/Saved/NativeBuild/latest-native-assets.json'
report_path=project/'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-blink-adapter.json'
root='/Game/VamStage06'

def save(**data):
    report_path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')

try:
    latest=json.loads(native.read_text(encoding='utf8'))
    folder=latest['folder']
    definition=u.load_asset(folder+'/CD_Character')
    assert definition and definition.get_editor_property('build_verified'),folder
    mesh=definition.get_editor_property('body')
    skeleton=definition.get_editor_property('skeleton')
    assert mesh and skeleton
    targets={p.get_editor_property('display_name'):str(p.get_editor_property('target'))
             for p in definition.get_editor_property('parameters')}
    eyelids=[targets[name] for name in ('Eyes Closed Left','Eyes Closed Right')]
    assert len(set(eyelids))==2,eyelids

    suffix=folder.rsplit('/',1)[-1][-8:]
    rig_path=root+'/DA_Rig_Eyelids_'+suffix
    rig=u.load_asset(rig_path)
    if not rig:
        rig=u.EditorAssetLibrary.duplicate_asset(root+'/DA_Rig_Stage05Accepted',rig_path)
    assert rig
    rig.set_editor_property('skeleton',skeleton)
    assert u.EditorAssetLibrary.save_loaded_asset(rig)

    physics_path=root+'/PA_Eyelids_'+suffix
    physics=u.load_asset(physics_path)
    if not physics:
        physics,error=u.VamStage06AssetEditor.build_physics_asset(physics_path,mesh,8.)
        assert physics,error
    physics_summary=u.VamStage06AssetEditor.configure_physics_asset(physics,rig)
    assert u.EditorAssetLibrary.save_loaded_asset(physics)

    animation_path=root+'/ABP_Eyelids_'+suffix
    animation=u.load_asset(animation_path)
    if not animation:
        factory=u.AnimBlueprintFactory()
        factory.set_editor_property('parent_class',u.VamShapeAnimInstance)
        factory.set_editor_property('target_skeleton',skeleton)
        factory.set_editor_property('preview_skeletal_mesh',mesh)
        animation=u.AssetToolsHelpers.get_asset_tools().create_asset('ABP_Eyelids_'+suffix,root,u.AnimBlueprint,factory)
    assert animation
    u.BlueprintEditorLibrary.compile_blueprint(animation)
    assert u.EditorAssetLibrary.save_loaded_asset(animation)

    blueprint=u.load_asset(root+'/BP_VamCharacter')
    assert blueprint
    defaults=u.get_default_object(blueprint.generated_class())
    character=defaults.get_editor_property('character')
    active=defaults.get_editor_property('active_pose')
    character.set_editor_property('definition',definition)
    character.set_editor_property('rig_profile',rig)
    character.set_editor_property('physics_asset',physics)
    character.set_editor_property('animation_class',animation.generated_class())
    active.set_editor_property('blink_morph_targets',eyelids)
    active.set_editor_property('blink_morph_target','')
    u.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert u.EditorAssetLibrary.save_loaded_asset(blueprint)
    save(status='complete',stage05_folder=folder,eyelid_targets=eyelids,
         rig=rig_path,physics=physics_path,physics_summary=physics_summary,
         animation=animation_path,blueprint=root+'/BP_VamCharacter')
except Exception:
    save(status='failed',error=traceback.format_exc())
    raise
