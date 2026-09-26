"""Local hand-constrained and face-tracked comparison; no cloud or assembly."""
import json,sys
from pathlib import Path
import unreal as u
root=Path(__file__).resolve().parents[1];out=root/'Saved/MetaHuman/FitRepair'
sys.path.insert(0,str(root/'Saved/Python'))
from PIL import Image
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
pixels=Image.open(out/'tracking-input.png').convert('RGB')
curves=sub.track_face_landmarks_from_image([u.Color(r,g,b,255) for r,g,b in pixels.getdata()],512,512)
assert curves, 'FaceTrackingFailed: retain input camera and request manual calibration'
camera=json.loads((out/'tracking-camera.json').read_text())
calibration={'CameraViewInfo':{'Location':dict(zip(('X','Y','Z'),camera['location'])),
    'Rotation':{'Pitch':0,'Yaw':-90,'Roll':0},'FOV':camera['fov'],'AspectRatio':1},
    'ImageSize':{'X':512,'Y':512},'CurveTrackingPoints':{str(k):{'TrackingPoints':[{'X':p.x,'Y':p.y} for p in v.tracking_points]} for k,v in curves.items()}}
(out/'face-calibration.json').write_text(json.dumps(calibration,indent=2),encoding='utf8')
folder='/Game/MetaHumans/启梦MH手脸校准诊断'
assert not u.EditorAssetLibrary.does_directory_exist(folder),'NameCollision: preserve diagnostic'
target=u.load_asset('/Game/MetaHumans/启梦MH方向法线诊断/SM_Target')
character=u.AssetToolsHelpers.get_asset_tools().create_asset('启梦MH手脸校准诊断',folder,u.MetaHumanCharacter,u.MetaHumanCharacterFactoryNew())
assert sub.try_add_object_to_edit(character)
data=json.loads((out/'hand-keypoints.json').read_text())
error=u.VamMetaHumanEditorAdapter.conform(character,target,{int(k):u.Vector(*v) for k,v in data['keypoints'].items()},json.dumps(calibration))
assert error=='',str(error)
u.EditorAssetLibrary.save_loaded_asset(character)
for posed,name in [(True,'calibrated_posed'),(False,'calibrated_apose')]:
    raw=u.VamMetaHumanEditorAdapter.inspect_fit_geometry(character,target,posed)
    assert raw;(out/(name+'.json')).write_text(raw,encoding='utf8')
actor=sub.spawn_meta_human_actor(character,True)
for component in actor.get_components_by_class(u.SkeletalMeshComponent):
    if component.get_name() not in ('Body','Face'):continue
    vertices,triangles=sub.get_mesh_data_for_conforming(component.get_editor_property('skeletal_mesh_asset'))
    (out/('calibrated_'+component.get_name()+'.json')).write_text(json.dumps({'vertices':[[v.x,v.y,v.z] for v in vertices],'triangles':list(triangles)}),encoding='utf8')
sub.remove_object_to_edit(character)
u.log('CALIBRATED_FIT_COMPLETE')
