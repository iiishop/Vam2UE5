import copy,os,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from vam_fit import apply_bone_centers,finalize_bone_centers
from vam_native_source import reference_bones
from vam_native_job_state import check_cancel,BuildCancelled,progress

class ShapeKernelTests(unittest.TestCase):
    def test_center_replacement_parent_offset_and_oriented_local_bind(self):
        source=[{'name':'root','parent':None,'source_object':'1','position':[0,0,0],
                 'orientation_degrees':[0,0,90],'rotation_order':0,'parameters':{'useUnityEulerOrientation':False}},
                {'name':'child','parent':'root','source_object':'2','position':[0,1,0],
                 'orientation_degrees':[0,0,90],'rotation_order':0,'parameters':{'useUnityEulerOrientation':False,'parentForMorphOffsets':{'m_PathID':1}}}]
        neutral=reference_bones(source)
        morph={'parameters':{'formulas':[{'target':'root','targetType':'BoneCenterX','multiplier':1},
                                       {'target':'root','targetType':'BoneCenterX','multiplier':2}]}}
        changed=copy.deepcopy(source);apply_bone_centers(changed,morph,.25);finalize_bone_centers(changed)
        self.assertEqual(changed[0]['position'],[.5,0,0]);self.assertEqual(changed[1]['position'],[.5,1,0])
        for b in changed:b['base_position']=b['position']
        shaped=reference_bones(changed)
        for a,b in zip(shaped[1]['translation'],neutral[1]['translation']):self.assertAlmostEqual(a,b)
        self.assertEqual(shaped[0]['translation'],[0,50,0])

    def test_cancel_is_separate_from_published_progress(self):
        with tempfile.TemporaryDirectory() as directory,patch.dict(os.environ,VAM_BUILD_JOB=directory):
            check_cancel();progress('preparing')
            self.assertTrue((Path(directory)/'status.json').is_file())
            (Path(directory)/'cancel').touch()
            with self.assertRaises(BuildCancelled):check_cancel()

if __name__=='__main__':unittest.main()
