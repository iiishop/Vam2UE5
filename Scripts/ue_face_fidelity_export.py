"""Generic read-only Character export / independent reload check, Editor only."""
import hashlib
import json
import os
from pathlib import Path
import sys
import unreal as u
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vam_metahuman import write_json
from vam_face_template_contract import compare_reload


def export_character(character_path, output, expected=None, reference=None):
    package = character_path.split('.')[0]
    if not package.startswith('/Game/'): raise ValueError('ExpectedGameCharacter')
    asset_file = Path(u.Paths.project_content_dir())/(package[6:]+'.uasset')
    before_hash = hashlib.sha256(asset_file.read_bytes()).hexdigest()
    character = u.load_asset(character_path)
    if not u.VamMetaHumanEditorAdapter.is_character_source_valid(character): raise ValueError('InvalidCharacterSource')
    sub = u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
    if not sub.try_add_object_to_edit(character): raise ValueError('EditorRegistrationFailed')
    actor = None
    try:
        actor = sub.spawn_meta_human_actor(character, True)
        components = actor.get_components_by_class(u.SkeletalMeshComponent)
        face = next(c for c in components if c.get_name() == 'Face')
        mesh = face.get_editor_property('skeletal_mesh_asset')
        vertices, triangles = sub.get_mesh_data_for_conforming(mesh)
        sections = json.loads(u.VamMetaHumanEditorAdapter.inspect_mesh_sections(mesh))['sections']
        skin = next(s for s in sections if s['slot'] == 'head_shader_shader')
        count = max(skin['triangles'])+1
        if set(skin['triangles']) != set(range(count)): raise ValueError('NonContiguousOfficialHeadIndices')
        raw = {'vertices': [[v.x, v.y, v.z] for v in vertices], 'triangles': list(triangles)}
        head = {'vertices': raw['vertices'][:count], 'triangles': skin['triangles']}
        difference = None; topology_change=None
        if expected:
            prior = json.loads(Path(expected).read_text(encoding='utf8'))
            difference,topology_change=compare_reload(prior,raw,reference)
        output = Path(output); output.mkdir(parents=True, exist_ok=True)
        write_json(output/'actual-face.json', raw); write_json(output/'actual-head.json', head)
        write_json(output/'actual-sections.json', {'sections': sections})
        report = {'state': 'Draft', 'character': character_path, 'character_sha256': before_hash,
                  'source_valid': True, 'head_vertex_count': count, 'face_vertex_count': len(vertices),
                  'max_reload_delta_cm': difference, 'topology_change': topology_change,
                  'full_rig': bool(u.VamMetaHumanEditorAdapter.has_full_rig(character)),
                  'assembly': None, 'visual_acceptance_passed': False}
        if hashlib.sha256(asset_file.read_bytes()).hexdigest() != before_hash: raise ValueError('ReadOnlyExportChangedAsset')
        write_json(output/'reload.json', report)
        return report
    finally:
        if actor: u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(actor)
        sub.remove_object_to_edit(character)


if __name__ == '__main__':
    request = json.loads(Path(os.environ['VAM_FACE_EXPORT_REQUEST']).read_text(encoding='utf8'))
    reference=None
    if request.get('reference_head_file'):
        path=Path(request['reference_head_file'])
        if hashlib.sha256(path.read_bytes()).hexdigest()!=request['reference_head_sha256']:raise ValueError('ReloadReferenceChanged')
        reference=json.loads(path.read_text(encoding='utf8'))
    export_character(request['character'], request['output'], request.get('expected'),reference)
    if request.get('blueprint'):
        from vam_face_assembly_check import compare_head
        bp=u.load_asset(request['blueprint'])
        if not isinstance(bp,u.Blueprint):raise ValueError('AssemblyBlueprintMissing')
        actors=u.get_editor_subsystem(u.EditorActorSubsystem)
        actor=actors.spawn_actor_from_class(bp.generated_class(),u.Vector(0,0,0))
        if not actor:raise ValueError('AssemblySpawnFailed')
        try:
            face=next(c for c in actor.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face')
            mesh=face.get_editor_property('skeletal_mesh_asset');sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
            vs,ts=sub.get_mesh_data_for_conforming(mesh)
            sections=json.loads(u.VamMetaHumanEditorAdapter.inspect_mesh_sections(mesh))['sections']
            skin=next(s for s in sections if s['slot']=='head_shader_shader');count=max(skin['triangles'])+1
            actual={'vertices':[[v.x,v.y,v.z] for v in vs[:count]],'triangles':skin['triangles']}
            out=Path(request['output']);reference=json.loads((out/'actual-head.json').read_text(encoding='utf8'))
            result=compare_head(reference,actual);result['blueprint']=request['blueprint']
            write_json(out/'assembly-head.json',actual);write_json(out/'assembly-head-check.json',result)
        finally:actors.destroy_actor(actor)
