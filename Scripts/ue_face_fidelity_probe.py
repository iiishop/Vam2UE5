"""Read-only installed API/settings smoke test. Does not claim fit/rig success."""
import json
import os
from pathlib import Path
import unreal as u

out = Path(os.environ['VAM_FACE_FIDELITY_PROBE'])
sub = u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
settings = u.BodyConformSolveSettings()
fields = ['PipelineName', 'Iterations', 'FaceIterations', 'FaceIcpWeight',
          'FaceIcpSearchTolerance', 'FaceNormalCompatibility',
          'FaceKeypointWeight', 'FaceLandmark2DWeight', 'ModelRegularization', 'PatchSmoothness']
result = {'engine': u.SystemLibrary.get_engine_version(), 'settings': {}, 'api': {},
          'scope': 'reflection only; no conform, cloud, rig, assembly or visual acceptance'}
for field in fields:
    value = settings.get_editor_property(field)
    result['settings'][field] = ({'start': value.get_editor_property('Start'),
                                   'end': value.get_editor_property('End'),
                                   'curve': str(value.get_editor_property('Curve'))}
                                  if isinstance(value, u.WeightSchedule) else value)
for name in ['fit_state_to_target_vertices', 'commit_face_state', 'commit_body_state',
             'conform_to_target_meshes', 'request_auto_rigging', 'build_meta_human']:
    result['api'][name] = callable(getattr(sub, name, None))
assert all(result['api'].values()), 'MissingOfficialAPI'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
