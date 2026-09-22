"""Synthetic coordinate/weight contract tests, no VaM assets distributed."""
import unittest
from vam_decode import DecodeError
from vam_native_source import reference_bones, rotate, qmul, qinverse, general_weights


def bone(name, parent, position, angles):
    return {'name':name,'parent':parent,'source_object':name,'position':position,
            'base_position':position,'orientation_degrees':angles,'rotation_order':0,
            'parameters':{'useUnityEulerOrientation':False}}


class NativeSourceTests(unittest.TestCase):
    def test_parent_first_and_world_to_local(self):
        result=reference_bones([bone('child','root',[1,2,3],[33,-16,71]),bone('root',None,[2,0,-1],[15,27,-10])])
        self.assertEqual([b['name'] for b in result],['root','child'])
        root,child=result
        q=qmul(root['quaternion_xyzw'],child['quaternion_xyzw'])
        p=[a+b for a,b in zip(rotate(root['quaternion_xyzw'],child['translation']),root['translation'])]
        for a,b in zip(p,[300,100,200]):self.assertAlmostEqual(a,b,places=8)
        # Reconstructed world and inverse bind must compose to identity.
        inv=child['inverse_bind']
        for point in ([0,0,0],[1,3,5],[-8,2,0]):
            world=[a+b for a,b in zip(rotate(q,point),p)]
            restored=[sum(inv[i][j]*world[j] for j in range(3))+inv[i][3] for i in range(3)]
            for a,b in zip(point,restored):self.assertAlmostEqual(a,b,places=8)

    def test_basis_rotation_not_euler_permutation(self):
        # Source +90 around x maps to UE +90 around y; z maps to x.
        result=reference_bones([bone('root',None,[0,0,0],[90,0,0])])[0]
        point=rotate(result['quaternion_xyzw'],[1,0,0])
        for a,b in zip(point,[0,0,-1]):self.assertAlmostEqual(a,b,places=8)

    def test_cycle_rejected(self):
        with self.assertRaises(DecodeError):reference_bones([bone('a','b',[0]*3,[0]*3),bone('b','a',[0]*3,[0]*3)])

    def test_active_mode_not_has_general_flag(self):
        with self.assertRaisesRegex(DecodeError,'triax_pending_calibration'):
            general_weights({'_useGeneralWeights':0,'_hasGeneralWeights':1},1,[{'name':'root'}])

    def test_general_duplicates_sum_fullweight_not_added(self):
        skin={'_useGeneralWeights':1,'_hasGeneralWeights':1,'nodes':[
            {'name':'root','generalWeights':[{'vertex':0,'weight':.25},{'vertex':0,'weight':.75}],
             'fullyWeightedVertices':[0]}]}
        self.assertEqual(general_weights(skin,1,[{'name':'root'}]),[[0,0,1.]])
        with self.assertRaisesRegex(DecodeError,'skin_normalization'):general_weights(skin,2,[{'name':'root'}])


if __name__=='__main__':unittest.main()
