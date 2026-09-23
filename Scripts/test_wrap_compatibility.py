import copy
import unittest
from vam_decode import DecodeError
from vam_fit import resolve_preview_wrap, apply_bone_centers, fit_wrap, preview_wrap_settings, smooth_wrap_vertices


class CompatibilityTests(unittest.TestCase):
    def test_source_offset_and_thickness_projection(self):
        target={'vertices':[[0,0,0],[1,0,0],[0,1,0]],'polygons':[{'materialNum':0,'vertices':[0,1,2]}]}
        mesh={'vertices':[[0,0,0]],'uv':[[0,0]]}
        wrap={'vertices':[[0,0,1,2,1,0,0,.5,2,3]]}
        base=fit_wrap(mesh,wrap,target)
        shifted=fit_wrap(mesh,wrap,target,surface_offset=-1)
        self.assertAlmostEqual(base['vertices'][0][2],-1)
        self.assertAlmostEqual(shifted['vertices'][0][2],0)
        thick=fit_wrap(mesh,wrap,target,surface_offset=-1,thickness=.1)
        self.assertAlmostEqual(thick['vertices'][0][2],-.05)
        self.assertAlmostEqual(thick['vertices'][0][0],-.033333,places=6)
        self.assertAlmostEqual(thick['vertices'][0][1],.166665,places=6)

    def test_exact_controller_and_appearance_precedence(self):
        result={'vam':{'uid':'author:Pin'},'vaj':{'storables':[{'id':'author:PinWrapControl','surfaceOffset':'-1','additionalThicknessMultiplier':'0'}]}}
        params,report=preview_wrap_settings(result,[('appearance',{'storables':[
            {'id':'different:PinWrapControl','surfaceOffset':'8'},
            {'id':'author:PinWrapControl','surfaceOffset':'-.5'}]})])
        self.assertEqual(params,{'surface_offset':-.5,'thickness':0.,'smooth_iterations':0})
        self.assertEqual(len(report['sources']),2)

    def test_source_hc_smoothing_and_zero_iterations(self):
        points=[[0.,0.,0.],[2.,0.,0.],[0.,2.,0.]]
        polygons=[{'vertices':[0,1,2]}]
        self.assertEqual(smooth_wrap_vertices(points,polygons,0),points)
        self.assertEqual(smooth_wrap_vertices(points,polygons,1),[[.75,.75,0.],[.5,.75,0.],[.75,.5,0.]])
        self.assertEqual(points,[[0.,0.,0.],[2.,0.,0.],[0.,2.,0.]])

    def test_identical_bindings_resolve_without_losing_provenance(self):
        mesh={'uv':[[0,0]]};wrap={'name':'Normal','vertices':[[0,1,2,3,0,0,0,1,0,0]]}
        sources=[wrap,copy.deepcopy(wrap)]
        chosen,report=resolve_preview_wrap([mesh],sources)
        self.assertEqual(chosen,wrap)
        self.assertEqual(report['equivalent_source_indices'],[0,1])
        self.assertEqual(len(sources),2)

    def test_same_name_and_length_are_not_evidence_of_equivalence(self):
        wrap={'name':'Normal','vertices':[[0,1,2,3,0,0,0,1,0,0]]}
        changed=copy.deepcopy(wrap);changed['vertices'][0][4]=.01
        with self.assertRaisesRegex(DecodeError,'differing skin bindings'):
            resolve_preview_wrap([{'uv':[[0,0]]}],[wrap,changed])
        with self.assertRaisesRegex(DecodeError,'wrap_ambiguous'):
            resolve_preview_wrap([{'uv':[[0,0]]}]*2,[wrap,wrap])

    def test_missing_bone_is_distinct_from_unimplemented_formula(self):
        bones=[{'name':'hip'}];issues=[]
        decoded={'parameters':{'formulas':[
            {'target':'hip','targetType':'BoneCenterY','multiplier':.2},
            {'target':'absent','targetType':'BoneCenterY','multiplier':.3},
            {'target':'hip','targetType':'RotationY','multiplier':4}]}}
        remaining=apply_bone_centers(bones,decoded,.5,issues)
        self.assertEqual(bones[0]['morph_center_offset'],[0,.1,0])
        self.assertEqual(remaining,{'RotationY'})
        self.assertEqual([d['code'] for d in issues],['formula_bone_missing','formula_not_executed'])

if __name__=='__main__':unittest.main()
