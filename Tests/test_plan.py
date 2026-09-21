import importlib.util
import json
from pathlib import Path
import tempfile
import time
import unittest
import threading
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).parents[1] / 'Scripts'


def module(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


index, plan = module('vam_index'), module('vam_plan')
APPEARANCE = 'Custom/Atom/Person/Appearance/Preset_Test.vap'
TEXTURE = 'Custom/Textures/test.png'


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.root = self.base / 'VaM'
        (self.root / 'AddonPackages').mkdir(parents=True)
        self.catalog = index.Catalog(self.base / 'Index')
        self.catalog.settings.update(root=str(self.root), auto=False)

    def tearDown(self): self.tmp.cleanup()

    def package(self, name, files, dependencies=None, subdir=''):
        target = self.root / 'AddonPackages' / subdir / (name + '.var')
        target.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('meta.json', json.dumps({'creatorName': name.split('.')[0], 'dependencies': dependencies or {}}))
            for path, value in files.items():
                z.writestr(path, value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False))
        return target

    def loose(self, path, obj):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(obj if isinstance(obj, bytes) else json.dumps(obj, ensure_ascii=False).encode('utf-8'))
        return target

    def scan(self):
        self.catalog.start_scan()
        while self.catalog.state['running']: time.sleep(.01)
        self.assertFalse(self.catalog.state['error'])

    def selected(self, package='', path=APPEARANCE):
        with self.catalog.connect() as db:
            return db.execute('SELECT id FROM assets WHERE package=? AND path=?', (package, path)).fetchone()[0]

    def generate(self, package='', path=APPEARANCE, **kwargs):
        return plan.Planner(self.catalog, **kwargs).generate([self.selected(package, path)])

    def test_cross_package_self_relative_and_preservation(self):
        config = {'texture': 'B.Textures.2:/Custom/Textures/test.png',
                  'include': 'SELF:/Custom/Atom/Person/Appearance/Parts/Child.vap',
                  'mystery': {'exactString': '0.1759808', 'unknownList': [True, None, 12], 'hair': 'future-field'}}
        self.package('A.Character.1', {APPEARANCE: config,
                     'Custom/Atom/Person/Appearance/Parts/Child.vap': {'texture': '../../../../Textures/local.png'},
                     'Custom/Textures/local.png': b'local texture'})
        self.package('B.Textures.2', {TEXTURE: b'texture bytes'})
        self.scan()
        p = self.generate('A.Character.1')
        self.assertEqual(p['status'], 'ready', p['items'])
        self.assertEqual(p['counts']['create'], 4)
        root = p['documents'][p['roots'][0]]
        self.assertEqual(root['parameters'], config)
        self.assertTrue(any(e['reference'].startswith('SELF:') for e in p['edges']))
        self.assertEqual(p['version_locks'][0]['resolved'], 'B.Textures.2')

    def test_latest_min_numeric_order_and_locked_replay_after_new_version(self):
        self.loose(APPEARANCE, {'texture': 'B.Textures.latest:/' + TEXTURE,
                                'include': 'B.Textures.min3:/Custom/Atom/Person/Appearance/Extra.vap'})
        for version in (2, 3, 10):
            self.package('B.Textures.' + str(version), {TEXTURE: str(version).encode(),
                         'Custom/Atom/Person/Appearance/Extra.vap': {}})
        self.scan()
        original = self.generate()
        self.assertEqual(original['status'], 'ready')
        self.assertEqual({v['resolved'] for v in original['version_locks']}, {'B.Textures.10'})
        self.package('B.Textures.11', {TEXTURE: b'11', 'Custom/Atom/Person/Appearance/Extra.vap': {}})
        self.scan()
        replay = self.generate(locked=original)
        self.assertEqual(replay['plan_id'], original['plan_id'])
        fresh = self.generate()
        self.assertNotEqual(fresh['plan_id'], original['plan_id'])
        self.assertEqual({v['resolved'] for v in fresh['version_locks']}, {'B.Textures.11'})

    def test_exact_version_missing_contains_origin_field_and_target(self):
        target = 'B.Textures.9:/' + TEXTURE
        self.loose(APPEARANCE, {'texture': target})
        self.package('B.Textures.10', {TEXTURE: b'other version'})
        self.scan()
        p = self.generate()
        self.assertEqual(p['status'], 'blocked')
        missing = next(i for i in p['items'] if i['action'] == 'missing')
        self.assertEqual(missing['code'], 'version_missing')
        self.assertEqual(missing['references'][0]['source_location'], APPEARANCE)
        self.assertEqual(missing['references'][0]['field'], '/texture')
        self.assertEqual(missing['references'][0]['reference'], target)

    def test_cycle_and_repeated_reference_do_not_duplicate_resources(self):
        b = 'Custom/Atom/Person/Appearance/B.vap'
        self.package('A.Cycle.1', {APPEARANCE: {'includes': ['B.vap', 'B.vap']}, b: {'include': 'Preset_Test.vap'}})
        self.scan()
        p = self.generate('A.Cycle.1')
        self.assertEqual(p['status'], 'ready')
        self.assertEqual(len(p['items']), 2)
        self.assertEqual(len(p['cycles']), 1)
        self.assertEqual(len(p['edges']), 3)

    def test_declared_unused_missing_dependency_not_traversed(self):
        self.package('A.Used.1', {APPEARANCE: {'texture': 'B.Used.1:/' + TEXTURE}},
                     {'B.Used.1': {}, 'C.Missing.latest': {'dependencies': {'D.AlsoMissing.1': {}}}})
        self.package('B.Used.1', {TEXTURE: b'ok'})
        self.scan()
        p = self.generate('A.Used.1')
        self.assertEqual(p['status'], 'ready')
        declared = {d['requested']: d for d in p['declared_dependencies']}
        self.assertTrue(declared['B.Used.1']['used'])
        self.assertFalse(declared['C.Missing.latest']['used'])
        self.assertFalse(declared['D.AlsoMissing.1']['blocking'])
        self.assertEqual(len(p['items']), 2)

    def test_same_name_different_content_and_identical_payload_reuse(self):
        self.loose(APPEARANCE, {'includes': ['A.One.1:/' + TEXTURE, 'B.Two.1:/' + TEXTURE, 'C.Same.1:/' + TEXTURE]})
        self.package('A.One.1', {TEXTURE: b'first'})
        self.package('B.Two.1', {TEXTURE: b'different'})
        self.package('C.Same.1', {TEXTURE: b'first'})
        self.scan()
        p = self.generate()
        textures = [i for i in p['items'] if i['path'] == TEXTURE]
        self.assertEqual(len(textures), 3)
        self.assertEqual(len({i['sha256'] for i in textures}), 2)
        self.assertEqual(sum(i['action'] == 'reuse' for i in textures), 1)

    def test_import_state_reuse_update_and_no_implicit_import(self):
        p = self.loose(APPEARANCE, {'future': 'preserved'})
        self.scan()
        first = self.generate()
        item = first['items'][0]
        baseline = {'schema': 1, 'assets': {item['id']: {'sha256': item['sha256'], 'destination': '/Game/Test'}}}
        self.assertEqual(self.generate(baseline=baseline)['counts']['reuse'], 1)
        p.write_text('{"future":"changed"}')
        self.assertEqual(self.generate(baseline=baseline)['counts']['update'], 1)
        self.assertEqual(self.generate()['counts']['create'], 1)
        self.assertFalse((self.catalog.data / 'ImportState').exists())

    def test_corrupt_config_never_success_and_raw_bytes_retained(self):
        for broken in (b'{"storables":', b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":1e999}', b'{"storables":"bad"}', b'{"storables":["bad"]}'):
            self.loose(APPEARANCE, broken)
            self.scan()
            resolver = plan.Planner(self.catalog)
            p = resolver.generate([self.selected()])
            self.assertEqual(p['status'], 'blocked')
            self.assertTrue(p['counts']['unsupported'])
            self.assertIn(broken, resolver.snapshots.values())

    def test_deterministic_bytes_and_selection_order(self):
        self.loose(APPEARANCE, {'include': 'Other.vap', 'extra': ['untouched', 7]})
        self.loose('Custom/Atom/Person/Appearance/Other.vap', {})
        self.scan()
        a = self.selected()
        b = self.selected(path='Custom/Atom/Person/Appearance/Other.vap')
        first = plan.Planner(self.catalog).generate([a, b])
        second = plan.Planner(self.catalog).generate([b, a, a])
        self.assertEqual(plan.canonical(first), plan.canonical(second))

    def test_builtin_disabled_and_zero_morph_handling(self):
        self.loose(APPEARANCE, {'storables': [{'id': 'geometry', 'character': 'Unknown Test Character',
                     'clothing': [{'id': 'Missing.Package.1:/Custom/Clothing/No.vam', 'enabled': 'false'}],
                     'morphs': [{'uid': 'NoSuchMorph.vmi', 'value': '0'}]}]})
        self.scan()
        p = self.generate()
        self.assertEqual(p['counts']['missing'], 0)
        self.assertEqual(len(p['inactive_references']), 2)
        self.assertTrue(any(i.get('code') == 'builtin_adapter_required' for i in p['items']))

    def builtin_fixture(self):
        payload = b'verified builtin bundle metadata fixture'
        self.loose('VaM_Data/StreamingAssets/test', payload)
        entries = []
        for gender in ('female', 'male'):
            for role, name in [('character', gender.title()), ('morph', 'Same Name'), ('hair', 'No Hair')]:
                locator = {'file': 'test', 'object': gender + '/' + role}
                entries.append({'role': role, 'names': [name], 'gender': gender,
                    'operation': 'clear_hair' if role == 'hair' else 'convert_builtin',
                    'files': ['test'], 'locator': locator,
                    'evidence': {'file': 'test', 'object': gender + '/' + role}})
        return {'schema': 1, 'entries': entries, 'files': {'test': {
            'path': 'VaM_Data/StreamingAssets/test', 'size': len(payload), 'sha256': plan.sha(payload)}}}

    def builtin_plan(self, catalog, **kwargs):
        worker = plan.Planner(self.catalog, **kwargs)
        worker.builtin_catalog = catalog
        return worker.generate([self.selected()])

    def test_builtin_mapping_gender_repeat_replay_and_baseline(self):
        mapping = self.builtin_fixture()
        self.loose(APPEARANCE, {'storables': [{'id': 'geometry', 'character': 'Female',
            'morphs': [{'uid': 'Same Name', 'value': 1}], 'hair': [{'id': 'No Hair'}],
            'morphsOtherGender': [{'uid': 'Same Name', 'value': 1}]}]})
        self.scan()
        first = self.builtin_plan(mapping)
        self.assertEqual(first['status'], 'ready', first['items'])
        self.assertEqual(first['plan_id'], self.builtin_plan(mapping)['plan_id'])
        self.assertEqual(first['plan_id'], self.builtin_plan(mapping, locked=first)['plan_id'])
        morphs = [i for i in first['items'] if i['resource_kind'] == 'morph']
        self.assertEqual({i['builtin_mapping']['entry']['gender'] for i in morphs}, {'male', 'female'})
        self.assertNotEqual(morphs[0]['id'], morphs[1]['id'])
        baseline = {'schema': 1, 'assets': {morphs[0]['id']: {'sha256': morphs[0]['sha256']}}}
        self.assertEqual(self.builtin_plan(mapping, baseline=baseline)['counts']['reuse'], 1)
        changed = json.loads(json.dumps(mapping))
        changed['entries'][0]['operation'] = 'changed_conversion'
        replay = self.builtin_plan(changed, locked=first)
        self.assertEqual(replay['status'], 'blocked')
        self.assertTrue(any(i.get('code') == 'locked_content_changed' for i in replay['items']))

    def test_builtin_missing_changed_and_unknown_have_origin(self):
        mapping = self.builtin_fixture()
        self.loose(APPEARANCE, {'character': 'Female'})
        self.scan()
        target = self.root / 'VaM_Data/StreamingAssets/test'
        target.write_bytes(b'changed')
        result = self.builtin_plan(mapping)
        self.assertEqual(result['status'], 'blocked')
        self.assertTrue(any(i.get('code') == 'builtin_catalog_stale' and i['references'] for i in result['items']))
        target.unlink()
        result = self.builtin_plan(mapping)
        missing = next(i for i in result['items'] if i['action'] == 'missing')
        self.assertIn('VaM_Data/StreamingAssets/test', missing['reason'])
        self.assertEqual(missing['references'][0]['reference'], 'Female')
        self.assertEqual(missing['references'][0]['field'], '/character')

    def test_builtin_unknown_gender_and_wrong_type_do_not_guess(self):
        mapping = self.builtin_fixture()
        self.loose(APPEARANCE, {'storables': [{'id': 'geometry',
            'morphs': [{'uid': 'Same Name', 'value': 1}], 'clothing': [{'id': 'No Hair'}]}]})
        self.scan()
        result = self.builtin_plan(mapping)
        self.assertEqual(result['status'], 'blocked')
        codes = {i.get('code') for i in result['items']}
        self.assertIn('builtin_ambiguous', codes)
        self.assertIn('builtin_adapter_required', codes)

    def test_builtin_corrupted_catalog_cannot_succeed(self):
        self.loose(APPEARANCE, {'character': 'Female'})
        self.scan()
        result = self.builtin_plan({'schema': 1, 'entries': 'invalid'})
        self.assertEqual(result['status'], 'blocked')

    def test_companions_and_vaj_relative_texture(self):
        clothing = 'Custom/Clothing/Female/A/Dress.vam'
        self.package('A.Dress.1', {APPEARANCE: {'storables': [{'id': 'geometry', 'clothing': [{'id': 'SELF:/' + clothing}]}]},
                     clothing: {'displayName': 'Dress'}, clothing[:-4] + '.vaj': {'texture': 'fabric.png'},
                     clothing[:-4] + '.vab': b'binary geometry only hashed',
                     'Custom/Clothing/Female/A/fabric.png': b'image bytes'})
        self.scan()
        p = self.generate('A.Dress.1')
        self.assertEqual(p['status'], 'ready', p['items'])
        self.assertEqual(p['counts']['create'], 5)

    def test_locked_content_change_rejected(self):
        self.loose(APPEARANCE, {'texture': 'B.Test.latest:/' + TEXTURE})
        self.package('B.Test.1', {TEXTURE: b'original'})
        self.scan()
        original = self.generate()
        self.package('B.Test.1', {TEXTURE: b'repacked same version different content'})
        self.scan()
        replay = self.generate(locked=original)
        self.assertEqual(replay['status'], 'blocked')
        self.assertTrue(any(i.get('code') == 'locked_content_changed' for i in replay['items']))

    def test_duplicate_package_identity_is_ambiguous(self):
        self.loose(APPEARANCE, {'texture': 'B.Test.1:/' + TEXTURE})
        self.package('B.Test.1', {TEXTURE: b'one'})
        self.package('B.Test.1', {TEXTURE: b'two'}, subdir='Other')
        self.scan()
        p = self.generate()
        self.assertTrue(any(i.get('code') == 'ambiguous_package' for i in p['items']))

    def test_scripts_unknown_fields_and_escaping_are_unsupported(self):
        self.loose(APPEARANCE, {'file': '../../../../../../escape.png',
                               'texture': 'https://example.com/never-fetch.png',
                               'unknownExtension': 'Custom/Textures/mystery.png',
                               'storables': [{'plugins': {'plugin#0': 'A.Script.1:/Custom/Scripts/test.cs'}}]})
        self.package('A.Script.1', {'Custom/Scripts/test.cs': b'never execute'})
        self.scan()
        p = self.generate()
        self.assertEqual(p['status'], 'blocked')
        codes = {i.get('code') for i in p['items']}
        self.assertIn('format_unsupported', codes)
        self.assertIn('external_url', codes)
        self.assertIn('uninterpreted_reference', codes)

    def test_service_persists_plan_and_exact_raw_objects(self):
        original = b'{ "unused": "exact whitespace and unknown fields" }\r\n'
        self.loose(APPEARANCE, original)
        self.scan()
        service = plan.PlanService(self.catalog)
        service.start([self.selected()])
        while service.status()['running']: time.sleep(.01)
        status = service.status()
        self.assertEqual(status['status'], 'ready', status)
        saved = service.read_plan(status['plan_id'])
        h = saved['documents'][saved['roots'][0]]['raw_sha256']
        self.assertEqual((service.directory / 'Objects' / h).read_bytes(), original)
        self.assertEqual(service.result(status['plan_id'], 'create')['total'], 1)
        self.assertFalse((self.catalog.data / 'ImportState').exists())

    def test_declared_missing_but_referenced_is_used_and_blocking_item(self):
        self.package('A.Test.1', {APPEARANCE: {'texture': 'B.Missing.2:/' + TEXTURE}}, {'B.Missing.latest': {}})
        self.scan()
        p = self.generate('A.Test.1')
        self.assertEqual(p['status'], 'blocked')
        self.assertTrue(p['declared_dependencies'][0]['used'])
        self.assertEqual(p['declared_dependencies'][0]['references'][0]['field'], '/texture')

    def test_duplicate_missing_target_keeps_both_references(self):
        self.loose(APPEARANCE, {'includes': ['B.Missing.1:/' + TEXTURE, 'B.Missing.1:/' + TEXTURE]})
        self.scan()
        p = self.generate()
        missing = [i for i in p['items'] if i['action'] == 'missing']
        self.assertEqual(len(missing), 1)
        self.assertEqual(len(missing[0]['references']), 2)

    def test_morph_group_paths_are_metadata_not_resource_references(self):
        morph = 'Custom/Atom/Person/Morphs/female/A/Test.vmi'
        self.loose(morph, {'group': 'Morphs/Morph Loader', 'region': 'Pose Controls/Head/Expressions', 'numDeltas': '0'})
        self.scan()
        p = self.generate(path=morph)
        self.assertEqual(p['status'], 'ready', p['items'])
        self.assertEqual(len(p['edges']), 0)

    def test_cancel_and_changed_source_never_ready(self):
        filename = self.loose(APPEARANCE, {})
        self.scan()
        cancel = threading.Event()
        cancel.set()
        self.assertEqual(self.generate(cancel=cancel)['status'], 'cancelled')
        planner = plan.Planner(self.catalog)
        original = planner.read
        def change_after_read(*args):
            result = original(*args)
            filename.write_text('{"changed":true}')
            return result
        with patch.object(planner, 'read', side_effect=change_after_read):
            p = planner.generate([self.selected()])
        self.assertEqual(p['status'], 'blocked')
        self.assertTrue(any(i.get('code') == 'source_changed' for i in p['items']))

    def test_corrupt_package_metadata_is_blocked_and_preserved(self):
        path = self.root / 'AddonPackages/A.BadMeta.1.var'
        broken = b'{broken metadata'
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('meta.json', broken)
            z.writestr(APPEARANCE, b'{}')
        self.scan()
        resolver = plan.Planner(self.catalog)
        p = resolver.generate([self.selected('A.BadMeta.1')])
        self.assertEqual(p['status'], 'blocked')
        self.assertTrue(p['counts']['unsupported'])
        self.assertIn(broken, resolver.snapshots.values())

    def test_locked_dependency_availability_snapshot_is_stable(self):
        self.package('A.Test.1', {APPEARANCE: {}}, {'B.Unused.latest': {}})
        self.scan()
        first = self.generate('A.Test.1')
        self.package('B.Unused.1', {TEXTURE: b'new but unused'})
        self.scan()
        replay = self.generate('A.Test.1', locked=first)
        self.assertEqual(first['plan_id'], replay['plan_id'])


if __name__ == '__main__': unittest.main(verbosity=2)
