"""Official state refinement on a new draft, not a runtime mesh replacement."""
import json, os
from pathlib import Path
import unreal as u
root=Path(__file__).resolve().parents[1];out=root/'Saved/MetaHuman/FitRepair'
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
variant=os.environ.get('VAM_MH_FACE_VARIANT','refined')
assert variant in ('refined','face_detail','face_solve','face_anchors','face_regularized','face_autocalibrated','face_generic','face_generic_refined','face_surface'), 'UnknownDiagnosticVariant'
asset_name={'refined':'启梦MH表面细化诊断','face_detail':'启梦MH面部细化诊断','face_solve':'启梦MH面部约束诊断','face_anchors':'启梦MH面部三维校准','face_regularized':'启梦MH面部平滑校准','face_autocalibrated':'启梦MH面部重拟合','face_generic':'启梦MH通用面部校准','face_generic_refined':'启梦MH通用曲面校准','face_surface':'启梦MH通用表面对应'}[variant]
folder='/Game/MetaHumans/'+asset_name
assert not u.EditorAssetLibrary.does_directory_exist(folder),'NameCollision'
origin='/Game/MetaHumans/启梦MH通用面部校准/启梦MH通用面部校准' if variant=='face_generic_refined' else '/Game/MetaHumans/启梦MH手脸校准诊断/启梦MH手脸校准诊断'
character=u.EditorAssetLibrary.duplicate_asset(origin,folder+'/'+asset_name)
target=u.load_asset('/Game/MetaHumans/启梦MH方向法线诊断/SM_Target')
assert sub.try_add_object_to_edit(character)
params=json.loads((out/'face-calibration.json').read_text())
points=json.loads((out/'hand-keypoints.json').read_text())['keypoints']
if variant in ('face_generic','face_generic_refined','face_surface'):
    generic=json.loads((out/('generic-surface-calibration.json' if variant=='face_surface' else 'generic-face-calibration.json')).read_text(encoding='utf8'))
    params=generic['calibration'];points.update(generic['keypoints'])
if variant in ('face_anchors','face_regularized','face_autocalibrated'):
    points.update(json.loads((out/'face-anchors.json').read_text())['keypoints'])
params['KeyPointTargets']={k:dict(zip(('X','Y','Z'),v)) for k,v in points.items()}
params['RefinementSettings']={'Iterations':5,'VertexWeight':0.5,'KeypointWeight':1.0,'Landmark2DWeight':0.2,'Laplacian':0.8,'DistanceTolerance':5.95}
if variant=='face_detail':
    params['RefinementSettings'].update(Iterations=20,VertexWeight=5.0,Landmark2DWeight=2.0,Laplacian=0.1,Strain=0.01,DistanceTolerance=15.0)
if variant=='face_generic_refined':
    params['RefinementSettings'].update(Iterations=10,VertexWeight=2.0,KeypointWeight=1.0,Landmark2DWeight=.2,Laplacian=2.0,Strain=.3,DistanceTolerance=5.95)
(out/(variant+'-recipe.json')).write_text(json.dumps(params,indent=2),encoding='utf8')
if variant in ('face_solve','face_anchors','face_regularized','face_autocalibrated','face_generic','face_surface'):
    weight=lambda v:{'Start':v,'End':v,'Curve':'Static'}
    params['bAutoSolve']=False
    params['BodyConformSolveSettings']={'Iterations':0,'FaceIterations':30,'bSolvePose':False,
        'FaceIcpWeight':weight(30),'FaceIcpSearchTolerance':weight(15),
        'FaceNormalCompatibility':weight(.3),'FaceLandmark2DWeight':weight(2),
        'ModelRegularization':weight(1),'PatchSmoothness':weight(1)}
    if variant=='face_anchors':
        params['BodyConformSolveSettings'].update(FaceIterations=12,FaceKeypointWeight=weight(30))
    if variant=='face_regularized':
        params['BodyConformSolveSettings'].update(FaceIterations=12,FaceKeypointWeight=weight(1),
            FaceNormalCompatibility=weight(.8),FaceLandmark2DWeight=weight(.2),
            ModelRegularization=weight(10),PatchSmoothness=weight(100))
    if variant in ('face_autocalibrated','face_generic','face_surface'):
        params['bAutoSolve']=True
        params['BodyConformSolveSettings']={'PipelineName':'combined'}
    (out/(variant+'-recipe.json')).write_text(json.dumps(params,indent=2),encoding='utf8')
    error=u.VamMetaHumanEditorAdapter.conform(character,target,{int(k):u.Vector(*v) for k,v in points.items()},json.dumps(params))
else:
    error=u.VamMetaHumanEditorAdapter.refine_fit(character,target,json.dumps(params))
assert error=='',str(error)
u.EditorAssetLibrary.save_loaded_asset(character)
for posed,name in [(True,variant+'_posed'),(False,variant+'_apose')]:
    raw=u.VamMetaHumanEditorAdapter.inspect_fit_geometry(character,target,posed)
    assert raw;(out/(name+'.json')).write_text(raw,encoding='utf8')
actor=sub.spawn_meta_human_actor(character,True)
for c in actor.get_components_by_class(u.SkeletalMeshComponent):
    if c.get_name() not in ('Body','Face'):continue
    vs,ts=sub.get_mesh_data_for_conforming(c.get_editor_property('skeletal_mesh_asset'))
    (out/(variant+'_'+c.get_name()+'.json')).write_text(json.dumps({'vertices':[[v.x,v.y,v.z] for v in vs],'triangles':list(ts)}),encoding='utf8')
sub.remove_object_to_edit(character)
(out/(variant+'-status.json')).write_text(json.dumps({
    'state':'Draft','character':character.get_path_name(),'assembly':None,
    'recipe':str(out/(variant+'-recipe.json')),'visual_acceptance_passed':False,
    'scope':'Local official Character fit and geometry export only; requires multi-view review and independent reload'
},ensure_ascii=False,indent=2),encoding='utf8')
