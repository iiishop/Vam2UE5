import copy
import json
from pathlib import Path
import tempfile
import unittest
from vam_metahuman import neutral_target, user_name, verify_plan, verify_recipe, fingerprint, write_json, SCHEMA, to_creator, TARGET_FRAME, require_current_target_frame, diagnostic_baseline_allowed
from vam_plan import sha, canonical


def source():
    mesh = {'vertices': [[0,0,0],[1,0,0],[0,1,0],[0,0,1]], 'uv': [[0,0]]*4,
        'materials': ['Face','Hidden'], 'polygons': [{'materialNum':0,'vertices':[0,1,2]}, {'materialNum':1,'vertices':[0,2,3]}]}
    records = [{'class':'DAZMergedMesh','parameters':{'targetMesh':{'m_PathID':1}}},
        {'kind':'unity_mesh','object':'1','mesh':mesh}]
    for name, group, delta in [('shape','Characters',1),('pose','JarExpressions',3),('graft','Actor',9)]:
        records.append({'kind':'morph','id':name,'path':name,'data':{'parameters':{'group':group},'deltas':[[0,delta,0,0]]}})
    return {'records':records,'applied_morphs':[{'id':'shape','value':2}, {'id':'pose','value':1}, {'id':'graft','value':1,'vertex_offset':4}]}


class MetaHumanInputTests(unittest.TestCase):
    def test_diagnostic_baseline_cannot_enter_production_or_skip_fidelity_gate(self):
        recipe={'cohort_tracked_fit_complete':True,'fit_origin':'CohortTrackedOfficialConform'}
        request={'purpose':'cross-preset-structural-baseline','diagnostic_only':True}
        self.assertTrue(diagnostic_baseline_allowed(recipe,request,'diagnostic-baseline'))
        self.assertFalse(diagnostic_baseline_allowed(recipe,request,'resume'))
        with self.assertRaisesRegex(ValueError,'ExplicitEntry'):
            diagnostic_baseline_allowed(dict(recipe,diagnostic_only=True),request,'resume')
        with self.assertRaisesRegex(ValueError,'ContractMissing'):
            diagnostic_baseline_allowed(dict(recipe,fit_origin='OfficialTemplateFidelity'),request,'diagnostic-baseline')
        with self.assertRaisesRegex(ValueError,'ContractMissing'):
            diagnostic_baseline_allowed(recipe,{},'diagnostic-baseline')

    def test_pose_category_is_excluded_even_when_boolean_metadata_is_false(self):
        ir=source();params=ir['records'][3]['data']['parameters']
        params.update(group='Pose Controls/Hands/Left',region='Hands',isPoseControl='false')
        result=neutral_target(ir)
        self.assertEqual(result['vertices'][0],[-200,0,0])
        self.assertIn('pose',[m['id'] for m in result['excluded_morphs']])

    def test_neutral_excludes_pose_graft_and_private_sections(self):
        ir=source(); before=copy.deepcopy(ir); result=neutral_target(ir)
        self.assertEqual(ir,before)
        self.assertEqual(result['vertices'][0],[-200,0,0])
        self.assertEqual(result['coordinate_frame'], TARGET_FRAME)
        self.assertEqual(result['input_to_source_vertex'],[0,1,2])
        self.assertEqual(result['triangles'],[0,1,2])
        self.assertEqual(len(result['excluded_morphs']),2)

    def test_creator_forward_and_up_are_not_native_gameplay_axes(self):
        self.assertEqual(to_creator([0,0,1]), [0,100,0])
        self.assertEqual(to_creator([0,1,0]), [0,0,100])
        self.assertEqual(to_creator([1,0,0]), [-100,0,0])

    def test_closed_surface_orientation_survives_creator_conversion(self):
        ir=source(); ir['applied_morphs']=[]
        mesh=ir['records'][1]['mesh']
        # Closed tetrahedron with the clockwise orientation used by source
        # polygons and UE skeletal MeshDescriptions. Old MH code inverted it.
        mesh['polygons']=[{'materialNum':0,'vertices':v} for v in
                          ([0,1,2],[0,3,1],[0,2,3],[1,3,2])]
        result=neutral_target(ir)
        def determinant(a,b,c):
            return a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
        volume=sum(determinant(*(result['vertices'][i] for i in result['triangles'][k:k+3]))
                   for k in range(0,len(result['triangles']),3))/6
        self.assertAlmostEqual(volume,-1000000/6)

    def test_recipe_classification_can_exclude_baked_expression(self):
        result=neutral_target(source(),{'shape':'exclude'})
        self.assertEqual(result['vertices'][0],[0,0,0])

    def test_graft_cannot_be_reenabled(self):
        result=neutral_target(source(),{'graft':'include'})
        self.assertTrue(any(x['reason']=='graft' for x in result['excluded_morphs']))

    def test_nonfinite_rejected(self):
        ir=source();ir['applied_morphs'][0]['value']=float('nan')
        with self.assertRaises(Exception): neutral_target(ir)

    def test_names_are_preserved_or_rejected(self):
        self.assertEqual(user_name('启梦'), '启梦')
        for name in ('',' Foo','Foo/Bar','Foo.1','Foo_1 '):
            with self.assertRaises(ValueError): user_name(name)

    def test_plan_tampering_rejected_before_source_read(self):
        plan={'source_root':'unused','items':[]}
        plan['plan_id']=sha(canonical(plan));plan['items']=[{'path':'tampered'}]
        with self.assertRaisesRegex(ValueError,'SourceLockChanged'): verify_plan(plan,{'plan_id':plan['plan_id']})

    def test_target_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);write_json(p/'target.json',{'vertices':[]})
            recipe={'schema':SCHEMA,'name':'Example','inputs':{},'target_sha256':fingerprint(p/'target.json')}
            write_json(p/'recipe.json',recipe);verify_recipe(p)
            write_json(p/'target.json',{'vertices':[[1,2,3]]})
            with self.assertRaisesRegex(ValueError,'TargetChanged'):verify_recipe(p)

    def test_previously_verified_legacy_task_cannot_resume_as_success(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            write_json(p/'target.json',{'schema':'vam-metahuman-target/1'})
            old={'state':'VerifiedEditorAssembly','blueprint':'/Game/Old/BP_Old'}
            with self.assertRaisesRegex(ValueError,'LegacyTargetFrame'):
                require_current_target_frame(p,old)
            old['target_frame']=TARGET_FRAME
            with self.assertRaisesRegex(ValueError,'LegacyTargetFrame'):
                require_current_target_frame(p,old)
            write_json(p/'target.json',{'schema':'vam-metahuman-target/2','coordinate_frame':TARGET_FRAME})
            require_current_target_frame(p,old)

    def test_source_change_does_not_silently_relock(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory); (p/'source.bin').write_bytes(b'locked')
            expected=fingerprint(p/'source.bin')
            plan={'source_root':str(p),'items':[{'source':'','path':'source.bin','sha256':expected}]}
            plan['plan_id']=sha(canonical(plan))
            ir={'plan_id':plan['plan_id'],'source_hashes':{}}
            verify_plan(plan,ir);(p/'source.bin').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'SourceLockChanged'):verify_plan(plan,ir)
            self.assertEqual((p/'source.bin').read_bytes(),b'changed')

    def test_recipe_cannot_redirect_assets_into_another_character(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);write_json(p/'target.json',{})
            recipe={'schema':SCHEMA,'name':'Alice','destination':'/Game/MetaHumans',
                'assets':{'character':'/Game/MetaHumans/Bob/Source/Bob'},'inputs':{},
                'target_sha256':fingerprint(p/'target.json')}
            write_json(p/'recipe.json',recipe)
            with self.assertRaisesRegex(ValueError,'RecipeAssetPathOutsideCharacter'):verify_recipe(p)


if __name__=='__main__':unittest.main()
