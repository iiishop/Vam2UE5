import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from test_face_fidelity import fixture
from vam_metahuman import write_json, fingerprint
from vam_face_fidelity_verify import verify
from vam_face_fidelity_gate import require_receipt
from vam_face_semantics import FidelityError


class LifecycleTest(unittest.TestCase):
    def test_new_fidelity_recipe_does_not_inherit_baseline_diagnostic_status(self):
        from vam_face_fidelity_finish import fresh_lifecycle
        original={'inputs':{'source_ir':{'sha256':'locked'}},'diagnostic_only':True,
            'quality_status':'DraftStructuralBaseline','cohort_tracked_fit_complete':True,'owned':{'character':'old'}}
        fresh=fresh_lifecycle(original)
        self.assertEqual(fresh['inputs'],original['inputs'])
        self.assertNotIn('diagnostic_only',fresh);self.assertNotIn('owned',fresh)
        self.assertNotIn('quality_status',fresh);self.assertNotIn('cohort_tracked_fit_complete',fresh)
        self.assertTrue(original['diagnostic_only'])

    def prepare(self, root):
        source, head, semantics = fixture(); exported=root/'export'; exported.mkdir()
        for name, value in [('source',source),('head',head),('semantic-correspondence',semantics)]: write_json(root/(name+'.json'),value)
        write_json(root/'task.json',{'source_sha256':fingerprint(root/'source.json'),'head_sha256':fingerprint(root/'head.json'),
            'semantic_correspondence_sha256':fingerprint(root/'semantic-correspondence.json')})
        write_json(root/'pose-transform.json',{'posed_center':[0,0,0],'apose_center':[0,0,0],'posed_to_apose_rotation':[[1,0,0],[0,1,0],[0,0,1]]})
        import json
        task=json.loads((root/'task.json').read_text())
        task['pose_transform_sha256']=fingerprint(root/'pose-transform.json')
        write_json(root/'task.json',task)
        write_json(root/'template.json',{'head_vertices':head['vertices']})
        write_json(exported/'actual-head.json',head)
        write_json(exported/'reload.json',{'max_reload_delta_cm':0,'character':'/Game/Test/Character.Character','character_sha256':'fixture','full_rig':False})
        return exported,head

    @patch('vam_face_fidelity_views.comparison')
    def test_actual_reload_must_preserve_geometry_and_initial_receipt(self, plot):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); exported,head=self.prepare(root)
            result=verify(root,exported); self.assertTrue(result['eligible_for_rig'])
            old=fingerprint(root/'post-template-metrics.json')
            with self.assertRaisesRegex(FidelityError,'MissingFullRig'): verify(root,exported,'post-rig-metrics.json')
            self.assertEqual(old,fingerprint(root/'post-template-metrics.json'))
            wrong=copy.deepcopy(head); wrong['vertices'][7][2]+=.4
            write_json(exported/'actual-head.json',wrong)
            with self.assertRaisesRegex(FidelityError,'Regression'): verify(root,exported,'rejected-candidate.json')
            self.assertEqual(old,fingerprint(root/'post-template-metrics.json'))

    @patch('vam_face_fidelity_views.comparison')
    def test_missing_reload_and_changed_source_are_rejected(self, plot):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); exported,_=self.prepare(root)
            write_json(exported/'reload.json',{'max_reload_delta_cm':None})
            with self.assertRaisesRegex(FidelityError,'MissingIndependentReload'): verify(root,exported)
            (root/'source.json').write_text('{}')
            with self.assertRaisesRegex(FidelityError,'TaskGeometryChanged'): verify(root,exported)

    def test_receipt_never_authorizes_another_character_or_mutated_rig(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'receipt.json'
            write_json(path,{'eligible_for_rig':True,'state':'OfficialTemplateGeometryVerified','character':'/Game/A/A.A','character_sha256':'hashA'})
            ref={'path':str(path),'sha256':fingerprint(path)}
            recipe={'assets':{'character':'/Game/B/B'},'owned':{'character':'hashA'},'fidelity_post_rig':ref}
            with self.assertRaisesRegex(ValueError,'CharacterMismatch'): require_receipt(recipe,'fidelity_post_rig')
            recipe['assets']['character']='/Game/A/A'; require_receipt(recipe,'fidelity_post_rig')
            recipe['owned']['character']='changed'
            with self.assertRaisesRegex(ValueError,'PostRigCharacterChanged'): require_receipt(recipe,'fidelity_post_rig')


if __name__=='__main__':unittest.main()
