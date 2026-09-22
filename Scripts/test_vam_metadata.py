import unittest
from vam_plan import resource_json, strict_json

class MetadataTests(unittest.TestCase):
    def test_observed_credit_and_raw_retention(self):
        tail='Thanks:"Example Author"+"hair_version: example.1"\r\n'
        obj,warnings=resource_json(('{"itemType":"HairFemale","displayName":"Example"}'+tail).encode(),'hair.vam')
        self.assertEqual(obj['displayName'],'Example')
        self.assertEqual(warnings[0]['raw_trailer'],tail)

    def test_strict_formats_remain_strict(self):
        raw=b'{"itemType":"HairFemale"}Thanks:"Author"+"hair_version: test.1"'
        for path in ('x.vap','x.vaj','x.json'):
            with self.assertRaises(ValueError):resource_json(raw,path)
        with self.assertRaises(ValueError):strict_json(raw)

    def test_reject_unknown_or_corrupt(self):
        for raw in (b'{"itemType":"HairFemale"}{}',b'{"itemType":"HairFemale"}garbage',
                    b'{"itemType":"HairFemale"}Thanks:"a"+"hair_version: b"{}',
                    b'{"itemType":"HairFemale","x":1,"x":2}Thanks:"a"+"hair_version: b"',
                    b'{"itemType":"HairFemale","x":NaN}Thanks:"a"+"hair_version: b"',
                    b'{"itemType":"HairFemale"',b'{"itemType":"ClothingFemale"}Thanks:"a"+"hair_version: b"'):
            with self.subTest(raw=raw),self.assertRaises(ValueError):resource_json(raw,'x.vam')

    def test_normal_json_unchanged(self):
        self.assertEqual(resource_json(b'{"x":1}','x.vam'),({'x':1},[]))

    def test_hair_companion_only(self):
        raw=b'{"components":[],"storables":[]}\r\nThanks:"a"+"hair_version: b"'
        self.assertEqual(len(resource_json(raw,'Custom/Hair/Female/a.vaj')[1]),1)
        with self.assertRaises(ValueError):resource_json(raw,'Custom/Clothing/Female/a.vaj')

if __name__=='__main__':unittest.main()
