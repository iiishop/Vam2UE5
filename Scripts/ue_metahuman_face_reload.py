"""Independent read-only reload check for the Qimeng face repair draft."""
import json
from pathlib import Path
import unreal as u
root=Path(__file__).resolve().parents[1];p=root/'Saved/MetaHuman/FitRepair'
path='/Game/MetaHumans/启梦MH面部重拟合/启梦MH面部重拟合'
character=u.load_asset(path)
assert character and u.VamMetaHumanEditorAdapter.is_character_source_valid(character)
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
assert sub.try_add_object_to_edit(character)
actor=sub.spawn_meta_human_actor(character,True)
assert actor
try:
    face=next(c for c in actor.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face')
    vertices,triangles=sub.get_mesh_data_for_conforming(face.get_editor_property('skeletal_mesh_asset'))
    old=json.loads((p/'face_autocalibrated_Face.json').read_text())
    assert len(vertices)==len(old['vertices']) and list(triangles)==old['triangles']
    error=max(abs(a-b) for v,w in zip(vertices,old['vertices']) for a,b in zip((v.x,v.y,v.z),w))
    assert error<1e-5, 'Saved Face geometry differs after independent reload'
    report={'state':'Draft','character':path,'editable_source_reload_verified':True,
            'face_vertex_count':len(vertices),'face_max_reload_delta_cm':error,
            'assembly':None,'visual_acceptance_passed':False,
            'remaining':'Eye/lip/jaw likeness refinement; full rig/assembly/cooked checks not performed for this draft'}
    (p/'face-autocalibrated-reload.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
finally:
    u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(actor)
    sub.remove_object_to_edit(character)
