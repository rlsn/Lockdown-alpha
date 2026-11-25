"""Turn a Scripts/CaptureFeelSample.ps1 run into evidence: feel.mp4 (window video + the game's own recorded mix, aligned on
their logged wall clocks) and timing.png (the recorded waveform with every scenario step and every played sound marked).

    python Scripts/MakeFeelEvidence.py --run Artifacts/<stamp>-FeelSample-1600x900 --ffmpeg <ffmpeg.exe> --out <folder>
Needs numpy, soundfile and matplotlib. The audio is what the game's audio mixer produced (submix recording), not a re-render.
"""
import argparse
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
START = re.compile(r"LD_FEEL_RECORDING_START")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--run", required=True, type=Path)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--nudge", type=float, default=0.0, help="extra audio delay in seconds after checking the sheet")
    args = parser.parse_args()
    run, out = args.run, args.out
    out.mkdir(parents=True, exist_ok=True)
    meta = json.loads((run / "Feel.json").read_text(encoding="utf-8-sig"))
    wav = Path(meta["game_audio"])
    log = (run / "Art.log").read_text(encoding="utf-8", errors="replace").splitlines()
    # World time at the recording start: the first step logs both clocks.
    steps, events, world_at_start = [], [], None
    for line in log:
        if m := STEP.search(line):
            steps.append((float(m["t"]), float(m["w"]), int(m["s"])))
        elif m := EVENT.search(line):
            events.append((float(m["t"]), m["cue"], m["take"], float(m["gain"])))
    if not steps:
        raise SystemExit("No LD_FEEL_STEP lines: the scenario did not run.")
    # The scenario clock starts with the recording (phase 4 begins right after StartRecordingOutput).
    world_at_start = steps[0][1] - steps[0][0]
    audio, rate = soundfile.read(wav, always_2d=True)
    mono = audio.mean(axis=1)
    t = np.arange(len(mono)) / rate
    fig, ax = plt.subplots(figsize=(16, 4.5))
    ax.plot(t, mono, lw=0.3, color="#444")
    for s_t, _, s in steps:
        ax.axvline(s_t, color="#2a7", lw=0.8, alpha=0.6)
    seen = set()
    for e_t, cue, take, gain in events:
        x = e_t - world_at_start
        if x < 0:
            continue
        ax.axvline(x, color="#d40", lw=0.8)
        ax.text(x, 0.92 - 0.12 * (len(seen) % 6), cue, fontsize=7, color="#d40", rotation=0)
        seen.add((x, cue))
    ax.set_xlim(0, t[-1])
    ax.set_ylim(-1, 1)
    ax.set_xlabel("seconds from the recording start (green: scenario steps, red: sounds the game played)")
    ax.set_title("Recorded game mix (master submix) with logged events")
    plt.tight_layout()
    plt.savefig(out / "timing.png", dpi=110)
    rows = [{"t": round(e_t - world_at_start, 4), "cue": cue, "take": take, "gain": gain} for e_t, cue, take, gain in events if e_t >= world_at_start]
    (out / "timing.json").write_text(json.dumps({"steps": [{"t": s_t, "step": s} for s_t, _, s in steps], "sounds": rows}, indent=1), encoding="utf-8")
    offset = float(meta["audio_offset_seconds"]) + args.nudge
    video = run / "window.mkv"
    # The window recording starts at map load; cut it to the game audio's first sample so both start together.
    cmd = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-ss", f"{max(offset, 0):.3f}", "-i", str(video), "-i", str(wav), "-shortest",
           "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
           str(out / "feel.mp4")]
    subprocess.run(cmd, check=True)
    print(f"wrote {out / 'feel.mp4'} (audio offset {offset:.3f} s), {out / 'timing.png'}, {len(rows)} sound events")


if __name__ == "__main__":
    main()
