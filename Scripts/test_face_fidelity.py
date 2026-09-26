import copy
import unittest
import numpy as np
from vam_face_semantics import template, validate_map, topology, annotation_matrix, FidelityError, REQUIRED
from vam_face_fidelity import Problem, triangle_check, VIEWS


def fixture():
    vertices = [[x/4, y/4, .1*(x/4)**2] for y in range(5) for x in range(5)]
    triangles = []
    for y in range(4):
        for x in range(4):
            a = y*5+x; triangles.extend([a, a+1, a+5, a+1, a+6, a+5])
    target = {'vertices': vertices, 'triangles': triangles}
    source = copy.deepcopy(target)
    for v in source['vertices']: v[2] += .008*np.sin(v[0]*np.pi)*np.sin(v[1]*np.pi)
    data = template(source, target); data.update(status='Reviewed', provenance='synthetic unit fixture, not anatomical evidence')
    data['source_face_triangles'] = data['target_face_triangles'] = list(range(len(triangles)//3))
    for i, name in enumerate(REQUIRED):
        ids = [i % 24, i % 24+1]
        samples = [{'vertices': [x], 'weights': [1.]} for x in ids]
        data['curves'][name] = {'source': samples, 'target': samples, 'evidence': 'synthetic fixture', 'verification_state': 'verified'}
    return source, target, data


class SemanticsTest(unittest.TestCase):
    def test_topology_independent_of_identity(self):
        source, target, data = fixture()
        self.assertEqual(topology(source), topology(target))
        source['vertices'] = (np.asarray(source['vertices'])*2+13).tolist()
        validate_map(data, source, target)

    def test_wrong_topology_and_missing_semantics_rejected(self):
        source, target, data = fixture(); source['triangles'][:2] = source['triangles'][1::-1]
        with self.assertRaisesRegex(FidelityError, 'UnsupportedTopology'): validate_map(data, source, target)
        source, target, data = fixture(); del data['curves']['nostril.left']
        with self.assertRaisesRegex(FidelityError, 'MissingSemantics'): validate_map(data, source, target)

    def test_unreviewed_map_is_not_a_correspondence(self):
        source, target, _ = fixture()
        with self.assertRaisesRegex(FidelityError, 'AwaitingSemanticMapReview'): validate_map(template(source, target), source, target)

    def test_edge_and_barycentric_identity(self):
        source, _, _ = fixture()
        m = annotation_matrix([{'vertices': [0, 1, 5], 'weights': [.2, .3, .5], 'triangle': 0}], source)
        np.testing.assert_allclose(m@np.array(source['vertices']), [.2*np.array(source['vertices'][0])+.3*np.array(source['vertices'][1])+.5*np.array(source['vertices'][5])])
        with self.assertRaisesRegex(FidelityError, 'NotMeshEdge'): annotation_matrix([{'vertices': [0, 24], 'weights': [.5, .5]}], source)

    def test_fold_is_rejected(self):
        v = np.array([[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]])
        q = v.copy(); q[2, 1] = -1
        self.assertEqual(triangle_check(v, q, np.array([[0, 1, 2]])), 1)

    def test_candidates_improve_known_synthetic_offset(self):
        source, target, data = fixture(); problem = Problem(source, target, data)
        fitted, report = problem.candidates()
        chosen = report['candidates'][report['chosen_solver_profile']]['metrics']
        baseline = report['candidates']['Baseline']['metrics']
        self.assertLess(chosen['score'], baseline['score'])
        self.assertEqual(chosen['flipped_triangles'], 0)
        self.assertEqual(len(chosen['semantics_normalized']), len(REQUIRED))
        self.assertEqual(len(chosen['projected_contour_normalized']), len(VIEWS))
        self.assertFalse(report['visual_acceptance_passed'])
        self.assertTrue(np.isfinite(fitted).all())

    def test_metric_scale_translation_invariance(self):
        source, target, data = fixture()
        a = Problem(source, target, data); ma = a.metrics(a.base)
        for mesh in (source, target): mesh['vertices'] = (np.asarray(mesh['vertices'])*3+[30, -4, 8]).tolist()
        b = Problem(source, target, data); mb = b.metrics(b.base)
        self.assertAlmostEqual(ma['score'], mb['score'], places=7)


if __name__ == '__main__': unittest.main()
