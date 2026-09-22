"""Synthetic topology regression; no proprietary asset fixtures."""
import unittest
from vam_ue_mesh import normals_for_ue, ue_triangles


class SurfaceTests(unittest.TestCase):
    def test_clockwise_ue_front_face(self):
        mesh={'vertices':[[0,0,0],[1,0,0],[0,1,0]],'sections':[[0,1,2]]}
        self.assertEqual(ue_triangles([0,1,2],{0:0,1:1,2:2}),[(0,2,1)])
        self.assertEqual(normals_for_ue(mesh),[[0.,0.,1.]]*3)

    def test_uv_and_material_seams_share_normals(self):
        mesh={'vertices':[[0,0,0],[1,0,0],[0,1,0],[0,0,0],[0,1,0],[0,0,1]],
              'sections':[[0,1,2],[3,4,5]],'converted_to_source_vertex':[0,1,2,0,2,3]}
        normals=normals_for_ue(mesh)
        self.assertEqual(normals[0],normals[3])
        self.assertEqual(normals[2],normals[4])
        self.assertGreater(normals[0][0],0)
        self.assertGreater(normals[0][2],0)

    def test_unrelated_coincident_vertices_not_welded(self):
        mesh={'vertices':[[0,0,0],[1,0,0],[0,1,0],[0,0,0],[0,1,0],[0,0,1]],
              'sections':[[0,1,2],[3,4,5]]}
        normals=normals_for_ue(mesh)
        self.assertNotEqual(normals[0],normals[3])


if __name__=='__main__':unittest.main()
