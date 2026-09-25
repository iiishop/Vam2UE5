"""Upgrade orchestration: exact input selection, failure gates and policy preservation."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch


class Definition:
    def get_path_name(self):
        return '/Game/People/Independent/CD_Character.CD_Character'


class Blueprint:
    def generated_class(self):
        return object()


class Actor:
    def __init__(self, configuration=None):
        self.configuration = configuration

    def get_editor_property(self, key):
        if key == 'character': return self
        if key == 'runtime_configuration': return self.configuration
        if key == 'definition': return Definition()


class UpgradeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'Config').mkdir()
        (self.root/'Scripts').mkdir()
        policy = json.loads((Path(__file__).resolve().parents[1]/'Config/RuntimeImportPolicy.json').read_text())
        (self.root/'Config/RuntimeImportPolicy.json').write_text(json.dumps(policy))
        self.unreal = types.SimpleNamespace(Blueprint=Blueprint, VamCharacterActor=Actor,
            VamCharacterDefinition=Definition, load_asset=lambda _: Blueprint(), get_default_object=lambda _: Actor(),
            EditorAssetLibrary=types.SimpleNamespace(does_asset_exist=lambda _: False),
            Paths=types.SimpleNamespace(engine_dir=lambda: 'engine',get_project_file_path=lambda: 'project',project_saved_dir=lambda: str(self.root)))
        with patch.dict('sys.modules', unreal=self.unreal):
            spec = importlib.util.spec_from_file_location('upgrade_test_module', Path(__file__).with_name('ue_runtime_upgrade.py'))
            self.module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.module)
        self.module.SCRIPTS = self.root/'Scripts'
        self.module.progress = lambda *args: None
        self.module.check_cancel = lambda: None
        self.env = patch.dict(os.environ, VAM_BUILD_JOB=str(self.root), VAM_UPGRADE_ASSET='/Game/People/Independent/BP_VamCharacter')
        self.env.start()
        self.addCleanup(self.env.stop)

    def complete_phase(self, *args, **kwargs):
        env = kwargs['env']
        phase = env['VAM_RUNTIME_PHASE']
        state = {'build':'saved_pending_reload','reload':'published_pending_verification','verify':'committed'}[phase]
        Path(env['VAM_RUNTIME_REPORT']).write_text(json.dumps({'status':state,'blueprint':'/Game/VamRuntime/Test/BP_VamCharacter'}))
        return types.SimpleNamespace(returncode=0)

    def test_native_upgrade_uses_its_own_mapping_and_three_phases(self):
        with patch.object(self.module.subprocess, 'run', side_effect=self.complete_phase) as run:
            self.module.run()
        self.assertEqual([c.kwargs['env']['VAM_RUNTIME_PHASE'] for c in run.call_args_list], ['build','reload','verify'])
        recipe = json.loads((self.root/'runtime-recipe.json').read_text())
        self.assertEqual(recipe['source_mapping'], '/Game/People/Independent/DA_SourceMapping')
        self.assertNotIn('base_animation', recipe)
        self.assertTrue(recipe['soft_tissue']['regions'])

    def test_failed_build_never_reloads_or_publishes(self):
        with patch.object(self.module.subprocess, 'run', return_value=types.SimpleNamespace(returncode=1)) as run:
            with self.assertRaisesRegex(ValueError, 'Runtime build failed'): self.module.run()
        self.assertEqual(run.call_count, 1)

    def test_invalid_selection_launches_no_builder(self):
        self.unreal.load_asset = lambda _: object()
        with patch.object(self.module.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Select a saved'): self.module.run()
        run.assert_not_called()

    def test_existing_runtime_preserves_custom_recipe_and_family(self):
        recipe = {'schema':'vam-runtime-recipe/1','definition':Definition().get_path_name(),
                  'source_mapping':'/Game/Custom/DA_SourceMapping','destination_root':'/Game/CustomRuntime',
                  'family':'old-file.json','base_animation':'/Game/Custom/Idle','skin_shading':'subsurface'}
        family = {'family':'custom family'}
        config = types.SimpleNamespace(get_editor_property=lambda key: Definition() if key == 'definition' else json.dumps({'recipe':recipe,'family':family}))
        self.unreal.get_default_object = lambda _: Actor(config)
        with patch.object(self.module.subprocess, 'run', side_effect=self.complete_phase): self.module.run()
        result = json.loads((self.root/'runtime-recipe.json').read_text())
        self.assertEqual(result['base_animation'], recipe['base_animation'])
        self.assertEqual(result['destination_root'], recipe['destination_root'])
        self.assertEqual(json.loads(Path(result['family']).read_text()), family)


if __name__ == '__main__': unittest.main()
