import copy
import unittest
from vam_face_topology_calibrate import build, discover, boundary_chains
from vam_face_topology_review import review
from vam_face_semantics import FidelityError


def fixture():
    mesh = {'names': {'sceneNodeId': 'Genesis2Female'}, 'vertices': [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]],
            'materials': ['Face', 'Head'], 'polygons': [{'materialNum': 0, 'vertices': [0, 1, 2]}, {'materialNum': 1, 'vertices': [0, 2, 3]}],
            'uv_map': [], 'uv_polygons': []}
    return {'records': [{'kind': 'unity_mesh', 'object': 'canonical-fixture', 'mesh': mesh},
                        {'kind': 'morph', 'id': 'local-control', 'data': {'parameters': {'displayName': 'Eyelid Lower Shape', 'region': 'Eyes'},
                         'deltas': [[0, 1, 0, 0], [0, -1, 0, 0], [1, .01, 0, 0], [99, 1, 0, 0]]}}],
            'applied_morphs': [{'id': 'local-control', 'value': 100}]}


class CalibrationTest(unittest.TestCase):
    def test_delta_support_not_metadata_or_applied_weight(self):
        result = build(fixture()); evidence = result['morph_support_evidence']['local-control']
        self.assertEqual(evidence['source_vertex_ids'], [1])
        self.assertEqual(evidence['excluded_nonbase_indices'], [99])
        self.assertEqual(evidence['verification_state'], 'unverified')
        semantic = result['semantics']['lower_eyelid.left']
        self.assertEqual(semantic['vertex_ids'], [])
        self.assertEqual(semantic['candidate_evidence_ids'], ['local-control'])

    def test_material_boundary_is_topological(self):
        result = build(fixture())
        self.assertEqual(result['material_boundary_edges']['Face|Head'], [[0, 2]])

    def test_ordered_boundary_does_not_guess_through_branches(self):
        loop = boundary_chains([[3, 7], [1, 7], [1, 3]])[0]
        self.assertEqual(loop['vertex_ids'], [1, 3, 7]); self.assertTrue(loop['closed'])
        self.assertFalse(boundary_chains([[1, 2], [1, 3], [1, 4]])[0]['ordered'])

    def test_identity_coordinates_do_not_change_calibration(self):
        ir = fixture(); original = build(ir)
        ir['records'][0]['mesh']['vertices'] = [[x*30+5, y-12, z+7] for x, y, z in ir['records'][0]['mesh']['vertices']]
        ir['applied_morphs'][0]['value'] = -200
        self.assertEqual(original, build(ir))

    def test_review_rejects_nonedge_and_preserves_pending_regions(self):
        ir = fixture(); calibration = build(ir)
        entry = {'vertex_ids': [1, 3], 'edge_chain': [1, 3], 'verification_state': 'verified', 'evidence': ['canonical review'], 'provenance': ['unit fixture']}
        annotation = {'topology_family': 'G2F', 'topology_digest': calibration['topology_digest'], 'reviewer': 'test', 'reviewed_at': 'test', 'semantics': {'jaw': entry}}
        with self.assertRaisesRegex(FidelityError, 'Disconnected'): review(calibration, annotation, ir)
        entry['vertex_ids'] = entry['edge_chain'] = [0, 2]
        result = review(calibration, annotation, ir)
        self.assertEqual(result['semantics']['jaw']['verification_state'], 'verified')
        self.assertEqual(result['state'], 'AwaitingCanonicalReview')
        self.assertEqual(calibration['semantics']['jaw']['verification_state'], 'unverified')

    def test_equal_connectivity_does_not_authorize_another_family(self):
        ir = fixture(); calibration = build(ir)
        annotation = {'topology_family': 'G2M', 'topology_digest': calibration['topology_digest'],
                      'reviewer': 'test', 'reviewed_at': 'test', 'semantics': {}}
        with self.assertRaisesRegex(FidelityError, 'FamilyMismatch'):
            review(calibration, annotation, ir)


if __name__ == '__main__': unittest.main()
