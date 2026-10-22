"""Invert UE guide-displacement interpolation for a selected captured guide.

This is an estimate from packed guide output, not a raw Niagara particle readback.
The displacement fit is independently checked. Absolute node positions assume
the production builder's zero strand smoothing and 32-node native layout.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def interpolation(rows: int, columns: int) -> np.ndarray:
    coordinate = np.linspace(0, columns - 1, rows)
    lower = np.minimum(np.floor(coordinate).astype(int), columns - 2)
    alpha = coordinate - lower
    result = np.zeros((rows, columns))
    result[np.arange(rows), lower] = 1 - alpha
    result[np.arange(rows), lower + 1] = alpha
    return result


def reconstruct(current, rest, tolerance=.02):
    current, rest = np.asarray(current, float), np.asarray(rest, float)
    if current.shape != (32, 3) or rest.shape != current.shape:
        raise ValueError("Expected exactly 32 guide points, each with XYZ")
    # Native UpdatePointPosition: physical k = (point / 31) * 30,
    # then interpolate physical particle slots k+1 (slot 0 is virtual).
    matrix = interpolation(32, 31)
    delta, _, _, _ = np.linalg.lstsq(matrix, current - rest, rcond=None)
    error = np.linalg.norm(rest + matrix @ delta - current, axis=1)
    # Native ComputeNodePosition samples canonical guide rest at k/30.
    # This part requires the builder's StrandsSmoothing=0 contract.
    node_rest = interpolation(31, 32) @ rest
    node_current = node_rest + delta
    rl = np.linalg.norm(np.diff(node_rest, axis=0), axis=1)
    cl = np.linalg.norm(np.diff(node_current, axis=0), axis=1)
    ratios = np.divide(cl, rl, out=np.zeros_like(cl), where=rl > 1e-8)
    order = np.argsort(ratios)[::-1]
    return dict(
        displacement_reprojection_pass=bool(error.max() <= tolerance),
        reprojection_tolerance_cm=tolerance,
        reprojection_rms_cm=float(np.sqrt(np.mean(error**2))),
        reprojection_max_cm=float(error.max()),
        interpolation_condition_number=float(np.linalg.cond(matrix)),
        estimated_physical_node_rest_cm=node_rest.tolist(),
        estimated_physical_node_current_cm=node_current.tolist(),
        estimated_physical_node_displacement_cm=delta.tolist(),
        estimated_total_length_ratio=float(cl.sum() / rl.sum()),
        estimated_total_rest_length_cm=float(rl.sum()),
        estimated_total_current_length_cm=float(cl.sum()),
        estimated_segment_ratio_max=float(ratios.max()),
        estimated_segment_ratio_p95=float(np.percentile(ratios, 95)),
        estimated_worst_segments=[dict(
            physical_segment=int(i), solver_particle_slots=[int(i + 1), int(i + 2)],
            u=[float(i / 30), float((i + 1) / 30)],
            rest_length_cm=float(rl[i]), current_length_cm=float(cl[i]), ratio=float(ratios[i]),
            from_current_cm=node_current[i].tolist(), to_current_cm=node_current[i + 1].tolist(),
        ) for i in order[:8]],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--group", type=int, required=True)
    parser.add_argument("--guide", type=int, required=True)
    parser.add_argument("--time", type=float, required=True)
    parser.add_argument("--tolerance", type=float, default=.02)
    args = parser.parse_args()
    raw = args.capture.read_bytes()
    data = json.loads(raw)
    index = min(range(len(data["gpu_samples"])), key=lambda i: abs(data["gpu_samples"][i]["time"] - args.time))
    frame = data["gpu_samples"][index]
    group = next(g for g in frame["groups"] if g["group"] == args.group)
    rest = np.asarray(group["guide_rest_points"]).reshape(-1, 32, 3)[args.guide]
    current = np.asarray(group["guide_points"]).reshape(-1, 32, 3)[args.guide]
    result = reconstruct(current, rest, args.tolerance)
    result.update(
        schema="vam-hair-guide-node-reconstruction/1",
        capture=str(args.capture.resolve()), capture_sha256=hashlib.sha256(raw).hexdigest(),
        group=args.group, guide=args.guide, requested_time=args.time,
        actual_time=frame["time"], geometry_sample=index,
        local_simulation=frame.get("local_simulation"),
        assumptions=[
            "32 native particles: slot 0 virtual, slots 1..31 physical; 32 imported guide control points.",
            "No skinned triangle binding: captured guide displacement and rest share Groom component-local coordinates.",
            "Runtime Groom component rigidly attached to head; component-local is head-local in these captures.",
            "Absolute node rest estimate uses native ComputeNodePosition with builder StrandsSmoothing=0.",
            "A low reprojection error validates linear displacement representation, not the assumed rest-node initialization.",
        ],
        limitations=[
            "Estimated from FP16 packed interpolated guides; not raw Niagara nodes or XPBD constraint multipliers.",
            "Interpolation inverse amplifies packed-coordinate error; inspect condition number and reprojection residual.",
            "Length ratios are estimated output geometry, not direct solver constraint compliance residuals.",
        ],
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf8") as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
    print(json.dumps({k: result[k] for k in (
        "actual_time", "group", "guide", "displacement_reprojection_pass",
        "reprojection_max_cm", "estimated_total_length_ratio", "estimated_segment_ratio_max")}))
    if not result["displacement_reprojection_pass"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
