# Shock assets for active_zap and the electrified / conduct (导电) reaction in the presentation slice:
#   tex_shock_arcs.png   512 x 512: 8 stacked lightning strip variants (512 x 64 each), stretchable along U
#   fb_shock_spark.png   4 x 4 frames of 256 x 256: contact spark burst (flickering forked bolts, erodes away)
#   fb_shock_field.png   4 x 4 frames of 256 x 256: LOOPING electric ground field (crackle net + hatch + chalk boundary)
# Run from the repository root (prints each file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_shock.py -- SourceData/VFX/Slice
import math, os, random, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

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


# ---------------------------------------------------------------- lightning strip atlas
def zigzag(rng, x0, x1, centre, amp_lo, amp_hi, step_lo, step_hi, clamp_lo, clamp_hi):
    """Sharp lightning zig-zag: x advances in irregular steps, y alternates around the centre line."""
    pts = [(x0, centre)]
    x, sign = x0, rng.choice((-1, 1))
    while x < x1 - step_hi:
        x += rng.uniform(step_lo, step_hi)
        sign = -sign if rng.random() < 0.8 else sign
        pts.append((x, min(max(centre + sign * rng.uniform(amp_lo, amp_hi), clamp_lo), clamp_hi)))
    pts.append((x1, centre))
    return pts


def shock_arcs(width=512, strip=64, variants=8):
    X, Y = L.grid(width, strip, 0.0, float(width), 0.0, float(strip))  # pixel units, y up
    layers = []
    centre = strip / 2.0
    for v in range(variants):
        rng = random.Random(1000 + v * 37)
        main = zigzag(rng, -8.0, width + 8.0, centre, 7.0, 18.0, 16.0, 46.0, 12.0, strip - 12.0)
        paths = [main]
        for _ in range(2 + v % 3):
            k = rng.randint(2, len(main) - 3)
            a = main[k]
            direction = 1 if a[1] < centre else -1  # branches leave toward the free side of the strip
            pts = [a]
            x, y = a
            for _step in range(rng.randint(2, 3)):
                x += rng.uniform(14, 34)
                y = min(max(y + direction * rng.uniform(4, 11) * (1 if _step % 2 == 0 else -0.5), 9.0), strip - 9.0)
                pts.append((min(x, width - 3.0), y))
            paths.append(pts)
        ink = np.full(X.shape, 9.0, dtype=np.float32)
        body, core = ink.copy(), ink.copy()
        for i, path in enumerate(paths):
            weight = 1.0 if i == 0 else 0.6
            ink = np.minimum(ink, draw_polyline(X, Y, path, 8.6 * weight, 0.35))
            body = np.minimum(body, draw_polyline(X, Y, path, 5.4 * weight, 0.35))
            core = np.minimum(core, draw_polyline(X, Y, path, 2.3 * weight, 0.35))
        layers.append(L.pack_layers(L.aa(ink, 1.0), L.aa(body, 1.0), L.aa(core, 1.0)))
    return np.vstack(layers)


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
