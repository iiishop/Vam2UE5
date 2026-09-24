"""Create Stage06 extensions beside, never inside, the accepted Stage05 package."""
import json
import traceback
from pathlib import Path
import sys
import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from ue_stage06_pose_rig_upgrade import configure_joint

PROJECT = Path(u.Paths.project_dir())
REPORT = PROJECT / 'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-build.json'
ROOT = '/Game/VamStage06'
SOURCE = '/Game/VamCharacters/C_1facd7a930a8e1eca11a18a6'

def save_report(**items):
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(items, ensure_ascii=False, indent=2), encoding='utf8')

def asset(name, cls):
    path = ROOT + '/' + name
    old = u.load_asset(path)
    if old:
        return old
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    result = u.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT, cls, factory)
    assert result, path
    return result

try:
    definition = u.load_asset(SOURCE + '/CD_Character')
    assert definition and definition.get_editor_property('build_verified')
    mesh = definition.get_editor_property('body')
    skeleton = definition.get_editor_property('skeleton')
    assert mesh and skeleton
    contract = json.loads((PROJECT / 'Plugins/VamResourceBrowser/Saved/NativeBuild/5397e1a7fbc24e440cc3f964fed840b57706e4013aef22ee905ec57651e826cb.calibrated.json').read_text(encoding='utf8'))
    names = [bone['name'] for bone in contract['bones']]
    semantic = {
        'hip':'root', 'pelvis':'pelvis', 'abdomen':'spine', 'abdomen2':'spine_upper',
        'chest':'chest', 'neck':'neck', 'head':'head', 'lowerJaw':'jaw',
        'lEye':'eye_l', 'rEye':'eye_r',
    }
    for side, prefix in [('l','left'),('r','right')]:
        semantic.update({side+'Collar':prefix+'_clavicle',side+'Shldr':prefix+'_shoulder',
                         side+'ForeArm':prefix+'_elbow',side+'Hand':prefix+'_hand',
                         side+'Thigh':prefix+'_hip',side+'Shin':prefix+'_knee',
                         side+'Foot':prefix+'_foot',side+'Toe':prefix+'_toe',
                         side+'BigToe':prefix+'_big_toe'})
        for finger in ('Thumb','Index','Mid','Ring','Pinky'):
            for segment in range(1,4):
                semantic[f'{side}{finger}{segment}']=f'{prefix}_{finger.lower()}_{segment}'
    rig = asset('DA_Rig_Eyelids_a11a18a6', u.VamRigProfile)
    joints=[]
    for name in names:
        sem=semantic.get(name)
        if sem is None:
            continue
        joint=u.VamRigJoint()
        joint.set_editor_property('semantic',sem)
        joint.set_editor_property('bone',name)
        configure_joint(joint,sem)
        if sem.endswith(('_elbow','_knee')):
            joint.set_editor_property('preferred_bend',u.Rotator(25 if sem.endswith('_knee') else -25,0,0))
        joints.append(joint)
    rig.set_editor_property('skeleton',skeleton)
    rig.set_editor_property('joints',joints)
    rig.set_editor_property('solver_root_semantic','root')
    rig.set_editor_property('effectors',['pelvis','chest','head','left_hand','right_hand','left_foot','right_foot'])
    rig.set_editor_property('iterations',24)
    assert u.EditorAssetLibrary.save_loaded_asset(rig)
    save_report(status='rig_created', rig=rig.get_path_name(), mapped_bones=len(joints), source_bones=len(names))

    physics=u.load_asset(ROOT+'/PA_Eyelids_a11a18a6')
    if not physics:
        physics,error=u.VamStage06AssetEditor.build_physics_asset(ROOT+'/PA_Eyelids_a11a18a6',mesh,8.)
        assert physics,error
    assert physics
    physics_summary=u.VamStage06AssetEditor.configure_physics_asset(physics,rig)
    assert u.EditorAssetLibrary.save_loaded_asset(physics)
    save_report(status='physics_created',rig=rig.get_path_name(),physics=physics.get_path_name(),physics_summary=physics_summary)

    animation=u.load_asset(ROOT+'/ABP_Eyelids_a11a18a6')
    if not animation:
        factory=u.AnimBlueprintFactory()
        factory.set_editor_property('parent_class',u.VamShapeAnimInstance)
        factory.set_editor_property('target_skeleton',skeleton)
        factory.set_editor_property('preview_skeletal_mesh',mesh)
        animation=u.AssetToolsHelpers.get_asset_tools().create_asset('ABP_Eyelids_a11a18a6',ROOT,u.AnimBlueprint,factory)
    assert animation
    u.BlueprintEditorLibrary.compile_blueprint(animation)
    assert u.EditorAssetLibrary.save_loaded_asset(animation)

    blueprint=u.load_asset(ROOT+'/BP_VamCharacter')
    if not blueprint:
        factory=u.BlueprintFactory()
        factory.set_editor_property('parent_class',u.VamCharacterActor)
        blueprint=u.AssetToolsHelpers.get_asset_tools().create_asset('BP_VamCharacter',ROOT,u.Blueprint,factory)
    assert blueprint
    defaults=u.get_default_object(blueprint.generated_class())
    character=defaults.get_editor_property('character')
    character.set_editor_property('definition',definition)
    character.set_editor_property('rig_profile',rig)
    character.set_editor_property('physics_asset',physics)
    character.set_editor_property('animation_class',animation.generated_class())
    eyelids={p.get_editor_property('display_name'):str(p.get_editor_property('target'))
             for p in definition.get_editor_property('parameters')}
    defaults.get_editor_property('active_pose').set_editor_property('blink_morph_targets',
        [eyelids[name] for name in ('Eyes Closed Left','Eyes Closed Right')])
    u.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert u.EditorAssetLibrary.save_loaded_asset(blueprint)
    save_report(status='complete',rig=rig.get_path_name(),physics=physics.get_path_name(),animation=animation.get_path_name(),blueprint=blueprint.get_path_name(),
                mapped_bones=len(joints),physics_summary=physics_summary)
except Exception as exc:
    save_report(status='failed', error=str(exc), traceback=traceback.format_exc())
    raise
