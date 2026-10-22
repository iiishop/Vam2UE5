"""Prepare measured VaM head motion for vam.HairCapture (no Euler pose matching).

The A1 audit supplies final bone order, hierarchy and bind matrices. A hair-only
runtime may reuse that order only when both verified bundles identify the same
base body. This script prepares inputs; actual UE head matrices must still be
checked after playback because the production rig decides which bones it drives.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation, Slerp


P = np.array([[0., 0., 1.], [1., 0., 0.], [0., 1., 0.]])


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def matrix(values):
    result = np.asarray(values, dtype=float).reshape(4, 4)
    if not np.all(np.isfinite(result)) or not np.allclose(result[3], [0, 0, 0, 1], atol=1e-6):
        raise ValueError("Invalid column-vector affine matrix")
    return result


def unity_to_ue(values):
    """Unity column-vector metres -> UE column-vector centimetres."""
    source = matrix(values)
    result = np.eye(4)
    result[:3, :3] = P @ source[:3, :3] @ P.T
    result[:3, 3] = 100. * P @ source[:3, 3]
    return result


def row_major(values):
    """UE FMatrix uses row vectors: transpose at the serialization boundary."""
    return np.asarray(values).T.ravel().tolist()


def interpolate(times, transforms, query):
    transforms = np.asarray(transforms)
    scales = np.linalg.norm(transforms[:, :3, :3], axis=1)
    if np.max(np.abs(scales - 1)) > 1e-5:
        raise ValueError("Measured head contains scale; rigid replay is insufficient")
    rotations = Rotation.from_matrix(transforms[:, :3, :3])
    result = np.tile(np.eye(4), (len(query), 1, 1))
    result[:, :3, :3] = Slerp(times, rotations)(query).as_matrix()
    for axis in range(3):
        result[:, axis, 3] = np.interp(query, times, transforms[:, axis, 3])
    return result


def validate_identity(manifest, bundle, audit_bundle=None):
    if bundle.get("status") != "committed" or not bundle.get("independent_reload_verified"):
        raise ValueError("Target runtime bundle has not passed independent reload")
    audit_id = manifest["configuration"]["BuildIdentity"]
    if bundle["identity"] == audit_id:
        return "same runtime identity"
    if bundle.get("base_identity") == audit_id:
        # A1 can come directly from the verified body configuration. Keep both
        # its identity and actual configuration path; never invent hair-bundle
        # metadata for that base just to satisfy the sibling-hair branch below.
        def config_path(value):
            if not isinstance(value, str) or not value:
                return None
            package, separator, obj = value.partition(".")
            return package if not separator or obj == package.rsplit("/", 1)[-1] else value
        base_path = config_path(bundle.get("base_configuration"))
        audit_path = config_path(manifest["configuration"].get("path"))
        if not base_path or audit_path != base_path:
            raise ValueError("Direct base identity matches, but actual audit configuration path differs")
        return "hair runtime explicitly references the audited base identity and configuration"
    if not audit_bundle or audit_bundle["identity"] != audit_id:
        raise ValueError("Different runtime: supply the exact audit's --audit-bundle")
    if audit_bundle.get("status") != "committed" or not audit_bundle.get("independent_reload_verified"):
        raise ValueError("Audit bundle has not passed independent reload")
    for key in ("base_identity", "base_configuration"):
        if not bundle.get(key) or bundle[key] != audit_bundle.get(key):
            raise ValueError(f"Hair runtimes do not share {key}; export a new A1 audit")
    return "hair-only runtime variants share explicit base identity and configuration"


def prepare(dynamic_path, body_path, audit_path, bundle_path, audit_bundle_path=None, camera_view=2):
    source = read(dynamic_path)
    body = read(body_path)
    manifest_path = audit_path / "manifest.json"
    inputs_path = audit_path / "01-final-weights/inputs.json"
    manifest, inputs, bundle = read(manifest_path), read(inputs_path), read(bundle_path)
    identity_proof = validate_identity(manifest, bundle, read(audit_bundle_path) if audit_bundle_path else None)
    bones = inputs["bones"]
    names = [b["name"] for b in bones]
    if len(names) != len(set(names)) or "head" not in names:
        raise ValueError("Ambiguous or missing final head bone")
    source_bones = {b["id"]: b for b in body["bones"]}
    if len(source_bones) != len(body["bones"]):
        raise ValueError("Duplicate source bone IDs")
    head_index = names.index("head")
    neck_name = names[bones[head_index]["parent"]]
    if neck_name not in source_bones:
        raise ValueError("Source head parent is unavailable")
    times = np.asarray([float(s["time"]) for s in source])
    if len(source) < 3 or abs(times[0]) > 1e-6 or np.any(np.diff(times) <= 0):
        raise ValueError("Require monotonically timed samples beginning at zero")
    source_world = np.asarray([matrix(s["head_world"]) for s in source])
    source_local = np.asarray([matrix(s["head_local"]) for s in source])
    parent = matrix(source_bones[neck_name]["worldMatrix"])
    parent_errors = source_world @ np.linalg.inv(source_local) - parent
    max_parent_cm = float(100 * np.max(np.linalg.norm(parent_errors[:, :3, 3], axis=1)))
    if max_parent_cm > .001 or np.max(np.abs(parent_errors[:, :3, :3])) > 1e-5:
        raise ValueError("Head parent moved: static-body replay would not be the same motion")
    skin_world = matrix(body["skin_world"])
    world_to_skin = np.linalg.inv(skin_world)
    base_head = matrix(source_bones["head"]["worldMatrix"])
    if np.max(np.abs(base_head - source_world[0])) > 1e-5:
        raise ValueError("Static body snapshot does not match the dynamic initial head")
    bind = [np.linalg.inv(np.asarray(b["inverse_bind_matrix_row_major"]).reshape(4, 4).T) for b in bones]
    base = []
    descendants = []
    inferred = []
    for index, bone in enumerate(bones):
        p = int(bone["parent"])
        if p >= index or p < -1:
            raise ValueError("Final bone hierarchy is not parent-first")
        descendant = index == head_index or (p >= 0 and descendants[p])
        descendants.append(descendant)
        if bone["name"] in source_bones:
            transform = unity_to_ue(world_to_skin @ matrix(source_bones[bone["name"]]["worldMatrix"]))
        else:
            if p < 0:
                raise ValueError("Cannot infer an extra root")
            transform = base[p] @ np.linalg.inv(bind[p]) @ bind[index]
            inferred.append(bone["name"])
        base.append(transform)
    base = np.asarray(base)
    ue_head = [unity_to_ue(world_to_skin @ m) for m in source_world]
    # Fixed 30 Hz required by the existing capture, which performs 60 Hz half steps.
    query = np.arange(int(np.floor(times[-1] * 30)) + 1) / 30.
    heads = interpolate(times, ue_head, query)
    inv_head = np.linalg.inv(base[head_index])
    camera_index = next(i for i in range(len(source)) if i % 3 == camera_view)
    camera = unity_to_ue(world_to_skin @ matrix(source[camera_index]["camera_world"]))
    frames = []
    for t, head in zip(query, heads):
        pose = base.copy()
        pose[descendants] = (head @ inv_head) @ base[descendants]
        phase_index = max(int(np.searchsorted(times, t, side="right")) - 1, 0)
        frames.append(dict(time=float(t), phase=source[phase_index]["phase"], actor=row_major(np.eye(4)),
                           camera=row_major(camera), expected_head_world=row_major(head),
                           bones=[row_major(m) for m in pose]))
    def light(pitch, yaw, intensity):
        a, b = np.radians([pitch, yaw])
        direction = np.array([np.sin(b)*np.cos(a), -np.sin(a), np.cos(b)*np.cos(a)])
        return dict(world_direction=(P @ world_to_skin[:3, :3] @ direction).tolist(),
                    source_intensity=intensity, source_engine="Unity")
    paths = [dynamic_path, body_path, manifest_path, inputs_path, bundle_path]
    if audit_bundle_path:
        paths.append(audit_bundle_path)
    files = {str(p.resolve()): digest(p) for p in paths}
    skeleton = [dict(name=b["name"], parent=b["parent"], inverse_bind_matrix_row_major=b["inverse_bind_matrix_row_major"]) for b in bones]
    skeleton_digest = hashlib.sha256(json.dumps(skeleton, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    result = dict(schema="vam-hair-dynamic-replay/1", fps=30, simulation_dt=1/60,
                  protocol="Measured VaM head world matrices, rigid SLERP at 30 Hz; static measured body; head descendants retain measured local poses. No UI Euler matching.",
                  target_front=(P @ world_to_skin[:3, :3] @ np.array([0., 0., 1.])).tolist(),
                  configuration=bundle["configuration"], runtime_identity=bundle["identity"],
                  audit_identity=manifest["configuration"]["BuildIdentity"], identity_proof=identity_proof,
                  bone_names=names, bone_parents=[b["parent"] for b in bones], head_bone_index=head_index,
                  skeleton_digest=skeleton_digest,
                  use_recorded_camera=True, vertical_fov=40, capture_width=640, capture_height=640,
                  source_camera_view=camera_view, key_light=light(35, 130, .8), fill_light=light(20, -40, .35),
                  key_direction_ue=light(35, 130, .8)["world_direction"],
                  fill_direction_ue=light(20, -40, .35)["world_direction"],
                  source_preset=body["preset"], source_files=files,
                  validation=dict(source_sample_count=len(source), source_duration_seconds=float(times[-1]),
                                  replay_last_time_seconds=float(query[-1]), replay_frame_count=len(query),
                                  source_max_sample_gap_seconds=float(np.max(np.diff(times))),
                                  head_parent_max_position_error_cm=max_parent_cm,
                                  head_parent_max_rotation_matrix_error=float(np.max(np.abs(parent_errors[:, :3, :3]))),
                                  auxiliary_bones_inferred_from_final_bind=inferred,
                                  all_non_head_source_bones_static=True,
                                  runtime_matrix_verification_required=True),
                  limitations=["VaM was measured at approximately 15 Hz; resampling cannot recover unrecorded high-frequency head acceleration.",
                               "Existing UE HairCapture adds a 3 s preroll; compare motion and recovery relative to measured timestamps, not loading transients.",
                               "Final auxiliary bones and Jiggle are computed by the production runtime; input helper transforms only establish final bone order.",
                               "Unity and UE materials, exposure and light units differ; same geometric camera/light directions do not imply equal photometry."],
                  source_head_samples=[dict(time=float(t), world_matrix_row_major=row_major(m)) for t, m in zip(times, ue_head)],
                  samples=frames)
    result["replay_identity"] = hashlib.sha256(json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dynamic", type=Path)
    parser.add_argument("body", type=Path)
    parser.add_argument("audit", type=Path)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--audit-bundle", type=Path)
    parser.add_argument("--camera-view", type=int, choices=[0, 1, 2], default=2)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists; preserve historical replay inputs")
    result = prepare(args.dynamic, args.body, args.audit, args.bundle, args.audit_bundle, args.camera_view)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":")), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("replay_identity", "runtime_identity", "identity_proof", "validation")}, indent=2))


if __name__ == "__main__":
    main()
