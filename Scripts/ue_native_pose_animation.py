"""Original 22-degree shoulder test clip, not imported VaM motion."""
import json,math,sys,os
from pathlib import Path
import unreal as u
scripts=Path(__file__).resolve().parent
sys.path.insert(0,str(scripts))
from vam_native_source import qmul
saved=scripts.parent/'Saved'
report_path=Path(os.environ.get('VAM_NATIVE_REPORT_FILE',str(saved/'NativeBuild/latest-native-assets.json')))
report=json.loads(report_path.read_text(encoding='utf8'))
contract=json.loads((saved/'NativeBuild'/(report['source_digest']+'.calibrated.json')).read_text(encoding='utf8'))
path=report['folder']+'/A_ShoulderValidation'
assert not u.EditorAssetLibrary.does_asset_exist(path),'Do not overwrite animation edits'
body=u.load_asset(report['folder']+'/SK_Body')
factory=u.AnimSequenceFactory();factory.set_editor_property('target_skeleton',body.get_editor_property('skeleton'))
factory.set_editor_property('preview_skeletal_mesh',body)
animation=u.AssetToolsHelpers.get_asset_tools().create_asset('A_ShoulderValidation',report['folder'],u.AnimSequence,factory)
controller=animation.controller
controller.open_bracket('Original single-joint validation clip')
try:
    controller.set_frame_rate(u.FrameRate(30,1));controller.set_number_of_frames(u.FrameNumber(30))
    for bone in contract['bones']:
        assert controller.add_bone_curve(bone['name'])
        rotations=[]
        for frame in range(31):
            angle=math.radians(22*math.sin(math.pi*frame/30)) if bone['name']=='lShldr' else 0
            q=qmul(bone['quaternion_xyzw'],[0,0,math.sin(angle/2),math.cos(angle/2)])
            rotations.append(u.Quat(*q))
        assert controller.set_bone_track_keys(bone['name'],[u.Vector(*bone['translation'])]*31,rotations,[u.Vector(1,1,1)]*31)
finally:controller.close_bracket()
assert u.EditorAssetLibrary.save_loaded_asset(animation,False)
report['validation_animation']=path
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_NATIVE_POSE_ANIMATION_SAVED '+path)
