"""Measure Demo cue loudness and write per-cue playback volumes into SourceData/Audio/audio_cues.json.

Authoring tool only (the import and validation never run it). It needs third-party packages:
    python -m pip install numpy scipy soundfile
The CC0 source bytes are never modified; only each cue's "volume" (USoundWave Volume, applied by
Scripts/ImportDemoAudio.ps1 -Mode Import) and its measured "loudness_lufs" change.

Loudness is the maximum momentary loudness (ITU-R BS.1770 K-weighting, 400 ms window, 10 ms hop,
channel energies summed) so short cues are measured over the same window as long jingles. Every cue is
brought to the median of all source files, and a boost is capped so the sample peak stays at or below PEAK_LIMIT.
A cue may reuse another cue's file with an optional "level_db" offset (e.g. -6 for a quieter variant), applied
after that normalization and cap.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import soundfile
from scipy.signal import lfilter

ROOT = Path(__file__).resolve().parent.parent / "SourceData" / "Audio"
MANIFEST = ROOT / "audio_cues.json"
WINDOW_SEC = 0.4
HOP_SEC = 0.01
PEAK_LIMIT = 0.95
MIN_VOLUME = 0.1


def biquad(rate, kind, gain_db, q, fc):
    a = 10 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / rate
    alpha = np.sin(w0) / (2.0 * q)
    cos = np.cos(w0)
    if kind == "high_shelf":
        b = [a * ((a + 1) + (a - 1) * cos + 2 * np.sqrt(a) * alpha),
             -2 * a * ((a - 1) + (a + 1) * cos),
             a * ((a + 1) + (a - 1) * cos - 2 * np.sqrt(a) * alpha)]
        den = [(a + 1) - (a - 1) * cos + 2 * np.sqrt(a) * alpha,
               2 * ((a - 1) - (a + 1) * cos),
               (a + 1) - (a - 1) * cos - 2 * np.sqrt(a) * alpha]
    else:
        b = [(1 + cos) / 2, -(1 + cos), (1 + cos) / 2]
        den = [1 + alpha, -2 * cos, 1 - alpha]
    return np.array(b) / den[0], np.array(den) / den[0]


def max_momentary_loudness(samples, rate):
    shelf = biquad(rate, "high_shelf", 4.0, 1 / np.sqrt(2), 1500.0)
    high_pass = biquad(rate, "high_pass", 0.0, 0.5, 38.0)
    window = int(round(WINDOW_SEC * rate))
    hop = max(1, int(round(HOP_SEC * rate)))
    # Zero padding lets a cue shorter than the window be measured as one complete window.
    padded = np.concatenate([np.zeros((window, samples.shape[1])), samples, np.zeros((window, samples.shape[1]))])
    weighted = lfilter(*high_pass, lfilter(*shelf, padded, axis=0), axis=0)
    energy = np.concatenate([np.zeros((1, weighted.shape[1])), np.cumsum(weighted ** 2, axis=0)])
    starts = np.arange(0, len(weighted) - window + 1, hop)
    per_window = (energy[starts + window] - energy[starts]).sum(axis=1) / window
    return -0.691 + 10 * np.log10(per_window.max())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--write", action="store_true", help="write volume and loudness_lufs into the manifest")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    measured = []
    for cue in manifest["cues"]:
        samples, rate = soundfile.read(ROOT / cue["source"], dtype="float64", always_2d=True)
        measured.append((cue, max_momentary_loudness(samples, rate), float(np.abs(samples).max())))
    # A file reused by several cues counts once for the target.
    first = {}
    for cue, loudness, _ in measured:
        first.setdefault(cue["source"], loudness)
    target = float(np.median(list(first.values())))
    print(f"target {target:.2f} LUFS (median max momentary loudness)")
    for cue, loudness, peak in measured:
        volume = min(10 ** ((target - loudness) / 20), PEAK_LIMIT / peak) * 10 ** (cue.get("level_db", 0.0) / 20)
        volume = round(max(volume, MIN_VOLUME), 3)
        cue["loudness_lufs"] = round(loudness, 2)
        cue["volume"] = volume
        print(f"{cue['cue']:<20} {loudness:7.2f} LUFS  peak {peak:5.3f}  volume {volume:5.3f}")
    if args.write:
        MANIFEST.write_bytes((json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        print(f"wrote {MANIFEST}")


if __name__ == "__main__":
    main()
