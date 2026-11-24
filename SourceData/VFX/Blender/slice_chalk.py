# Chalk telegraph for the presentation slice (the anticipation layer before a skill's release):
#   tex_chalk_lane.png 512 x 128, tileable along U: a court lane drawn in chalk - two border lines along the lane and
#   four forward chevrons per tile, grainy (broken by noise like chalk on paving), white with a thin ink rim.
# Laid flat on the ground over the skill's own footprint (U along the lane, V across), shown in white only.
# Run from the repository root (prints the file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_chalk.py -- SourceData/VFX/Slice
import math, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))


def chalk_lane(width=512, height=128):
    # u in [0, 4) across the tile (one chevron per unit, so the tile loops), v in [-1, 1] across the lane (+1 = top row)
    u = (np.arange(width, dtype=np.float32) + 0.5) / width * 4.0
    v = 1.0 - 2.0 * (np.arange(height, dtype=np.float32) + 0.5) / height
    U, V = np.meshgrid(u, v)
    px = 2.0 / height
    # border lines just inside the lane edges, with a slow tileable wobble so they read as hand-drawn
    wobble = 0.018 * np.sin(2 * math.pi * U / 4.0 * 3 + 0.7) + 0.012 * np.sin(2 * math.pi * U / 4.0 * 7 + 2.1)
    border = np.abs(np.abs(V) - (0.84 + wobble)) - 0.055
    # forward chevrons ">" centred in each unit: two strokes meeting at the tip on the centre line
    f = U - np.floor(U) - 0.5                                   # -0.5 .. 0.5 inside each unit, tip at +0.16
    arm = np.abs(V) * 0.42                                      # the arms sweep back as they leave the centre line
    chevron = np.abs(f - 0.16 + arm) - 0.05
    chevron = np.maximum(chevron, np.abs(V) - 0.46)             # arm length
    lines = np.minimum(border, chevron)
    body = L.aa(lines, px)
    # chalk grain: a tileable fine noise knocks holes into the strokes and frays their edges
    grain = L.fbm(width, height, 32, 8, 907, 3)
    streak = L.fbm(width, height, 8, 16, 911, 2)
    keep = L.smoothstep(0.30, 0.52, grain * 0.75 + streak * 0.25)
    body = body * keep
    ink = np.maximum(L.aa(lines - 0.035, px) * 0.55 * keep, body)
    return L.pack_layers(ink, body, body)


def main():
    img = chalk_lane()
    print("LD_SLICE_TEXTURE tex_chalk_lane %dx%d sha256=" % (img.shape[1], img.shape[0])
          + L.write_png(os.path.join(OUT, "tex_chalk_lane.png"), img))


main()
