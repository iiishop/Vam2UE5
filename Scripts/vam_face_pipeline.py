"""One saved request: locked p0 -> candidates -> official Character -> Assembly.

Requires an existing official initial fit and its source-pose/A-pose head
export. Never derives that transform by aligning to the source person's face.
"""
import argparse,json,sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_metahuman import write_json,verify_recipe,user_name,fingerprint
from vam_face_fidelity_cli import prepare
from vam_face_adaptive_cli import run,algorithm_fingerprints
from vam_face_adaptive_verify import attach_task
from vam_face_fidelity_finish import finish


def read(path):return json.loads(Path(path).read_text(encoding='utf8'))


def pipeline(request_path,local_only=False,solve_only=False):
    if local_only and solve_only:raise ValueError('ChooseOneStopStage')
    request=read(request_path);root=Path(request['output']).resolve();user_name(request['name'])
    destination=request['destination'].rstrip('/')
    if not destination.startswith('/Game/') or any(x in ('.','..','') for x in destination[6:].split('/')):raise ValueError('InvalidDestination')
    root.mkdir(parents=True,exist_ok=True);saved=root/'pipeline-request.json'
    if saved.exists() and read(saved)!=request:raise ValueError('PipelineRequestChanged: preserve existing task')
    asset=Path(request['project']).resolve().parent/'Content'/destination[6:]/request['name']
    if not saved.exists() and asset.exists():raise ValueError('RenameRequired: destination already exists; choose a user name')
    if not saved.exists():write_json(saved,request)
    recipe=verify_recipe(Path(request['source_mh_job']))
    inputs=root/'Inputs';experiment=root/'Experiment'
    if not inputs.exists():
        prepare(SimpleNamespace(job=str(inputs),source_ir=recipe['inputs']['source_ir']['path'],plan=recipe['inputs']['plan']['path'],head=request['head']))
    if not experiment.exists():
        source=read(inputs/'source.json')
        calibration=Path(__file__).resolve().parents[1]/'Config/FaceTopology'/(
            source['source_topology_family']+'_'+source['source_base_topology_sha256']+'.json')
        if not calibration.is_file():raise ValueError('UnsupportedSourceTopology: calibrate this family first')
        evidence=read(calibration)
        if evidence['topology_family']!=source['source_topology_family'] or evidence['topology_digest']!=source['source_base_topology_sha256']:
            raise ValueError('CalibrationTopologyMismatch')
        experiment.mkdir()
        source.update(surface_interpolation='phong',boundary_constraints='material-and-annulus',strain_model='arap',normalization='rms',
                      eyelid_correspondence='ordered-cycle',eye_self_intersection_guard=True,
                      eye_clearance_guard=True,eye_surface_ownership=True,
                      eye_tangent_refinement=True,eye_neighborhood='geodesic',
                      eye_curve_interpolation='phong',eye_sampling_regularization=True)
        write_json(experiment/'source.json',source)
        engine=Path(request['engine']);engine=engine/'Engine' if (engine/'Engine').is_dir() else engine
        from vam_face_topology_equivalence import with_official_quads
        head=with_official_quads(read(inputs/'head.json'),engine/'Plugins/MetaHuman/MetaHumanAnimator/Content/MeshFitting/Template/mean.obj')
        write_json(experiment/'head.json',head)
        landmarks=engine/'Plugins/MetaHuman/MetaHumanCharacter/Content/Face/IdentityTemplate/face_landmarks.json'
        write_json(experiment/'request.json',{'source':str(experiment/'source.json'),'head':str(experiment/'head.json'),
            'landmarks':str(landmarks),'output':str(experiment)})
    # Record executable project code alongside the recipe. Never silently mix
    # cached candidates from different implementations when resuming.
    code=algorithm_fingerprints()
    code_path=experiment/'algorithm-lock.json'
    if code_path.exists() and read(code_path)!=code:raise ValueError('AlgorithmChanged: create a new pipeline output')
    if (experiment/'task.json').exists() and not code_path.exists():raise ValueError('MissingAlgorithmLock: preserve task and create a new output')
    if not (experiment/'task.json').exists():
        run(experiment/'request.json');attach_task(experiment,inputs)
    if solve_only:return read(experiment/'task.json')
    finish_request={k:request[k] for k in ('name','destination','source_character','source_mh_job','engine','project')}
    finish_request['job']=str(experiment)
    finish_path=experiment/'finish-request.json'
    if finish_path.exists() and read(finish_path)!=finish_request:raise ValueError('FinishRequestChanged')
    write_json(finish_path,finish_request)
    result=finish(finish_path,local_only)
    # Report only after the actual official geometry has passed reload checks.
    source=read(experiment/'source.json')
    calibration=Path(__file__).resolve().parents[1]/'Config/FaceTopology'/(
        source['source_topology_family']+'_'+source['source_base_topology_sha256']+'.json')
    exported=experiment/'Official'/('RigReload' if (experiment/'post-rig-metrics.json').exists() else 'TemplateReload')
    if calibration.is_file() and (exported/'reload.json').is_file():
        from vam_face_region_report import report
        report(experiment,exported,calibration)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True)
    stage=p.add_mutually_exclusive_group();stage.add_argument('--local-only',action='store_true');stage.add_argument('--solve-only',action='store_true')
    a=p.parse_args();print(json.dumps(pipeline(a.request,a.local_only,a.solve_only),ensure_ascii=False))
