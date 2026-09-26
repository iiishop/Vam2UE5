import tempfile,unittest
from pathlib import Path
import numpy as np
from PIL import Image
from vam_face_cohort_input import tracking_image


class CohortCameraTests(unittest.TestCase):
    def test_same_framing_under_scale_and_translation(self):
        v=np.array([[-1.,0,-1],[1,0,-1],[1,.2,1],[-1,.2,1],[0,.5,0]])
        source={'vertices':v.tolist(),'triangles':[0,1,4,1,2,4,2,3,4,3,0,4],
                'triangle_materials':['Face']*4}
        shift=np.array([4.,-8,13]);changed=dict(source,vertices=(v*7+shift).tolist())
        with tempfile.TemporaryDirectory() as temp:
            a=Path(temp)/'a.png';b=Path(temp)/'b.png'
            ca=tracking_image(source,a,128);cb=tracking_image(changed,b,128)
            av=np.array(list(ca['CameraViewInfo']['Location'].values()));bv=np.array(list(cb['CameraViewInfo']['Location'].values()))
            np.testing.assert_allclose(bv,av*7+shift)
            self.assertEqual(ca['ImageSize'],cb['ImageSize'])
            delta=np.abs(np.asarray(Image.open(a)).astype(float)-np.asarray(Image.open(b)))
            self.assertLessEqual(delta.max(),1)


if __name__=='__main__':unittest.main()
