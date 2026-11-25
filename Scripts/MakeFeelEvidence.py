"""Turn a Scripts/CaptureFeelSample.ps1 run into evidence: feel.mp4 (the game's own frames and its own audio, both placed on
the game's own event clock) and timing.png / timing.json (the game audio with every logged step and sound).

    python Scripts/MakeFeelEvidence.py --run Artifacts/<stamp>-FeelSample-1600x900 --ffmpeg <ffmpeg.exe> --out <folder>
Needs numpy, soundfile and matplotlib. The audio is the game's master submix as it played (game_audio.wav), not a
re-render; the video is the game's own frames (Probes/LDGameRecorder.h, game.mp4). Neither reads the desktop or the system
audio. The audio is placed where the logged takes match it, the video where its frame changes match the logged releases;
both raw offsets are reported.
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
LABEL = re.compile(r"LD_FEEL_LABEL (?P<text>.+)$")


def subtitles(labels, seconds, path):
    """An ASS file showing each logged label from its step until the next one."""
    def clock(t):
        return f"{int(t // 3600)}:{int(t % 3600 // 60):02d}:{t % 60:05.2f}"
    lines = ["[Script Info]", "ScriptType: v4.00+", "PlayResX: 1600", "PlayResY: 900", "",
             "[V4+ Styles]", "Format: Name, Fontname, Fontsize, PrimaryColour, OutlineColour, BorderStyle, Outline, Shadow, Alignment, MarginV",
             "Style: Default,Microsoft YaHei,46,&H00FFFFFF,&H00000000,1,3,0,8,40", "", "[Events]",
             "Format: Layer, Start, End, Style, Text"]
    for i, (t, text) in enumerate(labels):
        end = labels[i + 1][0] if i + 1 < len(labels) else seconds
        lines.append(f"Dialogue: 0,{clock(t)},{clock(end)},Default,{text}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8-sig")


def take_sources():
    """SW_<Name>[_<n>] asset name -> its source file, from the audio manifest."""
    root = Path(__file__).resolve().parent.parent / "SourceData" / "Audio"
    manifest = json.loads((root / "audio_cues.json").read_text(encoding="utf-8"))
    out = {}
    for entry in manifest["cues"] + manifest.get("skills", []) + [dict(h, ability=h["ability"] + "_hit") for h in manifest.get("skill_hits", [])]:
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
    steps, events, labels = [], [], []
    for line in log:
        if m := STEP.search(line):
            steps.append((float(m["t"]), float(m["w"]), int(m["s"])))
        elif m := LABEL.search(line):
            labels.append((steps[-1][0] if steps else 0.0, m["text"].strip()))
        elif m := EVENT.search(line):
            events.append((float(m["t"]), m["cue"], m["take"], float(m["gain"])))
    if not steps:
        raise SystemExit("No LD_FEEL_STEP lines: the scenario did not run.")
    world_at_start = steps[0][1] - steps[0][0]
    rows = [{"t": round(e_t - world_at_start, 4), "cue": cue, "take": take, "gain": gain} for e_t, cue, take, gain in events if e_t >= world_at_start]
    seconds = max(r["t"] for r in rows) + 1.2

    # Audio: where the speaker output carries each logged take (around the wall-clock estimate of the scenario start).
    # The recorder's audio starts before the scenario: the takes are searched near the scenario start on its clock.
    recording = json.loads((run / "game" / "recorder.json").read_text(encoding="utf-8"))
    scenario = datetime.datetime.fromisoformat(next(l for l in log if "LD_FEEL_RECORDING_START" in l).split("utc=")[1].strip().replace("Z", "+00:00"))
    recorder = datetime.datetime.fromisoformat(recording["start_utc"].replace("Z", "+00:00"))
    estimate = (scenario - recorder).total_seconds() - float(recording["audio_start_seconds"])
    game_audio = run / "game" / "game_audio.wav"
    audio, rate = soundfile.read(game_audio, always_2d=True)
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
            row["audio_at"], row["match"] = round(at, 4), round(score, 2)
            if score > 0.5:
                placed.append(at - row["t"])
    if not placed:
        raise SystemExit("No logged take was found in the game audio.")
    audio_zero = float(np.median(placed))
    for row in rows:
        if "audio_at" in row:
            row["residual_ms"] = round((row["audio_at"] - audio_zero - row["t"]) * 1000, 1)

    # Video: the scenario starts in the frame where the lobby widgets are removed (the probe removes them in the same tick
    # that it logs the recording start), the largest frame change of the recording. The swing and cast releases are then
    # checked against the local frame-change peaks.
    releases = [r["t"] for r in rows if r["cue"].startswith("Swing")] + [r["t"] for r in rows if r["cue"].startswith("Skill")]
    times, change = frame_changes(args.ffmpeg, run / "game.mp4")
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
    ax.set_title("Game audio (master submix) on the game's event clock; green: scenario steps, red: sounds the game played")
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
              "audio_scenario_start_s": round(audio_zero, 4),
              "video_scenario_start_s": round(video_zero, 4), "video_release_spike_residual_ms": spikes}
    (out / "timing.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    video_filter = "fps=30"
    if labels:
        subtitles(labels, seconds, out / "labels.ass")
        video_filter += ",subtitles=labels.ass"
    cmd = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{video_zero:.3f}", "-i", str((run / "game.mp4").resolve()),
           "-ss", f"{audio_zero:.3f}", "-i", str(game_audio.resolve()), "-t", f"{seconds:.2f}",
           "-map", "0:v", "-map", "1:a", "-vf", video_filter, "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", "feel.mp4"]
    subprocess.run(cmd, check=True, cwd=out)
    residuals = [r["residual_ms"] for r in rows if r.get("match", 0) > 0.5]
    print(f"audio: {len(placed)} takes placed, residual {min(residuals):.1f}..{max(residuals):.1f} ms")
    print(f"video: release spikes {spikes} ms from the logged releases")
    print(f"wrote {out / 'feel.mp4'} and {out / 'timing.png'}")


if __name__ == "__main__":
    main()
