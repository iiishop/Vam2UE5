"""Compact measured HairDynamicRepair candidates; no solver/skin pass inference.

Example::
  python Scripts/summarize_hair_dynamic_candidates.py --case baseline=Saved/HairDynamicRepair/baseline-ponytail/metrics.json --source baseline=Saved/HairGeneric/ponytail/source-v5.json --output Saved/HairDynamicRepair/comparison-01

Rest-displacement occupancy is measured on the interpolated guide buffer. A band
near the legacy limit is evidence of a geometric plateau, not proof that every
raw Niagara particle was clamped. Candidate source JSON identifies whether that
legacy cap was requested; omitting it leaves cap activation UNKNOWN.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re

import numpy as np


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def value(group, field, stat):
    return (group.get(field) or {}).get(stat)


def cap_occupancy(capture, metrics, phase, group_id, source_group=None):
    group_metrics = next(g for g in metrics["phases"][phase] if g["group"] == group_id)
    first, last = group_metrics["first_time"], group_metrics["last_time"]
    solver = group_metrics["parameters"].get("solver", "")
    match = re.search(r"NS_Style_(compact|supported|loose)_", solver)
    kind = match.group(1) if match else None
    limit = {"compact":.25, "supported":.15, "loose":10.}.get(kind)
    enabled = "UNKNOWN"
    if source_group is not None:
        source_kind = source_group.get("physics_policy", {}).get("kind")
        if source_kind is not None and kind is not None and source_kind != kind:
            raise ValueError(f"Source/captured solver class mismatch: {source_kind} vs {kind}")
        enabled = "DISABLED" if source_group.get("source_constraints", {}).get("source_owns_shape") else "ENABLED"
    result = dict(legacy_cap_class=kind, legacy_cap_reference_cm=limit,
                  legacy_cap_requested=enabled, free_point_samples=0,
                  fraction_at_or_beyond_95pct_legacy_cap=None,
                  fraction_within_1pct_legacy_cap_band=None,
                  max_frame_fraction_within_1pct_legacy_cap_band=None)
    if limit is None:
        return result
    count = beyond = band = 0
    frame_fractions = []
    tolerance = max(.01, limit * .01)
    for frame in capture["gpu_samples"]:
        if not first - 1e-7 <= frame["time"] <= last + 1e-7:
            continue
        group = next(g for g in frame["groups"] if g["group"] == group_id)
        points, rest = np.asarray(group["guide_points"], float), np.asarray(group["guide_rest_points"], float)
        if points.shape != rest.shape or not np.isfinite(points).all() or not np.isfinite(rest).all():
            raise ValueError("Invalid recorded guide geometry")
        curves = int(group["guide_curves"])
        # Mirror the audit's actual exclusion count, never invent a looser mask.
        excluded = group_metrics["collision_root_prefix_excluded_points"]
        if len(points) % curves or excluded % curves:
            raise ValueError("Variable guide layout needs explicit per-curve root exclusion")
        nodes, skip = len(points) // curves, excluded // curves
        distance = np.linalg.norm((points - rest).reshape(curves, nodes, 3)[:, skip:], axis=2).ravel()
        near = np.abs(distance - limit) <= tolerance
        count += len(distance)
        beyond += int((distance >= .95 * limit).sum())
        band += int(near.sum())
        frame_fractions.append(float(near.mean()))
    if count:
        result.update(free_point_samples=count,
            fraction_at_or_beyond_95pct_legacy_cap=beyond/count,
            fraction_within_1pct_legacy_cap_band=band/count,
            max_frame_fraction_within_1pct_legacy_cap_band=max(frame_fractions))
    return result


def summarize_case(name, metric_path, source_path=None):
    metrics = read(metric_path)
    capture_path = Path(metrics["capture"])
    if sha(capture_path) != metrics["capture_sha256"]:
        raise ValueError(f"{name}: capture hash no longer matches metrics")
    capture = read(capture_path)
    replay_path = Path(metrics["replay_file"]) if metrics.get("replay_file") else None
    if replay_path and sha(replay_path) != metrics["replay_sha256"]:
        raise ValueError(f"{name}: replay hash no longer matches metrics")
    replay = read(replay_path) if replay_path else None
    source = read(source_path) if source_path else None
    head = metrics.get("head_replay_validation", {})
    complete = capture["frames"] == len(replay["samples"]) if replay else None
    rows = []
    for phase, groups in metrics["phases"].items():
        for group in groups:
            group_id = group["group"]
            row = dict(case=name, phase=phase, group=group_id,
                source_group=(source["groups"][group_id].get("name") if source else None),
                driver_status=head.get("status", "UNVERIFIED"), complete_against_replay=complete,
                geometry_samples=group["geometry_samples"], guide_curves=group["guide_curves"],
                tip_excursion_max_cm=value(group,"tip_excursion_from_phase_first_cm","max"),
                tip_excursion_p95_cm=value(group,"tip_excursion_from_phase_first_cm","p95"),
                rest_displacement_rms_cm=value(group,"guide_displacement_from_authored_rest_cm","rms"),
                rest_displacement_p95_cm=value(group,"guide_displacement_from_authored_rest_cm","p95"),
                rest_displacement_max_cm=value(group,"guide_displacement_from_authored_rest_cm","max"),
                polyline_length_ratio_min=value(group,"polyline_length_ratio_to_rest","min"),
                polyline_length_ratio_p95=value(group,"polyline_length_ratio_to_rest","p95"),
                polyline_length_ratio_max=value(group,"polyline_length_ratio_to_rest","max"),
                sampled_tip_speed_p95_cm_s=value(group,"sampled_tip_speed_cm_s","p95"),
                sampled_tip_speed_final_median_cm_s=group.get("sampled_tip_speed_final_median_cm_s"),
                root_displacement_max_cm=value(group,"root_displacement_from_authored_rest_cm","max"))
            for region in ("head", "body"):
                proxy = group["proxy_penetration"][region]
                row[region+"_proxy_max_depth_cm"] = (proxy["depth_cm"] or {}).get("max")
                row[region+"_proxy_max_fraction_over_threshold"] = proxy["max_fraction_over_threshold"]
                row[region+"_proxy_threshold_cm"] = proxy["threshold_cm"]
            row.update(cap_occupancy(capture, metrics, phase, group_id, source["groups"][group_id] if source else None))
            rows.append(row)
    provenance = dict(metrics=str(metric_path.resolve()), metrics_sha256=sha(metric_path),
        capture=str(capture_path), capture_sha256=metrics["capture_sha256"],
        replay=str(replay_path) if replay_path else None, replay_sha256=metrics.get("replay_sha256"),
        actor=capture.get("actor"), head_status=head.get("status", "UNVERIFIED"),
        frames=capture["frames"], complete_against_replay=complete,
        source=str(source_path.resolve()) if source_path else None, source_sha256=sha(source_path) if source_path else None)
    return rows, provenance


def named_paths(values):
    result = {}
    for item in values:
        name, path = item.split("=", 1)
        if not name or name in result:
            raise ValueError("Empty or duplicate case name")
        result[name] = Path(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--case", action="append", required=True, help="NAME=metrics.json; only completed captures")
    parser.add_argument("--source", action="append", default=[], help="NAME=source.json; records whether legacy shape cap was requested")
    parser.add_argument("--output", type=Path, required=True, help="New output directory; preserves earlier comparison reports")
    args = parser.parse_args()
    cases, sources = named_paths(args.case), named_paths(args.source)
    if set(sources) - set(cases):
        parser.error("Each --source must name a provided --case")
    rows, provenance = [], {}
    for name, path in cases.items():
        case_rows, provenance[name] = summarize_case(name, path, sources.get(name))
        rows.extend(case_rows)
    result = dict(schema="vam-hair-dynamic-candidate-summary/1", cases=provenance, rows=rows,
        limitations=["Guide polyline statistics describe interpolated Groom output, not raw Niagara constraints.",
            "Rest displacement is relative to imported authored geometry, not the settled reference.",
            "Cap occupancy is a geometric band statistic; disabled-cap candidates retain the same reference band for comparison.",
            "Head/body numbers describe collision proxies; root prefix is excluded and actual skin penetration is not measured here.",
            "Head PASS verifies the driver only. Excursion size does not independently establish physical realism or success.",
            "Recovered tip speed uses sampled finite differences and the final five intervals of each phase; no motion between geometry samples can be reconstructed.",
            "No VaM/UE pointwise correspondence or per-group index correspondence is assumed."])
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/"comparison.json").write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False),encoding="utf-8")
    with (args.output/"comparison.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    print(json.dumps(dict(output=str(args.output),cases=len(cases),rows=len(rows)),ensure_ascii=False))


if __name__ == "__main__":
    main()
