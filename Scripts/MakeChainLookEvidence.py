# Assembles the chain look evidence of a CaptureDemoArt.ps1 -ChainLooks run (DemoArt.json chain_runs / chain_perf):
#   sheet-impact.png        rows = looks, columns = scenarios at the impact frame: a native 800 x 540 crop around the
#                           action (gameplay scale, no scaling) and under it a 2x crop of the same frame
#   <look>/<scenario>.png   keyframes at normal speed: six consecutive 1/30 s frames from the one before the impact
#   <look>.mp4              every scenario of the look back to back at real game speed (30 fps, H.264), labelled
#   <look>-0.25x.mp4        the same at quarter speed (labelled 0.25x)
#   combined.mp4            all looks side by side, frame-locked, at real game speed
#   perf-<look>.json        the frame-cost run of that look
#   python -B Scripts/MakeChainLookEvidence.py <capture folder> <output folder> [ffmpeg.exe]
import json, os, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw

capture, out = sys.argv[1], sys.argv[2]
ffmpeg = sys.argv[3] if len(sys.argv) > 3 else "ffmpeg"
os.makedirs(out, exist_ok=True)
report = json.load(open(os.path.join(capture, "DemoArt.json"), encoding="utf-8"))
runs = report.get("chain_runs", [])
looks = list(dict.fromkeys(run["look"] for run in runs))
scenarios = list(dict.fromkeys(run["scenario"] for run in runs))
W, H = 800, 540
LABEL = (255, 0, 255)

def run_of(look, scenario):
    return next((r for r in runs if r["look"] == look and r["scenario"] == scenario), None)

def frame(run, index):
    index = max(0, min(index, len(run["frame_game_seconds"]) - 1))
    return Image.open(os.path.join(capture, run["folder"], "%03d.png" % index)).convert("RGB")

def impact_frame(run):
    hit = run["first_hit_seconds"] + 0.06  # the hit shows on the second frame after the event
    for index, seconds in enumerate(run["frame_game_seconds"]):
        if seconds >= hit:
            return index
    return len(run["frame_game_seconds"]) - 1

# The crop centre of a scenario: where the impact frames of all looks differ from their first frame.
centres = {}
for scenario in scenarios:
    weight = None
    for look in looks:
        run = run_of(look, scenario)
        if not run:
            continue
        a = np.asarray(frame(run, 0), dtype=np.int16)
        diff = np.zeros(a.shape[:2], dtype=np.float64)
        for k in range(impact_frame(run), impact_frame(run) + 4):
            diff += np.abs(np.asarray(frame(run, k), dtype=np.int16) - a).sum(axis=2) > 60
        weight = diff if weight is None else weight + diff
    ys, xs = np.nonzero(weight)
    cx, cy = (int(xs.mean()), int(ys.mean())) if len(xs) else (960, 540)
    centres[scenario] = (min(max(cx, W // 2), 1920 - W // 2), min(max(cy, H // 2), 1080 - H // 2))

def crop(image, centre, scale):
    cx, cy = centre
    w, h = W // scale, H // scale
    return image.crop((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2)).resize((W, H), Image.LANCZOS)

sheet = Image.new("RGB", (W * len(scenarios), H * 2 * len(looks)), (0, 0, 0))
draw = ImageDraw.Draw(sheet)
for row, look in enumerate(looks):
    os.makedirs(os.path.join(out, look), exist_ok=True)
    for col, scenario in enumerate(scenarios):
        run = run_of(look, scenario)
        if not run:
            continue
        hit = impact_frame(run)
        shot = frame(run, hit)
        sheet.paste(crop(shot, centres[scenario], 1), (col * W, row * 2 * H))
        sheet.paste(crop(shot, centres[scenario], 2), (col * W, (row * 2 + 1) * H))
        draw.text((col * W + 8, row * 2 * H + 8), "%s  %s  frame %d  t=%.3fs (1x)" % (look, scenario, hit, run["frame_game_seconds"][hit]), fill=LABEL)
        draw.text((col * W + 8, (row * 2 + 1) * H + 8), "%s  %s  2x crop" % (look, scenario), fill=LABEL)
        strip = Image.new("RGB", (W * 3, H * 2))
        for k in range(6):
            index = hit - 1 + k
            strip.paste(crop(frame(run, index), centres[scenario], 1), ((k % 3) * W, (k // 3) * H))
            ImageDraw.Draw(strip).text(((k % 3) * W + 8, (k // 3) * H + 8), "%s %s frame %d t=%.3fs" % (
                look, scenario, index, run["frame_game_seconds"][max(0, min(index, len(run["frame_game_seconds"]) - 1))]), fill=LABEL)
        strip.save(os.path.join(out, look, "%s.png" % scenario))
sheet.save(os.path.join(out, "sheet-impact.png"))

# Videos: labelled 1280 x 720 frames, every scenario of a look in order.
work = tempfile.mkdtemp(prefix="ldchain")
def encode(folder, target, speed):
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", "30", "-i", os.path.join(folder, "%05d.png"),
                    "-vf", "setpts=%g*PTS" % (1.0 / speed), "-r", "30", "-c:v", "libx264", "-pix_fmt", "yuv420p", target], check=True)
lengths = {}
for look in looks:
    folder = os.path.join(work, look)
    os.makedirs(folder)
    count = 0
    for scenario in scenarios:
        run = run_of(look, scenario)
        if not run:
            continue
        for index in range(len(run["frame_game_seconds"])):
            image = frame(run, index).resize((1280, 720), Image.LANCZOS)
            ImageDraw.Draw(image).text((12, 12), "%s  %s  t=%.3fs" % (look, scenario, run["frame_game_seconds"][index]), fill=LABEL)
            image.save(os.path.join(folder, "%05d.png" % count))
            count += 1
    lengths[look] = count
    encode(folder, os.path.join(out, "%s.mp4" % look), 1.0)
    encode(folder, os.path.join(out, "%s-0.25x.mp4" % look), 0.25)
grid = os.path.join(work, "combined")
os.makedirs(grid)
cols = 3 if len(looks) > 4 else 2
rows = (len(looks) + cols - 1) // cols
for index in range(min(lengths.values())):
    tile = Image.new("RGB", (640 * cols, 360 * rows))
    for k, look in enumerate(looks):
        tile.paste(Image.open(os.path.join(work, look, "%05d.png" % index)).resize((640, 360), Image.LANCZOS), ((k % cols) * 640, (k // cols) * 360))
    tile.save(os.path.join(grid, "%05d.png" % index))
encode(grid, os.path.join(out, "combined.mp4"), 1.0)
shutil.rmtree(work)
for perf in report.get("chain_perf", []):
    json.dump(perf, open(os.path.join(out, "perf-%s.json" % perf["look"]), "w", encoding="utf-8"), indent=2)
print("looks", looks, "scenarios", scenarios, "centres", centres, "->", out)
