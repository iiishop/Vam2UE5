import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).parents[1]/'Scripts'))
from vam_fit import apply_morph, apply_bone_centers, finalize_bone_centers, fit_wrap, triangles
from vam_decode import DecodeError,decode_vmb
from vam_preview import decode_plan,DecodeService
from test_decode import triangle,vab_fixture


class FitTests(unittest.TestCase):
    def test_runtime_morph_domain_duplicates_and_graft_protection(self):
        body={'vertices':[[0.,0.,0.]for _ in range(5)]}
        morph={'deltas':[[0,1,0,0],[0,2,0,0],[3,90,0,0],[4,80,0,0],[8,70,0,0]]}
        report=apply_morph(body,morph,.5,3,5)
        self.assertEqual(body['vertices'][0],[1.5,0,0])
        self.assertEqual(body['vertices'][3:],[ [0,0,0],[0,0,0]])
        self.assertEqual(report,{'outside_uv':1,'uv_duplicates':2,'applied_deltas':2})

    def test_vmb_compatibility_preserves_repeated_records(self):
        raw=struct.pack('<iifffifff',2,1,1,0,0,1,2,0,0)
        self.assertEqual(len(decode_vmb(raw,{'numDeltas':2},allow_repeated=True)['deltas']),2)
        with self.assertRaises(DecodeError):decode_vmb(raw,{'numDeltas':2})

    def test_bone_setter_and_parent_offsets(self):
        bones=[{'name':'head','source_object':'1','position':[0.,1.,0.]},
               {'name':'eye','source_object':'2','position':[0.,1.,1.],'parameters':{'parentForMorphOffsets':{'m_PathID':1}}}]
        m={'parameters':{'formulas':[{'targetType':'BoneCenterY','target':'head','multiplier':.3},
                                    {'targetType':'BoneCenterY','target':'head','multiplier':.1}]}}
        apply_bone_centers(bones,m,2);finalize_bone_centers(bones)
        self.assertEqual(bones[0]['position'],[0.,1.2,0.]);self.assertEqual(bones[1]['position'],[0.,1.2,1.])

    def test_wrap_translation_and_bad_binding(self):
        mesh=triangle();wrap={'vertices':[[0,0,1,2,.2,.7,.3,0,0,0]]*3}
        a=fit_wrap(mesh,wrap,mesh)
        target=copy.deepcopy(mesh);target['vertices']=[[x+2,y+3,z+4] for x,y,z in target['vertices']]
        b=fit_wrap(mesh,wrap,target)
        # VaM's literal 0.33333 introduces a small, reproducible translation residual.
        for old,new in zip(a['vertices'],b['vertices']):
            for axis,delta in enumerate((2,3,4)):self.assertAlmostEqual(new[axis]-old[axis],delta,places=4)
        wrap['vertices'][0]=[500,0,1,2,0,0,0,0,0,0]
        with self.assertRaises(DecodeError):fit_wrap(mesh,wrap,target)

    def test_material_triangle_order(self):
        mesh=triangle();mesh['polygons']=[{'materialNum':1,'vertices':[0,1,2]},{'materialNum':0,'vertices':[2,1,0]}]
        self.assertEqual(triangles(mesh),[[0,1,2],[2,1,0]])

    def test_blocked_plan_and_corrupt_resource_keep_valid_geometry(self):
        from vam_plan import sha
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);raw,vam,vaj=vab_fixture();files={'ok.vab':raw,'ok.vam':json.dumps(vam).encode(),'ok.vaj':json.dumps(vaj).encode(),'bad.vmi':b'{bad'}
            items=[]
            for name,data in files.items():
                (root/name).write_bytes(data);items.append({'id':name,'path':name,'source':'','resource_kind':name[-3:],'action':'create','sha256':sha(data)})
            items.append({'id':'missing','path':'absent.vmb','source':'','resource_kind':'unresolved','action':'missing','reason':'file absent'})
            plan={'plan_id':'fixed','status':'blocked','selection':[],'roots':[],'documents':{},'source_root':str(root),'items':items,'edges':[{'to':'missing','from':'preset','field':'/morph','reference':'absent.vmb'}]}
            catalog=type('Catalog',(),{'data':root})()
            with patch('vam_preview.Planner') as planner:
                planner.return_value.generate.return_value=plan
                ir,a=decode_plan(plan,catalog);_,b=decode_plan(plan,catalog)
            self.assertEqual(a,b);self.assertEqual(a['status'],'partial');self.assertEqual(len(a['meshes']),1)
            self.assertEqual(len(a['errors']),2);self.assertTrue(a['errors'][0]['references'])
            plan['items']=items[-1:]
            with patch('vam_preview.Planner') as planner:
                planner.return_value.generate.return_value=plan
                with self.assertRaisesRegex(DecodeError,'no_geometry'):decode_plan(plan,catalog)

if __name__=='__main__':unittest.main()
