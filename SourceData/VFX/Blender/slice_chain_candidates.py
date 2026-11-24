# Element source assets for the three chain-lightning art-direction candidates (vfx-direction/CANDIDATES.md):
#   A stylised 3D energy:    chainA_profile, chainA_energy_noise, chainA_spark_streak, chainA_flash, chainA_ground_glow
#   B refined street/comic:  chainB_stroke_profile, chainB_shards, chainB_scorch_dash
#   C competitive clarity:   chainC_core_profile, chainC_spark_head, chainC_pinpoint, chainC_ground_tick
# The trunk shape comes from Niagara points at runtime, so nothing here bakes a bolt: only cross-section profiles,
# surfaces and hit elements. Angular and stroked shapes are Blender curve geometry (slice_render.py); soft falloffs and
# tileable noise are analytic numpy with fixed seeds. Run from the repository root:
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_chain_candidates.py -- SourceData/VFX/Slice
#
# Encoding (slice_lib): RGB = glow (0 ink, 0.6 element body, 1.0 white-hot core), A = coverage, linear. Exceptions:
#   chainA_energy_noise    utility noise: R flow, G filaments, B second filament phase, A = 1
#   chainB_stroke_profile, chainB_shards
#                          R = G = glow, B = warm-dark edge mask (1 on the edge band, 0 elsewhere), A = coverage with
#                          the edge band baked at 0.6; the existing M_LDSlice reads only R, so the edge shows there as
#                          Ink at 60 % alpha, and a candidate material can tint B deep brown and scale its alpha itself.
import math, os, random, sys

sys.dont_write_bytecode = True  # a __pycache__ folder under SourceData/VFX fails the importer whitelist
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import slice_lib as L
import slice_render as R

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
EDGE_ALPHA = 0.6


# ---------------------------------------------------------------- helpers
def blur(a, sigma, wrap=False):
    """Separable Gaussian blur of a 2D array; wrap keeps a tileable image tileable."""
    if sigma <= 0:
        return a
    r = int(math.ceil(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2).astype(np.float32)
    k /= k.sum()
    mode = "wrap" if wrap else "constant"
    out = np.pad(a, ((0, 0), (r, r)), mode=mode)
    out = np.apply_along_axis(lambda row: np.convolve(row, k, mode="valid"), 1, out)
    out = np.pad(out, ((r, r), (0, 0)), mode=mode)
    return np.apply_along_axis(lambda col: np.convolve(col, k, mode="valid"), 0, out).astype(np.float32)


def soften(img, sigma, halo_alpha, halo_glow):
    """Adds a soft halo around a crisp render: alpha = max(crisp, blurred * halo_alpha), the halo carries halo_glow."""
    g, a = img[..., 0], img[..., 3]
    halo = blur(a, sigma) * halo_alpha
    out_a = np.maximum(a, halo)
    glow = np.where(out_a > 1e-4, (g * a + halo_glow * (out_a - a)) / np.maximum(out_a, 1e-4), 0.0)
    return glow, out_a


def pack(glow, alpha):
    glow = np.clip(glow, 0, 1)
    return np.dstack([glow, glow, glow, np.clip(alpha, 0, 1)]).astype(np.float32)


def circle(cx, cy, r, n=24):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def clip_half(poly, p, n):
    """Sutherland-Hodgman: keeps the part of a convex polygon where (x - p) . n >= 0."""
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        da = (a[0] - p[0]) * n[0] + (a[1] - p[1]) * n[1]
        db = (b[0] - p[0]) * n[0] + (b[1] - p[1]) * n[1]
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def periodic_sample(img, dx, dy):
    """Bilinear sample of a periodic image at (x + dx, y + dy) pixel offsets (wraps, so the result stays periodic)."""
    h, w = img.shape
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    x, y = xs + dx, ys + dy
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x0 %= w
    y0 %= h
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    top = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    bottom = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def across(width, height):
    """v in [-1, 1] across the strip (row 0 = V 0 = -1 side), u in [0, 1) along."""
    v = ((np.arange(height, dtype=np.float32) + 0.5) / height * 2 - 1)[:, None].repeat(width, 1)
    u = ((np.arange(width, dtype=np.float32) + 0.5) / width)[None, :].repeat(height, 0)
    return u, v


# ---------------------------------------------------------------- A: stylised 3D energy
def chainA_profile(width=256, height=64):
    """Cross-section strip, tileable along U: warm-white core (about 25 % of the width) inside a soft energy layer that
    falls off to transparent, over a wider, dim amber under-layer (low glow) that keeps it from washing out on the floor."""
    u, v = across(width, height)
    streak = L.fbm(width, height, 4, 10, 301, 3)          # periodic; long along U, fine across
    av = np.abs(v)
    core = L.smoothstep(0.30, 0.12, av + (streak - 0.5) * 0.06)
    energy = np.clip(1.0 - av, 0, 1) ** 1.5 * (0.75 + 0.5 * streak)
    under = np.clip(1.0 - av / 0.98, 0, 1) ** 0.8
    alpha = np.maximum.reduce([core, np.clip(energy, 0, 1) * 0.85, under * 0.22])
    glow = 0.28 + 0.32 * L.smoothstep(0.1, 0.6, energy) + 0.4 * core
    return pack(glow, alpha * L.smoothstep(1.0, 0.94, av))


def chainA_energy_noise(size=256):
    """Tileable flowing energy noise: R domain-warped flow, G ridged filaments stretched along U, B a second filament
    phase to cross-fade or scroll against G. A = 1."""
    base = L.fbm(size, size, 4, 4, 311, 4)
    wx = (L.fbm(size, size, 3, 3, 312, 3) - 0.5) * size * 0.18
    wy = (L.fbm(size, size, 3, 3, 313, 3) - 0.5) * size * 0.18
    flow = L.stretch01(periodic_sample(base, wx, wy))
    def filaments(seed):
        n = periodic_sample(L.fbm(size, size, 3, 14, seed, 3), wx * 0.5, wy * 0.5)
        ridge = 1.0 - np.abs(2.0 * L.stretch01(n) - 1.0)
        return L.stretch01(ridge ** 4)
    return np.dstack([flow, filaments(321), filaments(331), np.ones_like(flow)]).astype(np.float32)


def chainA_spark_streak(width=128, height=32):
    """One velocity-stretched spark: +X = velocity, hot round head near the right, thin tail fading to the left."""
    c = R.Canvas(width, height, 0.0, float(width), 0.0, float(height))
    pts = [(10.0 + i * 9.6, 16.0) for i in range(11)]          # x 10 .. 106
    radii = [0.35 + 2.9 * (i / 10) ** 1.6 for i in range(11)]
    c.stroke(pts, radii, R.BODY)
    c.stroke(pts, [r * 0.5 for r in radii], R.CORE)
    c.fill(circle(106.0, 16.0, 3.3), R.BODY)
    c.fill(circle(106.0, 16.0, 1.8), R.CORE)
    img = c.render()
    tail = L.smoothstep(6.0, 70.0, np.arange(width, dtype=np.float32))[None, :]   # tail fades in alpha, not width only
    glow, alpha = soften(img, 2.2, 0.55, 0.45)
    return pack(glow, alpha * tail)


def chainA_flash(size=128):
    """Small soft hit flash: warm-white soft core with faint anamorphic streaks (longer along X), no spikes or outline."""
    c = R.Canvas(size, size, -1.0, 1.0, -1.0, 1.0)
    for ang, length, r in ((0, 0.78, 0.035), (180, 0.62, 0.03), (90, 0.36, 0.025), (270, 0.32, 0.025)):
        a = math.radians(ang)
        pts = [(math.cos(a) * length * t, math.sin(a) * length * t) for t in (0.0, 0.35, 0.7, 1.0)]
        c.stroke(pts, [r, r * 0.8, r * 0.4, 0.0], R.BODY)
    c.fill(circle(0.0, 0.0, 0.16), R.BODY)
    c.fill(circle(0.0, 0.0, 0.09), R.CORE)
    img = c.render()
    glow, alpha = soften(img, 6.0, 0.7, 0.5)
    X, Y = L.grid(size, size, -1, 1, -1, 1)
    window = L.smoothstep(0.98, 0.85, np.maximum(np.abs(X), np.abs(Y)))
    return pack(glow, alpha * window)


def chainA_ground_glow(size=256):
    """Soft additive ground-glow mask under a hit: smooth falloff with a gently uneven rim, hotter centre."""
    X, Y = L.grid(size, size, -1, 1, -1, 1)
    r = np.hypot(X, Y)
    ang = np.arctan2(Y, X)
    rim = 0.82 + 0.08 * np.sin(3 * ang + 0.7) + 0.05 * np.sin(5 * ang + 2.1)
    alpha = np.clip(1.0 - r / rim, 0, 1) ** 2.2
    glow = 0.45 + 0.4 * np.clip(1.0 - r / 0.35, 0, 1) ** 1.5
    return pack(glow, alpha * L.smoothstep(0.99, 0.9, np.maximum(np.abs(X), np.abs(Y))))


# ---------------------------------------------------------------- B: refined street / comic
def chainB_stroke_profile(width=512, height=64):
    """Brush-stroke profile tileable along U: flat cream core and gold body (two-tone), ragged dry-brush edges, a thin
    warm-dark edge band in B (alpha 0.6)."""
    u, v = across(width, height)
    px = 2.0 / height
    n1 = L.fbm(width, height, 12, 1, 341, 3)[0:1].repeat(height, 0)   # edge wobble along U (periodic)
    n2 = L.fbm(width, height, 9, 1, 342, 3)[0:1].repeat(height, 0)
    n3 = L.fbm(width, height, 5, 22, 343, 3)                          # dry-brush streaks along U
    top = 0.70 + (n1 - 0.5) * 0.16
    bottom = 0.70 + (n2 - 0.5) * 0.16
    edge_out = np.where(v < 0, top, bottom)
    body = L.aa(np.abs(v) - edge_out, px)
    dry = L.smoothstep(0.62, 0.72, n3) * L.smoothstep(0.35, 0.65, np.abs(v) / edge_out)   # streak gaps near the edges
    body = body * (1.0 - dry * 0.5)
    core = L.aa(np.abs(v + (n2 - n1) * 0.08) - (0.22 + (n3 - 0.5) * 0.08), px)
    band = L.aa(np.abs(v) - (edge_out + 0.16), px) * (1.0 - L.aa(np.abs(v) - edge_out, px) * (1.0 - dry * 0.5))
    edge = np.clip(band, 0, 1) * (1.0 - body)
    glow = np.where(body > 0, (0.6 * body + 0.4 * np.minimum(core, body)) / np.maximum(body, 1e-4), 0.0)
    alpha = np.maximum(body, edge * EDGE_ALPHA)
    return np.dstack([glow, glow, edge, alpha]).astype(np.float32)


SHARD_CELL = 256


def shard_set(rng):
    """3-4 thin angular slivers in a narrow fan around +X (not radial), staggered; returns (body polygon, core polygon)."""
    out = []
    count = rng.choice((3, 3, 4))
    for k in range(count):
        ang = math.radians(rng.uniform(-22, 22) + (k - (count - 1) / 2) * 9)
        d = (math.cos(ang), math.sin(ang))
        n = (-d[1], d[0])
        start = rng.uniform(0.02, 0.16)
        length = rng.uniform(0.85, 1.15) * (1.0 if k % 2 == 0 else 0.72)
        w = rng.uniform(0.07, 0.11)
        side = rng.choice((-1, 1))
        p0 = (d[0] * start + n[0] * rng.uniform(-0.12, 0.12), d[1] * start + n[1] * rng.uniform(-0.12, 0.12))
        def at(t, off):
            return (p0[0] + d[0] * length * t + n[0] * off, p0[1] + d[1] * length * t + n[1] * off)
        k0 = rng.uniform(0.22, 0.38)
        body = [at(0, 0), at(k0, w * side), at(1.0, 0), at(k0 + 0.12, -w * 0.55 * side)]
        core = [at(0.12, 0), at(k0, w * 0.32 * side), at(0.82, 0), at(k0 + 0.1, -w * 0.12 * side)]
        out.append((body, core, d, n))
    return out


def break_shard(rng, body, core, d, n):
    """Cuts a sliver into 2-3 fragments across its spine; each drifts along +X and outward, turns a little and shrinks."""
    cuts = sorted(rng.uniform(0.3, 0.75) for _ in range(rng.choice((1, 2))))
    xs = [p[0] * d[0] + p[1] * d[1] for p in body]
    lo, hi = min(xs), max(xs)
    pieces = []
    bounds = [None] + cuts + [None]
    for i in range(len(bounds) - 1):
        b, c = body, core
        for t, sign in ((bounds[i], 1), (bounds[i + 1], -1)):
            if t is None:
                continue
            skew = math.radians(rng.uniform(-35, 35))
            cn = (d[0] * math.cos(skew) - d[1] * math.sin(skew), d[0] * math.sin(skew) + d[1] * math.cos(skew))
            p = (d[0] * (lo + (hi - lo) * t), d[1] * (lo + (hi - lo) * t))
            # the cut line passes through the spine point p; shift p sideways to the shard's spine
            mid = (sum(q[0] for q in body) / len(body), sum(q[1] for q in body) / len(body))
            off = (mid[0] - p[0]) * n[0] + (mid[1] - p[1]) * n[1]
            p = (p[0] + n[0] * off, p[1] + n[1] * off)
            b = clip_half(b, p, (cn[0] * sign, cn[1] * sign))
            c = clip_half(c, p, (cn[0] * sign, cn[1] * sign)) if len(c) >= 3 else c
        if len(b) < 3:
            continue
        cx = sum(q[0] for q in b) / len(b)
        cy = sum(q[1] for q in b) / len(b)
        drift = rng.uniform(0.05, 0.12) * (1 + i * 0.4)
        out = rng.uniform(-0.05, 0.05)
        turn = math.radians(rng.uniform(-14, 14))
        def tf(poly):
            res = []
            for x, y in poly:
                x, y = cx + (x - cx) * 0.88, cy + (y - cy) * 0.88
                rx = cx + (x - cx) * math.cos(turn) - (y - cy) * math.sin(turn)
                ry = cy + (x - cx) * math.sin(turn) + (y - cy) * math.cos(turn)
                res.append((rx + d[0] * drift + n[0] * out, ry + d[1] * drift + n[1] * out))
            return res
        pieces.append((tf(b), tf(c) if len(c) >= 3 else []))
    return pieces


def chainB_shards():
    """4 shard / slash variants (columns) x 2 frames (row 0 solid, row 1 broken into 2-3 drifting fragments).
    Cell = 256 px over x -0.6..1.4, y -1..1; the hit point is cell UV (0.30, 0.50), +X = hop direction."""
    cols, rows = 4, 2
    glow_c = R.Canvas(SHARD_CELL * cols, SHARD_CELL * rows, 0.0, 2.0 * cols, 0.0, 2.0 * rows)
    polys = []
    for v in range(cols):
        rng = random.Random(5100 + 17 * v)
        shards = shard_set(rng)
        for r in range(rows):
            ox, oy = v * 2.0 + 0.6, (rows - 1 - r) * 2.0 + 1.0
            if r == 0:
                items = [(b, c) for b, c, _d, _n in shards]
            else:
                items = [piece for b, c, d, n in shards for piece in break_shard(rng, b, c, d, n)]
            for b, c in items:
                polys.append(([(x + ox, y + oy) for x, y in b], [(x + ox, y + oy) for x, y in c]))
    for b, c in polys:
        glow_c.fill(b, R.BODY)
        if len(c) >= 3:
            glow_c.fill(c, R.CORE)
    img = glow_c.render()
    edge_c = R.Canvas(SHARD_CELL * cols, SHARD_CELL * rows, 0.0, 2.0 * cols, 0.0, 2.0 * rows)
    for b, _c in polys:
        edge_c.fill(b, R.BODY, grow=0.022)
    grown = edge_c.render()[..., 3]
    body = img[..., 3]
    edge = np.clip(grown - body, 0, 1)
    alpha = np.maximum(body, edge * EDGE_ALPHA)
    glow = img[..., 0]
    for c0 in range(cols):  # 3 px empty border per cell
        for r0 in range(rows):
            for arr in (alpha, edge, glow):
                cell = arr[r0 * SHARD_CELL:(r0 + 1) * SHARD_CELL, c0 * SHARD_CELL:(c0 + 1) * SHARD_CELL]
                cell[:3] = cell[-3:] = 0
                cell[:, :3] = cell[:, -3:] = 0
    return np.dstack([glow, glow, edge, alpha]).astype(np.float32)


def chainB_scorch_dash(width=256, height=128):
    """Angular scorch dash decal (x = hop direction): a tapered slash with two side chips, burnt ragged edge and a soot
    feather (glow 0 = scorch, tinted by the decal's ink), a short ember seam inside (glow 0.6)."""
    c = R.Canvas(width, height, 0.0, 2.0, -0.5, 0.5)
    dash = [(0.10, 0.03), (0.55, 0.17), (1.35, 0.11), (1.90, -0.01), (1.25, -0.13), (0.62, -0.10)]
    c.fill(dash, R.INK)
    c.fill([(0.70, 0.13), (0.92, 0.30), (0.98, 0.13)], R.INK)
    c.fill([(1.20, -0.10), (1.42, -0.27), (1.46, -0.08)], R.INK)
    c.stroke([(0.55, 0.01), (0.95, 0.025), (1.25, 0.0), (1.50, 0.005)], [0.0, 0.012, 0.012, 0.0], R.BODY)
    img = c.render()
    a = img[..., 3]
    n = L.stretch01(L.fbm(width, height, 16, 8, 351, 3))
    burnt = a * L.smoothstep(0.15, 0.55, n + a * 0.6 - 0.3)
    soot = blur(a, 5.0) * 0.45
    alpha = np.maximum(burnt * 0.9, soot)
    glow = np.where(img[..., 0] > 0.3, img[..., 0], 0.0)
    alpha = np.maximum(alpha, (glow > 0) * img[..., 3])
    X, Y = L.grid(width, height, 0, 2, -0.5, 0.5)
    return pack(glow, alpha * L.edge_fade(X, Y, 0, 2, -0.5, 0.5, 0.04))


# ---------------------------------------------------------------- C: competitive clarity
def chainC_core_profile(width=128, height=32):
    """Thin crisp core profile: flat warm-white core across the middle 60 %, a narrow amber falloff to 1.5 x the core,
    nothing beyond (no halo). Constant along U, so it tiles at any world length."""
    _u, v = across(width, height)
    px = 2.0 / height
    av = np.abs(v)
    core = L.aa(av - 0.58, px)
    edge = L.smoothstep(0.88, 0.6, av)
    alpha = np.maximum(core, edge)
    glow = 0.6 + 0.4 * core
    return pack(glow, alpha)


def chainC_spark_head(width=128, height=32):
    """Small bright elongated head (+X = travel): crisp capsule head, short tapered tail, narrow amber rim."""
    c = R.Canvas(width, height, 0.0, float(width), 0.0, float(height))
    pts = [(30.0 + i * 7.0, 16.0) for i in range(11)]          # x 30 .. 100
    radii = [0.4 + 4.6 * (i / 10) ** 1.3 for i in range(11)]
    c.stroke(pts, [r + 1.2 for r in radii], R.BODY)
    c.stroke(pts, radii, R.CORE)
    c.fill(circle(100.0, 16.0, 6.2), R.BODY)
    c.fill(circle(100.0, 16.0, 5.0), R.CORE)
    return c.render()


def chainC_pinpoint(size=64):
    """Tiny pinpoint flash: crisp white dot, a narrow amber falloff and two hair-thin crossing glints."""
    c = R.Canvas(size, size, -1.0, 1.0, -1.0, 1.0)
    for ang, length in ((0, 0.8), (90, 0.55)):
        a = math.radians(ang)
        pts = [(-math.cos(a) * length, -math.sin(a) * length), (0.0, 0.0), (math.cos(a) * length, math.sin(a) * length)]
        c.stroke(pts, [0.0, 0.045, 0.0], R.BODY)
    c.fill(circle(0.0, 0.0, 0.2), R.BODY)
    c.fill(circle(0.0, 0.0, 0.14), R.CORE)
    return c.render()


def chainC_ground_tick(size=128):
    """Small ground tick under the hit target: four short tapered ticks pointing at the centre (open, not a ring)."""
    c = R.Canvas(size, size, -1.0, 1.0, -1.0, 1.0)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        pts = [(math.cos(a) * r, math.sin(a) * r) for r in (0.78, 0.6, 0.42)]
        c.stroke(pts, [0.075, 0.06, 0.012], R.BODY)
        c.stroke(pts, [0.03, 0.024, 0.004], R.CORE)
    return c.render()


ASSETS = {
    "chainA_profile": chainA_profile, "chainA_energy_noise": chainA_energy_noise, "chainA_spark_streak": chainA_spark_streak,
    "chainA_flash": chainA_flash, "chainA_ground_glow": chainA_ground_glow,
    "chainB_stroke_profile": chainB_stroke_profile, "chainB_shards": chainB_shards, "chainB_scorch_dash": chainB_scorch_dash,
    "chainC_core_profile": chainC_core_profile, "chainC_spark_head": chainC_spark_head, "chainC_pinpoint": chainC_pinpoint,
    "chainC_ground_tick": chainC_ground_tick,
}


def main():
    only = set(ARGS[1].split(",")) if len(ARGS) > 1 else None
    for name, make in ASSETS.items():
        if only and name not in only:
            continue
        img = make()
        print(f"LD_SLICE_TEXTURE {name} {img.shape[1]}x{img.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), img))


main()
