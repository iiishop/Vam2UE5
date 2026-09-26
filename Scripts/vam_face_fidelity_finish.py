"""Resume official Template -> reload -> score -> rig -> score -> Assembly.

Every cloud action is delegated to the existing scoped-consent MH job. This
driver never edits an assembled mesh or treats an offline result as final.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'Saved/Python'))
from vam_metahuman import write_json, fingerprint, verify_recipe, user_name
from vam_face_fidelity_verify import verify

SCRIPTS = Path(__file__).resolve().parent


def read(path): return json.loads(Path(path).read_text(encoding='utf8'))


def fresh_lifecycle(source_recipe):
    """Reuse locked inputs, never another character's delivery receipts."""
    recipe = copy.deepcopy(source_recipe)
    for key in ('owned', 'blueprint', 'blueprint_sha256', 'runtime_engine', 'project',
                'assembly_attempts', 'cooked_verification', 'fidelity_template',
                'fidelity_post_rig', 'recoverable_error', 'diagnostic_only', 'diagnostic_scope',
                'quality_status', 'cohort_tracked_fit_complete'):
        recipe.pop(key, None)
    return recipe


def reusable_rig_exports(official, character, sha256):
    """Reuse two independent captures only while the official asset is unchanged."""
    try:
        captures=[]
        for name in ('RigSnapshot','RigReload'):
            root=Path(official)/name;r=read(root/'reload.json')
            if r['character'].split('.')[0]!=character or r['character_sha256']!=sha256 or not r['source_valid'] or not r['full_rig']:
                return False
            face=read(root/'actual-face.json');head=read(root/'actual-head.json')
            if head['vertices']!=face['vertices'][:len(head['vertices'])]:return False
            captures.append(face)
        a,b=captures
        return (a['triangles']==b['triangles'] and len(a['vertices'])==len(b['vertices'])
                and max(abs(x-y) for p,q in zip(a['vertices'],b['vertices']) for x,y in zip(p,q))<=1e-5)
    except (OSError,KeyError,ValueError,TypeError):return False


def finish(request_file, local_only=False):
    request = read(request_file); job = Path(request['job']).resolve()
    task = read(job/'task.json'); official = job/'Official'; official.mkdir(exist_ok=True)
    verify_output = verify
    if task.get('solver_kind') == 'adaptive-provisional':
        from vam_face_adaptive_verify import verify as verify_output
    user_name(request['name'])
    if task.get('state') not in ('AwaitingOfficialTemplateImport', 'OfficialPartial', 'VerifiedEditorAssembly'):
        raise ValueError('FidelitySolveNotComplete:'+task.get('state', 'Unknown'))
    if fingerprint(job/'template.json') != task['template_sha256']: raise ValueError('TemplateRecipeChanged')
    if (official/'finish-request.json').exists():
        if read(official/'finish-request.json') != request: raise ValueError('FinishRequestChanged: preserve task and create a new one')
    else: write_json(official/'finish-request.json', request)
    source_recipe = verify_recipe(Path(request['source_mh_job']))
    if source_recipe['inputs']['source_ir']['sha256'] != task['inputs']['source_ir']['sha256']:
        raise ValueError('DifferentSourceIdentity')
    engine = Path(request['engine']); engine = engine/'Engine' if (engine/'Engine').is_dir() else engine
    exe = engine/'Binaries/Win64/UnrealEditor-Cmd.exe'
    project = Path(request['project']).resolve()

    def ue(script, label, variables=None, args=()):
        env = os.environ.copy(); env.update(variables or {})
        if (job/'cancel').exists(): raise InterruptedError('Cancelled: checkpoints retained')
        command = [str(exe), str(project), '-run=pythonscript', '-script='+str(SCRIPTS/script),
                   '-AllowCommandletRendering', '-ini:Engine:[ConsoleVariables]:TextureGraph.AllowCommandlets=1',
                   '-unattended', '-nosplash', '-abslog='+str(official/(label+'.log')), *args]
        with (official/(label+'-stdout.log')).open('w', encoding='utf8') as stream:
            result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode: raise RuntimeError('OfficialPhaseFailed:'+label+'; see '+str(official/(label+'.log')))

    imported = official/'Imported'; imported.mkdir(exist_ok=True)
    template_request = {'source_character': request['source_character'], 'name': request['name'],
                        'destination': request['destination'], 'template_file': str(job/'template.json'),
                        'reference_head_file': str(job/'head.json'), 'reference_head_sha256': task['head_sha256'],
                        'template_sha256': task['template_sha256'], 'output_dir': str(imported), 'output_prefix': 'candidate'}
    state_file = imported/'candidate-status.json'
    if not state_file.exists():
        write_json(official/'template-request.json', template_request)
        ue('ue_metahuman_surface_template.py', 'template', {'VAM_MH_TEMPLATE_REQUEST': str(official/'template-request.json')})
    state = read(state_file)
    if not state.get('character_sha256'): raise ValueError('IncompleteTemplateCheckpoint: preserve Draft; no overwrite')
    lifecycle = official/'Lifecycle'
    if not (lifecycle/'recipe.json').exists():
        package = state['character'].split('.')[0]
        character_file = project.parent/'Content'/(package.removeprefix('/Game/')+'.uasset')
        if fingerprint(character_file) != state['character_sha256']: raise ValueError('TemplateDraftModified')
        export_request = {'character': state['character'], 'output': str(official/'TemplateReload'),
                          'reference_head_file':str(job/'head.json'),'reference_head_sha256':task['head_sha256'],
                          'expected': str(imported/'candidate_Face.json')}
        write_json(official/'export-request.json', export_request)
        ue('ue_face_fidelity_export.py', 'template-reload', {'VAM_FACE_EXPORT_REQUEST': str(official/'export-request.json')})
        verify_output(job, official/'TemplateReload')
        receipt = read(job/'post-template-metrics.json')
        lifecycle.mkdir(exist_ok=True)
        recipe = fresh_lifecycle(source_recipe)
        root = request['destination'].rstrip('/')+'/'+request['name']
        recipe.update(name=request['name'], destination=request['destination'], revision=1, state='Draft',
                      fit_complete=True, fit_origin='OfficialTemplateFidelity',
                      source_semantics_reviewed=task.get('solver_kind') != 'adaptive-provisional',
                      assets={'character': package, 'target': root+'/Source/SM_ConformTarget',
                              'mapping': root+'/Source/DA_SourceMapping', 'assembly': root+'/Assembly'},
                      owned={'character': receipt['character_sha256']},
                      fidelity_template={'path': str(job/'post-template-metrics.json'), 'sha256': fingerprint(job/'post-template-metrics.json')})
        (lifecycle/'target.json').write_bytes((job/'source.json').read_bytes())
        target = read(lifecycle/'target.json')
        recipe.update(target_sha256=fingerprint(lifecycle/'target.json'), target_frame=target['coordinate_frame'])
        write_json(lifecycle/'recipe.json', recipe)
    if local_only:
        task.update(state='OfficialPartial', character=state['character'], bp=None,
                    next_phase='AutoRig via existing authorization gate')
        write_json(job/'task.json', task); return task
    recipe = read(lifecycle/'recipe.json')
    if not recipe.get('blueprint'):
        # Old pipeline versions copied unrelated source delivery receipts.
        # Remove those before resuming; they never prove this new BP was cooked.
        for key in ('assembly_attempts','cooked_verification'):recipe.pop(key,None)
        write_json(lifecycle/'recipe.json',recipe)
        asset_file=project.parent/'Content'/(recipe['assets']['character'].removeprefix('/Game/')+'.uasset')
        stable=(fingerprint(asset_file)==recipe['owned']['character'])
        if not stable:raise ValueError('RigCheckpointAssetChanged')
        if recipe['state']!='AwaitingPostRigVerification':
            ue('ue_metahuman_job.py', 'rig', args=('-VamMHJob='+str(lifecycle), '-VamMHMode=rig-only'))
        recipe = read(lifecycle/'recipe.json')
        if recipe['state'] != 'AwaitingPostRigVerification':
            task.update(state='OfficialPartial', next_phase=recipe['state'], character=state['character'])
            write_json(job/'task.json', task); return task
        # Export twice in independent processes: rig geometry may differ from
        # pre-rig geometry, but must be stable on disk before scoring.
        if not reusable_rig_exports(official,recipe['assets']['character'],recipe['owned']['character']):
            for suffix, expected in (('RigSnapshot', None), ('RigReload', str(official/'RigSnapshot/actual-face.json'))):
                value = {'character': state['character'], 'output': str(official/suffix), 'expected': expected}
                write_json(official/'export-request.json', value)
                ue('ue_face_fidelity_export.py', suffix.lower(), {'VAM_FACE_EXPORT_REQUEST': str(official/'export-request.json')})
        verify_output(job, official/'RigReload', 'post-rig-metrics.json')
        recipe['fidelity_post_rig'] = {'path': str(job/'post-rig-metrics.json'), 'sha256': fingerprint(job/'post-rig-metrics.json')}
        write_json(lifecycle/'recipe.json', recipe)
        ue('ue_metahuman_job.py', 'assembly', args=('-VamMHJob='+str(lifecycle), '-VamMHMode=resume'))
    recipe = read(lifecycle/'recipe.json')
    task.pop('recoverable_error',None)
    task.update(state=recipe['state'], character=state['character'], bp=recipe.get('blueprint'), visual_acceptance_passed=False)
    write_json(job/'task.json', task)
    return task


if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--request', required=True); parser.add_argument('--local-only', action='store_true')
    args=parser.parse_args()
    try: print(json.dumps(finish(args.request, args.local_only), ensure_ascii=False))
    except Exception as exc:
        request=read(args.request); job=Path(request['job']); task=read(job/'task.json')
        if task.get('state') in ('AwaitingOfficialTemplateImport', 'OfficialPartial'):
            task.update(state='OfficialPartial', recoverable_error=str(exc)); write_json(job/'task.json', task)
        raise
