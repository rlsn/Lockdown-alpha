# Shock assets for active_zap and the electrified / conduct (导电) reaction in the presentation slice:
#   tex_shock_arcs.png   512 x 512: 8 stacked lightning strip variants (512 x 64 each), stretchable along U; rendered from
#                        Blender stroke geometry (slice_render.py)
#   fb_shock_spark.png   4 x 4 frames of 256 x 256: contact spark burst (flickering forked bolts, erodes away)
#   fb_shock_field.png   4 x 4 frames of 256 x 256: LOOPING electric ground field (crackle net + hatch + chalk boundary)
# Run from the repository root (prints each file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_shock.py -- SourceData/VFX/Slice
import math, os, random, sys
import numpy as np

sys.dont_write_bytecode = True  # a __pycache__ folder under SourceData/VFX fails the importer whitelist
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L
import slice_render as R

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))


# ---------------------------------------------------------------- bolt geometry
def bolt_points(rng, p0, p1, depth, jag):
    """Midpoint-displacement lightning between p0 and p1; returns a polyline."""
    pts = [p0, p1]
    amp = jag
    for _ in range(depth):
        nxt = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dy)
            nx, ny = -dy / max(length, 1e-9), dx / max(length, 1e-9)
            off = rng.uniform(-1, 1) * amp * length
            nxt.append(((a[0] + b[0]) / 2 + nx * off, (a[1] + b[1]) / 2 + ny * off))
            nxt.append(b)
        pts = nxt
        amp *= 0.75
    return pts


def draw_polyline(X, Y, pts, radius, tip=0.25):
    sd = np.full(X.shape, 9.0, dtype=np.float32)
    n = len(pts) - 1
    for i in range(n):
        s0, s1 = i / n, (i + 1) / n
        sd = np.minimum(sd, L.sd_segment(X, Y, pts[i], pts[i + 1], radius * (1 - tip * s0), radius * (1 - tip * s1)))
    return sd


def arc_with_branches(rng, p0, p1, depth, jag, branches, branch_len, bounds=None):
    main = bolt_points(rng, p0, p1, depth, jag)
    paths = [main]
    for _ in range(branches):
        k = rng.randint(1, len(main) - 2)
        a = main[k]
        base = math.atan2(p1[1] - p0[1], p1[0] - p0[0]) + rng.choice((-1, 1)) * rng.uniform(0.5, 1.0)
        length = branch_len * rng.uniform(0.6, 1.0)
        b = (a[0] + math.cos(base) * length, a[1] + math.sin(base) * length)
        if bounds:
            b = (min(max(b[0], bounds[0]), bounds[1]), min(max(b[1], bounds[2]), bounds[3]))
        paths.append(bolt_points(rng, a, b, max(depth - 2, 1), jag))
    return paths


# ---------------------------------------------------------------- lightning strip atlas (Blender-rendered strokes)
# Each variant is one confident main bolt plus 1-3 thin short forks, drawn as tapered poly strokes in slice_render:
# ink rim under the shock body under a white-hot core. Strip pixels: x 0..512 along U, y 0..64 across (centre 32).
# Chain links stretch the strip to about 4-8 x its height, so kinks are designed for that aspect (shallow long runs,
# a few sharp short kicks), and the bolt tapers to a thin point where it meets each body.
ARC_W, ARC_H, ARC_VARIANTS = 512, 64, 8
ARC_BODY, ARC_INK, ARC_CORE = 6.2, 3.2, 0.45   # body half-width (px), ink rim (px), core / body ratio
ARC_SWING = 19.0                              # max |y - centre| of the main bolt; ink stays clear of the strip border

# Per variant: segment count, kick probability, swing scale, forks, seed. Distinct silhouettes, not one zig-zag rule.
ARC_STYLE = [
    dict(segs=8, kick=0.35, swing=1.00, forks=2, seed=11),
    dict(segs=7, kick=0.25, swing=0.85, forks=1, seed=23),
    dict(segs=10, kick=0.40, swing=0.95, forks=3, seed=37),
    dict(segs=6, kick=0.50, swing=1.00, forks=2, seed=41),
    dict(segs=9, kick=0.20, swing=0.80, forks=1, seed=59),
    dict(segs=8, kick=0.45, swing=1.00, forks=3, seed=67),
    dict(segs=11, kick=0.30, swing=0.90, forks=2, seed=73),
    dict(segs=7, kick=0.35, swing=0.95, forks=1, seed=89),
]


def bolt_main(rng, style):
    """Main bolt as a turtle walk: irregular segment lengths (long runs, mid steps, short steep kicks), the heading
    turning 25-70 degrees mostly against the last turn and steered back toward the centre; ends on the centre line."""
    centre = ARC_H / 2.0
    xs, ys, heading = [0.0], [0.0], math.radians(rng.uniform(-35.0, 35.0))
    for k in range(style["segs"]):
        roll = rng.random()
        if roll < style["kick"] and 0 < k < style["segs"] - 1:
            length, turn = rng.uniform(8.0, 16.0), rng.uniform(55.0, 80.0)
        elif roll < style["kick"] + 0.3:
            length, turn = rng.uniform(70.0, 130.0), rng.uniform(15.0, 35.0)
        else:
            length, turn = rng.uniform(26.0, 64.0), rng.uniform(25.0, 60.0)
        sign = -1.0 if heading > 0 else 1.0
        if rng.random() < 0.25:
            sign = -sign
        heading = sign * math.radians(turn) - 0.02 * ys[-1]
        heading = max(-1.35, min(1.35, heading))
        xs.append(xs[-1] + math.cos(heading) * length)
        ys.append(ys[-1] + math.sin(heading) * length)
    scale = ARC_W / xs[-1]
    xs = [x * scale for x in xs]
    ys = [y - ys[-1] * x / ARC_W for x, y in zip(xs, ys)]       # both ends back on the centre line
    peak = max(1e-6, max(abs(y) for y in ys))
    swing = ARC_SWING * style["swing"] * rng.uniform(0.9, 1.0)
    ys = [y * swing / peak for y in ys]                         # every variant uses its full swing
    xs[0], xs[-1] = -4.0, ARC_W + 4.0                           # run off both ends so links meet without a gap
    return [(x, centre + y) for x, y in zip(xs, ys)]


def bolt_widths(rng, pts):
    """Half-width per point: thin points at both ends, fattest in the first half, kinks a little heavier."""
    total = pts[-1][0] - pts[0][0]
    out = []
    for i, (x, _y) in enumerate(pts):
        s = (x - pts[0][0]) / total
        taper = L.smoothstep(0.0, 0.10, s) * L.smoothstep(0.0, 0.15, 1.0 - s)
        swell = 0.85 + 0.3 * math.sin(math.pi * min(1.0, s * 1.4)) + rng.uniform(-0.08, 0.12)
        out.append(ARC_BODY * float(max(0.18, taper)) * swell if 0 < i < len(pts) - 1 else ARC_BODY * 0.22)
    return out


def bolt_fork(rng, pts, widths, used):
    """A thin short fork from an interior bolt vertex, leaving toward the roomier side, tapering to a point."""
    centre = ARC_H / 2.0
    candidates = [i for i in range(1, len(pts) - 1) if 0.12 < pts[i][0] / ARC_W < 0.85 and i not in used] or [len(pts) // 2]
    i = rng.choice(candidates)
    used.add(i)
    x, y = pts[i]
    tx, ty = pts[i + 1][0] - pts[i - 1][0], pts[i + 1][1] - pts[i - 1][1]
    side = 1.0 if y < centre else -1.0
    angle = math.atan2(ty, tx) + side * math.radians(rng.uniform(24.0, 48.0))
    length = rng.uniform(50.0, 95.0)
    path, ang = [(x, y)], angle
    for k in range(rng.randint(2, 3)):
        step = length * rng.uniform(0.3, 0.5)
        ang += math.radians(rng.uniform(-28.0, 28.0)) * (1 if k else 0.4)
        nx = path[-1][0] + math.cos(ang) * step
        ny = min(max(path[-1][1] + math.sin(ang) * step * 0.7, 14.0), ARC_H - 14.0)
        path.append((min(max(nx, 6.0), ARC_W - 6.0), ny))
    root = widths[i] * 0.55
    return path, [root * (1.0 - j / (len(path) - 1)) ** 0.8 + 0.25 for j in range(len(path))]


def shock_arcs():
    canvas = R.Canvas(ARC_W, ARC_H * ARC_VARIANTS, 0.0, float(ARC_W), 0.0, float(ARC_H * ARC_VARIANTS))
    for v, style in enumerate(ARC_STYLE):
        rng = random.Random(4000 + style["seed"])
        main = bolt_main(rng, style)
        widths = bolt_widths(rng, main)
        # Pull the swing in where a fat (mitred) kink plus its ink would reach the strip border.
        centre, room = ARC_H / 2.0, ARC_H / 2.0 - 4.0
        fit = min([1.0] + [(room - w * 1.4 - ARC_INK) / abs(y - centre) for (_x, y), w in zip(main, widths) if abs(y - centre) > 1e-3])
        main = [(x, centre + (y - centre) * fit) for x, y in main]
        base = (ARC_VARIANTS - 1 - v) * ARC_H        # variant 0 is the top strip (row 0 of the image)
        lift = lambda path: [(x, y + base) for x, y in path]
        used = set()
        for _ in range(style["forks"]):
            path, radii = bolt_fork(rng, main, widths, used)
            canvas.layered_stroke(lift(path), radii, ARC_INK * 0.7, ARC_CORE)
        canvas.layered_stroke(lift(main), widths, ARC_INK, ARC_CORE, order=1.0)
    img = canvas.render()
    for v in range(ARC_VARIANTS):                     # strip borders stay empty so neighbouring variants never bleed
        img[v * ARC_H:v * ARC_H + 2] = 0.0
        img[(v + 1) * ARC_H - 2:(v + 1) * ARC_H] = 0.0
    return img


# ---------------------------------------------------------------- shared flipbook frame space
SIZE, COLS, FRAMES = 256, 4, 16
X, Y = L.grid(SIZE, SIZE, -1.0, 1.0, -1.0, 1.0)
PX = 2.0 / SIZE


def spark_frame(f):
    if f == FRAMES - 1:
        return np.zeros((SIZE, SIZE, 4), dtype=np.float32)
    rng = random.Random(500 + (f // 2) * 17)  # a new bolt set every second frame = electric flicker
    grow = [0.45, 0.85, 1.0][min(f, 2)]
    body = np.full((SIZE, SIZE), 9.0, dtype=np.float32)
    ink, core = body.copy(), body.copy()
    life_rng = random.Random(91)
    for k in range(7):
        ang = 2 * math.pi * k / 7 + rng.uniform(-0.3, 0.3)
        length = rng.uniform(0.6, 0.95) * grow * (0.75 if k % 3 == 0 else 1.0)
        p0 = (0.07 * math.cos(ang), 0.07 * math.sin(ang))
        p1 = (length * math.cos(ang), length * math.sin(ang))
        paths = arc_with_branches(rng, p0, p1, 3, 0.3, 1, 0.3 * grow, (-0.97, 0.97, -0.97, 0.97))
        life = life_rng.uniform(7, 13)
        keep = min(1.0, max(0.0, (life - f) / 4.0))
        if keep <= 0:
            continue
        for i, path in enumerate(paths):
            cut = max(2, int(math.ceil(len(path) * keep)))
            part = path[:cut]
            weight = (1.0 if i == 0 else 0.6) * (0.5 + 0.5 * keep)
            ink = np.minimum(ink, draw_polyline(X, Y, part, 0.05 * weight, 0.45))
            body = np.minimum(body, draw_polyline(X, Y, part, 0.032 * weight, 0.45))
            core = np.minimum(core, draw_polyline(X, Y, part, 0.013 * weight, 0.45))
    # detached zig-zag fragments around the contact point
    if 1 <= f <= 9:
        for k in range(3):
            a = rng.uniform(0, 6.283)
            r = rng.uniform(0.35, 0.6)
            c = (r * math.cos(a), r * math.sin(a))
            frag = bolt_points(rng, (c[0] - 0.1, c[1] - 0.05), (c[0] + 0.1, c[1] + 0.05), 3, 0.35)
            ink = np.minimum(ink, draw_polyline(X, Y, frag, 0.036, 0.3))
            body = np.minimum(body, draw_polyline(X, Y, frag, 0.02, 0.3))
            core = np.minimum(core, draw_polyline(X, Y, frag, 0.008, 0.3))
    # hot centre: a small four-point flash for the first frames
    if f <= 5:
        size = [0.12, 0.2, 0.24, 0.2, 0.15, 0.1][f]
        flash = [(size * 1.4, 0), (size * 0.18, size * 0.18), (0, size * 1.4), (-size * 0.18, size * 0.18), (-size * 1.4, 0), (-size * 0.18, -size * 0.18), (0, -size * 1.4), (size * 0.18, -size * 0.18)]
        sd = L.sd_poly(X, Y, flash)
        ink = np.minimum(ink, sd - 0.012)
        body = np.minimum(body, sd)
        core = np.minimum(core, sd + 0.01)
    return L.pack_layers(L.aa(ink, PX), L.aa(body, PX), L.aa(np.maximum(core, body), PX))


def shock_spark():
    return L.flipbook([spark_frame(f) for f in range(FRAMES)], COLS)


# ---------------------------------------------------------------- electric ground field (looping)
def worley_edge(Xw, Yw, cells, seed):
    """F2 - F1 on warped coordinates: small along cell boundaries (jagged crack network)."""
    rng = np.random.RandomState(seed)
    pts = rng.rand(cells + 10, cells + 10, 2).astype(np.float32)
    u = (Xw + 1.0) * 0.5 * cells + 5
    v = (Yw + 1.0) * 0.5 * cells + 5
    cx, cy = np.floor(u).astype(int), np.floor(v).astype(int)
    f1 = np.full(u.shape, 9.0, dtype=np.float32)
    f2 = f1.copy()
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            gx = np.clip(cx + ox, 0, cells + 9)
            gy = np.clip(cy + oy, 0, cells + 9)
            p = pts[gy, gx]
            d = np.hypot(u - (gx + p[..., 0]), v - (gy + p[..., 1]))
            f2 = np.minimum(f2, np.maximum(f1, d))
            f1 = np.minimum(f1, d)
    return (f2 - f1) / cells * 2.0  # in frame units


def field_frame(f, warp_x, warp_y, flicker, edge):
    # boundary: irregular rounded patch with a dashed chalk line
    theta = np.arctan2(Y, X)
    rad = 0.74 + 0.05 * np.sin(3 * theta + 0.6) + 0.035 * np.sin(5 * theta + 2.0) + 0.02 * np.sin(9 * theta + 4.1)
    patch_sd = np.hypot(X, Y) - rad
    # crack network flickers: each of 4 noise fields lights part of the net, cycling so frame f and f + 16 match
    vis = L.smoothstep(0.38, 0.52, flicker[f % 4])
    gy_, gx_ = np.gradient(edge, PX)
    crack_sd = (edge - 0.02) / np.maximum(np.hypot(gx_, gy_), 0.5)  # gradient-normalised: a true distance, no fuzzy junctions
    crack_sd = np.maximum(crack_sd, patch_sd + 0.02)
    crack_sd = np.where(vis > 0.5, crack_sd, 9.0)
    # diagonal court hatch inside the patch; the pattern scrolls exactly one period over the loop
    phase = f / FRAMES
    d = (X * math.cos(math.radians(40)) + Y * math.sin(math.radians(40))) / 0.17 + phase
    frac = d - np.floor(d)
    hatch_sd = np.maximum(np.abs(frac - 0.5) * 0.17 - 0.02 * 0.5 - 0.0, patch_sd + 0.05)
    hatch_sd = hatch_sd + 0.012
    # chalk boundary: thin line on the patch edge, broken into dashes that also rotate one period over the loop
    dash = np.sin(theta * 14 + 2 * math.pi * phase * 1.0 * 1)  # 14 dashes, rotates seamlessly
    boundary_sd = np.maximum(np.abs(patch_sd + 0.015) - 0.011, -(dash - 0.1) * 0.05)
    # a few bolts jump across the patch (new set every 2 frames, 8 distinct sets over the loop)
    rng = random.Random(3000 + (f // 2) % 8 * 29)
    ink, body, core = np.full((SIZE, SIZE), 9.0, dtype=np.float32), np.full((SIZE, SIZE), 9.0, dtype=np.float32), np.full((SIZE, SIZE), 9.0, dtype=np.float32)
    for k in range(3):
        a0 = rng.uniform(0, 6.283)
        a1 = a0 + rng.uniform(1.8, 4.2)
        p0 = (0.62 * math.cos(a0), 0.62 * math.sin(a0))
        p1 = (0.62 * math.cos(a1), 0.62 * math.sin(a1))
        for i, path in enumerate(arc_with_branches(rng, p0, p1, 4, 0.2, 1, 0.3, (-0.8, 0.8, -0.8, 0.8))):
            w = 1.0 if i == 0 else 0.6
            ink = np.minimum(ink, draw_polyline(X, Y, path, 0.04 * w, 0.2))
            body = np.minimum(body, draw_polyline(X, Y, path, 0.024 * w, 0.2))
            core = np.minimum(core, draw_polyline(X, Y, path, 0.009 * w, 0.2))
    # junction crosses where the net meets: small white crosses at the strongest node points
    ink_all = np.minimum.reduce([ink, crack_sd - 0.016])
    body_all = np.minimum.reduce([body, crack_sd, hatch_sd, boundary_sd])
    ink_all = np.minimum(ink_all, np.minimum(hatch_sd - 0.008, boundary_sd - 0.01))
    core_all = np.minimum(core, crack_sd + 0.008)
    packed = L.pack_layers(L.aa(ink_all, PX), L.aa(body_all, PX), L.aa(np.maximum(core_all, body_all), PX))
    # the hatch is a fill: keep it dimmer than the cracks and bolts so the net and arcs lead the read
    hatch_only = L.aa(hatch_sd, PX) * (1.0 - L.aa(np.minimum.reduce([ink, crack_sd, boundary_sd]) , PX))
    packed[..., :3] = packed[..., :3] * (1.0 - 0.45 * hatch_only[..., None])
    return packed


def shock_field():
    # domain-warped crack network, built once; per-frame flicker fields choose which parts are lit
    wx = (L.fbm(SIZE, SIZE, 4, 4, 801, 3) - 0.5) * 0.28
    wy = (L.fbm(SIZE, SIZE, 4, 4, 802, 3) - 0.5) * 0.28
    edge = worley_edge(X + wx, Y + wy, 6, 803)
    flicker = [L.stretch01(L.fbm(SIZE, SIZE, 5, 5, 810 + 13 * i, 2)) for i in range(4)]
    return L.flipbook([field_frame(f, wx, wy, flicker, edge) for f in range(FRAMES)], COLS)


def main():
    out = {"tex_shock_arcs": shock_arcs(), "fb_shock_spark": shock_spark(), "fb_shock_field": shock_field()}
    for name, img in out.items():
        print(f"LD_SLICE_TEXTURE {name} {img.shape[1]}x{img.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), img))


main()
