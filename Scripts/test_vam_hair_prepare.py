"""Deterministic geometry utilities; real source/runtime evidence is separate."""
import math,unittest
from vam_hair_prepare import arclength,interp,resample

class HairGeometry(unittest.TestCase):
    def test_lengths_and_endpoints(self):
        points=[[0,0,0],[0,0,2],[0,3,2]]
        curve=resample(points,16)
        self.assertEqual(curve[0],points[0]);self.assertEqual(curve[-1],points[-1])
        self.assertEqual(arclength(points)[-1],5)
        self.assertTrue(all(math.isfinite(v) for p in curve for v in p))

    def test_tied_segment_continuity(self):
        source=[[0,0,0],[0,0,1],[0,1,2],[1,2,3],[2,4,3]]
        anchor=resample(source[:3],4);free=resample(source[2:],32)
        self.assertEqual(anchor[-1],free[0])

    def test_coiled_shape_is_not_straightened(self):
        coil=[[math.cos(t*.1),math.sin(t*.1),t*.01] for t in range(63)]
        out=resample(coil,32)
        self.assertGreater(arclength(out)[-1],5.5)
        self.assertLess(math.dist(out[0],out[-1]),1)

    def test_repeated_source_points(self):
        self.assertEqual(resample([[0,0,0],[0,0,0],[0,0,2]],3),[[0,0,0],[0,0,1],[0,0,2]])

    def test_degenerate_rejected(self):
        with self.assertRaises(ValueError):resample([[0,0,0],[0,0,0]])

    def test_rigidity_interpolation(self):
        self.assertEqual(interp(1,[0,1,2],[1,.6,.1]),.6)
        self.assertAlmostEqual(interp(1.5,[0,1,2],[1,.6,.1]),.35)

class ScalpFrames(unittest.TestCase):
    def test_rigid_frame_transport(self):
        from vam_hair_frames import transport
        frame=([1,0,0],[0,1,0],[0,0,1])
        turned=([0,1,0],[-1,0,0],[0,0,1])
        self.assertEqual(transport([2,0,1],[0,0,0],[3,4,5],frame,turned),[3,6,6])

    def test_source_frame_identity(self):
        from vam_hair_frames import frames,transport
        mesh={'vertices':[[0,0,0],[1,0,0],[1,1,0],[0,1,0]],'uv':[[0,0],[1,0],[1,1],[0,1]],
              'polygons':[{'materialNum':0,'vertices':[0,1,2,3]}],
              'uv_polygons':[{'materialNum':0,'vertices':[0,1,2,3]}],'uv_map':[],'materials':['scalp']}
        f=frames(mesh,mesh['vertices'])
        self.assertEqual(transport([.2,.3,.7],[0,0,0],[0,0,0],f[0],f[0]),[.2,.3,.7])

if __name__=='__main__':unittest.main()
