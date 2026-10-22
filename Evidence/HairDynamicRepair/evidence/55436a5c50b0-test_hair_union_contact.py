"""Geometry regressions for overlapping body proxies, not an XPBD emulator."""
import unittest
from pathlib import Path
import numpy as np
from prepare_hair_constraints import collision_radii


def query(point, spheres):
    candidates = []
    for center, radius in spheres:
        delta = point-np.asarray(center)
        length = np.linalg.norm(delta)
        candidates.append((length-radius, delta/max(length,1e-20)))
    return min(candidates,key=lambda x:x[0])


def project(point, spheres, clearance, passes=6, mobile=True):
    point = np.asarray(point,dtype=float).copy()
    if not mobile:return point
    for _ in range(passes):
        distance,normal=query(point,spheres)
        gap=distance-clearance
        if gap>=-1e-4:break
        point-=gap*normal
    return point


class HairUnionContactTests(unittest.TestCase):
    def test_authored_radius_uses_source_units_and_root_slot(self):
        group=dict(native_physics=dict(WorldScale='1',StandRadius='.001',StandRootRadius='.002',UseSeparateRootRadius='true'),
                   curves=[dict(source_curve=7,visible_binding=dict(length=1))])
        hair=dict(segments=31,vertices=np.zeros((1,31,3)).tolist())
        baked=collision_radii(group,hair,[7])
        self.assertAlmostEqual(baked['body_radius_cm'],.1)
        self.assertEqual(baked['node_radii_cm'][0][:3],[0,0,.2])
        self.assertAlmostEqual(baked['node_radii_cm'][0][-1],.1)

    def test_single_projection_can_enter_another_body(self):
        spheres=[((-1,0,0),2),((1,0,0),2)]
        point=np.array([-.2,.3,0])
        once=project(point,spheres,.1,passes=1)
        self.assertLess(query(once,spheres)[0],0)
        final=project(once,spheres,.1)
        self.assertGreaterEqual(query(final,spheres)[0],.1-1e-4)

    def test_exterior_and_pinned_points_unchanged(self):
        spheres=[((0,0,0),2)]
        np.testing.assert_array_equal(project([4,1,0],spheres,.1),[4,1,0])
        np.testing.assert_array_equal(project([1,0,0],spheres,.1,mobile=False),[1,0,0])

    def test_single_body_is_idempotent(self):
        spheres=[((0,0,0),2)]
        first=project([1,0,0],spheres,.1)
        np.testing.assert_allclose(first,[2.1,0,0])
        np.testing.assert_allclose(project(first,spheres,.1),first)

    def test_graph_uses_same_gate_and_does_not_repeat_friction(self):
        code=(Path(__file__).resolve().parents[1]/'Source/VamResourceBrowser/Private/VamHairUnionContact.h').read_text()
        self.assertIn('Link(Contact,TEXT("Enable Constraint"),TEXT("Enabled"))',code)
        self.assertIn('Physics.GetClosestPoint(Result,FrameDt,TimeFraction',code)
        self.assertIn('Result-=Gap*Normal',code)
        self.assertNotIn('Hair.ProjectHardCollisionConstraint(',code)


if __name__=='__main__':unittest.main()
