# Assembles the chain look evidence of a CaptureDemoArt.ps1 -ChainLooks run (DemoArt.json chain_runs / chain_perf):
#   sheet-impact.png   rows = looks, columns = scenarios at the first frame after the first hit, gameplay scale (a native
#                      640 x 540 crop around the action, no scaling), and under each look a 2x crop row of the same frames
#   <look>-<scenario>.mp4          the captured frames at real game speed (30 fps, H.264)
#   <look>-<scenario>-0.25x.mp4    the same frames at quarter speed (labelled in the file name)
#   perf-<look>.json   the frame-cost run of that look
#   python -B Scripts/MakeChainLookEvidence.py <capture folder> <output folder> [ffmpeg.exe]
import json, os, subprocess, sys
from PIL import Image, ImageDraw

capture, out = sys.argv[1], sys.argv[2]
ffmpeg = sys.argv[3] if len(sys.argv) > 3 else "ffmpeg"
os.makedirs(out, exist_ok=True)
report = json.load(open(os.path.join(capture, "DemoArt.json"), encoding="utf-8"))
runs = report.get("chain_runs", [])
looks = list(dict.fromkeys(run["look"] for run in runs))
scenarios = list(dict.fromkeys(run["scenario"] for run in runs))
W, H = 640, 540
CX, CY = 980, 470  # the stage bodies sit around the frame centre at the default game camera

def impact_frame(run):
    hit = run["first_hit_seconds"] + 1.0 / 30.0
    for index, seconds in enumerate(run["frame_game_seconds"]):
        if seconds >= hit:
            return index
    return len(run["frame_game_seconds"]) - 1

sheet = Image.new("RGB", (W * len(scenarios), H * 2 * len(looks)), (0, 0, 0))
draw = ImageDraw.Draw(sheet)
for row, look in enumerate(looks):
    for col, scenario in enumerate(scenarios):
        run = next((r for r in runs if r["look"] == look and r["scenario"] == scenario), None)
        if not run:
            continue
        folder = os.path.join(capture, run["folder"])
        frame = Image.open(os.path.join(folder, "%03d.png" % impact_frame(run))).convert("RGB")
        native = frame.crop((CX - W // 2, CY - H // 2, CX + W // 2, CY + H // 2))
        zoom = frame.crop((CX - W // 4, CY - H // 4, CX + W // 4, CY + H // 4)).resize((W, H), Image.LANCZOS)
        sheet.paste(native, (col * W, row * 2 * H))
        sheet.paste(zoom, (col * W, (row * 2 + 1) * H))
        draw.text((col * W + 8, row * 2 * H + 8), "%s  %s  frame %d (1x)" % (look, scenario, impact_frame(run)), fill=(255, 0, 255))
        draw.text((col * W + 8, (row * 2 + 1) * H + 8), "%s  %s  2x crop" % (look, scenario), fill=(255, 0, 255))
        for speed, suffix in ((1.0, ""), (0.25, "-0.25x")):
            target = os.path.join(out, "%s-%s%s.mp4" % (look, scenario, suffix))
            filters = "setpts=%g*PTS,scale=1280:-2" % (1.0 / speed)
            subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", "30", "-i", os.path.join(folder, "%03d.png"),
                            "-vf", filters, "-r", "30", "-c:v", "libx264", "-pix_fmt", "yuv420p", target], check=True)
sheet.save(os.path.join(out, "sheet-impact.png"))
for perf in report.get("chain_perf", []):
    json.dump(perf, open(os.path.join(out, "perf-%s.json" % perf["look"]), "w", encoding="utf-8"), indent=2)
print("looks", looks, "scenarios", scenarios, "->", out)
