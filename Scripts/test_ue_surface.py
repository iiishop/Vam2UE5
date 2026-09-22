"""Synthetic topology regression; no proprietary asset fixtures."""
import unittest
from vam_fit import apply_graft_boundary
from vam_ue_mesh import normals_for_ue, ue_triangles, hair_reference_mesh


class SurfaceTests(unittest.TestCase):
    def test_graft_follows_body_and_keeps_local_morph(self):
        target={'vertices':[[0,0,0],[1,0,0]]}
        body={'vertices':[[0,2,0],[1,4,0],[0,0,0],[.5,0,1]]}
        merged={'graftMethod':1,'numGraftBaseVertices':2,'startGraftVertIndex':2,
                '_graftWeights':[0,1.], '_graftIsFreeVert':[0,1],
                '_graftXFactor':1.,'_graftYFactor':1.,'_graftZFactor':1.}
        graft={'meshGraft':{'vertexPairs':[{'vertexNum':0,'graftToVertexNum':0}]}}
        apply_graft_boundary(body,target,merged,graft)
        self.assertEqual(body['vertices'][2],[0,2,0])
        self.assertEqual(body['vertices'][3],[.5,2,1])

    def test_hair_cards_keep_center_and_separate_strands(self):
        mesh={'vertices':[[-1,0,0],[1,0,0],[-1,0,2],[1,0,2],[-1,0,4],[1,0,4]],
              'sections':[[0,2,1,1,2,3,2,4,3,3,4,5]]}
        result=hair_reference_mesh(mesh,{'width':.0002,'hairMultiplier':16})
        self.assertEqual(len(result['vertices']),16)
        self.assertEqual(len(result['sections'][0]),24)
        self.assertEqual(result['uv'][0],[0.,0.])
        self.assertEqual(result['uv'][-1],[1.,1.])
        for i in range(0,len(result['vertices']),2):
            a,b=result['vertices'][i:i+2]
            self.assertAlmostEqual(a[0]+b[0],0)
            self.assertAlmostEqual(a[1]+b[1],0)

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
