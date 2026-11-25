"""Mux a -LDRecordGame folder (Source/Lockdown/Private/Probes/LDGameRecorder.h) into one mp4.

The frames carry their game-clock times in frames.csv, so the video keeps real timing even when the capture rate varied;
game_audio.wav is the game's own master submix, placed at its recorded start on the same clock. Nothing here touches the
desktop or the system audio.
"""
import argparse
import csv
import json
import pathlib
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=pathlib.Path)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args()
    folder = args.folder.resolve()
    out = (args.out or folder / "game.mp4").resolve()
    meta = json.loads((folder / "recorder.json").read_text(encoding="utf-8"))
    with (folder / "frames.csv").open(encoding="utf-8") as handle:
        rows = [(int(row["index"]), float(row["seconds"])) for row in csv.DictReader(handle)]
    if len(rows) < 2:
        print(f"Too few frames in {folder}: {len(rows)}", file=sys.stderr)
        return 1
    listing = folder / "frames.ffconcat"
    lines = ["ffconcat version 1.0"]
    for (index, seconds), (_, following) in zip(rows, rows[1:]):
        lines += [f"file 'frames/{index:06d}.jpg'", f"duration {max(following - seconds, 0.001):.4f}"]
    lines += [f"file 'frames/{rows[-1][0]:06d}.jpg'", "duration 0.0333"]
    listing.write_text("\n".join(lines) + "\n", encoding="utf-8")
    command = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(listing)]
    audio = folder / "game_audio.wav"
    if meta.get("audio") and audio.exists():
        # The audio began at audio_start_seconds on the frame clock; the video begins at the first frame's time.
        offset = float(meta["audio_start_seconds"]) - rows[0][1]
        command += ["-itsoffset", f"{offset:.4f}", "-i", str(audio), "-map", "0:v", "-map", "1:a", "-c:a", "aac", "-b:a", "192k"]
    else:
        print("No game audio was recorded; the video is silent.", file=sys.stderr)
    command += ["-vf", "fps=30,format=yuv420p", "-c:v", "libx264", "-preset", "medium", "-crf", "22", str(out)]
    subprocess.run(command, check=True)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
