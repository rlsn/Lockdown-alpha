"""Encode actual timestamped native client screenshots; no synthetic frames."""
import csv
import hashlib
import json
import subprocess
import sys
from pathlib import Path
import imageio_ffmpeg


def encode(directory):
    report = json.loads((directory/'Network.json').read_bytes())
    assert report['complete'] and not report['failures']
    output = {}
    roles = [role for role in ('A', 'B') if (directory/f'{role}_Frames.csv').is_file()]
    assert roles
    for role in roles:
        rows = list(csv.DictReader((directory/f'{role}_Frames.csv').read_text().splitlines()))
        frames = [(row, directory/'frames'/role/row['frame']) for row in rows]
        frames = [(row, p) for row, p in frames if p.is_file()]
        assert len(frames) >= 10
        concat = directory/f'{role}_VideoInput.txt'
        lines = []
        for i, (row, p) in enumerate(frames):
            lines.append("file '"+p.resolve().as_posix().replace("'", "'\\''")+"'")
            duration = float(frames[i+1][0]['world_seconds'])-float(row['world_seconds']) if i+1 < len(frames) else .2
            assert duration > 0
            lines.append(f'duration {duration:.6f}')
        concat.write_text('\n'.join(lines)+'\n')
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), '-y', '-loglevel', 'warning', '-f', 'concat', '-safe', '0',
                        '-i', str(concat), '-fps_mode', 'vfr', '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-crf', '23', '-movflags', '+faststart', str(directory/f'{role}.mp4')], check=True)
        output[role] = dict(frames=len(frames), missing=len(rows)-len(frames),
                            duration_seconds=float(frames[-1][0]['world_seconds'])-float(frames[0][0]['world_seconds']),
                            video_sha256=hashlib.sha256((directory/f'{role}.mp4').read_bytes()).hexdigest(),
                            raw_frames_sha256={r['frame']: hashlib.sha256(p.read_bytes()).hexdigest() for r, p in frames})
    (directory/'VideoSummary.json').write_text(json.dumps(dict(clients=output,
        scope='Actual native offscreen client frames sampled at ~5Hz; timestamp-paced VFR videos. Diagnostic setup placements; no FPS/performance/human acceptance.'), indent=2)+'\n')
    print('Encoded native client videos with source timestamps and frame hashes:', ', '.join(roles))


if __name__ == '__main__':
    encode(Path(sys.argv[1]))
