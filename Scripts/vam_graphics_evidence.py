"""Validate an explicitly selected UE CSV capture, without discarding raw samples."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def summarize(path, target_fps, quality):
    if quality['schema'] != 'vam-graphics-quality/1' or target_fps <= 0:
        raise ValueError('Invalid graphics quality or frame rate')
    csv.field_size_limit(16 * 1024 * 1024)
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.reader(stream)
        header = next(reader)
        column = header.index('FrameTime')
        frames = []
        for row in reader:
            if len(row) <= column:
                continue
            try:
                value = float(row[column])
            except ValueError:  # UE's trailing metadata and repeated header.
                continue
            if not math.isfinite(value) or value <= 0:
                raise ValueError('Invalid recorded frame time')
            frames.append(value)
    required = quality['capture_frames']
    excluded = quality['startup_frames_excluded_from_cap_check']
    if len(frames) < required or not 0 <= excluded < required:
        raise ValueError('Incomplete capture or invalid startup exclusion')
    measured = frames[excluded:]
    median_fps = 1000 / statistics.median(measured)
    return {
        'schema': 'vam-render-pacing-evidence/1',
        'source_csv': str(Path(path).resolve()),
        'source_sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest(),
        'quality': quality,
        'requested_fps': target_fps,
        'median_fps_after_startup': median_fps,
        'maximum_frame_ms_including_startup': max(frames),
        'maximum_frame_ms_after_startup': max(measured),
        'all_frame_ms': frames,
        'frame_pacing_passed': abs(median_fps / target_fps - 1) <= quality['median_fps_relative_tolerance'],
        'stage07_passed': False,
        'scope': 'Rendered frame pacing only. Does not certify skin pixels, solver convergence or tissue coverage.'
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', required=True)
    parser.add_argument('--quality', required=True)
    parser.add_argument('--fps', required=True, type=float)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = summarize(args.csv, args.fps, json.loads(Path(args.quality).read_text(encoding='utf-8-sig')))
    Path(args.output).write_text(json.dumps(result, indent=2), encoding='utf8')
    if not result['frame_pacing_passed']:
        raise SystemExit('Actual rendered frame pacing failed the predefined tolerance')
