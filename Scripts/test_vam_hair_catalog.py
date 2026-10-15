"""The reusable source resource must never depend on its character or composition."""
import copy
import unittest
from vam_hair_catalog import catalog


def fixture():
    return {'records': [{'path': 'pack:/Hair/strand.vam', 'data': {
        'source_path': 'C:/source/strand.vam', 'vam': {'uid': 'author:strand'},
        'vaj': {'storables': []}, 'dynamic': {'hair': {'scalp': 'CustomScalp',
        'vertices': [[0, 0, 0], [0, 1, 0]]}}, 'meshes': [], 'wraps': []}}],
        'plan_documents': {'preset': {'parameters': {'storables': []}}}}


class HairCatalog(unittest.TestCase):
    def test_character_independence(self):
        a = fixture(); b = copy.deepcopy(a)
        b['body_contract'] = 'a different character'
        b['records'].append({'class': 'DAZMergedMesh', 'mesh': {'vertices': [[9, 8, 7]]}})
        self.assertEqual(catalog(a), catalog(b))

    def test_override_changes_style_not_shared_hair(self):
        a = fixture(); b = copy.deepcopy(a)
        b['plan_documents']['preset']['parameters']['storables'] = [
            {'id': 'author:strandSim', 'length1': '.8'}]
        x, y = catalog(a), catalog(b)
        self.assertEqual(x['assets'], y['assets'])
        self.assertNotEqual(x['hairstyle']['identity'], y['hairstyle']['identity'])

    def test_same_resource_can_be_referenced_twice(self):
        a = fixture(); a['records'].append(copy.deepcopy(a['records'][0]))
        result = catalog(a)
        self.assertEqual(len(result['assets']), 1)
        self.assertEqual(len(result['hairstyle']['entries']), 2)

    def test_geometry_edit_invalidates_resource(self):
        a = fixture(); b = copy.deepcopy(a)
        b['records'][0]['data']['dynamic']['hair']['vertices'][1][1] = 2
        self.assertNotEqual(catalog(a)['assets'][0]['identity'], catalog(b)['assets'][0]['identity'])

    def test_machine_provenance_not_hair_identity(self):
        a = fixture(); b = copy.deepcopy(a)
        b['records'][0]['data']['source_path'] = 'D:/other/strand.vam'
        self.assertEqual(catalog(a)['assets'][0]['identity'], catalog(b)['assets'][0]['identity'])


if __name__ == '__main__':
    unittest.main()
