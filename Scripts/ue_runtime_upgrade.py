"""Fresh-process upgrade of an explicit persisted native character; no preview cache."""
import json
import os
import subprocess
import sys
from pathlib import Path
import unreal as u

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
from vam_runtime_recipe import require, validate_recipe, digest
from vam_native_job_state import progress, check_cancel, exclusive_build


def run():
    job = Path(os.environ['VAM_BUILD_JOB'])
    asset = u.load_asset(os.environ['VAM_UPGRADE_ASSET'])
    require(asset is not None, 'Selected character asset is missing; save it first')
    previous = None
    if isinstance(asset, u.Blueprint):
        require(isinstance(u.get_default_object(asset.generated_class()), u.VamCharacterActor), 'Select a VamCharacter Blueprint or CharacterDefinition')
        character = u.get_default_object(asset.generated_class()).get_editor_property('character')
        previous = character.get_editor_property('runtime_configuration')
        asset = character.get_editor_property('definition')
        if previous:
            asset = previous.get_editor_property('definition')
    require(isinstance(asset, u.VamCharacterDefinition), 'Select a saved VamCharacter Blueprint or CharacterDefinition')
    policy_path = SCRIPTS.parent / 'Config/RuntimeImportPolicy.json'
    policy = json.loads(policy_path.read_text(encoding='utf8'))
    require(policy['schema'] == 'vam-runtime-import-policy/1', 'Unknown import policy')
    folder = asset.get_path_name().split('.')[0].rsplit('/', 1)[0]
    if previous:
        receipt = json.loads(previous.get_editor_property('receipt_json'))
        recipe = receipt['recipe']
        # Preserve explicit existing settings, including animation and region policy.
        recipe.setdefault('soft_tissue', policy['soft_tissue'])
        family_file = Path(u.Paths.project_saved_dir())/'VamRuntimeUpgrade/Policies'/('family-'+digest(receipt['family'])+'.json')
        family_file.parent.mkdir(parents=True, exist_ok=True)
        family_file.write_text(json.dumps(receipt['family']), encoding='utf8')
        recipe['family'] = str(family_file)
    else:
        recipe = dict(schema='vam-runtime-recipe/1', revision=policy['revision'],
                      definition=asset.get_path_name(), source_mapping=folder+'/DA_SourceMapping',
                      destination_root=policy['destination_root'],
                      family=str((policy_path.parent / policy['family']).resolve()),
                      skin_shading='source', soft_tissue=policy['soft_tissue'])
        idle = folder+'/Animations/A_VamIdle'
        if u.EditorAssetLibrary.does_asset_exist(idle):
            recipe['base_animation'] = idle
    validate_recipe(recipe)
    recipe_file = job/'runtime-recipe.json'
    report_file = job/'runtime-report.json'
    recipe_file.write_text(json.dumps(recipe, ensure_ascii=False, indent=2), encoding='utf8')
    editor = Path(u.Paths.engine_dir())/'Binaries/Win64/UnrealEditor-Cmd.exe'
    for phase, expected in [('build','saved_pending_reload'), ('reload','published_pending_verification'), ('verify','committed')]:
        check_cancel()
        progress('runtime-'+phase, 'Building character-owned soft tissue; native assets remain unchanged', False)
        env = dict(os.environ, VAM_RUNTIME_RECIPE=str(recipe_file), VAM_RUNTIME_REPORT=str(report_file), VAM_RUNTIME_PHASE=phase)
        result = subprocess.run([str(editor), u.Paths.get_project_file_path(), '-run=pythonscript',
            '-script='+str(SCRIPTS/'ue_runtime_build.py'), '-NullRHI', '-unattended', '-nosplash',
            '-abslog='+str(job/('runtime-'+phase+'.log'))], env=env, timeout=1200,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        require(result.returncode == 0, 'Runtime '+phase+' failed; inspect '+str(job/('runtime-'+phase+'.log')))
        report = json.loads(report_file.read_text(encoding='utf8'))
        require(report['status'] == expected, 'Runtime transaction did not reach '+expected)
    progress('complete', '已完成。请使用新人物：'+report['blueprint']+'；原 BP 和场景实例保持不变。', False)


if __name__ == '__main__':
    with exclusive_build(Path(u.Paths.project_saved_dir())/'VamRuntimeUpgrade'):
        run()
