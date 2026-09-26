import unittest
from vam_face_template_contract import check,triangle_digest,compare_reload


class TemplateTopologyTest(unittest.TestCase):
    def test_reload_keeps_all_positions_and_auxiliary_topology_strict(self):
        old=[0,1,2,0,2,3];new=[0,1,3,1,2,3];aux=[4,5,6]
        head={'vertices':[[0,0,0]]*4,'triangles':old,'official_quads':[[0,1,2,3]]}
        a={'vertices':[[0,0,0]]*7,'triangles':old+aux};b=dict(a,triangles=new+aux)
        self.assertEqual(compare_reload(a,b,head)[0],0)
        with self.assertRaisesRegex(ValueError,'ReloadTopologyChanged'):compare_reload(a,b)
        with self.assertRaisesRegex(ValueError,'ReloadAuxiliaryTopologyChanged'):compare_reload(a,dict(b,triangles=new+[4,6,5]),head)
        with self.assertRaisesRegex(ValueError,'ReloadGeometryChanged'):compare_reload(a,dict(b,vertices=[[1,0,0]]*7),head)
        with self.assertRaisesRegex(ValueError,'ReloadInvalidCoordinates'):compare_reload(a,dict(b,vertices=[[float('nan'),0,0]]*7),head)
        a={'vertices':[[0,0,0]]*8,'triangles':old+[4,5,6,4,6,7]}
        b=dict(a,triangles=new+[4,5,7,5,6,7])
        self.assertEqual(len(compare_reload(a,b,head)[1]['auxiliary']['quad_flips']),1)

    def test_only_official_quad_diagonal_change_is_allowed(self):
        old=[0,1,2,0,2,3];new=[0,1,3,1,2,3]
        head={'vertices':[[0,0,0]]*4,'triangles':old,'official_quads':[[0,1,2,3]]}
        self.assertTrue(check(head,new,triangle_digest(old))['official_quad_coverage_verified'])
        with self.assertRaises(ValueError):check(head,[0,3,1,1,3,2],triangle_digest(old))
        with self.assertRaises(ValueError):check(head,[0,1,4,1,2,4],triangle_digest(old))
        with self.assertRaisesRegex(ValueError,'TemplateReferenceTopologyChanged'):check(head,new,'changed')
        del head['official_quads']
        with self.assertRaisesRegex(ValueError,'OfficialQuadEvidenceRequired'):check(head,new,triangle_digest(old))
        self.assertEqual(check(head,old,triangle_digest(old))['kind'],'identical')


if __name__=='__main__':unittest.main()
