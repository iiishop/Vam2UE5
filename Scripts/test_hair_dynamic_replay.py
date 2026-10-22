"""Numerical replay preparation checks; these do not claim UE runtime success."""
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from scipy.spatial.transform import Rotation

from prepare_hair_dynamic_replay import interpolate, prepare, unity_to_ue, row_major, validate_identity


class HairDynamicReplayTests(unittest.TestCase):
    def test_axes_units_and_serialization(self):
        source = np.eye(4)
        source[:3, 3] = [1, 2, 3]
        result = unity_to_ue(source)
        np.testing.assert_array_equal(result[:3, 3], [300, 100, 200])
        np.testing.assert_array_equal(np.asarray(row_major(result)).reshape(4, 4)[3], [300, 100, 200, 1])

    def test_slerp_takes_short_arc(self):
        matrices = np.tile(np.eye(4), (2, 1, 1))
        matrices[:, :3, :3] = Rotation.from_euler("z", [179, -179], degrees=True).as_matrix()
        middle = interpolate([0, 1], matrices, [.5])[0]
        np.testing.assert_allclose(middle[:3, :3], Rotation.from_euler("z", 180, degrees=True).as_matrix(), atol=1e-10)

    def test_scale_rejected(self):
        matrices = np.tile(np.eye(4), (2, 1, 1))
        matrices[1, 0, 0] = 1.1
        with self.assertRaisesRegex(ValueError, "scale"):
            interpolate([0, 1], matrices, [.5])

    def test_identity_mismatch_rejected(self):
        manifest = {"configuration": {"BuildIdentity": "audit"}}
        bundle = dict(identity="target", status="committed", independent_reload_verified=True,
                      base_identity="body1", base_configuration="/body1")
        audit_bundle = dict(bundle, identity="audit", base_identity="body2")
        with self.assertRaisesRegex(ValueError, "base_identity"):
            validate_identity(manifest, bundle, audit_bundle)

    def test_shared_body_identity(self):
        manifest = {"configuration": {"BuildIdentity": "audit"}}
        bundle = dict(identity="target", status="committed", independent_reload_verified=True,
                      base_identity="body1", base_configuration="/body1")
        self.assertIn("hair-only", validate_identity(manifest, bundle, dict(bundle, identity="audit")))

    def test_direct_base_identity_and_actual_object_path(self):
        manifest = {"configuration": {"BuildIdentity": "body", "path": "/Game/M/RC_Runtime.RC_Runtime"}}
        bundle = dict(identity="hair", status="committed", independent_reload_verified=True,
                      base_identity="body", base_configuration="/Game/M/RC_Runtime")
        self.assertIn("audited base", validate_identity(manifest, bundle))

    def test_direct_base_requires_configuration_path(self):
        bundle = dict(identity="hair", status="committed", independent_reload_verified=True,
                      base_identity="body", base_configuration="/Game/M/RC_Runtime")
        for path in (None, "", "/Game/Other/RC_Runtime", "/Game/M/RC_Runtime.OtherObject"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "actual audit configuration"):
                validate_identity({"configuration": {"BuildIdentity": "body", "path": path}}, bundle)

    def test_direct_base_still_requires_independent_reload(self):
        manifest = {"configuration": {"BuildIdentity": "body", "path": "/Game/M/RC_Runtime"}}
        bundle = dict(identity="hair", status="saved_pending_reload", independent_reload_verified=False,
                      base_identity="body", base_configuration="/Game/M/RC_Runtime")
        with self.assertRaisesRegex(ValueError, "independent reload"):
            validate_identity(manifest, bundle)

    def test_whole_chain_and_head_descendants(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "01-final-weights").mkdir()
            neck, head, eye = [np.eye(4) for _ in range(3)]
            neck[:3, 3] = [0, 1.5, 0]
            head[:3, 3] = [0, 1.6, 0]
            eye[:3, 3] = [.03, 1.62, .08]
            names, transforms = ["neck", "head", "eye"], [neck, head, eye]
            body = dict(preset="test", skin_world=np.eye(4).ravel().tolist(),
                        bones=[dict(id=n, worldMatrix=m.ravel().tolist()) for n, m in zip(names, transforms)])
            bones = [dict(name=n, parent=i-1, inverse_bind_matrix_row_major=row_major(np.linalg.inv(unity_to_ue(m))))
                     for i, (n, m) in enumerate(zip(names, transforms))]
            dynamic = []
            for t in [0, .5, 1]:
                m = head.copy()
                m[:3, :3] = Rotation.from_euler("y", 60*t, degrees=True).as_matrix()
                dynamic.append(dict(time=t, phase="motion", head_world=m.ravel().tolist(),
                                    head_local=(np.linalg.inv(neck)@m).ravel().tolist(), camera_world=np.eye(4).ravel().tolist()))
            data = {"body.json": body, "dynamic.json": dynamic,
                    "manifest.json": {"configuration": {"BuildIdentity": "test"}},
                    "bundle.json": dict(identity="test", status="committed", independent_reload_verified=True, configuration="/test"),
                    "01-final-weights/inputs.json": dict(bones=bones)}
            for file, contents in data.items():
                (root/file).write_text(json.dumps(contents))
            result = prepare(root/"dynamic.json", root/"body.json", root, root/"bundle.json")
            self.assertEqual(len(result["samples"]), 31)
            for sample in result["samples"]:
                pose = np.asarray(sample["bones"]).reshape(-1, 4, 4).transpose(0, 2, 1)
                np.testing.assert_allclose(pose[0], unity_to_ue(neck), atol=1e-12)
                np.testing.assert_allclose(np.linalg.inv(pose[1])@pose[2], np.linalg.inv(unity_to_ue(head))@unity_to_ue(eye), atol=1e-12)
                np.testing.assert_allclose(np.asarray(sample["expected_head_world"]).reshape(4, 4).T, pose[1], atol=1e-12)


if __name__ == "__main__":
    unittest.main()
