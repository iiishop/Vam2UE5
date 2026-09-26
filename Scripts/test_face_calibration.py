import unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
import numpy as np
from vam_face_calibration import Camera, surface_hit, CalibrationError

def calibration(origin=(0,10,0)):
    return {'CameraViewInfo':{'Location':dict(zip(('X','Y','Z'),origin)),
            'Rotation':{'Pitch':0,'Yaw':-90,'Roll':0},'FOV':60},'ImageSize':{'X':640,'Y':480}}

class FaceGeometryTests(unittest.TestCase):
    def test_projection_ray_consistency(self):
        c=Camera(calibration());v=np.array([[1.,0,2],[-2,1,-1]])
        px,depth=c.project(v)
        np.testing.assert_allclose([c.origin+d*c.ray(p) for p,d in zip(px,depth)],v,atol=1e-12)
    def test_surface_ray_rejects_back_of_head(self):
        c=Camera(calibration());v=np.array([[-5,-10,-5],[5,-10,-5],[0,-10,5.]])
        with self.assertRaisesRegex(CalibrationError,'DepthAmbiguous'):
            surface_hit(v,[[0,1,2]],c,[320,240],10,2)
    def test_scale_and_translation_equivariance(self):
        v=np.array([[-5,0,-5],[5,0,-5],[0,0,5.]])
        c=Camera(calibration());point,_=surface_hit(v,[[0,1,2]],c,[320,240],10,2)
        shift=np.array([103.,-27,54]);scale=2.7
        transformed=Camera(calibration(c.origin*scale+shift))
        result,_=surface_hit(v*scale+shift,[[0,1,2]],transformed,[320,240],10*scale,2*scale)
        np.testing.assert_allclose(result,point*scale+shift,atol=1e-10)
    def test_missed_surface_is_recoverable_error(self):
        c=Camera(calibration());v=np.array([[-1,0,-1],[1,0,-1],[0,0,1.]])
        with self.assertRaisesRegex(CalibrationError,'SurfaceRayMiss'):
            surface_hit(v,[[0,1,2]],c,[0,0],10,2)
if __name__=='__main__':unittest.main()
