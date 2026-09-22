"""Run by UE's PythonScriptPlugin in an isolated, unsaved inspection project."""
import json
import os
from pathlib import Path
import traceback
import unreal

def main():
    # ExecutePythonScript otherwise closes the editor as soon as this script ends.
    if '-executepythonscript=' in unreal.SystemLibrary.get_command_line().lower():
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    request=Path(os.environ['VAM_PREVIEW_REQUEST'])
    data=json.loads(request.read_text(encoding='utf-8'))
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors=[]
    for mesh in data['meshes']:
        actor=subsystem.spawn_actor_from_class(unreal.DynamicMeshActor,unreal.Vector(),transient=True)
        actor.set_actor_label('VaM inspection - '+mesh['name'])
        component=actor.get_dynamic_mesh_component()
        dynamic=component.get_dynamic_mesh()
        for section,indices in enumerate(mesh['sections']):
            if not indices:continue
            used=sorted(set(indices));remap={v:i for i,v in enumerate(used)}
            buffers=unreal.GeometryScriptSimpleMeshBuffers()
            buffers.vertices=[unreal.Vector(*mesh['vertices'][v])for v in used]
            buffers.uv0=[unreal.Vector2D(*mesh['uv'][v])for v in used]
            buffers.triangles=[unreal.IntVector(*(remap[v]for v in indices[i:i+3]))for i in range(0,len(indices),3)]
            unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers,section)
        unreal.GeometryScript_Normals.recompute_normals(dynamic,unreal.GeometryScriptCalculateNormalsOptions())
        actors.append(actor)
    subsystem.set_selected_level_actors(actors)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(290,-290,160),unreal.Rotator(-9,135,0))
    # A simple inspection light. No source script, physics or game logic runs.
    light=subsystem.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,250),unreal.Rotator(-35,-30,0),transient=True)
    light.light_component.set_intensity(8)
    request.with_suffix('.ue-result.json').write_text(json.dumps({'status':'ready','actors':len(actors),'statistics':data['statistics']}),encoding='utf-8')
    unreal.log('VAM_PREVIEW_READY '+str(data['statistics']))

try:
    main()
except Exception:
    message=traceback.format_exc()
    Path(os.environ['VAM_PREVIEW_REQUEST']).with_suffix('.ue-result.json').write_text(json.dumps({'status':'failed','error':message}),encoding='utf-8')
    unreal.log_error(message)
