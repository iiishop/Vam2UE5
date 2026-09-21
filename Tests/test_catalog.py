import importlib.util
import base64
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile
import urllib.request
import urllib.error
import urllib.parse

spec = importlib.util.spec_from_file_location('vam_index', Path(__file__).parents[1] / 'Scripts/vam_index.py')
vam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vam)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base / 'VaM'
        (self.root / 'AddonPackages').mkdir(parents=True)
        self.cat = vam.Catalog(self.base / 'Index')
        self.cat.settings.update(root=str(self.root), auto=False)

    def tearDown(self):
        self.temp.cleanup()

    def loose(self, path, value):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
        return p

    def package(self, name, contents):
        p = self.root / 'AddonPackages' / name
        with zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('meta.json', json.dumps({'creatorName': '作者', 'tags': ['测试']}))
            for path, value in contents.items():
                z.writestr(path, value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False))
        return p

    def scan(self):
        self.cat.start_scan()
        deadline = time.monotonic() + 10
        while self.cat.state['running'] and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertFalse(self.cat.state['running'])
        self.assertFalse(self.cat.state['error'])

    def test_same_names_distinct_and_defaults_and_details_lazy(self):
        path = 'Custom/Atom/Person/Appearance/Preset_同名.vap'
        self.loose(path, {'storables': [{'id': 'geometry'}]})
        self.package('A.Character.1.var', {path: b'bad json'})
        self.package('A.Character.2.var', {path: {}})
        self.scan()
        all_rows = self.cat.query({'kind': 'appearance'})
        self.assertEqual(all_rows['total'], 3)
        self.assertEqual(len({r['id'] for r in all_rows['items']}), 3)
        self.assertEqual(self.cat.query({'kind': 'appearance', 'source': 'user'})['total'], 1)
        self.assertEqual(self.cat.status()['diagnostics'], 0)  # No preset bodies parsed during scan.
        bad = next(r for r in all_rows['items'] if r['package'] == 'A.Character.1')
        self.assertTrue(self.cat.detail(bad['id'])['diagnostic'])
        self.assertEqual(self.cat.status()['diagnostics'], 1)

    def test_incremental_modify_delete_and_restart(self):
        path = 'Custom/Clothing/Female/Creator/Item.vam'
        p = self.loose(path, {'displayName': 'Original', 'tags': 'cotton'})
        self.scan()
        first = self.cat.query({'kind': 'clothing'})['items'][0]
        with patch.object(self.cat, 'add_asset', side_effect=AssertionError('Unchanged source reread')):
            self.scan()
        p.write_text('{"displayName":"Modified name", "tags":"silk"}')
        self.scan()
        second = self.cat.query({'kind': 'clothing', 'tag': 'silk'})['items'][0]
        self.assertEqual(first['id'], second['id'])
        self.assertEqual(second['name'], 'Modified name')
        reopened = vam.Catalog(self.base / 'Index')
        self.assertEqual(reopened.query({'kind': 'clothing'})['total'], 1)
        p.unlink()
        self.scan()
        self.assertEqual(self.cat.query({'kind': 'clothing'})['total'], 0)

    def test_corrupt_packages_and_unsafe_paths_do_not_stop_scan(self):
        (self.root / 'AddonPackages/broken.var').write_bytes(b'not zip')
        self.package('Good.Hair.1.var', {'Custom/Hair/Female/A/Hair.vam': {'displayName': 'Hair'},
                                        '../escape.vam': {}})
        self.scan()
        self.assertEqual(self.cat.query({'kind': 'hair'})['total'], 1)
        self.assertEqual(self.cat.status()['diagnostics'], 2)
        self.assertFalse((self.base / 'escape.vam').exists())

    def test_cancel_keeps_previous_source_transaction(self):
        p = self.loose('Custom/Hair/Female/A/Hair.vam', {'displayName': 'Old'})
        self.scan()
        p.write_text('{"displayName":"New value"}')
        original = self.cat.add_asset
        def cancel_inside(*args, **kwargs):
            original(*args, **kwargs)
            self.cat.cancel.set()
        with patch.object(self.cat, 'add_asset', side_effect=cancel_inside):
            self.scan()
        self.assertTrue(self.cat.state['cancelled'])
        self.assertEqual(self.cat.query({'kind': 'hair'})['items'][0]['name'], 'Old')

    def test_failed_inventory_never_reconciles_deletions(self):
        self.loose('Custom/Hair/Female/A/Hair.vam', {})
        self.scan()
        with patch.object(self.cat, 'inventory', side_effect=PermissionError('unreadable')):
            self.cat.start_scan()
            while self.cat.state['running']: time.sleep(.01)
        self.assertTrue(self.cat.state['error'])
        self.assertEqual(self.cat.query({'kind': 'hair'})['total'], 1)

    def test_metadata_read_cap_and_no_geometry_read(self):
        contents = {'Custom/Clothing/Female/A/Dress.vam': {'displayName': 'Dress'},
                    'Custom/Clothing/Female/A/Dress.vab': b'geometry must never be read',
                    'Custom/Clothing/Female/A/Dress.vaj': b'geometry metadata must never be read',
                    'Custom/Atom/Person/Morphs/female/A/Huge.vmi': b'x' * (vam.META_LIMIT+1)}
        self.package('A.Test.1.var', contents)
        original = self.cat.read_zip
        def guarded(z, path, limit):
            self.assertNotIn(Path(path).suffix, ('.vab', '.vaj', '.vmb'))
            return original(z, path, limit)
        with patch.object(self.cat, 'read_zip', side_effect=guarded): self.scan()
        self.assertEqual(self.cat.query({'kind': 'clothing'})['total'], 1)
        self.assertTrue(self.cat.query({'kind': 'morph'})['items'][0]['diagnostic'])

    def test_pagination_unicode_search_and_literal_wildcards(self):
        self.package('A.Many.1.var', {f'Custom/Hair/Female/A/{i:03}.vam': {'displayName': f'头发 {i:03}'} for i in range(103)})
        self.scan()
        first = self.cat.query({'kind': 'hair'})
        self.assertEqual(len(first['items']), vam.PAGE_SIZE)
        last = self.cat.query({'kind': 'hair', 'page': 999})
        self.assertEqual(last['page'], 2)
        self.assertEqual(len(last['items']), 7)
        self.assertEqual(self.cat.query({'kind': 'hair', 'search': '头发 001'})['total'], 1)
        self.assertEqual(self.cat.query({'kind': 'hair', 'search': '%'})['total'], 0)

    def test_image_and_path_limits(self):
        with self.assertRaises(ValueError): vam.validate_image(b'not an image')
        with self.assertRaises(ValueError): vam.safe_member('C:/escape.png')
        with self.assertRaises(ValueError): vam.safe_member('../escape.png')
        with self.assertRaises(ValueError): self.cat.configure(str(self.base))

    def test_legacy_presets_are_not_all_appearances(self):
        self.assertEqual(vam.classify('Saves/Person/pose/A.json'), 'pose')
        self.assertEqual(vam.classify('Saves/Person/appearance/A.json'), 'appearance')
        self.assertEqual(vam.classify('Custom/Clothing/Female/A/Preset_Red.vap'), 'clothing_item_preset')
        self.assertEqual(vam.classify('Custom/Hair/Female/A/Preset_Black.vap'), 'hair_item_preset')

    def test_unchanged_scan_does_not_invalidate_view(self):
        self.loose('Custom/Hair/Female/A/Hair.vam', {})
        self.scan()
        revision = self.cat.state['revision']
        self.scan()
        self.assertEqual(revision, self.cat.state['revision'])

    def test_thumbnail_is_lazy_cached_and_source_unchanged(self):
        png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wl2nWQAAAAASUVORK5CYII=')
        package = self.package('A.Thumb.1.var', {'Custom/Hair/Female/A/Hair.vam': {},
                                               'Custom/Hair/Female/A/Hair.png': png})
        original_hash = hashlib.sha256(package.read_bytes()).hexdigest()
        self.scan()
        self.assertEqual(list(self.cat.cache.iterdir()), [])
        row = self.cat.query({'kind': 'hair'})['items'][0]
        self.assertTrue(row['thumb'])
        data, ext = self.cat.thumbnail(row['id'])
        self.assertEqual((data, ext), (png, '.png'))
        with patch.object(self.cat, 'read_asset', side_effect=AssertionError('Cache not used')):
            self.cat.thumbnail(row['id'])
        self.assertEqual(hashlib.sha256(package.read_bytes()).hexdigest(), original_hash)
        self.assertEqual(len(list(self.cat.cache.iterdir())), 1)

    def test_thumbnail_cache_count_and_byte_caps(self):
        for i in range(10): (self.cat.cache / str(i)).write_bytes(b'1234567890')
        with patch.object(vam, 'CACHE_FILES', 3), patch.object(vam, 'CACHE_BYTES', 25):
            self.cat.trim_cache()
        self.assertLessEqual(len(list(self.cat.cache.iterdir())), 2)

    def test_corruption_retains_but_flags_last_good_assets(self):
        p = self.package('A.Good.1.var', {'Custom/Hair/Female/A/Hair.vam': {}})
        self.scan()
        p.write_bytes(b'broken now')
        self.scan()
        result = self.cat.query({'kind': 'hair'})
        self.assertEqual(result['total'], 1)
        self.assertIn('上次索引', result['items'][0]['diagnostic'])

    def test_http_service_auto_detects_add_delete_and_checks_session(self):
        p = self.loose('Custom/Hair/Female/A/First.vam', {})
        self.cat.settings['auto'] = True
        self.cat.settings['interval'] = 1
        vam.atomic_json(self.cat.data / 'settings.json', self.cat.settings)
        service = subprocess.Popen([sys.executable, '-I', str(Path(vam.__file__)), '--serve', '--data', str(self.cat.data)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 12
            ready = self.cat.data / 'ready.json'
            while not ready.exists() and time.monotonic() < deadline: time.sleep(.05)
            self.assertTrue(ready.exists())
            url = json.loads(ready.read_text('utf-8'))['url']
            parts = urllib.parse.urlsplit(url)
            origin = parts.scheme + '://' + parts.netloc
            with self.assertRaises(urllib.error.HTTPError) as forbidden:
                urllib.request.urlopen(origin + '/wrong/api/state')
            self.assertEqual(forbidden.exception.code, 403)
            with self.assertRaises(urllib.error.HTTPError) as cross_origin:
                urllib.request.urlopen(urllib.request.Request(url + 'api/cancel', data=b'{}', headers={'Origin': 'https://example.com'}))
            self.assertEqual(cross_origin.exception.code, 403)
            def wait_count(count):
                limit = time.monotonic() + 12
                while time.monotonic() < limit:
                    with urllib.request.urlopen(url + 'api/query?kind=hair', timeout=2) as r:
                        result = json.load(r)
                    if result['total'] == count: return
                    time.sleep(.1)
                self.fail(f'Automatic watcher did not reach {count} resources')
            wait_count(1)
            self.loose('Custom/Hair/Female/A/Second.vam', {})
            wait_count(2)
            p.unlink()
            wait_count(1)
        finally:
            service.terminate()
            service.wait(timeout=5)


if __name__ == '__main__': unittest.main(verbosity=2)
