"""Original tiny fixtures, no redistributed source assets."""
import unittest
import numpy as np
from vam_native_source import reference_bones
from test_native_source import bone
from vam_triax_lbs import fit,axis_response


class TriAxFitTests(unittest.TestCase):
    def test_full_weight_rigid_motion(self):
        bones=reference_bones([bone('root',None,[0,0,0],[0,0,0])])
        skin={'bulgeScale':.0015,'nodes':[{'name':'root','weights':[],
            'fullyWeightedVertices':[0,1],'bulgeFactors':{}}]}
        weights,report=fit(skin,[[1,2,3],[-2,3,4]],bones)
        self.assertEqual(weights,[[0,0,1.],[1,0,1.]])
        self.assertLess(report['maximum_cm'],1e-10)

    def test_disjoint_merged_nodes_not_overwritten(self):
        bones=reference_bones([bone('root',None,[0,0,0],[0,0,0])])
        skin={'nodes':[{'name':'root','weights':[],'fullyWeightedVertices':[i],'bulgeFactors':{}} for i in (0,1)]}
        rows,_=fit(skin,[[1,2,3],[4,5,6]],bones)
        self.assertEqual(len(rows),2)

    def test_uncovered_domain_rejected(self):
        bones=reference_bones([bone('root',None,[0,0,0],[0,0,0])])
        with self.assertRaisesRegex(Exception,'triax_uncovered'):
            fit({'nodes':[]},[[1,2,3]],bones)

    def test_weighted_angle_not_linear_blend(self):
        p=np.array([[1.,0,0]])
        d=axis_response(p,2,np.pi/2,np.array([.5]),np.zeros(1),np.zeros(1),{},0)
        np.testing.assert_allclose(p+d,[[2**-.5,2**-.5,0]],atol=1e-12)

if __name__=='__main__':unittest.main()
