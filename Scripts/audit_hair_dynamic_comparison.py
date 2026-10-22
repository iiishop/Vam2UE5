"""Audit HairCapture GPU geometry in Groom/head space, without inventing correspondence.

Example::

    python audit_hair_dynamic_comparison.py capture.json metrics.json \
        --phase settle:0:8 --phase motion:8:18 --phase recovery:18:24 \
        --vam-metrics Evidence/HairDynamicReference/ponytail/metrics.json

The recorded guide buffer contains *interpolated Groom control points*, not raw
Niagara solver particles. VamHairCaptureAudit adds each resource's position offset;
The runtime NiagaraDataInterfaceHairStrandsTemplate.ush::UpdatePointPosition
interpolates node displacements and UpdateDeformedPositions writes the Groom
component-local guide buffer. NiagaraInterpolateGroomGuides.usf is the separate
SimCache readback path and is not evidence of this live simulation's ordering.
VamHairComponent rigidly attaches the component to head.
Do not inverse-transform these points by the head again. Both collision proxies
and strand points are already in that same component space, in centimetres.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np


LIMITS = [
    "Head-local means the current rigid head-attached Groom component. Legacy captures do not export its world matrix; an independently measured bone-to-Groom transform error cannot be recovered.",
    "Guide points are GPU-interpolated Groom control points, not Niagara simulation particles. Polyline length change is a geometric diagnostic, not a solver constraint residual.",
    "Geometry samples may be only once per second. Their finite-difference speeds and extrema miss motion between samples; dense temporal summaries are separately labelled.",
    "Collision results measure the captured sphere/capsule proxy union, not the skin surface. Roots/prefix are excluded; rendered strands are a deterministic subset. Captured radii assume unit component scale.",
    "Rest points are imported authored geometry, not a settled physical equilibrium. First-sample excursion and rest displacement answer different questions.",
    "VaM reference summaries are side-by-side observations only: different topology, guide selection, phase timing or movement invalidate pointwise RMS and direct solver-quality rankings.",
    "Capture GPU readback blocks rendering. Recorded simulation/video rates and this report are not a performance benchmark.",
]


def stats(values):
    values = np.asarray(values, dtype=float).ravel()
    if not values.size:
        return None
    if not np.isfinite(values).all():
        raise ValueError("Nonfinite measurement")
    return dict(count=int(values.size), rms=float(np.sqrt(np.mean(values * values))),
                median=float(np.median(values)), p95=float(np.percentile(values, 95)),
                max=float(values.max()), min=float(values.min()))


def positions(group, key):
    result = np.asarray(group.get(key, []), dtype=float)
    if result.ndim != 2 or result.shape[1] != 3 or not result.size:
        raise ValueError(f"Group {group.get('group')}: missing/invalid {key}")
    if not np.isfinite(result).all():
        raise ValueError(f"Group {group.get('group')}: nonfinite {key}")
    return result


def layout(group, default_nodes):
    """Use explicit counts when present; otherwise enforce known builder layout."""
    count = len(group["guide_points"])
    curves = int(group["guide_curves"])
    counts = group.get("guide_curve_counts")
    if counts is None:
        if curves * default_nodes != count:
            raise ValueError(f"Group {group['group']}: expected {default_nodes} points/guide; "
                             "supply actual --guide-nodes or export guide_curve_counts")
        counts = [default_nodes] * curves
    if len(counts) != curves or sum(counts) != count or any(n < 2 for n in counts):
        raise ValueError("Invalid guide curve layout")
    offsets = np.r_[0, np.cumsum(counts)]
    return [slice(int(a), int(b)) for a, b in zip(offsets[:-1], offsets[1:])]


def proxy_depth(points, proxies):
    """Maximum penetration into union, one value per point; memory bounded."""
    if not proxies or not len(points):
        return None
    depth = np.full(len(points), -np.inf)
    for proxy in proxies:
        a, b = np.asarray(proxy["a"], float), np.asarray(proxy["b"], float)
        radius = float(proxy["radius_cm"])
        if not np.isfinite(np.r_[a, b, radius]).all() or radius < 0:
            raise ValueError("Invalid captured collision proxy")
        edge = b - a
        t = np.clip((points - a) @ edge / max(float(edge @ edge), 1e-20), 0, 1)
        depth = np.maximum(depth, radius - np.linalg.norm(points - a - t[:, None] * edge, axis=1))
    return np.maximum(depth, 0)


def phase_windows(frames, requested):
    if requested:
        result = []
        for specification in requested:
            name, start, end = specification.rsplit(":", 2)
            start, end = float(start), float(end)
            if not name or not np.isfinite([start, end]).all() or start >= end:
                raise ValueError(f"Invalid phase: {specification}")
            result.append((name, start, end))
        if len({x[0] for x in result}) != len(result):
            raise ValueError("Duplicate phase names")
        return result
    if all("phase" in frame for frame in frames):
        # Capture-authored labels are preferred; never infer a settle interval.
        names = list(dict.fromkeys(frame["phase"] for frame in frames))
        return [(name, None, None) for name in names]
    return [("all_recorded", -np.inf, np.inf)]


def ue_matrix(values, label):
    """Serialized UE row-vector FMatrix -> column-vector matrix for analysis."""
    result = np.asarray(values, dtype=float).reshape(4, 4).T
    if not np.isfinite(result).all() or not np.allclose(result[3], [0, 0, 0, 1], atol=1e-6):
        raise ValueError(f"Invalid UE affine matrix: {label}")
    if np.linalg.det(result[:3, :3]) <= 1e-12:
        raise ValueError(f"Degenerate or reflected head matrix: {label}")
    return result


def rigid_rotation(matrix):
    # Isolate rotation from positive FTransform scale; scale difference is also
    # reported, so a scale mismatch cannot be hidden by this normalization.
    u, _, vt = np.linalg.svd(matrix[:3, :3])
    return u @ vt


def matrix_errors(actual, expected):
    delta = rigid_rotation(actual) @ rigid_rotation(expected).T
    cosine = np.clip((np.trace(delta) - 1.) * .5, -1., 1.)
    return dict(position_error_cm=float(np.linalg.norm(actual[:3, 3] - expected[:3, 3])),
                rotation_error_deg=float(np.degrees(np.arccos(cosine))),
                scale_error_max=float(np.max(np.abs(np.linalg.norm(actual[:3, :3], axis=0)
                                                   - np.linalg.norm(expected[:3, :3], axis=0)))))


def expected_head_matrix(replay, index):
    sample = replay["samples"][index]
    if "expected_head_world" in sample:
        return ue_matrix(sample["expected_head_world"], f"replay[{index}].expected_head_world"), "expected_head_world"
    head = int(replay["head_bone_index"])
    if head < 0 or head >= len(sample["bones"]):
        raise ValueError("Invalid replay head_bone_index")
    # bones are component-space. UE row convention is HeadCS * ActorWorld;
    # with the column-vector representation used here this order reverses.
    bone = ue_matrix(sample["bones"][head], f"replay[{index}].bones[{head}]")
    actor = ue_matrix(sample["actor"], f"replay[{index}].actor") if "actor" in sample else np.eye(4)
    return actor @ bone, "bones[head_bone_index] composed with actor"


def compare_replay(capture, replay, position_tolerance=.01, rotation_tolerance=.05):
    """Keep the declared frame match; nearby offsets are diagnostics, never fixes."""
    if float(replay.get("fps", 30)) != 30:
        raise ValueError("HairCapture replay contract requires 30 Hz samples")
    samples = replay.get("samples", [])
    if not samples:
        raise ValueError("Missing replay samples")
    rows, actuals, missing = [], [], []
    for frame_id, frame in enumerate(capture["gpu_samples"]):
        time = float(frame["time"])
        # Existing HairCapture serializes Written / 30, with 60 Hz half steps.
        # No clamping: an out-of-range sample is an identity/timing failure.
        index = int(round(time * 30))
        if index < 0 or index >= len(samples):
            missing.append(dict(geometry_sample=frame_id, time=time, replay_index=index,
                                reason="Replay index out of range"))
            continue
        if "head_world_matrix" not in frame:
            missing.append(dict(geometry_sample=frame_id, time=time, replay_index=index,
                                reason="Capture predates measured head_world_matrix"))
            continue
        actual = ue_matrix(frame["head_world_matrix"], f"capture[{frame_id}].head_world_matrix")
        expected, source = expected_head_matrix(replay, index)
        errors = matrix_errors(actual, expected)
        sample_time = float(samples[index].get("time", index / 30))
        row = dict(geometry_sample=frame_id, time=time, replay_index=index,
                   replay_time=sample_time, time_quantization_error_seconds=time - sample_time,
                   phase=samples[index].get("phase"), expected_source=source, **errors)
        if "groom_world_matrix" in frame:
            groom = ue_matrix(frame["groom_world_matrix"], f"capture[{frame_id}].groom_world_matrix")
            row["groom_vs_head"] = matrix_errors(groom, actual)
        if "head_attachment_error_cm" in frame:
            row["logged_head_attachment_error_cm"] = float(frame["head_attachment_error_cm"])
            if "groom_vs_head" in row:
                row["attachment_log_matrix_difference_cm"] = abs(row["logged_head_attachment_error_cm"]
                    - row["groom_vs_head"]["position_error_cm"])
        row["within_tolerance"] = (errors["position_error_cm"] <= position_tolerance
                                   and errors["rotation_error_deg"] <= rotation_tolerance
                                   and errors["scale_error_max"] <= 1e-4
                                   and abs(time - sample_time) <= 1/60 + 1e-6)
        rows.append(row)
        actuals.append((actual, index))
    result = dict(status="PASS" if rows and not missing and all(r["within_tolerance"] for r in rows) else "FAIL",
                  replay_identity=replay.get("replay_identity"), runtime_identity=replay.get("runtime_identity"),
                  matching_rule="replay_index = round(capture time * 30); no offset correction or clamping",
                  position_tolerance_cm=position_tolerance, rotation_tolerance_deg=rotation_tolerance,
                  scale_tolerance=1e-4, compared_samples=len(rows), missing_samples=missing, rows=rows,
                  position_error_cm=stats([r["position_error_cm"] for r in rows]),
                  rotation_error_deg=stats([r["rotation_error_deg"] for r in rows]),
                  scale_error=stats([r["scale_error_max"] for r in rows]))
    attached = [r["groom_vs_head"] for r in rows if "groom_vs_head" in r]
    attachment_ok = all(r["position_error_cm"] <= position_tolerance
                        and r["rotation_error_deg"] <= rotation_tolerance
                        and r["scale_error_max"] <= 1e-4 for r in attached) and all(
                        r.get("logged_head_attachment_error_cm", 0) <= position_tolerance
                        and r.get("attachment_log_matrix_difference_cm", 0) <= position_tolerance for r in rows)
    result["attachment"] = dict(status=("FAIL" if not attachment_ok else "PASS" if len(attached) == len(rows) and rows else "UNVERIFIED"),
        compared_samples=len(attached),
        position_error_cm=stats([r["position_error_cm"] for r in attached]),
        rotation_error_deg=stats([r["rotation_error_deg"] for r in attached]),
        logged_position_error_cm=stats([r["logged_head_attachment_error_cm"] for r in rows if "logged_head_attachment_error_cm" in r]),
        log_matrix_difference_cm=stats([r["attachment_log_matrix_difference_cm"] for r in rows if "attachment_log_matrix_difference_cm" in r]))
    if not attachment_ok:
        result["status"] = "FAIL"
    result["nearby_frame_offsets_diagnostic_only"] = []
    for offset in (-2, -1, 0, 1, 2):
        errors = [matrix_errors(actual, expected_head_matrix(replay, index + offset)[0])
                  for actual, index in actuals if 0 <= index + offset < len(samples)]
        result["nearby_frame_offsets_diagnostic_only"].append(dict(replay_index_offset=offset,
            compared_samples=len(errors), position_error_cm=stats([r["position_error_cm"] for r in errors]),
            rotation_error_deg=stats([r["rotation_error_deg"] for r in errors])))
    result["interpretation"] = ("Recorded head agrees with declared replay indices within stated tolerances."
        if result["status"] == "PASS" else
        "Head replay or Groom attachment is unverified/mismatched. Possible tick/readback frame phase, hierarchy or pose error. "
        "Inspect per-sample errors and nearby-offset diagnostics; do not rank hair solvers as a same-motion comparison until resolved.")
    return result


def group_metrics(frames, group_id, nodes, root_fraction, penetration_threshold):
    groups = [next(g for g in f["groups"] if g["group"] == group_id) for f in frames]
    if not all(g.get("readback_ready") for g in groups):
        raise ValueError(f"Group {group_id}: incomplete guide GPU readback")
    curves = layout(groups[0], nodes)
    p = np.stack([positions(g, "guide_points") for g in groups])
    rest = np.stack([positions(g, "guide_rest_points") for g in groups])
    if p.shape != rest.shape or any(layout(g, nodes) != curves for g in groups):
        raise ValueError("Guide topology changed during capture")
    rest_drift = float(np.linalg.norm(rest - rest[0], axis=2).max())
    if rest_drift > 1e-5:
        raise ValueError(f"Rest topology/order/geometry changed: {rest_drift:.6g} cm")
    times = np.asarray([f["time"] for f in frames], float)
    roots = np.asarray([c.start for c in curves])
    tips = np.asarray([c.stop - 1 for c in curves])
    free = np.concatenate([np.arange(c.start + min(c.stop - c.start - 1,
                          max(2, int((c.stop - c.start) * root_fraction))), c.stop)
                           for c in curves])
    segment_indices = np.concatenate([np.arange(c.start, c.stop - 1) for c in curves])
    segment_length = np.linalg.norm(p[:, segment_indices + 1] - p[:, segment_indices], axis=2)
    segment_rest = np.linalg.norm(rest[0, segment_indices + 1] - rest[0, segment_indices], axis=1)
    valid = segment_rest > 1e-6
    lengths = np.stack([np.linalg.norm(np.diff(p[:, c], axis=1), axis=2).sum(1) for c in curves], axis=1)
    rest_lengths = np.asarray([np.linalg.norm(np.diff(rest[0, c], axis=0), axis=1).sum() for c in curves])
    if np.any(rest_lengths < 1e-6):
        raise ValueError("Zero-length guide curve")
    tip_positions = p[:, tips]
    row = dict(group=group_id, geometry_samples=len(frames), guide_curves=len(curves),
               guide_points=p.shape[1], all_samples_simulation_enabled=all(g.get("simulation") for g in groups),
               first_time=float(times[0]), last_time=float(times[-1]),
               sampled_dt_seconds=stats(np.diff(times)),
               tip_excursion_from_phase_first_cm=stats(np.linalg.norm(tip_positions - tip_positions[0], axis=2)),
               guide_excursion_from_phase_first_cm=stats(np.linalg.norm(p - p[0], axis=2)),
               guide_displacement_from_authored_rest_cm=stats(np.linalg.norm(p - rest, axis=2)),
               root_displacement_from_authored_rest_cm=stats(np.linalg.norm(p[:, roots] - rest[:, roots], axis=2)),
               root_motion_from_phase_first_cm=stats(np.linalg.norm(p[:, roots] - p[0, roots], axis=2)),
               polyline_length_ratio_to_rest=stats(lengths / rest_lengths),
               polyline_length_abs_change_cm=stats(np.abs(lengths - rest_lengths)),
               segment_abs_relative_length_change=stats(np.abs(segment_length[:, valid] / segment_rest[valid] - 1)),
               degenerate_rest_segments=int((~valid).sum()),
               collision_root_prefix_excluded_points=int(p.shape[1] - len(free)),
               parameters={k:groups[0].get(k) for k in ("solver", "substeps", "iterations", "density", "thickness_cm", "bend_stiffness", "air_drag", "self_collision_viscosity")})
    if len(times) > 1:
        speeds = np.linalg.norm(np.diff(tip_positions, axis=0), axis=2) / np.diff(times)[:, None]
        row["sampled_tip_speed_cm_s"] = stats(speeds)
        row["sampled_tip_speed_final_median_cm_s"] = float(np.median(speeds[-min(5, len(speeds)):]))
    else:
        row["sampled_tip_speed_cm_s"] = None
        row["sampled_tip_speed_final_median_cm_s"] = None
    row["proxy_penetration"] = {}
    for region in ("head", "body"):
        depths, counts, fractions, missing = [], [], [], 0
        for i, frame in enumerate(frames):
            proxies = [x for x in frame.get("collision_proxies_head_local", [])
                       if (x["bone"] == "head") == (region == "head")]
            depth = proxy_depth(p[i, free], proxies)
            if depth is None:
                missing += 1
                continue
            depths.append(depth)
            counts.append(int((depth > penetration_threshold).sum()))
            fractions.append(float((depth > penetration_threshold).mean()))
        row["proxy_penetration"][region] = dict(samples_missing_proxies=missing,
            depth_cm=stats(np.concatenate(depths)) if depths else None,
            max_points_over_threshold=max(counts) if counts else None,
            max_fraction_over_threshold=max(fractions) if fractions else None,
            threshold_cm=penetration_threshold)
    # Render IDs are sparse; only stable matching IDs justify a temporal comparison.
    render_maps = []
    for g in groups:
        if not g.get("render_readback_ready"):
            break
        rp = positions(g, "render_points")
        ids = g.get("render_point_ids", [])
        if len(ids) != len(rp) or len(set(ids)) != len(ids):
            raise ValueError("Invalid deterministic render point IDs")
        render_maps.append(dict(zip(ids, rp)))
    if len(render_maps) == len(groups):
        ids = sorted(set.intersection(*(set(m) for m in render_maps)))
        rp = np.asarray([[m[i] for i in ids] for m in render_maps])
        row["render_matched_points"] = len(ids)
        row["render_excursion_from_phase_first_cm"] = stats(np.linalg.norm(rp - rp[0], axis=2)) if ids else None
    else:
        row["render_matched_points"] = 0
        row["render_excursion_from_phase_first_cm"] = None
    return row


def audit(capture, phases=None, guide_nodes=32, root_fraction=.1, penetration_threshold=.2):
    frames = capture.get("gpu_samples", [])
    if not frames:
        raise ValueError("Missing GPU geometry samples")
    times = np.asarray([f["time"] for f in frames], float)
    if not np.isfinite(times).all() or np.any(np.diff(times) <= 0):
        raise ValueError("Nonfinite/nonmonotonic capture times")
    ids = [g["group"] for g in frames[0]["groups"]]
    if len(ids) != len(set(ids)) or any(sorted(g["group"] for g in f["groups"]) != sorted(ids) for f in frames):
        raise ValueError("Group identities changed during capture")
    result = dict(schema="vam-ue-hair-dynamic-comparison-v1", coordinate_space="Groom component local / rigid head local, cm",
        actor=capture.get("actor"), hair=capture.get("hair"), world=capture.get("world"),
        simulation_dt=capture.get("simulation_dt"), video_fps=capture.get("video_fps"),
        geometry_samples=len(frames), phase_origin="explicit --phase" if phases else "capture phase labels" if "phase" in frames[0] else "whole recording (no inferred phase)",
        layout_fallback_points_per_guide=guide_nodes, limitations=LIMITS, phases={})
    for name, start, end in phase_windows(frames, phases):
        selected = [f for f in frames if f.get("phase") == name] if start is None else [f for f in frames if start <= f["time"] < end]
        if not selected:
            raise ValueError(f"No geometry samples in phase {name}")
        result["phases"][name] = [group_metrics(selected, i, guide_nodes, root_fraction, penetration_threshold) for i in ids]
    dense = capture.get("temporal_samples", [])
    result["dense_temporal"] = {"intervals":len(dense), "note":"All-guide/render RMS and max step only; no tip positions or collision geometry in these rows.", "groups":{}}
    for i in ids:
        for kind in ("guide", "render"):
            rows = [g for f in dense for g in f["groups"] if g["group"] == i and f"{kind}_max_step_cm" in g]
            if rows:
                result["dense_temporal"]["groups"][f"{i}_{kind}"] = dict(
                    max_step_cm=max(float(g[f"{kind}_max_step_cm"]) for g in rows),
                    rms_step_cm=stats([g[f"{kind}_rms_step_cm"] for g in rows]))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--phase", action="append", help="Explicit NAME:START:END seconds, end exclusive; no default guessed motion phases")
    parser.add_argument("--guide-nodes", type=int, default=32, help="Known uniform imported guide layout; reject mismatched counts")
    parser.add_argument("--root-skip-fraction", type=float, default=.1)
    parser.add_argument("--penetration-threshold", type=float, default=.2)
    parser.add_argument("--vam-metrics", type=Path, action="append", default=[], help="Existing native VaM metrics.json to include as unpaired references")
    parser.add_argument("--replay", type=Path, help="Measured 30 Hz replay JSON; validate actual captured head matrices at round(time*30)")
    parser.add_argument("--replay-position-tolerance", type=float, default=.01, help="Head position error tolerance in cm")
    parser.add_argument("--replay-rotation-tolerance", type=float, default=.05, help="Head rotation error tolerance in degrees")
    args = parser.parse_args()
    if args.guide_nodes < 2 or not 0 <= args.root_skip_fraction < 1 or args.penetration_threshold < 0:
        parser.error("Invalid guide layout, root fraction, or penetration threshold")
    if args.replay_position_tolerance < 0 or args.replay_rotation_tolerance < 0:
        parser.error("Replay tolerances must be nonnegative")
    data = args.capture.read_bytes()
    capture = json.loads(data)
    replay_result = None
    if args.replay:
        replay = json.loads(args.replay.read_text(encoding="utf-8-sig"))
        replay_result = compare_replay(capture, replay, args.replay_position_tolerance, args.replay_rotation_tolerance)
        # Use measured protocol phase labels, rather than guessing times from video.
        # The validation status is still FAIL when a head matrix does not agree.
        if not args.phase:
            for frame in capture["gpu_samples"]:
                index = int(round(float(frame["time"]) * 30))
                if 0 <= index < len(replay["samples"]) and "phase" in replay["samples"][index]:
                    frame["phase"] = replay["samples"][index]["phase"]
    result = audit(capture, args.phase, args.guide_nodes, args.root_skip_fraction, args.penetration_threshold)
    if replay_result is not None:
        result["head_replay_validation"] = replay_result
        result["replay_file"] = str(args.replay.resolve())
        result["replay_sha256"] = hashlib.sha256(args.replay.read_bytes()).hexdigest()
        if not args.phase:
            result["phase_origin"] = "replay protocol labels at round(capture time * 30)"
    result["capture"] = str(args.capture.resolve())
    result["capture_sha256"] = hashlib.sha256(data).hexdigest()
    result["vam_references_unpaired"] = [dict(path=str(p.resolve()), metrics=json.loads(p.read_text(encoding="utf-8-sig"))) for p in args.vam_metrics]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    compact = {phase:[dict(group=g["group"], tip_max_cm=g["tip_excursion_from_phase_first_cm"]["max"],
        root_max_cm=g["root_displacement_from_authored_rest_cm"]["max"],
        polyline_ratio_min=g["polyline_length_ratio_to_rest"]["min"], polyline_ratio_max=g["polyline_length_ratio_to_rest"]["max"],
        head_proxy_max_cm=(g["proxy_penetration"]["head"]["depth_cm"] or {}).get("max"),
        body_proxy_max_cm=(g["proxy_penetration"]["body"]["depth_cm"] or {}).get("max")) for g in groups]
        for phase, groups in result["phases"].items()}
    print(json.dumps(compact, ensure_ascii=False, indent=2, allow_nan=False))
    if replay_result is not None:
        print(json.dumps({k:replay_result[k] for k in ("status", "compared_samples", "position_error_cm", "rotation_error_deg", "attachment")}, indent=2, allow_nan=False))
        if replay_result["status"] != "PASS":
            print(replay_result["interpretation"], file=sys.stderr)
            raise SystemExit(2)


if __name__ == "__main__":
    main()
