"""Record what the default speaker plays (WASAPI loopback) until a stop file appears; used by CaptureFeelSample.ps1.

    python Scripts/RecordAudioLoopback.py --out loopback.wav --stop stop.flag
Needs `python -m pip install soundcard soundfile numpy`. Writes <out>.json with the UTC time of the first recorded frame.
The recording is the device output itself (every application's sound), so close other audio sources while capturing.
"""
import argparse
import datetime
import json
import threading
import time
from pathlib import Path

import numpy as np
import soundcard
import soundfile

RATE = 48000
BLOCK = 9600  # 200 ms: large enough that a busy machine never overruns the capture buffer


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--stop", required=True, type=Path)
    parser.add_argument("--max-seconds", type=float, default=600)
    args = parser.parse_args()
    speaker = soundcard.default_speaker()
    loopback = soundcard.get_microphone(id=str(speaker.name), include_loopback=True)
    blocks, started = [], None
    # WASAPI loopback only delivers frames while a render stream is active; digital silence on the same device keeps the
    # endpoint running, so the recording's timeline has no gaps when the game is silent or still loading.
    running = threading.Event()
    running.set()

    def keep_alive():
        with speaker.player(samplerate=RATE, blocksize=BLOCK) as player:
            silence = np.zeros((BLOCK, 2))
            while running.is_set():
                player.play(silence)

    pump = threading.Thread(target=keep_alive, daemon=True)
    pump.start()
    time.sleep(0.3)
    with loopback.recorder(samplerate=RATE, blocksize=BLOCK * 4) as recorder:
        begin = time.monotonic()
        while not args.stop.exists() and time.monotonic() - begin < args.max_seconds:
            data = recorder.record(numframes=BLOCK)
            if started is None:
                # The first block ends now; its first frame started one block earlier.
                started = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(seconds=len(data) / RATE)
            blocks.append(data)
    running.clear()
    pump.join(timeout=2)
    audio = np.concatenate(blocks) if blocks else np.zeros((0, 2))
    soundfile.write(args.out, audio, RATE, subtype="PCM_16")
    args.out.with_suffix(".json").write_text(json.dumps({"start_utc": started.isoformat() if started else None,
                                                          "seconds": len(audio) / RATE, "device": str(speaker.name)}), encoding="utf-8")


if __name__ == "__main__":
    main()
