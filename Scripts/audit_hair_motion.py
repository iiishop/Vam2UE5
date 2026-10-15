"""Summarize actual native Groom guide readbacks; encoded FPS is not a benchmark."""
import argparse
import json
from pathlib import Path
import numpy as np


def summarize(path):
    capture = json.loads(path.read_text())
    first = {}
    rows = []
    for frame in capture['gpu_samples']:
        for group in frame['groups']:
            if capture['rest_control'] and not group['simulation']:
                continue  # Disabled Groom has no simulated guide buffer to read.
            assert group.get('readback_ready'), 'Missing GPU readback'
            points = np.asarray(group['guide_points'])
            rest = np.asarray(group['guide_rest_points'])
            assert points.shape == rest.shape and np.isfinite(points).all()
            index = group['group']
            first.setdefault(index, points)
            distance = np.linalg.norm(points-rest, axis=1)
            motion = np.linalg.norm(points-first[index], axis=1)
            rows.append(dict(time=frame['time'], group=index, curves=group['guide_curves'],
                             simulation=group['simulation'],
                             rest_rms_cm=float(np.sqrt(np.mean(distance**2))),
                             rest_p95_cm=float(np.percentile(distance, 95)),
                             rest_max_cm=float(distance.max()),
                             motion_rms_cm=float(np.sqrt(np.mean(motion**2))),
                             motion_max_cm=float(motion.max())))
    return dict(world=capture['world'], actor=capture['actor'], hair=capture['hair'],
                frames=capture['frames'], simulation_dt=capture['simulation_dt'],
                video_fps=capture['video_fps'], measurements=rows,
                note='Guide-local motion sampled once per second; does not detect all between-sample peaks or prove strand nonpenetration.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(summarize(args.capture), indent=2))
