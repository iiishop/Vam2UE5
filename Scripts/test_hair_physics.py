"""Constraint classification must follow geometry/source support, never resource names."""
import unittest
import numpy as np
from prepare_hair_physics import classify


class HairPhysicsTests(unittest.TestCase):
    def curve(self, points, rigidity):
        return dict(points=np.asarray(points).tolist(), rigidity=rigidity)

    def test_loose_straight(self):
        result = classify([self.curve([[0, 0, 0], [0, 0, -25]], .02)])
        self.assertEqual(result['kind'], 'loose')
        self.assertAlmostEqual(result['median_length_cm'], 25)

    def test_supported_straight(self):
        self.assertEqual(classify([self.curve([[0, 0, 0], [10, 0, 0]], .4)])['kind'], 'supported')

    def test_compact_is_rotation_and_scale_invariant(self):
        angle = np.linspace(0, 2*np.pi, 32)
        points = np.column_stack([np.cos(angle), np.sin(angle), angle*.05])
        for p in (points, points[:, [2, 0, 1]]*12 + [8, 13, 24]):
            self.assertEqual(classify([self.curve(p, .3)])['kind'], 'compact')

    def test_loose_curl_is_not_assumed_tied(self):
        t = np.linspace(0, 2*np.pi, 32)
        self.assertEqual(classify([self.curve(np.column_stack([np.cos(t), np.sin(t), t*.05]), .02)])['kind'], 'loose')


if __name__ == '__main__':
    unittest.main()
