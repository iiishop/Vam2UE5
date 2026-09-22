"""Run by UE's PythonScriptPlugin in an isolated, unsaved inspection project."""
import json
import os
from pathlib import Path
import traceback
import sys
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vam_ue_mesh import normals_for_ue, ue_triangles

def main():
    # ExecutePythonScript otherwise closes the editor as soon as this script ends.
    if '-executepythonscript=' in unreal.SystemLibrary.get_command_line().lower():
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    request=Path(os.environ['VAM_PREVIEW_REQUEST'])
    data=json.loads(request.read_text(encoding='utf-8'))
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    appearance=None
    if data.get('source_material_ir'):
        from ue_source_materials import Appearance
        appearance=Appearance(data['source_material_ir'])
    actors=[]
    for mesh_index,mesh in enumerate(data['meshes']):
        actor=subsystem.spawn_actor_from_class(unreal.DynamicMeshActor,unreal.Vector(),transient=True)
        actor.set_actor_label('VaM inspection - '+mesh['name'])
        component=actor.get_dynamic_mesh_component()
        if appearance:component.set_tangents_type(unreal.DynamicMeshComponentTangentsMode.AUTO_CALCULATED)
        dynamic=component.get_dynamic_mesh()
        normals=normals_for_ue(mesh)
        for section,indices in enumerate(mesh['sections']):
            if not indices:continue
            if appearance:
                material,hidden=appearance.material(mesh_index,section)
                if hidden:continue
                if material:component.set_material(section,material)
            used=sorted(set(indices));remap={v:i for i,v in enumerate(used)}
            buffers=unreal.GeometryScriptSimpleMeshBuffers()
            buffers.vertices=[unreal.Vector(*mesh['vertices'][v])for v in used]
            buffers.normals=[unreal.Vector(*normals[v])for v in used]
            buffers.uv0=[unreal.Vector2D(*mesh['uv'][v])for v in used]
            buffers.triangles=[unreal.IntVector(*t)for t in ue_triangles(indices,remap)]
            unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers,section)
        actors.append(actor)
    subsystem.set_selected_level_actors(actors)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(290,-290,160),unreal.Rotator(-9,135,0))
    # A simple inspection light. No source script, physics or game logic runs.
    if appearance:appearance.scene(subsystem,'-executepythonscript=' in unreal.SystemLibrary.get_command_line().lower())
    else:
        light=subsystem.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,250),unreal.Rotator(-35,-30,0),transient=True)
        light.light_component.set_intensity(8)
    request.with_suffix('.ue-result.json').write_text(json.dumps({'status':'partial' if appearance and (appearance.errors or appearance.ir['diagnostics']) else 'ready','actors':len(actors),'statistics':data['statistics'],'material_errors':appearance.errors if appearance else [],'material_count':len(appearance.materials) if appearance else 0}),encoding='utf-8')
    unreal.log('VAM_PREVIEW_READY '+str(data['statistics']))

try:
    main()
except Exception:
    message=traceback.format_exc()
    Path(os.environ['VAM_PREVIEW_REQUEST']).with_suffix('.ue-result.json').write_text(json.dumps({'status':'failed','error':message}),encoding='utf-8')
    unreal.log_error(message)
