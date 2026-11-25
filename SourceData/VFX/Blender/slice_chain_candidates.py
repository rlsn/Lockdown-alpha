# Element source assets of the generated Niagara effects (LDDemoVfxNiagara): the energy profile, its noise, the spark
# streak, the soft flash and ground glow, and the scorch dash:
#   chainA_profile, chainA_energy_noise, chainA_spark_streak, chainA_flash, chainA_ground_glow, chainB_scorch_dash
# The trunk shape comes from Niagara points at runtime, so nothing here bakes a bolt: only cross-section profiles,
# surfaces and hit elements. Angular and stroked shapes are Blender curve geometry (slice_render.py); soft falloffs and
# tileable noise are analytic numpy with fixed seeds. Run from the repository root:
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_chain_candidates.py -- SourceData/VFX/Slice
#
# Encoding (slice_lib): RGB = glow (0 ink, 0.6 element body, 1.0 white-hot core), A = coverage, linear. Exceptions:
#   chainA_energy_noise    utility noise: R flow, G filaments, B second filament phase, A = 1
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
ASSETS = {
    "chainA_profile": chainA_profile, "chainA_energy_noise": chainA_energy_noise, "chainA_spark_streak": chainA_spark_streak,
    "chainA_flash": chainA_flash, "chainA_ground_glow": chainA_ground_glow,
    "chainB_scorch_dash": chainB_scorch_dash,
}


def main():
    only = set(ARGS[1].split(",")) if len(ARGS) > 1 else None
    for name, make in ASSETS.items():
        if only and name not in only:
            continue
        img = make()
        print(f"LD_SLICE_TEXTURE {name} {img.shape[1]}x{img.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), img))


main()
