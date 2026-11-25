"""Turn a Scripts/CaptureFeelSample.ps1 run into evidence: feel.mp4 (the game window and the speaker output, both placed on
the game's own event clock) and timing.png / timing.json (the speaker output with every logged step and sound).

    python Scripts/MakeFeelEvidence.py --run Artifacts/<stamp>-FeelSample-1600x900 --ffmpeg <ffmpeg.exe> --out <folder>
Needs numpy, soundfile and matplotlib. The audio is what the speaker received from the running game (WASAPI loopback), not
a re-render. Process start clocks of the recorders are not trusted: the loopback is placed where the logged takes match it,
the video where its frame changes match the logged releases; both raw offsets are reported.
"""
import argparse
import datetime
import json
import re
import subprocess
from pathlib import Path

import numpy as np
import soundfile
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

EVENT = re.compile(r"LD_AUDIO_EVENT t=(?P<t>[-\d.]+) cue=(?P<cue>\S+) take=(?P<take>\S+) gain=(?P<gain>[\d.]+)")
STEP = re.compile(r"LD_FEEL_STEP t=(?P<t>[\d.]+) world=(?P<w>[\d.]+) step=(?P<s>\d+)")


def take_sources():
    """SW_<Name>[_<n>] asset name -> its source file, from the audio manifest."""
    root = Path(__file__).resolve().parent.parent / "SourceData" / "Audio"
    manifest = json.loads((root / "audio_cues.json").read_text(encoding="utf-8"))
    out = {}
    for entry in manifest["cues"] + manifest.get("skills", []):
        name = entry.get("cue") or entry.get("ability")
        files = [t["source"] for t in entry["takes"]] if "takes" in entry else [entry["source"]]
        for index, source in enumerate(files):
            out[f"SW_{name}" + (f"_{index}" if index else "")] = root / source
    return out


def match(mono, rate, ref, around, before=0.1, after=0.6):
    """Best normalised match of ref in mono around a time; (seconds, score)."""
    lo, hi = int((around - before) * rate), int((around + after) * rate) + len(ref)
    lo = max(lo, 0)
    seg = mono[lo:hi]
    if len(seg) <= len(ref):
        return None, 0.0
    corr = np.correlate(seg, ref, mode="valid")
    energy = np.sqrt(np.convolve(seg ** 2, np.ones(len(ref)), mode="valid")) * np.linalg.norm(ref) + 1e-12
    score = corr / energy
    best = int(np.argmax(score))
    return (lo + best) / rate, float(score[best])


def frame_changes(ffmpeg, video):
    """Per-frame change of the window video (its centre, where the stage is) with the frame times."""
    probe = str(Path(ffmpeg).with_name("ffprobe.exe"))
    w, h = 320, 180
    raw = subprocess.run([ffmpeg, "-loglevel", "error", "-i", str(video), "-vf", f"crop=iw*0.6:ih*0.6,scale={w}:{h}",
                          "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-"], capture_output=True).stdout
    frames = np.frombuffer(raw, np.uint8).reshape(-1, h, w).astype(float)
    times = [float(x.strip(",")) for x in subprocess.run([probe, "-v", "error", "-select_streams", "v", "-show_entries", "frame=pts_time",
                                                           "-of", "csv=p=0", str(video)], capture_output=True, text=True).stdout.split() if x.strip(",")]
    times = np.array(times[: len(frames)])
    change = np.abs(np.diff(frames, axis=0)).mean(axis=(1, 2))
    return times[1:], change


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--run", required=True, type=Path)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    run, out = args.run, args.out
    out.mkdir(parents=True, exist_ok=True)
    log = (run / "Art.log").read_text(encoding="utf-8", errors="replace").splitlines()
    steps, events = [], []
    for line in log:
        if m := STEP.search(line):
            steps.append((float(m["t"]), float(m["w"]), int(m["s"])))
        elif m := EVENT.search(line):
            events.append((float(m["t"]), m["cue"], m["take"], float(m["gain"])))
    if not steps:
        raise SystemExit("No LD_FEEL_STEP lines: the scenario did not run.")
    world_at_start = steps[0][1] - steps[0][0]
    rows = [{"t": round(e_t - world_at_start, 4), "cue": cue, "take": take, "gain": gain} for e_t, cue, take, gain in events if e_t >= world_at_start]
    seconds = max(r["t"] for r in rows) + 1.2

    # Audio: where the speaker output carries each logged take (around the wall-clock estimate of the scenario start).
    meta = json.loads((run / "Feel.json").read_text(encoding="utf-8-sig"))
    game_start = datetime.datetime.fromisoformat(next(l for l in log if "LD_FEEL_RECORDING_START" in l).split("utc=")[1].strip().replace("Z", "+00:00"))
    loop_start = datetime.datetime.fromisoformat(meta["loopback_start_utc"])
    estimate = (game_start - loop_start).total_seconds()
    audio, rate = soundfile.read(run / "loopback.wav", always_2d=True)
    mono = audio.mean(axis=1)
    sources = take_sources()
    placed = []
    for row in rows:
        src = sources.get(row["take"])
        if src is None:
            continue
        ref, ref_rate = soundfile.read(src, always_2d=True)
        ref = ref.mean(axis=1)[: int(0.12 * ref_rate)]
        if ref_rate != rate or len(ref) < 64:
            continue
        at, score = match(mono, rate, ref, estimate + row["t"], before=0.3, after=0.8)
        if at is not None:
            row["loopback_at"], row["match"] = round(at, 4), round(score, 2)
            if score > 0.5:
                placed.append(at - row["t"])
    if not placed:
        raise SystemExit("No logged take was found in the speaker output.")
    audio_zero = float(np.median(placed))
    for row in rows:
        if "loopback_at" in row:
            row["residual_ms"] = round((row["loopback_at"] - audio_zero - row["t"]) * 1000, 1)

    # Video: the scenario starts in the frame where the lobby widgets are removed (the probe removes them in the same tick
    # that it logs the recording start), the largest frame change of the recording. The swing and cast releases are then
    # checked against the local frame-change peaks.
    releases = [r["t"] for r in rows if r["cue"].startswith("Swing")] + [r["t"] for r in rows if r["cue"].startswith("Skill")]
    times, change = frame_changes(args.ffmpeg, run / "window.mkv")
    video_zero = float(times[int(np.argmax(change))])
    spikes = {}
    for r in releases:
        near = np.abs(times - (video_zero + r)) <= 0.1
        spikes[str(r)] = round(float(times[near][np.argmax(change[near])] - video_zero - r) * 1000, 1) if near.any() else None

    # Sheet: the speaker output on the scenario clock, with the video's frame change under it.
    first = max(int(round(audio_zero * rate)), 0)
    shown = mono[first: first + int(seconds * rate)]
    t = np.arange(len(shown)) / rate
    fig, (ax, bx) = plt.subplots(2, 1, figsize=(16, 6.5), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    ax.plot(t, shown, lw=0.3, color="#444")
    for s_t, _, _ in steps:
        ax.axvline(s_t, color="#2a7", lw=0.8, alpha=0.6)
    for i, row in enumerate(rows):
        ax.axvline(row["t"], color="#d40", lw=0.8)
        ax.text(row["t"], 0.92 - 0.11 * (i % 7), row["cue"], fontsize=7, color="#d40")
    ax.set_ylim(-1, 1)
    ax.set_title("Speaker output (WASAPI loopback) on the game's event clock; green: scenario steps, red: sounds the game played")
    vt = times - video_zero
    keep = (vt >= 0) & (vt <= seconds)
    bx.plot(vt[keep], np.minimum(change[keep], np.percentile(change[keep], 99) if keep.any() else 1), lw=0.8, color="#36c")
    for r in releases:
        bx.axvline(r, color="#d40", lw=0.6)
    bx.set_ylabel("frame change")
    bx.set_xlabel("seconds from the scenario start")
    bx.set_xlim(0, seconds)
    plt.tight_layout()
    plt.savefig(out / "timing.png", dpi=110)
    report = {"steps": [{"t": s_t, "step": s} for s_t, _, s in steps], "sounds": rows,
              "loopback_scenario_start_s": round(audio_zero, 4), "loopback_wallclock_estimate_s": round(estimate, 4),
              "speaker_lag_vs_wallclock_ms": round((audio_zero - estimate) * 1000, 1),
              "video_scenario_start_s": round(video_zero, 4), "video_release_spike_residual_ms": spikes}
    (out / "timing.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    cmd = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{video_zero:.3f}", "-i", str(run / "window.mkv"),
           "-ss", f"{audio_zero:.3f}", "-i", str(run / "loopback.wav"), "-t", f"{seconds:.2f}",
           "-map", "0:v", "-map", "1:a", "-vf", "fps=30", "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", str(out / "feel.mp4")]
    subprocess.run(cmd, check=True)
    residuals = [r["residual_ms"] for r in rows if r.get("match", 0) > 0.5]
    print(f"audio: {len(placed)} takes placed, residual {min(residuals):.1f}..{max(residuals):.1f} ms; "
          f"speaker vs wall clock {report['speaker_lag_vs_wallclock_ms']} ms")
    print(f"video: release spikes {spikes} ms from the logged releases")
    print(f"wrote {out / 'feel.mp4'} and {out / 'timing.png'}")


if __name__ == "__main__":
    main()
