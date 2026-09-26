import unittest
import numpy as np
from vam_face_topology_equivalence import equivalent,quad_triangles
from vam_face_fidelity import triangle_check
from vam_face_assembly_check import compare_head


class OfficialTopologyTests(unittest.TestCase):
    def test_assembly_must_retain_fitted_vertex_identity(self):
        reference={'vertices':[[0.,0,0],[1,0,0],[0,1,0]],'triangles':[0,1,2]}
        actual={'vertices':[[0.,0,0],[1,0,0],[0,1,0]],'triangles':[1,2,0]}
        self.assertEqual(compare_head(reference,actual)['max_rig_to_assembly_delta_cm'],0)
        actual['vertices'][1][0]=1.01
        with self.assertRaisesRegex(ValueError,'AssemblyChangedFittedHead'):compare_head(reference,actual)

    def test_other_diagonal_can_invert_while_original_triangles_pass(self):
        base=np.array([[0.,0,0],[1,0,0],[1,1,0],[0,1,0]])
        current=base.copy();current[0]=[.6,.6,0]
        original=np.array([[0,1,2],[0,2,3]])
        both=np.array(quad_triangles([[0,1,2,3]],original.ravel().tolist()))
        self.assertEqual(triangle_check(base,current,original),0)
        self.assertGreater(triangle_check(base,current,both),0)

    def test_quad_guard_covers_both_diagonals_and_rejects_wrong_identity(self):
        guard=quad_triangles([[0,1,2,3]],[0,1,3,1,2,3])
        self.assertEqual(len(guard),4)
        with self.assertRaises(ValueError):quad_triangles([[0,1,2,4]],[0,1,3,1,2,3])
        with self.assertRaises(ValueError):quad_triangles([[0,1,2,3]]*2,[0,1,3,1,2,3])

    def test_order_and_cyclic_corner_rotation(self):
        r=equivalent([0,1,2,0,2,3],[2,3,0,1,2,0])
        self.assertEqual(r['kind'],'oriented_triangle_reorder')

    def test_quad_diagonal_flip(self):
        r=equivalent([0,1,2,0,2,3],[0,1,3,1,2,3])
        self.assertEqual(len(r['quad_flips']),1)

    def test_adjacent_quad_flips(self):
        r=equivalent([0,1,4,0,4,3,1,2,5,1,5,4],[0,1,3,1,4,3,1,2,4,2,5,4])
        self.assertEqual(len(r['quad_flips']),2)

    def test_winding_and_connectivity_and_duplicates_are_rejected(self):
        for new in ([0,2,1,0,3,2],[0,1,2,0,2,4],[0,1,2,0,1,2]):
            with self.assertRaises(ValueError):equivalent([0,1,2,0,2,3],new)


if __name__=='__main__':unittest.main()
