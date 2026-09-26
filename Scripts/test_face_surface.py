import unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
import numpy as np
from vam_face_surface import closest,transfer

class SurfaceTransferTests(unittest.TestCase):
    def test_triangle_interior_edge_and_vertex(self):
        triangle=np.array([[0.,0,0],[1,0,0],[0,1,0]])
        points=np.array([[.2,.3,2],[1,1,0],[-2,-1,3]])
        result=closest(points,np.broadcast_to(triangle,(3,3,3)))
        np.testing.assert_allclose(result,[[.2,.3,0],[.5,.5,0],[0,0,0]],atol=1e-12)
    def test_smooth_patch_reduces_error_without_inversion(self):
        n=18;xx,zz=np.meshgrid(np.linspace(-4,4,n),np.linspace(-4,4,n))
        head=np.column_stack((xx.ravel(),np.zeros(n*n),zz.ravel()))
        source=head.copy();source[:,1]=.02*np.exp(-(head[:,0]**2+head[:,2]**2)/4)
        tri=[]
        for y in range(n-1):
            for x in range(n-1):
                i=y*n+x;tri.extend([[i,i+1,i+n],[i+1,i+n+1,i+n]])
        curve=lambda pts:{'TrackingPoints':[dict(zip(('X','Y'),p)) for p in pts]}
        c={'CameraViewInfo':{'Location':{'X':0,'Y':10,'Z':0},'Rotation':{'Pitch':0,'Yaw':-90,'Roll':0},'FOV':60},
           'ImageSize':{'X':640,'Y':480},'CurveTrackingPoints':{
               'eyelid_l':curve([[220,180],[280,180]]),'eyelid_r':curve([[360,180],[420,180]]),
               'lip_upper_outer_l':curve([[300,280],[340,280]])}}
        out,r=transfer(source,tri,head,tri,c,iterations=2)
        self.assertEqual(r['flipped_triangles'],0);self.assertEqual(out.shape,head.shape)
        active=np.linalg.norm(out-head,axis=1)>1e-7
        self.assertTrue(active.any())
        self.assertLess(np.mean(np.linalg.norm(out[active]-source[active],axis=1)),np.mean(np.linalg.norm(head[active]-source[active],axis=1)))
if __name__=='__main__':unittest.main()
