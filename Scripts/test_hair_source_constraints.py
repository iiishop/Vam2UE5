import copy
import unittest
import numpy as np
from prepare_hair_constraints import point_rigidity, bake_group, bundle_anchor


class SourceConstraintsTests(unittest.TestCase):
    def fixture(self):
        p=np.array([[[x, 0, -j] for j in range(4)] for x in [0, 1, 2]], float)
        hair=dict(segments=4, root_to_scalp=[0,1,2], vertices=p.reshape(-1,3).tolist(),
                  rigidities=[],nearby_groups=[[[1,5,.01,.7],[2,6,.01,.7],[5,9,.01,.7]]])
        group=dict(settings={},curves=[dict(source_curve=i,points=[[x,0,-j*3/31] for j in range(32)],
                   rigidity_profile=[0]*32) for i,x in enumerate([0,1,2])])
        return group,hair

    def test_source_root_main_tip_semantics(self):
        g,h=self.fixture()
        r=point_rigidity(h,dict(usePaintedRigidity='false',rootRigidity=.8,mainRigidity=.4,tipRigidity=.1,rigidityRolloffPower=1))
        np.testing.assert_allclose(r[0],[1,.8,.25,.1])

    def test_painted_support_not_group_average(self):
        g,h=self.fixture();h['rigidities']=[0,.8,.7,0,0,.2,.1,0,0,1,1,1]
        r=bake_group(g,h)
        self.assertNotEqual(r['rigidity'][0],r['rigidity'][1])
        self.assertTrue(all(row[:2]==[1,1] for row in r['rigidity']))

    def test_links_are_authored_and_bounded(self):
        g,h=self.fixture();r=bake_group(g,h)
        self.assertGreater(r['retained_directed_links'],0)
        self.assertTrue(all(len(row)==2 for row in r['links']))
        h['nearby_groups']=[]
        self.assertEqual(bake_group(g,h)['retained_directed_links'],0)

    def test_identity_independent_of_name_rotation_translation(self):
        g,h=self.fixture();a=bake_group(g,h)
        for c in g['curves']:
            c['points']=(np.asarray(c['points'])[:,[2,0,1]]+[4,10,8]).tolist()
        b=bake_group(g,h)
        self.assertEqual(a['guide_source_ids'],b['guide_source_ids'])
        np.testing.assert_allclose(a['links'],b['links'],atol=1e-6)

    def test_visible_guides_preserve_render_and_source_ids(self):
        import copy
        from prepare_hair_constraints import fit_visible_guides
        g,h=self.fixture()
        g['render_curves']=[(np.asarray(c['points'])+[0,.2,0]).tolist() for c in g['curves']]
        g['render_source_bindings']=[dict(source_curves=[i,i,i],weights=[1,0,0],length=1) for i in range(3)]
        before=copy.deepcopy(g['render_curves'])
        ids=[c['source_curve'] for c in g['curves']]
        report=fit_visible_guides(g,h)
        self.assertEqual(g['render_curves'],before)
        self.assertEqual(sorted(c['source_curve'] for c in g['curves']),ids)
        self.assertTrue(report['render_unchanged'])
        for c,i in zip(g['curves'],report['render_curve_ids']):
            np.testing.assert_allclose(c['points'],before[i],atol=1e-6)

    def test_reject_invalid_geometry(self):
        g,h=self.fixture();g['curves'][0]['points'][1][0]=float('nan')
        with self.assertRaises(ValueError):bake_group(g,h)

    def test_disabled_source_links_stay_disabled(self):
        g,h=self.fixture();g['native_physics']={'NearbyJointPower':'0'}
        self.assertEqual(bake_group(g,h)['retained_directed_links'],0)

    def test_source_material_clock_has_composable_position_decay(self):
        g,h=self.fixture();g['native_physics']={'Iterations':'2'}
        result=bake_group(g,h)
        self.assertTrue(result['source_owns_shape'])
        self.assertTrue(result['prediction_support'])
        self.assertEqual(result['support_passes_per_second'],120)
        # This tests frozen-target position decay only, not dynamic timestep
        # invariance: subsequent velocity reconstruction still depends on h.
        # UE quality iterations must not multiply this source support rate.
        keep=(1-.2)**(result['support_passes_per_second']/60)
        for substeps in (1,5,10):
            self.assertAlmostEqual(((1-.2)**(result['support_passes_per_second']/60/substeps))**substeps,keep)

    def test_bundle_detection_is_not_a_name_rule(self):
        radius=np.interp(np.arange(32),[0,8,14,31],[5,1,3,3])
        t=np.arange(16)*2*np.pi/16
        p=np.array([np.column_stack([radius*np.cos(a),radius*np.sin(a),np.arange(32)]) for a in t])
        rigidity=np.full((16,32),.1)
        for points in [p,p[:,:,[2,0,1]]*3+[10,20,30]]:
            self.assertEqual(bundle_anchor(points,100,rigidity)['index'],8)
        self.assertIsNone(bundle_anchor(p,0,rigidity))
        self.assertIsNone(bundle_anchor(p,100,np.zeros_like(rigidity)))
        loose=p.copy();loose[:,:,:2]*=np.linspace(1,5,32)[None,:,None]/radius[None,:,None]
        self.assertIsNone(bundle_anchor(loose,100,rigidity))


if __name__=='__main__':unittest.main()
