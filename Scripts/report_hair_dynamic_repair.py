"""Publish actual VaM/UE hair replay evidence; missing evidence stays unverified.

Example (run only after UE captures finish):
  python Scripts/report_hair_dynamic_repair.py --select ponytail=source-clock \
      --select long=no-extra --finalized Saved/HairDynamicRepair/finalized.json

Finalized JSON may specify summary, findings [{status,finding,change,evidence}],
styles {long:{candidate,replay,editor_check,pie_capture,benchmark}}, build {log}.
extra_sections [{title,summary,evidence:[{label,path}],videos:[{label,path}]}]
can describe M-production integration separately from the same-body comparison.
Paths are absolute or relative to the plugin root. No outcome is inferred from
candidate names. The source and target guide topologies are not point-matched.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import re
from functools import lru_cache

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RENDERER = Path("C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py")
LABELS = {"ponytail": "高马尾", "long": "长发"}


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


@lru_cache(maxsize=256)
def _file_sha(path, size, modified):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def sha(path):
    path=Path(path).resolve();stat=path.stat()
    return _file_sha(str(path),stat.st_size,stat.st_mtime_ns)


def capture_header(path):
    # A1 hair captures put all scalar identity fields before the large GPU
    # arrays. Reports consume the separately hashed metrics, never recompute
    # geometry here. Fall back for older layouts instead of inventing fields.
    with Path(path).open(encoding='utf-8-sig') as stream:
        prefix=stream.read(65536)
    boundary=re.search(r',\s*"gpu_samples"\s*:',prefix)
    if boundary:
        header=json.loads(prefix[:boundary.start()]+'}')
        if {'world','actor','frames','simulation_dt','video_fps'}<=header.keys():
            return header
    return read(path)


def resolved(path):
    path = Path(path)
    return path if path.is_absolute() else ROOT / path


def archive_small_evidence(path, out):
    """Keep compact evidence portable; raw multi-hundred-MB captures stay local."""
    if path.suffix.lower() == ".html":
        return path  # Existing reports need their original relative media paths.
    if path.is_file() and path.stat().st_size <= 8*1024*1024:
        folder = out / "evidence"
        folder.mkdir(exist_ok=True)
        target = folder / (sha(path)[:12] + "-" + path.name)
        shutil.copy2(path, target)
        return target
    return path


def url(path, out):
    return html.escape(os.path.relpath(path, out).replace("\\", "/"), quote=True)


def num(value, digits=3):
    return "未验证" if value is None else f"{value:.{digits}f}"


def video_poster(video, ffmpeg):
    poster = video.with_name(video.stem+"-7s.jpg")
    if ffmpeg and video.is_file() and not poster.exists():
        subprocess.run([ffmpeg,"-n","-loglevel","error","-threads","2","-ss","7","-i",str(video),"-frames:v","1","-threads","2","-q:v","2",str(poster)],check=True,capture_output=True)
    return poster if poster.is_file() else None


def inspect_capture(path, replay_path=None, bundle_path=None):
    if not path.is_file():
        return dict(status="UNVERIFIED", path=str(path), reason="没有完成的 capture.json")
    c = capture_header(path)
    replay = read(replay_path) if replay_path and replay_path.is_file() else None
    bundle = read(bundle_path) if bundle_path and bundle_path.is_file() else None
    frames = int(c.get("frames", 0))
    expected = len(replay["samples"]) if replay else None
    actor_match = c.get("actor") == bundle["blueprint"]+"_C" if bundle else None
    measured = None
    metrics_path = None
    for candidate in (path.parent/"metrics.json", path.parent.parent/"metrics.json"):
        if candidate.is_file():
            audit = read(candidate)
            if audit.get("capture_sha256") == sha(path) and (not replay_path or not replay_path.is_file() or audit.get("replay_sha256") == sha(replay_path)):
                measured, metrics_path = audit, candidate
                break
    raw_head = (measured or {}).get("head_replay_validation", {})
    def maxima(value):
        return dict(value, maximum=value.get("max")) if value else {}
    head = dict(status=raw_head.get("status", "UNVERIFIED"),
                position_cm=maxima(raw_head.get("position_error_cm")),
                rotation_degrees=maxima(raw_head.get("rotation_error_deg")),
                reason="使用同 capture/replay 哈希的 audit_hair_dynamic_comparison 输出；缺失时不重算、不宣称通过")
    groups = {}
    for phase, items in (measured or {}).get("phases", {}).items():
        for item in items:
            group = groups.setdefault(str(item["group"]), dict(guide_curves=item.get("guide_curves"), phases={}))
            group["phases"][phase] = dict(tip_excursion_max_cm=(item.get("tip_excursion_from_phase_first_cm") or {}).get("max"),
                                         all_point_excursion_cm=item.get("guide_excursion_from_phase_first_cm") or {},
                                         final_speed_median_cm_s=item.get("sampled_tip_speed_final_median_cm_s"),
                                         polyline_length_ratio_to_rest=item.get("polyline_length_ratio_to_rest") or {},
                                         head_proxy_depth_cm=(item.get("proxy_penetration", {}).get("head", {}).get("depth_cm") or {}),
                                         body_proxy_depth_cm=(item.get("proxy_penetration", {}).get("body", {}).get("depth_cm") or {}),
                                         proxy_root_prefix_excluded_points=item.get("collision_root_prefix_excluded_points"),
                                         render_matched_points=item.get("render_matched_points"))
    return dict(status="MEASURED", path=str(path), sha256=sha(path), world=c.get("world"),
                actor=c.get("actor"), runtime_identity=bundle.get("identity") if bundle else None,
                actor_matches_bundle=actor_match, frames=frames, expected_frames=expected,
                complete_against_replay=frames == expected if expected else None,
                simulation_dt=c.get("simulation_dt"), video_fps=c.get("video_fps"),
                metrics_path=str(metrics_path) if metrics_path else None,
                metrics_sha256=sha(metrics_path) if metrics_path else None,
                diagnostic_metrics=measured, head=head, groups=groups)


def encode_capture(path, destination, ffmpeg, square_viewport=False):
    if not path.is_file():
        return dict(status="UNVERIFIED", reason="缺少 capture.json")
    capture = capture_header(path)
    count = int(capture.get("frames", 0))
    folder = path.parent / "video"
    missing = [i for i in range(count) if not (folder/f"frame-{i:04d}.png").is_file()]
    if not count or missing:
        return dict(status="UNVERIFIED", reason="截图序列缺失", missing_count=len(missing), first_missing=missing[:5])
    destination.parent.mkdir(parents=True, exist_ok=True)
    first = folder/"frame-0000.png"
    decoder = ["-c:v", "mjpeg"] if first.read_bytes()[:2] == b"\xff\xd8" else []
    with Image.open(first) as image:
        width, height = image.size
    crop = None
    if square_viewport and width > height:
        x = (width-height)//2
        # The recorded-camera path constrains aspect=1. Verify actual bars in
        # beginning/middle/end frames; only remove those bars, never body pixels.
        for index in sorted({0, count//2, count-1}):
            with Image.open(folder/f"frame-{index:04d}.png") as image:
                if image.size != (width, height):
                    return dict(status="FAIL", reason="截图尺寸变化，拒绝统一裁切")
                for box in ((0, 0, max(0, x-8), height), (min(width, x+height+8), 0, width, height)):
                    if box[2] > box[0] and max(v[1] for v in image.convert("RGB").crop(box).getextrema()) > 8:
                        return dict(status="FAIL", reason="实图未确认纯黑pillarbox，拒绝自动裁切")
        crop = dict(x=x, y=0, width=height, height=height, reason="aspect=1 recorded camera; black pillarboxes verified in first/middle/last screenshots")
    filters = ["-vf", f"crop={height}:{height}:{crop['x']}:0"] if crop else []
    command = [ffmpeg, "-n", "-loglevel", "error", "-threads", "2", "-filter_threads", "1", "-framerate", str(capture.get("video_fps", 30)), *decoder,
               "-i", str(folder/"frame-%04d.png"), "-frames:v", str(count), *filters, "-c:v", "libx264", "-preset", "fast", "-crf", "19",
               "-threads", "2", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(destination)]
    try:
        subprocess.run(command, check=True, timeout=300, capture_output=True)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        return dict(status="FAIL", reason=str(error), stderr=(error.stderr or b"").decode(errors="replace"))
    probe = None
    ffprobe = shutil.which("ffprobe")
    if ffprobe:
        inspected = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                                    "stream=width,height,r_frame_rate,duration,nb_frames", "-of", "json", str(destination)],
                                   check=True, capture_output=True, text=True)
        probe = json.loads(inspected.stdout)["streams"][0]
    return dict(status="ENCODED", path=str(destination), frames=count, command=command, video_probe=probe,
                source_dimensions=[width,height], square_viewport_crop=crop,
                source_capture_sha256=sha(path), source_first_frame_sha256=sha(first), source_last_frame_sha256=sha(folder/f"frame-{count-1:04d}.png"),
                source_note="只编码真实 UE 截图；如记录crop，仅去除实图确认的相机黑边，原始截图保留")


def encode_vam_aligned(style, replay_path, destination, ffmpeg):
    """Repeat actual source photographs by measured timestamps, never interpolate pixels."""
    samples_path = ROOT/f"Evidence/HairDynamicReference/{style}/dynamic-samples.json"
    identities_path = ROOT/"Evidence/HairDynamicReference/identities.json"
    if not samples_path.is_file() or not identities_path.is_file() or not replay_path.is_file():
        return dict(status="UNVERIFIED", reason="缺少源时间戳、源路径或重放输入")
    samples, replay = read(samples_path), read(replay_path)
    identity = next((x for x in read(identities_path) if x["style"] == style), None)
    if not identity:
        return dict(status="UNVERIFIED", reason="没有对应发型原始采样身份")
    raw = Path(identity["raw_capture"])
    ids = list(range(2, len(samples), 3))
    times = [float(samples[i]["time"]) for i in ids]
    frames = []
    for sample in replay["samples"]:
        t = float(sample["time"])
        index = ids[max(0, bisect.bisect_right(times, t)-1)]
        original_time = float(samples[index]["time"])
        image = raw/f"frame-{index:04d}.jpg"
        if not image.is_file():
            return dict(status="UNVERIFIED", reason=f"原始VaM截图缺失：{image}")
        frames.append(dict(time=t, source_frame=index, source_time=original_time,
                           sample_age_seconds=t-original_time, initial_hold_before_first_sample=t < times[0]))
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise FileExistsError(destination)
    command = [ffmpeg, "-n", "-loglevel", "error", "-threads", "2", "-filter_threads", "1",
               "-f", "image2pipe", "-framerate", "30", "-c:v", "mjpeg", "-i", "pipe:0",
               "-frames:v", str(len(frames)), "-c:v", "libx264", "-threads", "2", "-preset", "fast",
               "-crf", "19", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(destination)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    cache = {}
    try:
        for frame in frames:
            index = frame["source_frame"]
            if index not in cache:
                cache[index] = (raw/f"frame-{index:04d}.jpg").read_bytes()
            process.stdin.write(cache[index])
        process.stdin.close()
        error = process.stderr.read()
        code = process.wait(timeout=300)
    except Exception:
        process.kill()
        process.wait()
        raise
    if code:
        return dict(status="FAIL", reason="源原帧编码失败", stderr=error.decode(errors="replace"), command=command)
    result = dict(status="ENCODED", path=str(destination), command=command, frames=len(frames), fps=30,
                  source_view=2, source_video_time_zero=0, first_actual_source_sample_seconds=times[0],
                  initial_hold_frames=sum(f["initial_hold_before_first_sample"] for f in frames),
                  source_sample_file=str(samples_path), source_sample_sha256=sha(samples_path),
                  replay_file=str(replay_path), replay_sha256=sha(replay_path), raw_capture=str(raw),
                  selection="For each 30 Hz replay timestamp, hold the latest measured view2 source frame. Before the first source timestamp, hold the first available source frame.",
                  pixel_processing="No optical flow, generated interpolation, cropping or image synthesis; repeated original JPEG frames encoded to H.264.",
                  source_frame_hashes={str(i):hashlib.sha256(data).hexdigest() for i,data in cache.items()},
                  frame_map=frames)
    destination.with_suffix(".json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf8")
    return result


def evidence_status(path, kind):
    if not path or not path.is_file():
        return dict(status="UNVERIFIED", reason="没有本轮证据文件")
    data = capture_header(path) if kind == "pie" else read(path)
    if kind == "editor":
        status = data.get("status", "UNVERIFIED")
    elif kind == "pie":
        status = "MEASURED" if data.get("world") == "PIE" and data.get("frames", 0) > 0 else "FAIL"
    else:
        status = "MEASURED" if data.get("phases", {}).get("fine", {}).get("frames", 0) > 0 else "UNVERIFIED"
    return dict(status=status, path=str(path), sha256=sha(path), data=data if kind != "pie" else {k: data.get(k) for k in ("world", "actor", "frames", "simulation_dt")})


def make_report(args):
    root, out = args.runs.resolve(), args.output.resolve()
    config = read(args.finalized) if args.finalized else {}
    selects = {style: value.get("candidate") for style, value in config.get("styles", {}).items()}
    for value in args.select:
        style, candidate = value.split("=", 1)
        if style not in LABELS or not candidate or Path(candidate).name != candidate:
            raise ValueError("--select requires ponytail=name or long=name")
        selects[style] = candidate
    if out.exists():
        raise FileExistsError("Choose a new output directory; existing report evidence is preserved")
    actual = list(root.glob("baseline-*/capture.json")) + list(root.glob("*/capture/capture.json"))
    if not actual:
        raise RuntimeError("No actual completed UE capture found; refusing to generate a result")
    out.mkdir(parents=True)
    metrics = dict(schema="vam-hair-dynamic-repair-report/1", selections=selects, styles={}, candidates={},
                   limitations=["VaM/UE guide counts, resampling and rendered topology differ; no cross-engine nearest-neighbour RMS is presented.",
                                "Recorded capture FPS is not runtime performance. GPU readback and screenshot capture perturb performance.",
                                "Head matrix agreement validates the driver only; hair shape, collisions and visual quality need separate evidence."],
                   finalized_input=config, finalized_sha256=sha(args.finalized) if args.finalized else None)
    panels, rows, motion_rows, structural_rows, status_rows = [], [], [], [], []
    ffmpeg = args.ffmpeg or shutil.which("ffmpeg")
    for style, label in LABELS.items():
        options = config.get("styles", {}).get(style, {})
        replay = resolved(options.get("replay", f"Saved/HairDynamicUE/inputs-v2/{style}.json"))
        candidate = selects.get(style)
        after_dir = root/f"{style}-{candidate}" if candidate else None
        before = root/f"baseline-{style}"/"capture.json"
        after = after_dir/"capture/capture.json" if after_dir else root/"UNSELECTED"
        record = dict(candidate=candidate, before=inspect_capture(before, replay, before.parent/"bundle.json"),
                      after=inspect_capture(after, replay, after_dir/"bundle.json" if after_dir else None))
        source_metrics = ROOT/f"Evidence/HairDynamicReference/{style}/metrics.json"
        record["vam_reference"] = dict(path=str(source_metrics), sha256=sha(source_metrics), data=read(source_metrics)) if source_metrics.is_file() else dict(status="UNVERIFIED")
        for key, kind, default in [("editor_check", "editor", "editor/check.json"), ("pie_capture", "pie", "pie/capture.json"), ("benchmark", "performance", "benchmark/summary.json")]:
            p = resolved(options[key]) if options.get(key) else after_dir/default if after_dir else None
            record[kind] = evidence_status(p, kind)
        validation_bundle = resolved(options["validation_bundle"]) if options.get("validation_bundle") else after_dir/"bundle.json" if after_dir else None
        validation_replay = resolved(options["validation_replay"]) if options.get("validation_replay") else replay
        if validation_bundle and validation_bundle.is_file():
            asset = read(validation_bundle)
            record["validation_asset"] = dict(identity=asset["identity"],base_identity=asset.get("base_identity"),bundle=str(validation_bundle),replay=str(validation_replay),scope="production M verification" if options.get("validation_bundle") else "same-body comparison verification")
            for kind in ("editor", "pie"):
                data = json.dumps(record[kind].get("data", {}))
                record[kind]["asset_identity_verified"] = asset["identity"] in data or asset["blueprint"] in data
                if record[kind]["status"] not in ("UNVERIFIED", "FAIL") and not record[kind]["asset_identity_verified"]:
                    record[kind]["status"] += " / 身份未核对"
            pie_path = resolved(options["pie_capture"]) if options.get("pie_capture") else after_dir/"pie/capture.json"
            pie_check = inspect_capture(pie_path, validation_replay, validation_bundle)
            record["pie"]["validation"] = {k:pie_check.get(k) for k in ("frames","expected_frames","complete_against_replay","actor_matches_bundle","head")}
            if record["pie"]["status"] == "MEASURED":
                if not (pie_check.get("complete_against_replay") and pie_check.get("actor_matches_bundle") and pie_check.get("head",{}).get("status")=="PASS"):
                    record["pie"]["status"] = "FAIL / 完整帧或实际矩阵未通过"
            perf_protocol = record["performance"].get("data",{}).get("protocol","")
            record["performance"]["asset_identity_verified"] = asset["blueprint"] in perf_protocol
            if record["performance"]["status"] == "MEASURED" and not record["performance"]["asset_identity_verified"]:
                record["performance"]["status"] = "FAIL / 性能角色身份不匹配"
        invalid = [key for key in ("before", "after") if record[key].get("head", {}).get("status") != "PASS"]
        if invalid:
            panels.append(f'<p><strong>{label}：{html.escape("、".join(invalid))} 的实际头矩阵尚未通过核对。本组视频可检查外观，不能据此宣布同动作求解效果通过。</strong></p>')
        panels.append(f'<h3>{label} · 选用 {html.escape(candidate or "尚未选择")}</h3><button onclick="syncVideos(\'{style}\',0)">同步从头</button> <button onclick="syncVideos(\'{style}\',7)">沉降后 7s</button> <button onclick="syncVideos(\'{style}\',12)">运动中 12s</button> <button onclick="syncVideos(\'{style}\',20)">停止后 20s</button><div class="triplet" id="{style}">')
        source_video = ROOT/f"Evidence/HairDynamicReference/{style}/view-2.mp4"
        source_samples = ROOT/f"Evidence/HairDynamicReference/{style}/dynamic-samples.json"
        offset = float(read(source_samples)[2]["time"]) if source_samples.is_file() else 0
        source_target = out/style/"vam-source.mp4"
        source_target.parent.mkdir(parents=True, exist_ok=True)
        cached_source = root/"media"/f"vam-{style}-aligned.mp4"
        cached_source_meta = cached_source.with_suffix(".json")
        aligned = None
        if cached_source.is_file() and cached_source_meta.is_file() and replay.is_file():
            cached = read(cached_source_meta)
            if cached.get("replay_sha256") == sha(replay) and cached.get("source_sample_sha256") == sha(source_samples):
                shutil.copy2(cached_source, source_target)
                aligned = dict(cached, path=str(source_target), cached_from=str(cached_source))
        if aligned is None and ffmpeg:
            aligned = encode_vam_aligned(style, replay, source_target, ffmpeg)
        if aligned and aligned["status"] == "ENCODED":
            aligned_path = source_target.with_suffix(".json")
            aligned_path.write_text(json.dumps(aligned, ensure_ascii=False, indent=2), encoding="utf8")
            record["source_video"] = aligned
            poster = video_poster(source_target,ffmpeg)
            poster_attr = f' poster="{url(poster,out)}"' if poster else ''
            panels.append(f'<figure><video controls preload="metadata" data-start="0"{poster_attr} src="{url(source_target,out)}"></video><figcaption>VaM 原生物理，后侧 B。封面为沉降 7s；按实测时间重复最近源原帧，开始 {aligned["first_actual_source_sample_seconds"]:.3f}s 使用首张，约 5 Hz 的原始时间分辨率未增加。</figcaption></figure>')
        elif source_video.is_file() and not source_target.exists():
            shutil.copy2(source_video, source_target)
            record["source_video"] = dict(status="HISTORICAL_TIMING_ONLY", aligned_attempt=aligned)
            panels.append(f'<figure><video controls preload="metadata" data-start="{offset}" src="{url(source_target,out)}"></video><figcaption>VaM 历史视频，后侧 B；首帧源时间 {offset:.3f}s。原时间基转换存在尾差，仅作近似同步。</figcaption></figure>')
        else:
            panels.append('<figure><p>VaM 原视频缺失，未验证。</p></figure>')
        for key, capture_path, title in [("before", before, "UE 修复前，同动作"), ("after", after, "UE 选用候选，同动作")]:
            destination = out/style/f"{key}.mp4"
            square_viewport = replay.is_file() and read(replay).get("use_recorded_camera", False)
            cached_video = root/"media"/(f"baseline-{style}-square.mp4" if key == "before" else f"{style}-{candidate}-square.mp4")
            encoded = None
            if cached_video.is_file() and cached_video.with_suffix(".json").is_file() and capture_path.is_file():
                cached = read(cached_video.with_suffix(".json"))
                if cached.get("source_capture_sha256") == sha(capture_path) and (not square_viewport or cached.get("square_viewport_crop") or (cached.get("video_probe") or {}).get("width") == (cached.get("video_probe") or {}).get("height")):
                    shutil.copy2(cached_video, destination)
                    encoded = dict(cached, path=str(destination), cached_from=str(cached_video), video_sha256=sha(destination))
            if encoded is None:
                encoded = encode_capture(capture_path, destination, ffmpeg, square_viewport) if ffmpeg else dict(status="UNVERIFIED", reason="ffmpeg unavailable")
            record[key]["video"] = encoded
            if encoded["status"] == "ENCODED":
                probe = encoded.get("video_probe") or {}
                dimensions = f'{probe.get("width","?")}×{probe.get("height","?")}'
                mismatch = '；由原1280×720截图去除两侧黑边，方形相机视野未改变' if encoded.get("square_viewport_crop") else '；输出比例非方形，需核对实际相机视野' if probe and probe.get("width") != probe.get("height") else ''
                poster = video_poster(destination,ffmpeg)
                poster_attr = f' poster="{url(poster,out)}"' if poster else ''
                panels.append(f'<figure><video controls preload="metadata" data-start="0"{poster_attr} src="{url(destination,out)}"></video><figcaption>{title} · 封面为沉降 7s · 30 FPS 编码 · {dimensions}{mismatch}。</figcaption></figure>')
            else:
                panels.append(f'<figure><p>{title}：{html.escape(encoded.get("reason","未验证"))}</p></figure>')
            info = record[key]
            head = info.get("head", {})
            rows.append(f'| {label} / {key} | {info.get("frames","—")} | {head.get("status","UNVERIFIED")} | {num(head.get("position_cm",{}).get("maximum"))} | {num(head.get("rotation_degrees",{}).get("maximum"))} |')
            for group_id, group in info.get("groups", {}).items():
                motion = group.get("phases", {}).get("motion") or {}
                recovery = group.get("phases", {}).get("recovery") or {}
                motion_rows.append(f'| {label} / {key} / 组 {group_id} | {group.get("guide_curves","—")} | {num(motion.get("tip_excursion_max_cm"))} | {num(motion.get("all_point_excursion_cm",{}).get("rms"))} | {num(recovery.get("final_speed_median_cm_s"))} |')
                ratio = motion.get("polyline_length_ratio_to_rest", {})
                structural_rows.append(f'| {label} / {key} / 组 {group_id} | {num(ratio.get("p95"))} | {num(ratio.get("max"))} | {num(ratio.get("min"))} | {num(motion.get("head_proxy_depth_cm",{}).get("max"))} | {num(motion.get("body_proxy_depth_cm",{}).get("max"))} |')
        panels.append('</div>')
        perf = record["performance"].get("data", {})
        fine = perf.get("phases", {}).get("fine", {})
        status_rows.append(f'| {label} | {record["editor"]["status"]} | {record["pie"]["status"]} | {num(fine.get("mean_ms"))} | {num(fine.get("p95_ms"))} | {num(perf.get("hair_delta_ms",{}).get("fine"))} |')
        metrics["styles"][style] = record
        for run in sorted(root.glob(f"{style}-*")):
            if run.is_dir():
                metrics["candidates"][run.name] = inspect_capture(run/"capture/capture.json", replay, run/"bundle.json")
    findings = []
    for item in config.get("findings", []):
        links = []
        for evidence in item.get("evidence", []):
            p = archive_small_evidence(resolved(evidence), out)
            links.append(f'[{p.name}]({url(p,out)})' if p.exists() else f'缺失：{p.name}')
        findings.append(f'### {item.get("status","待验证")} · {item.get("finding","")}\n\n{item.get("change","")}\n\n'+" / ".join(links))
    extras = []
    metrics["extra_sections"] = []
    for index, section in enumerate(config.get("extra_sections", [])):
        title = section.get("title", "生产角色接入回归")
        summary = section.get("summary", section.get("text", ""))
        record = dict(title=title, summary=summary, evidence=[], videos=[], images=[], scope="additional explicitly labelled evidence")
        links = []
        for entry in section.get("evidence", []):
            item = dict(path=entry) if isinstance(entry, str) else entry
            p = resolved(item["path"])
            label = item.get("label", p.name)
            evidence = dict(path=str(p), exists=p.exists(), label=label)
            if p.is_file():
                evidence["sha256"] = sha(p)
            record["evidence"].append(evidence)
            linked = archive_small_evidence(p, out)
            links.append(f'<a href="{url(linked,out)}">{html.escape(label)}</a>' if p.exists() else f'{html.escape(label)}：未验证，文件缺失')
        videos = []
        for item in section.get("videos", []):
            p = resolved(item["path"])
            label = item.get("label", p.name)
            record["videos"].append(dict(path=str(p), exists=p.is_file(), label=label, sha256=sha(p) if p.is_file() else None))
            if p.is_file():
                linked = out/"production"/p.name
                linked.parent.mkdir(exist_ok=True)
                if linked.exists() and sha(linked) != sha(p):
                    raise ValueError("Different production videos share the same basename")
                shutil.copy2(p,linked)
                videos.append(f'<figure><video controls preload="metadata" src="{url(linked,out)}"></video><figcaption>{html.escape(label)}</figcaption></figure>')
            else:
                videos.append(f'<p>{html.escape(label)}：未验证，视频缺失。</p>')
        images = []
        for item in section.get("images", []):
            p = resolved(item["path"])
            label = item.get("label", p.name)
            record["images"].append(dict(path=str(p), exists=p.is_file(), label=label, sha256=sha(p) if p.is_file() else None))
            linked = archive_small_evidence(p, out)
            images.append(f'<figure><img style="width:100%" loading="lazy" src="{url(linked,out)}" alt="{html.escape(label)}"><figcaption>{html.escape(label)}</figcaption></figure>' if p.is_file() else f'<p>{html.escape(label)}：文件缺失。</p>')
        extras.append(f'<h3>{html.escape(title)}</h3><p>{html.escape(summary)}</p><p>{" / ".join(links) or "未提供本轮回归证据。"}</p><div class="triplet">{"".join(videos)}</div>{"".join(images)}')
        metrics["extra_sections"].append(record)
    build = config.get("build", {})
    build_path = resolved(build["log"]) if build.get("log") else None
    metrics["build"] = dict(status="UNVERIFIED")
    if build_path and build_path.is_file():
        text = build_path.read_text(errors="replace")
        success = "Result: Succeeded" in text or "BUILD SUCCESSFUL" in text
        metrics["build"] = dict(status="SUCCEEDED_IN_LOG" if success else "UNVERIFIED", path=str(build_path), sha256=sha(build_path))
    (out/"metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf8")
    markdown = '''---
title: 长发与马尾：VaM 同动作驱动下的 UE 修复
subtitle: 实际角色 / 实际矩阵 / 精细单档 / 动态证据
lang: zh
---

## A 本轮状态

'''+config.get("summary", "本页汇集实际完成的对照。候选是否采用以配置记录为准；缺失的 Editor、PIE、性能或矩阵证据均明确标记未验证，不根据视频编码帧率或单一位移幅度判定通过。")+'''

编译日志状态：'''+metrics["build"]["status"]+'''。完整指标与证据路径见 [metrics.json](metrics.json)。

## B 同动作视频

0–8 秒沉降，8–16 秒摆头，16 秒后停止恢复。UE 使用实测 VaM 头骨矩阵，以四元数插值重采样，另有 3 秒预热。源每视角约 5 Hz，UE 编码 30 FPS；两者不是相同采样密度。源视频按实际时间戳重复最近已采样原帧，保存逐帧来源表，没有生成中间画面；开头不足一张源采样间隔时使用首张并明确记录。相机与光源方向复用实际源设置，但两引擎材质、光照和曝光不同。

[[VIDEOS]]

## C 根因、改动和未解决项

'''+("\n\n".join(findings) if findings else "尚未提供经证据确认的最终根因说明；本生成器不根据候选名称编造结论。")+'''

## D 实际头骨矩阵核对

矩阵状态与门槛直接采用审计器输出（默认位置 0.01 cm、旋转 0.05°，并检查 scale 和 Groom 附着）。比较实际 UE socket 世界矩阵与输入重放矩阵；不使用 UI 欧拉角。矩阵通过只说明驱动一致，不代表发型、穿模和摆动质量通过。

| 发型 / 阶段 | 实际录像帧 | 矩阵状态 | 最大位置差 cm | 最大旋转差 ° |
|---|---|---|---|---|
'''+"\n".join(rows)+'''

## E 发束运动

以下每一行都基于该 UE 发组对应阶段的首个采样。源 VaM 原始指标附在 metrics.json；其发组顺序、拓扑和参考时刻定义不同，不能把两表按组编号直接相减。

| 发型 / 阶段 / 发组 | 导线数 | 运动期发梢最大位移 cm | 运动期全点位移 RMS cm | 恢复末段速度中位数 cm/s |
|---|---|---|---|---|
'''+"\n".join(motion_rows)+'''

### E2 运动期结构与代理接触

折线长度比 = 当前 GPU 导线折线长度 / 导入造型的折线长度；1 表示长度相同。这里统计运动期全部导线和几何采样的 P95、最大、最小值。它是最终导线几何变化，**不是直接读取求解器约束误差**。头/身代理深度来自同一运动期原始审计，排除了根部前缀；**不等同真实皮肤穿模深度**。必须结合视频检查根部、尾部和停止恢复。

| 发型 / 阶段 / 发组 | 长度比 P95 | 长度比最大 | 长度比最小 | 头代理深度最大 cm | 身体代理深度最大 cm |
|---|---|---|---|---|---|
'''+"\n".join(structural_rows)+'''

## F Editor、PIE 和性能

Game 世界录像不冒充 Editor 或 PIE 验证。下表使用明确指定的生产 M 角色证据，身份记录在 metrics.json 的 validation_asset；它与上方同身体隔离对照分别核验。Editor 为正常 BP 的资产／附着／求解器绑定检查，动态运动单独由完整 PIE 录像验证。未提供本轮专用性能采样时，帧时间保持未验证；截图、GPU 读回和视频编码速度不能用于游戏性能结论。60 Hz source-clock 是 UE 适配器的参考时钟，VaM 源 fixed-step 尚未实测，不能称为完整等价复刻。性能数值为整帧时间，头发增量包含渲染与模拟，不是孤立求解器 GPU 时间。

| 发型 | Editor 证据 | PIE 证据 | 精细平均 ms | P95 ms | 头发增量 ms |
|---|---|---|---|---|---|
'''+"\n".join(status_rows)+'''

## G M 生产角色接入回归

以下证据独立记录生产 M 角色上的集成结果。生产身体的资产身份、骨骼权重或碰撞代理可能与上方隔离对照不同；本节不加入同身体因果对照的前后指标，也不据此改写旧对照身份。实际 Editor、PIE、动作范围和性能以所链接文件为准。

[[EXTRA]]

## H 数据范围和验收边界

VaM 内部粒子、UE 重新采样的模拟导线、最终渲染发丝具有不同拓扑。没有建立逐点来源同一性，因此本报告不输出跨引擎最近邻 RMS。运动指标只在同一求解器、同一发组内统计：GPU buffer 已经位于附着头部的 Groom 局部空间，不能再做一次逆头骨变换。数值直接复用同 capture/replay 哈希的 audit_hair_dynamic_comparison.py 结果，以各阶段首个采样为参考，记录发梢幅度、全点 RMS/P95/Max 和末段速度。

发丝位移变大本身不是成功标准；必须同时检查束根、发髻、轮廓、肩背穿模和停止后的恢复。碰撞代理重叠也不能冒充真实皮肤穿入。本页保留所有候选的完成状态及原始路径，失败或缺失候选不会被写成通过。

本轮固定 5 substeps。源支撑时钟与 UE 质量迭代数解耦，并不等于整个动态系统对 timestep/substeps 不变；位置回形的半群关系无法证明速度重建和等效加速度也不变。60 Hz 是适配器参考时钟，尚未测得 VaM 的实际 fixed-step，不能称为完整等价复刻。

跨阶段渲染点指标只使用稳定 ID 交集。如果 active point count / LOD 改变导致交集只剩少量根部点，不能从很小的 render excursion 推断整片可见头发不动。完整导线数据与真实视频需要同时判断。

[Epic Groom 物理设置](https://dev.epicgames.com/documentation/en-us/unreal-engine/enabling-physics-simulation-on-grooms-in-unreal-engine) 将 Project Stretch、Project Bend 和 Project Collision 描述为 XPBD 迭代后的投影步骤；本轮对照需要检查额外造型支撑插入该顺序后是否又破坏长度或接触。文档中的默认频率不是本轮 VaM 或 UE 的实际运行时钟证据。
'''
    (out/"draft.md").write_text(markdown, encoding="utf8")
    subprocess.run([sys.executable, "-X", "utf8", str(args.renderer), "render", str(out/"draft.md"), "--output", str(out/"report.html"), "--no-open"], check=True)
    page = (out/"report.html").read_text(encoding="utf8").replace("[[VIDEOS]]", "".join(panels)).replace("[[EXTRA]]", "".join(extras) or '<p>尚未提供 M 生产角色本轮回归文件，未验证。</p>')
    page = page.replace("</head>", '<style>.shell{max-width:1440px}.grid>.panel{grid-column:span 12}.triplet{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.triplet:has(>figure:only-child){grid-template-columns:1fr;max-width:960px}.triplet:has(>figure:nth-child(2):last-child){grid-template-columns:repeat(2,1fr)}.triplet figure{margin:0}.triplet video{width:100%}.triplet figcaption{font-size:.85em}button{margin:4px}@media(max-width:900px){.triplet,.triplet:has(>figure:nth-child(2):last-child){grid-template-columns:1fr}}</style></head>')
    page = page.replace("</body>", "<script>function syncVideos(id,t){document.querySelectorAll('#'+id+' video').forEach(v=>{v.pause();v.currentTime=Math.max(0,t-Number(v.dataset.start||0));v.play().catch(()=>{});});}</script></body>")
    (out/"report.html").write_text(page, encoding="utf8")
    print(out/"report.html")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=Path, default=ROOT/"Saved/HairDynamicRepair")
    parser.add_argument("--output", type=Path, default=ROOT/"Evidence/HairDynamicRepair")
    parser.add_argument("--select", action="append", default=[])
    parser.add_argument("--finalized", type=Path)
    parser.add_argument("--ffmpeg")
    parser.add_argument("--renderer", type=Path, default=RENDERER)
    make_report(parser.parse_args())


if __name__ == "__main__":
    main()
