"""Derived PNG reuse must preserve exact pixels and reject stale candidates."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

PLUGIN = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(PLUGIN / 'Scripts'), str(PLUGIN / 'Saved/Python')]
from PIL import Image
from vam_materials import Sources


class MaterialCacheTests(unittest.TestCase):
    def test_reuses_verified_pixels_and_rejects_changed_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'SourceAppearance'
            out.mkdir()
            plan = {'source_root': str(root), 'items': [], 'edges': []}
            source = Sources(plan, {}, out)
            raw = source.blob(b'locked Unity texture data', '.unity-texture')
            image = Image.new('RGB', (32, 32), (23, 87, 159))
            buffer = io.BytesIO()
            image.save(buffer, format='PNG')
            old = source.blob(buffer.getvalue(), '.png')
            asset = {**old, 'source_raw': raw, 'encoding': 'unity_decoded_pixels'}
            (out / ('a' * 64 + '.materials.json')).write_text(
                json.dumps({'materials': [{'textures': {'_MainTex': {'asset': asset}}}]}), encoding='utf8')

            again = Sources(plan, {}, out)
            self.assertEqual(again.png(raw, image, 'unity_decoded_pixels'), old)
            changed = Image.new('RGB', (32, 32), (24, 87, 159))
            replacement = again.png(raw, changed, 'unity_decoded_pixels')
            self.assertNotEqual(replacement['sha256'], old['sha256'])
            with Image.open(replacement['file']) as decoded:
                self.assertEqual(decoded.tobytes(), changed.tobytes())

    def test_rejects_tampered_cached_png(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'SourceAppearance'
            out.mkdir()
            source = Sources({'source_root': str(root), 'items': [], 'edges': []}, {}, out)
            raw = source.blob(b'raw', '.unity-texture')
            image = Image.new('RGBA', (8, 8), (1, 2, 3, 4))
            expected = hashlib.sha256(b'bad').hexdigest()
            path = out / 'blobs' / (expected + '.png')
            path.write_bytes(b'bad')
            (out / ('b' * 64 + '.materials.json')).write_text(json.dumps({'materials': [
                {'textures': {'_MainTex': {'asset': {'source_raw': raw,
                    'sha256': expected, 'file': str(path), 'encoding': 'unity_decoded_pixels'}}}}]}), encoding='utf8')
            result = source.png(raw, image, 'unity_decoded_pixels')
            self.assertNotEqual(result['sha256'], expected)
            with Image.open(result['file']) as decoded:
                self.assertEqual(decoded.tobytes(), image.tobytes())


if __name__ == '__main__':
    unittest.main()
