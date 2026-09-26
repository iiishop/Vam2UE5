"""Local official same-topology head import; preserves independent source draft."""
import json,os,hashlib,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import unreal as u
request_path=os.environ.get('VAM_MH_TEMPLATE_REQUEST')
assert request_path,'MissingTemplateRequest: set VAM_MH_TEMPLATE_REQUEST to a saved local request JSON'
request=json.loads(Path(request_path).read_text(encoding='utf8'))
p=Path(request['output_dir']);p.mkdir(parents=True,exist_ok=True);prefix=request['output_prefix']
assert prefix and all(c.isalnum() or c in '_-' for c in prefix),'InvalidEvidencePrefix'
template=Path(request['template_file'])
assert hashlib.sha256(template.read_bytes()).hexdigest()==request['template_sha256'],'TemplateRecipeChanged'
data=json.loads(template.read_text(encoding='utf8'))
reference=None
if request.get('reference_head_file'):
    reference_path=Path(request['reference_head_file'])
    assert hashlib.sha256(reference_path.read_bytes()).hexdigest()==request['reference_head_sha256'],'TemplateReferenceChanged'
    reference=json.loads(reference_path.read_text(encoding='utf8'))
    from vam_face_template_contract import check as check_topology
assert data['report']['flipped_triangles']==0
name=request['name'];assert name and '/' not in name and '.' not in name,'InvalidAssetName'
folder=request['destination'].rstrip('/')+'/'+name
if u.EditorAssetLibrary.does_directory_exist(folder):
    owned=Path(u.Paths.project_content_dir())/(folder[6:]+'/'+name+'.uasset')
    assert request.get('resume_owned_sha256') and owned.is_file() and hashlib.sha256(owned.read_bytes()).hexdigest()==request['resume_owned_sha256'],'NameCollision: rename explicitly; existing assets preserved'
    character=u.load_asset(folder+'/'+name)
else:
    character=u.EditorAssetLibrary.duplicate_asset(request['source_character'],folder+'/'+name)
assert character,'SourceCharacterMissing'
(p/(prefix+'-status.json')).write_text(json.dumps({'state':'Draft','character':character.get_path_name(),'visual_acceptance_passed':False}),encoding='utf8')
sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem);assert sub.try_add_object_to_edit(character)
# Template editing must operate on the editable archetype, even when the
# independently saved source has already completed AutoRig. Only the new clone
# is changed; the official API restores archetype DNA and unregisters morphs.
if u.VamMetaHumanEditorAdapter.has_full_rig(character):
    sub.remove_face_rig(character)
probe=sub.spawn_meta_human_actor(character,True)
try:
    head=next(c for c in probe.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face')
    groups=json.loads(u.VamMetaHumanEditorAdapter.inspect_mesh_sections(head.get_editor_property('skeletal_mesh_asset')))['sections']
    skin=next(s for s in groups if s['slot']=='head_shader_shader')
    assert max(skin['triangles'])+1==len(data['head_vertices']),'OfficialHeadVertexCountMismatch'
    if reference is not None:
        check_topology(reference,skin['triangles'],data['topology_sha256'])
    else:
        assert hashlib.sha256(json.dumps(skin['triangles'],separators=(',',':')).encode()).hexdigest()==data['topology_sha256'],'OfficialHeadTopologyMismatch'
finally:
    u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(probe)
params=u.MetaHumanCharacterFitToVerticesParams()
params.set_editor_property('head_vertices',[u.Vector(*v) for v in data['head_vertices']])
options=u.FitToTargetOptions()
options.set_editor_property('alignment_options',u.MetaHumanAlignmentOptions.NONE)
options.set_editor_property('disable_high_frequency_delta',True)
options.set_editor_property('adapt_neck',True)
params.set_editor_property('options',options)
assert sub.fit_state_to_target_vertices(character,params),'OfficialTemplateHeadFitFailed'
sub.commit_face_state(character)
sub.commit_body_state(character)
assert u.EditorAssetLibrary.save_loaded_asset(character),'TemplateAssetSaveFailed'
actor=sub.spawn_meta_human_actor(character,True)
try:
    for component in actor.get_components_by_class(u.SkeletalMeshComponent):
        if component.get_name()!='Face':continue
        mesh=component.get_editor_property('skeletal_mesh_asset')
        vertices,triangles=sub.get_mesh_data_for_conforming(mesh)
        (p/(prefix+'_Face.json')).write_text(json.dumps({'vertices':[[v.x,v.y,v.z] for v in vertices],'triangles':list(triangles)}),encoding='utf8')
        sections=u.VamMetaHumanEditorAdapter.inspect_mesh_sections(mesh)
        (p/(prefix+'-sections.json')).write_text(sections,encoding='utf8')
        if reference is not None:
            actual_skin=next(s for s in json.loads(sections)['sections'] if s['slot']=='head_shader_shader')
            topology=check_topology(reference,actual_skin['triangles'],data['topology_sha256'])
            topology['reference_head_sha256']=request['reference_head_sha256']
            (p/(prefix+'-topology-check.json')).write_text(json.dumps(topology,indent=2),encoding='utf8')
    (p/(prefix+'-status.json')).write_text(json.dumps({'state':'Draft','character':character.get_path_name(),
        'character_sha256':hashlib.sha256((Path(u.Paths.project_content_dir())/(folder[6:]+'/'+name+'.uasset')).read_bytes()).hexdigest(),
        'api':'UMetaHumanCharacterEditorSubsystem::FitStateToTargetVertices','assembly':None,'visual_acceptance_passed':False},ensure_ascii=False,indent=2),encoding='utf8')
finally:
    u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(actor)
    sub.remove_object_to_edit(character)
