import unittest
import numpy as np
from vam_face_eye_correspondence import ordered_cycle,ordered_correspondence
from vam_face_eye_intersections import crossings,pair_distances
from vam_face_adaptive import local_guard
from vam_face_fidelity import operators
from vam_face_eye_regions import partition,ownership,neighborhood
from vam_face_phong import evaluate


class EyeCorrespondenceTests(unittest.TestCase):
    def test_ordered_curve_uses_same_interpolation_as_source_surface(self):
        a=np.arange(12)*2*np.pi/12;source=np.c_[np.cos(a),np.sin(a),a*0]
        b=np.arange(36)*2*np.pi/36;target=np.c_[np.cos(b),np.sin(b),b*0]
        hit,idx,f,diagnostic=ordered_correspondence(target,source,source)
        triangles=np.stack([source[idx],source[(idx+1)%len(source)],np.tile([0.,0,1],(len(idx),1))],axis=1)
        w=np.c_[1-f,f,np.zeros(len(f))]
        expected=evaluate(triangles,triangles,w)[0]
        np.testing.assert_allclose(hit,expected,atol=1e-12)
        self.assertEqual(diagnostic['interpolation'],'phong-alpha-0.5')
        reversed_hit=ordered_correspondence(target,source[::-1],source[::-1])[0]
        np.testing.assert_allclose(hit,reversed_hit,atol=1e-6)

    def test_geodesic_support_is_rigid_and_scale_invariant(self):
        angles=np.arange(8)*2*np.pi/8
        vertices=np.vstack([np.c_[r*np.cos(angles),r*np.sin(angles),angles*0] for r in (1,2,5,10)])
        triangles=[]
        for ring in range(3):
            for j in range(8):
                a=ring*8+j;b=ring*8+(j+1)%8;c=(ring+1)*8+j;d=(ring+1)*8+(j+1)%8
                triangles.extend([[a,b,c],[b,d,c]])
        triangles=np.array(triangles);loops=[np.arange(8,16)]
        selected=neighborhood(vertices,triangles,loops)
        rotation=np.array([[0.,-1,0],[1,0,0],[0,0,1]])
        self.assertEqual(selected,neighborhood(7*vertices@rotation+[2,3,4],triangles,loops))
        self.assertLess(len(selected),len(vertices))

    def test_topology_partition_keeps_exterior_off_socket_wall(self):
        triangles=[]
        for r in range(3):
            for j in range(8):
                a=r*8+j;b=r*8+(j+1)%8;c=(r+1)*8+j;d=(r+1)*8+(j+1)%8
                triangles.extend([[a,b,c],[b,d,c]])
        triangles=np.array(triangles);loop=np.arange(8,16)
        labels,outer,inner=partition(triangles,32,[loop])
        self.assertTrue(np.all(labels[:8]==inner[0]))
        self.assertTrue(np.all(labels[16:]==outer))
        groups=ownership(triangles,triangles,32,32,[loop],[loop])
        exterior=groups[0]
        self.assertFalse(np.any(exterior[2]<8))
        self.assertTrue(np.all(exterior[1]>=16))

    def test_clearance_stops_parallel_triangles_before_contact(self):
        v=np.array([[0.,0,0],[1,0,0],[0,1,0],[0,0,.1],[1,0,.1],[0,1,.1]])
        t=np.array([[0,1,2],[3,4,5]]);pairs=np.array([[0,1]])
        np.testing.assert_allclose(pair_distances(v,t,t,pairs),[.1])
        delta=np.zeros_like(v);delta[3:,2]=-.099999
        e,inc,_=operators(t,len(v));lengths=np.linalg.norm(inc@v,axis=1)
        result,_=local_guard(v,v,delta,t,e,lengths,t[:1],t,.05)
        self.assertGreaterEqual(pair_distances(result,t,t,pairs)[0],.01-1e-10)
        reference,_=local_guard(v,v,delta,t,e,lengths,t[:1],t,.05,False)
        np.testing.assert_array_equal(result,reference)

    def test_collision_guard_blocks_nonadjacent_skin_crossing(self):
        v=np.array([[-1.,-1,0],[1,-1,0],[0,1,0],[-.2,0,1],[.2,0,1],[0,.2,2]])
        t=np.array([[0,1,2],[3,4,5]])
        delta=np.zeros_like(v);delta[3:,2]=-1.5
        self.assertTrue(crossings(v+delta,t[:1],t,True))
        self.assertFalse(crossings(v,t[:1],t,True))
        e,inc,_=operators(t,len(v));lengths=np.linalg.norm(inc@v,axis=1)
        result,_=local_guard(v,v,delta,t,e,lengths,t[:1],t)
        self.assertFalse(crossings(result,t[:1],t,True))
        reference,_=local_guard(v,v,delta,t,e,lengths,t[:1],t,0.,False)
        np.testing.assert_array_equal(result,reference)

    def test_orders_using_edges_and_rejects_disconnected_cycles(self):
        self.assertEqual(ordered_cycle([3,1,0,2],[[0,1,2,3]]).tolist(),[0,1,2,3])
        with self.assertRaisesRegex(ValueError,'Disconnected'):
            ordered_cycle(range(8),[[0,1,2,3],[4,5,6,7]])

    def test_crowded_points_cannot_collapse_or_reverse_source_arc(self):
        a=np.arange(20)*2*np.pi/20
        source=np.c_[np.cos(a),.3*np.sin(a),a*0]
        b=np.arange(60)*2*np.pi/60
        target=np.c_[np.cos(b),.12*np.sin(b),b*0]
        hit,idx,f,report=ordered_correspondence(target,source)
        self.assertEqual(report['collapsed_intervals'],0)
        self.assertAlmostEqual(report['source_windings'],1)
        self.assertGreaterEqual(report['minimum_arc_ratio'],.25-1e-7)
        np.testing.assert_allclose(hit,source[idx]+f[:,None]*(source[(idx+1)%len(source)]-source[idx]))

    def test_rigid_transform_and_reversed_source_preserve_result(self):
        a=np.arange(12)*2*np.pi/12
        source=np.c_[np.cos(a),.4*np.sin(a),.03*np.sin(2*a)]
        target=source*.95
        hit=ordered_correspondence(target,source)[0]
        rotation=np.array([[0.,-1,0],[0,0,1],[-1,0,0]])
        shifted=ordered_correspondence(target@rotation+[2,3,4],source[::-1]@rotation+[2,3,4])[0]
        np.testing.assert_allclose(shifted,hit@rotation+[2,3,4],atol=1e-6)


if __name__=='__main__':unittest.main()
