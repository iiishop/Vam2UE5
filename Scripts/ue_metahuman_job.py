"""Editor-only resumable MH00 task. No source or texture upload without a scoped grant."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback
import unreal as u

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
from vam_metahuman import prepare, verify_recipe, write_json, fingerprint, SCHEMA, require_current_target_frame, diagnostic_baseline_allowed
from vam_native_job_state import progress, check_cancel, BuildCancelled, exclusive_build
from vam_metahuman_consent import standing_consent


def asset_file(package):
    if not package.startswith('/Game/'): raise ValueError('NonGameAsset')
    return Path(u.Paths.project_content_dir())/(package[6:]+'.uasset')


def preflight_folder(folder):
    disk = Path(u.Paths.project_content_dir())/folder.removeprefix('/Game/')
    if (disk.exists() and any(disk.iterdir())) or u.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
        raise ValueError('RenameRequired: destination exists; choose a new name: '+folder)


def save(asset):
    if not asset or not u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('AssetSaveFailed: '+str(asset))


def owned(recipe, key):
    path = recipe['assets'][key]
    record = recipe.get('owned', {}).get(key)
    if not record or fingerprint(asset_file(path)) != record:
        raise RuntimeError('DraftModified: preserve user edits; use adopt-calibration explicitly: '+path)
    asset = u.load_asset(path)
    if not asset: raise RuntimeError('AssetReloadFailed: '+path)
    return asset


def checkpoint(job, recipe, character=None, state=None):
    if character:
        save(character)
        recipe.setdefault('owned', {})['character'] = fingerprint(asset_file(recipe['assets']['character']))
    if state: recipe['state'] = state
    write_json(job/'recipe.json', recipe)


def reload_check(job, mode):
    command = [str(Path(u.Paths.engine_dir())/'Binaries/Win64/UnrealEditor-Cmd.exe'),
        str(Path(u.Paths.get_project_file_path()).resolve()), '-run=pythonscript',
        '-script='+str(SCRIPTS/'ue_metahuman_verify.py'), '-VamMHJob='+str(job),
        '-VamMHVerify='+mode, '-unattended', '-nosplash', '-NullRHI', '-abslog='+str(job/(mode+'-reload.log'))]
    result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
        timeout=1200, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode: raise RuntimeError('IndependentReloadFailed: '+str(job/(mode+'-reload.log')))


def disclosure(job, recipe):
    value = {'schema': 'vam-metahuman-upload-disclosure/1', 'recipient': 'Epic MetaHuman Auto-Rigging and Texture Sources services',
        'character': recipe['assets']['character'], 'character_sha256': recipe['owned']['character'],
        'recipe_revision': recipe['revision'], 'operation': 'JointsAndBlendShapes plus official texture-source download',
        'fields_from_local_service_implementation': ['Conformed MetaHuman head, teeth, eyes, saliva, eye shell, lashes, eye edge and cartilage vertices',
            'MetaHuman face bind pose, model coefficients, model identifier, global scale, high-frequency variant, rig/refinement options',
            'Texture requests: official face high-frequency index, body tone index, body surface-map identifier, texture types and requested resolutions'],
        'excluded': ['SourceIR', 'VaM source folder', 'original textures', 'clothing/hair assets', 'anatomical extensions', 'paid plugin code'],
        'authorization': 'Authorization is recorded separately; requires an exact disclosure grant or existing explicit consent for the identical service/data scope.',
        'texture_download': 'Authorization covers official texture-source requests only; no original source texture is uploaded.'}
    write_json(job/'upload-disclosure.json', value)
    return fingerprint(job/'upload-disclosure.json')


def run(job, mode):
    request = json.loads((job/'request.json').read_text(encoding='utf8')) if (job/'request.json').exists() else {}
    if not (job/'recipe.json').exists():
        preflight_folder(request['target']+'/'+request['name'])
        progress('Preparing', 'Checking source lock and extracting neutral head/body', True)
        prepare(SCRIPTS.parent/'Saved', request['preview'], request['name'], request['target'], job,
                u.Paths.engine_dir(), request.get('morph_classification'), request.get('keypoints'), request.get('calibration'))
    recipe = verify_recipe(job)
    diagnostic_baseline = diagnostic_baseline_allowed(recipe,request,mode)
    if diagnostic_baseline:
        recipe['diagnostic_only'] = True
        recipe['quality_status'] = 'DraftStructuralBaseline'
        recipe['diagnostic_scope'] = 'Official conversion/rig/assembly lifecycle only; residual solver gates remain failed or untested. Body hand constraints and visual fidelity are not certified.'
    require_current_target_frame(job, recipe)
    # Official SetPostProcessAnimBP resolves assets through registry queries, not LoadObject.
    # Commandlets must finish discovery before generation, or the Face post-process is silently null.
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    caps = json.loads(u.VamMetaHumanEditorAdapter.probe()); write_json(job/'capabilities-runtime.json', caps)
    if recipe.get('runtime_engine') and recipe['runtime_engine'] != caps['engine']:
        raise RuntimeError('RuntimeVersionChanged: review and rebaseline recipe explicitly before resuming')
    recipe['runtime_engine'] = caps['engine']
    if not caps['core_data']:
        raise RuntimeError('CoreDataMissing: finish Creator Core Data installation, then resume the same job')
    if recipe['engine']['MajorVersion'] != 5 or recipe['engine']['MinorVersion'] != 8:
        raise RuntimeError('VersionUnsupported: 5.6/5.7 need a verified official-topology template; Genesis topology is not a template')
    if not u.SystemLibrary.get_engine_version().startswith('5.8.'):
        raise RuntimeError('RunningEngineVersionMismatch')
    check_cancel()
    if recipe.get('blueprint') and mode != 'adopt-calibration':
        if recipe.get('blueprint_sha256') != fingerprint(asset_file(recipe['blueprint'].split('.')[0])):
            raise RuntimeError('AssemblyChanged: existing Blueprint must match the saved task checkpoint')
        reload_check(job, 'assembly')
        checkpoint(job, recipe, state='VerifiedEditorAssembly')
        progress('VerifiedEditorAssembly', recipe['blueprint']+'; independent verification repeated', False)
        return True
    adapter = u.VamMetaHumanEditorAdapter
    subsystem = u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
    project = str(Path(u.Paths.get_project_file_path()).resolve())
    if recipe.get('project') and recipe['project'] != project:
        raise RuntimeError('RecipeProjectMismatch: resume against the project that owns the Draft')
    recipe['project'] = project
    if 'owned' not in recipe:
        preflight_folder(recipe['destination']+'/'+recipe['name'])
        recipe['owned'] = {}; checkpoint(job, recipe, state='Partial')
    geometry = json.loads((job/'target.json').read_text(encoding='utf8'))
    if 'target' not in recipe['owned']:
        target, error = adapter.create_target(recipe['assets']['target'], [u.Vector(*v) for v in geometry['vertices']], geometry['triangles'])
        if not target: raise RuntimeError(error)
        save(target); recipe['owned']['target'] = fingerprint(asset_file(recipe['assets']['target'])); checkpoint(job, recipe)
    else: target = owned(recipe, 'target')
    check_cancel()
    if 'mapping' not in recipe['owned']:
        mapping = u.VamNativeBuilder.create_source_mapping(recipe['assets']['mapping'], recipe['inputs']['source_ir']['sha256'],
            'MH00-target-'+recipe['target_sha256'], Path(recipe['inputs']['source_ir']['path']).read_text(encoding='utf8'),
            Path(recipe['inputs']['material_ir']['path']).read_text(encoding='utf8'), json.dumps(recipe, ensure_ascii=False),
            [], geometry['input_to_source_vertex'])
        save(mapping); recipe['owned']['mapping'] = fingerprint(asset_file(recipe['assets']['mapping'])); checkpoint(job, recipe)
    else: owned(recipe, 'mapping')
    check_cancel()
    if 'character' not in recipe['owned']:
        if u.EditorAssetLibrary.does_asset_exist(recipe['assets']['character']) or asset_file(recipe['assets']['character']).exists():
            raise RuntimeError('RenameRequired: unowned Character already exists')
        character = u.AssetToolsHelpers.get_asset_tools().create_asset(recipe['name'], recipe['assets']['character'].rsplit('/',1)[0],
            u.MetaHumanCharacter, u.MetaHumanCharacterFactoryNew())
        checkpoint(job, recipe, character, 'Draft')
    else:
        if mode == 'adopt-calibration':
            character = u.load_asset(recipe['assets']['character'])
            if not adapter.is_character_source_valid(character): raise RuntimeError('CharacterMissingOrInvalid')
            recipe['fit_complete'] = True; recipe['fit_origin'] = 'user-calibrated'
            recipe['revision'] += 1; checkpoint(job, recipe, character, 'Draft')
        character = owned(recipe, 'character')
    if not subsystem.try_add_object_to_edit(character): raise RuntimeError('CharacterEditRegistrationFailed')
    try:
        if mode == 'adopt-calibration':
            calibration = adapter.export_calibration(character, target)
            if not calibration: raise RuntimeError('CalibrationExportFailed')
            recipe['calibration'] = json.loads(calibration)
            exported = recipe['calibration'].get('keyPointTargets', recipe['calibration'].get('KeyPointTargets', {}))
            recipe['keypoints'] = {str(k): [v.get(axis, v.get(axis.lower())) for axis in ('X','Y','Z')]
                                   for k,v in exported.items()}
            checkpoint(job, recipe, character, 'Draft')
        if not recipe.get('fit_complete'):
            progress('Conforming', 'Local official combined mesh solver; cancellation applied before publication', False)
            points = {int(k): u.Vector(*v) for k,v in recipe['keypoints'].items()}
            # UE Python maps bool + one out parameter to Optional[out], not (bool, out).
            error = adapter.conform(character, target, points, json.dumps(recipe['calibration']) if recipe.get('calibration') else '')
            checkpoint(job, recipe, character, 'Draft')
            if error is None or error: raise RuntimeError(str(error or 'ConformFailed: retain Draft and calibrate From Custom Mesh; see solver log'))
            check_cancel(); recipe['fit_complete'] = True; recipe['fit_origin'] = 'ConformToTargetMeshes'
            checkpoint(job, recipe, character, 'Draft')
        reload_check(job, 'source')
        check_cancel()
        if mode == 'fit':
            checkpoint(job, recipe, character, 'Draft')
            progress('Draft', 'Local fit saved and reloaded; inspect geometry before cloud rigging and assembly', False)
            return False
        # A successful nearest-surface solve can leave arms/hands in the wrong
        # pose. Do not promote an unconstrained combined fit to a game asset.
        calibration = recipe.get('calibration') or {}
        size = calibration.get('ImageSize', calibration.get('imageSize', {}))
        curves = calibration.get('CurveTrackingPoints', calibration.get('curveTrackingPoints', {}))
        fidelity = recipe.get('fit_origin') == 'OfficialTemplateFidelity'
        if fidelity:
            from vam_face_fidelity_gate import require_receipt
            require_receipt(recipe, 'fidelity_template')
        if not fidelity and ((not recipe.get('keypoints') and not diagnostic_baseline) or not curves or min(size.get('X',size.get('x',0)),size.get('Y',size.get('y',0))) <= 0):
            checkpoint(job, recipe, character, 'AwaitingCalibration')
            progress('AwaitingCalibration', 'Draft retained. Combined fitting needs saved body/hand correspondences and calibrated face tracking; solver success alone is insufficient. See MH00_FIT_REPAIR.md.', False)
            return False
        cloud_allowed = False
        if not adapter.has_full_rig(character) or not subsystem.can_build_meta_human(character, False):
            grant = disclosure(job, recipe)
            disclosed = json.loads((job/'upload-disclosure.json').read_text(encoding='utf8'))
            standing = standing_consent(SCRIPTS.parent/'Saved', disclosed)
            exact_grant = mode in ('rig', 'rig-only') and os.environ.get('VAM_MH_AUTHORIZED_UPLOAD_SHA256') == grant
            if not standing and not exact_grant:
                checkpoint(job, recipe, state='AwaitingAuthorization')
                progress('AwaitingAuthorization', 'Draft saved and independently reloaded. Review '+str(job/'upload-disclosure.json'), False)
                return False
            cloud_allowed = True
            receipt = {'disclosure_sha256': grant, 'basis': 'existing-scoped-user-consent' if standing else 'explicit-disclosure-grant',
                       'character': recipe['assets']['character'], 'recipe_revision': recipe['revision']}
            write_json(job/('authorization-'+grant+'.json'), receipt)
        if not adapter.has_full_rig(character):
            progress('AutoRig', 'Explicitly authorized official AutoRig request; awaiting official completion', False)
            params = u.MetaHumanCharacterAutoRiggingRequestParams()
            params.set_editor_property('rig_type', u.MetaHumanRigType.JOINTS_AND_BLEND_SHAPES)
            params.set_editor_property('blocking', True); params.set_editor_property('report_progress', False)
            # The official blocking wrapper pumps the cloud lifecycle; full DNA/correctives checked afterwards.
            subsystem.request_auto_rigging(character, params)
            checkpoint(job, recipe, character, 'Draft')
            if not adapter.has_full_rig(character): raise RuntimeError('AutoRigIncomplete: authentication/service/DNA failure; review log and retry with new disclosure')
        check_cancel()
        if not subsystem.can_build_meta_human(character, False):
            if not cloud_allowed: raise RuntimeError('TextureSourcesAuthorizationRequired')
            progress('TextureSources', 'Explicitly authorized official texture requests; no original texture upload', False)
            texture_params = u.MetaHumanCharacterTextureRequestParams()
            texture_params.set_editor_property('blocking', True); texture_params.set_editor_property('report_progress', False)
            subsystem.request_texture_sources(character, texture_params)
            checkpoint(job, recipe, character, 'Draft')
            if not subsystem.can_build_meta_human(character, True):
                raise RuntimeError('AssemblyPrerequisitesMissing: official CanBuildMetaHuman rejected after texture request; inspect authentication/service errors')
        if mode == 'rig-only':
            checkpoint(job, recipe, character, 'AwaitingPostRigVerification')
            return False
        if fidelity:
            if not recipe.get('fidelity_post_rig'):
                checkpoint(job, recipe, character, 'AwaitingPostRigVerification')
                return False
            require_receipt(recipe, 'fidelity_post_rig')
        preflight_folder(recipe['assets']['assembly'])
        progress('Assembly', 'Building official Cinematic assembly with full LODs', False)
        params = u.MetaHumanCharacterEditorBuildParameters()
        params.set_editor_property('absolute_build_path', recipe['assets']['assembly'])
        params.set_editor_property('name_override', recipe['name'])
        params.set_editor_property('common_folder_path', recipe['assets']['assembly']+'/Common')
        build_error = None
        try:
            subsystem.build_meta_human(character, params)
        except RuntimeError as exc:
            # UE may finish assembly but surface Control Rig migration diagnostics through Python.
            # Save the actual output as Partial; validation below must independently prove usability.
            build_error = str(exc)
            write_json(job/'assembly-build-diagnostics.json', {'error': build_error, 'state': 'Partial'})
        assembly_paths = u.EditorAssetLibrary.list_assets(recipe['assets']['assembly'], recursive=True, include_folder=False)
        for path in assembly_paths:
            asset = u.load_asset(path)
            if asset: save(asset)
        checkpoint(job, recipe, character, 'Partial')
        if build_error and not all('Cannot break link' in line for line in build_error.splitlines() if line.strip()):
            raise RuntimeError('OfficialAssemblyFailed: '+build_error)
        # BuildMetaHuman returns void. Never infer success from its return value.
        candidates = []
        for path in assembly_paths:
            asset = u.load_asset(path)
            if isinstance(asset, u.Blueprint) and asset.get_name() == 'BP_'+recipe['name']:
                candidates.append(asset)
        if len(candidates) != 1: raise RuntimeError('AssemblyIncomplete: expected one official character BP')
        blueprint = candidates[0]
        error = adapter.attach_runtime(blueprint)
        if error is None or error: raise RuntimeError(str(error or 'RuntimeAdapterAttachmentFailed'))
        save(blueprint); checkpoint(job, recipe, character, 'Partial')
        recipe['blueprint'] = blueprint.get_path_name()
        recipe['blueprint_sha256'] = fingerprint(asset_file(blueprint.get_path_name().split('.')[0]))
        checkpoint(job, recipe)
        check_cancel(); reload_check(job, 'assembly'); check_cancel()
        checkpoint(job, recipe, state='VerifiedEditorAssembly')
        progress('VerifiedEditorAssembly', recipe['blueprint']+'; Cooked/visual acceptance not asserted', False)
        return True
    finally:
        subsystem.remove_object_to_edit(character)


def main():
    command = u.SystemLibrary.get_command_line()
    match = re.search(r'-VamMHJob=(?:"([^"]+)"|(\S+))', command)
    if not match: raise RuntimeError('Missing -VamMHJob')
    job = Path(match.group(1) or match.group(2)).resolve()
    mode_match = re.search(r'-VamMHMode=(\S+)', command)
    mode = mode_match.group(1) if mode_match else 'resume'
    if mode not in ('resume', 'rig', 'adopt-calibration', 'fit', 'rig-only', 'diagnostic-baseline'): raise ValueError('UnknownTaskMode')
    os.environ['VAM_BUILD_JOB'] = str(job)
    try:
        with exclusive_build(SCRIPTS.parent/'Saved'): run(job, mode)
    except BuildCancelled:
        progress('Cancelled', 'Draft retained; no publication', False)
    except Exception as exc:
        progress('Partial', str(exc)+'; recovery recipe: '+str(job/'recipe.json'), False)
        u.log_error(traceback.format_exc())
        raise


if __name__ == '__main__': main()
