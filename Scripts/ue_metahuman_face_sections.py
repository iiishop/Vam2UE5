"""Read actual Face material groups; no saved character modifications."""
import json,os
from pathlib import Path
import unreal as u
p=Path(__file__).resolve().parents[1]/'Saved/MetaHuman/FitRepair'
variant=os.environ.get('VAM_MH_FACE_VARIANT','face_autocalibrated')
name={'face_autocalibrated':'启梦MH面部重拟合','face_generic':'启梦MH通用面部校准','face_generic_refined':'启梦MH通用曲面校准','face_surface':'启梦MH通用表面对应','face_transfer':'启梦MH保形表面校准'}[variant]
character=u.load_asset('/Game/MetaHumans/'+name+'/'+name)
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
assert sub.try_add_object_to_edit(character)
actor=sub.spawn_meta_human_actor(character,True)
try:
    face=next(c for c in actor.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face')
    raw=u.VamMetaHumanEditorAdapter.inspect_mesh_sections(face.get_editor_property('skeletal_mesh_asset'))
    assert raw
    (p/(variant+'-sections.json')).write_text(raw,encoding='utf8')
    vertices,triangles=sub.get_mesh_data_for_conforming(face.get_editor_property('skeletal_mesh_asset'))
    before=json.loads((p/(variant+'_Face.json')).read_text(encoding='utf8'))
    assert list(triangles)==before['triangles'] and len(vertices)==len(before['vertices'])
    error=max(abs(a-b) for v,w in zip(vertices,before['vertices']) for a,b in zip((v.x,v.y,v.z),w))
    assert error<1e-5,'IndependentFaceReloadChanged'
    assert u.VamMetaHumanEditorAdapter.is_character_source_valid(character)
    (p/(variant+'-reload.json')).write_text(json.dumps({'state':'Draft','character':character.get_path_name(),
        'source_valid':True,'face_vertex_count':len(vertices),'max_reload_delta_cm':error,
        'assembly':None,'visual_acceptance_passed':False},ensure_ascii=False,indent=2),encoding='utf8')
    u.log(str([(s['slot'],s.get('material'),len(s['triangles'])//3) for s in json.loads(raw)['sections']]))
finally:
    u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(actor)
    sub.remove_object_to_edit(character)
