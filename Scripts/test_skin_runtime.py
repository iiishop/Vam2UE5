import copy
import unittest
from vam_skin_runtime import runtime_skin_nodes


class RuntimeBulgeTests(unittest.TestCase):
    def fixture(self):
        row = dict(vertex=1, xweight=.4, yweight=0, zweight=.2)
        row.update({a + s + 'bulge': .25 for a in 'xyz' for s in ('left', 'right')})
        skin = dict(dazMesh={'m_PathID': 7}, nodes=[dict(name='thigh', weights=[row])])
        ir = dict(skeleton=[dict(name='thigh', position=[2, 0, 0], base_position=[0, 0, 0])],
                  records=[dict(kind='unity_mesh', object='7', mesh=dict(
                      vertices=[[.1, 0, 0]], uv=[0, 0], uv_map=[dict(fromvert=0, tovert=1)]))])
        return ir, skin

    def test_metres_alias_and_original_reference(self):
        ir, skin = self.fixture()
        before = copy.deepcopy(skin)
        result = runtime_skin_nodes(ir, skin)
        self.assertAlmostEqual(result[0]['weights'][0]['xleftbulge'], 2.5)
        self.assertEqual(result[0]['weights'][0]['xweight'], .4)
        self.assertEqual(skin, before)
        self.assertEqual(runtime_skin_nodes(ir, skin), result)

    def test_zero_distance_rejected(self):
        ir, skin = self.fixture()
        ir['records'][0]['mesh']['vertices'][0] = [0, 0, 0]
        with self.assertRaises(ValueError):
            runtime_skin_nodes(ir, skin)


if __name__ == '__main__':
    unittest.main()
