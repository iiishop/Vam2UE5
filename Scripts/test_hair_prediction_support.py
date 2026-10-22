"""Small algebra/graph-contract regressions for source hair support ordering.

These tests do not substitute for a compiled Niagara graph or a UE capture.
In particular, fixed-position decay invariance is not dynamic timestep
invariance after PBD reconstructs velocities from corrected positions.
"""
import math
import unittest
from pathlib import Path


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, scalar):
    return tuple(x * scalar for x in a)


def norm(a):
    return math.sqrt(sum(x * x for x in a))


def support(current, rest, rigidity, dt, passes_per_second, mobile=True):
    if not mobile:
        return current
    keep = (1.0 - rigidity) ** (dt * passes_per_second)
    return add(rest, mul(sub(current, rest), keep))


def project_length(point, root, length):
    delta = sub(point, root)
    return add(root, mul(delta, length / norm(delta)))


class HairPredictionSupportTests(unittest.TestCase):
    def assertVectorAlmostEqual(self, a, b):
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=11)

    def test_immobile_root_keeps_native_interpolated_position(self):
        current = (0.1, 0.2, 0.3)
        self.assertEqual(support(current, (2, 0, 0), 1, 1 / 300, 120, False), current)

    def test_zero_support_preserves_prediction(self):
        current = (2, -1, 3)
        self.assertVectorAlmostEqual(support(current, (1, 1, 1), 0, 1 / 300, 120), current)

    def test_fixed_target_decay_partitions_without_quality_iterations(self):
        current, rest = (4, -3, 2), (1, 2, 3)
        once = support(current, rest, 0.09, 1 / 60, 120)
        split = current
        for _ in range(5):
            split = support(split, rest, 0.09, 1 / 300, 120)
        self.assertVectorAlmostEqual(once, split)

    def test_support_is_rigid_transform_equivariant(self):
        def transform(v):
            return (-v[1] + 100, v[0] - 50, v[2] + 20)
        current, rest = (4, -3, 2), (1, 2, 3)
        self.assertVectorAlmostEqual(
            transform(support(current, rest, 0.09, 1 / 300, 120)),
            support(transform(current), transform(rest), 0.09, 1 / 300, 120),
        )

    def test_support_before_rod_does_not_leave_the_ordering_length_error(self):
        root, rest, current = (0, 0, 0), (1, 0, 0), (0, 1, 0)
        after_rod = support(project_length(current, root, 1), rest, 0.5, 1, 1)
        before_rod = project_length(support(current, rest, 0.5, 1, 1), root, 1)
        self.assertAlmostEqual(norm(before_rod), 1)
        self.assertGreater(abs(norm(after_rod) - 1), 0.25)

    def test_post_contact_rest_cap_can_reintroduce_penetration(self):
        # A support path imported inside the collision surface; final contact
        # projects x to .6 cm, while the former .15 cm rest cap pulls it back.
        rest, contact_position = (0, 0, 0), (0.6, 0, 0)
        delta = sub(contact_position, rest)
        capped = add(rest, mul(delta, min(1, 0.15 / norm(delta))))
        self.assertAlmostEqual(contact_position[0] - 0.6, 0)
        self.assertAlmostEqual(0.6 - capped[0], 0.45)

    def test_native_velocity_reconstruction_already_contains_support(self):
        old, velocity, rest, dt = (0, 1, 0), (1, 0, 0), (1, 0, 0), 0.1
        predicted = add(old, mul(velocity, dt))
        corrected = support(predicted, rest, 0.5, dt, 10)
        reconstructed = mul(sub(corrected, old), 1 / dt)
        expected = add(velocity, mul(sub(corrected, predicted), 1 / dt))
        self.assertVectorAlmostEqual(reconstructed, expected)
        double_counted = add(reconstructed, mul(sub(corrected, predicted), 1 / dt))
        self.assertGreater(norm(sub(double_counted, expected)), 1)

    def test_exponential_position_support_is_not_inertial_timestep_invariant(self):
        # For x'=k(x+v*h), v'=(x'-x)/h, the update matrix has eigenvalues
        # magnitude sqrt(k), angle acos(sqrt(k)). Its oscillation frequency
        # changes with h, despite exact composition for a frozen input point.
        def angular_frequency(dt):
            keep = (1 - 0.01) ** (120 * dt)
            return math.acos(math.sqrt(keep)) / dt
        low, high = angular_frequency(1 / 300), angular_frequency(1 / 600)
        self.assertGreater(high / low, 1.4)

    def test_prediction_graph_contract_and_no_shared_memory_mutation(self):
        root = Path(__file__).resolve().parents[1]
        header = (root / 'Source/VamResourceBrowser/Private/VamHairPredictionSupport.h').read_text()
        self.assertIn('FName(TEXT("AdvectNodePosition"))', header)
        self.assertIn('Destination->PinName != FName(TEXT("Particles.NodePosition"))', header)
        self.assertIn('BindMapInput(TEXT("Transient.IsPositionMobile"), TEXT("Mobile"))', header)
        self.assertIn('PositionOut->MakeLinkTo(Current)', header)
        self.assertIn('Result->MakeLinkTo(Destination)', header)
        self.assertNotIn('SharedNodePosition', header)
        self.assertNotIn('GroupMemoryBarrier', header)


if __name__ == '__main__':
    unittest.main()
