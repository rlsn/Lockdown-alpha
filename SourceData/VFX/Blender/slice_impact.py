# Impact-burst flipbooks for the presentation slice: basic hit, finisher hit, skill hit.
#   4 x 4 frames of 256 x 256 (1024 x 1024 atlas), RGB = glow (0 ink, 0.6 body, 1.0 white-hot core), A = coverage.
# Run from the repository root (writes SourceData/VFX/Slice/fb_hit_*.png and prints each file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_impact.py -- SourceData/VFX/Slice [id,id]
# Frame space: x in [-0.6, 1.4], y in [-1, 1], the hit point is the origin = frame UV (0.3, 0.5), the hit direction is +X
# (image right). The runtime rotates the sprite so +X points along the attack direction (target - attacker).
import math, os, random, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
ONLY = set(ARGS[1].split(",")) if len(ARGS) > 1 else None

SIZE, COLS, FRAMES = 256, 4, 16
X0, X1, Y0, Y1 = -0.6, 1.4, -1.0, 1.0
PX = (X1 - X0) / SIZE
X, Y = L.grid(SIZE, SIZE, X0, X1, Y0, Y1)


class Frame:
    """Accumulates body / ink / core signed distance fields (negative inside) with erosion applied per primitive."""

    def __init__(self, noise, erode, erode_core):
        self.body = np.full((SIZE, SIZE), 9.0, dtype=np.float32)
        self.ink = self.body.copy()
        self.core = self.body.copy()
        self.noise, self.erode, self.erode_core = noise, erode, erode_core

    def add(self, sd, ink=0.02, erode_scale=1.0):
        e = sd + self.erode * erode_scale * self.noise
        self.body = np.minimum(self.body, e)
        if ink is not None:
            # the ink outline shrinks with the erosion so no dark blob outlives its body
            width = ink * max(0.0, 1.0 - (self.erode / 0.35) ** 2)
            self.ink = np.minimum(self.ink, e - width)

    def add_core(self, sd):
        self.core = np.minimum(self.core, sd + self.erode_core * self.noise)

    def pack(self, clear=False):
        if clear:
            return np.zeros((SIZE, SIZE, 4), dtype=np.float32)
        body = L.aa(self.body, PX)
        ink = L.aa(self.ink, PX)
        core = L.aa(np.maximum(self.core, self.body), PX)
        window = L.edge_fade(X, Y, X0, X1, Y0, Y1, 0.05)  # keep every frame clear of its atlas neighbours
        return L.pack_layers(ink * window, body * window, core * window)


def burst_poly(cx, cy, spikes, inner, rot=0.0, scale=1.0):
    """Comic burst: spikes = [(angle_deg, length)], inner = valley radius between spikes."""
    pts = []
    for i, (ang, length) in enumerate(spikes):
        nxt = spikes[(i + 1) % len(spikes)]
        a0 = math.radians(ang)
        a1 = math.radians(nxt[0] + (360 if nxt[0] <= ang else 0))
        pts.append((cx + math.cos(a0 + rot) * length * scale, cy + math.sin(a0 + rot) * length * scale))
        mid = 0.5 * (a0 + a1)
        pts.append((cx + math.cos(mid + rot) * inner * scale, cy + math.sin(mid + rot) * inner * scale))
    return pts


def crescent(cx, cy, radius, thick, half_angle_deg, steps=14):
    """Arc band opening toward +X with tapered tips: outer edge on `radius`, inner edge pulled in by `thick` at the middle."""
    outer, inner = [], []
    for i in range(steps + 1):
        s = i / steps
        a = math.radians(-half_angle_deg + 2 * half_angle_deg * s)
        outer.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
        w = thick * math.sin(math.pi * s) ** 0.8
        inner.append((cx + (radius - w) * math.cos(a), cy + (radius - w) * math.sin(a)))
    return outer + inner[::-1]


def ease_out(t):
    return 1.0 - (1.0 - t) ** 3


# ---------------------------------------------------------------- basic hit: directional sparkle + speed-line fan
def basic_frame(f, noise):
    t = f / (FRAMES - 1)
    e = 0.0 if f <= 2 else 0.5 * ((f - 2) / 12.0) ** 1.5
    fr = Frame(noise, e, e * 2.6 + (0.0 if f <= 1 else 0.02))
    grow = [0.45, 0.9, 1.0][min(f, 2)] * (1.0 + 0.12 * t)
    spark = [(0.95, 0), (0.2, 0.05), (0.05, 0.29), (-0.04, 0.05), (-0.32, 0), (-0.04, -0.05), (0.05, -0.29), (0.2, -0.05)]
    fr.add(L.sd_poly(X, Y, L.move(spark, 0, 0, grow)), ink=0.024)
    fr.add_core(L.sd_poly(X, Y, L.move(spark, 0.0, 0.0, grow * 0.52)))
    rng = random.Random(7)
    if f >= 1:
        tt = ease_out((f - 1) / 13.0)
        for k, ang in enumerate((-36, -24, -12, 12, 24, 36, 0)):
            a = math.radians(ang + rng.uniform(-3, 3))
            r0 = 0.34 + (0.2 if k == 6 else 0.0) + 0.75 * tt * rng.uniform(0.9, 1.1)
            length = (0.36 if k == 6 else 0.26) * (1.0 - 0.45 * tt) * rng.uniform(0.85, 1.15)
            p0 = (r0 * math.cos(a), r0 * math.sin(a))
            p1 = ((r0 + length) * math.cos(a), (r0 + length) * math.sin(a))
            fr.add(L.sd_segment(X, Y, p0, p1, 0.017, 0.004), ink=0.012, erode_scale=0.5)
    if f >= 2:
        tt = ease_out((f - 2) / 13.0)
        for k in range(5):
            a = math.radians(rng.uniform(-48, 48))
            r = 0.38 + rng.uniform(0.0, 0.3) + 0.55 * tt
            size = rng.uniform(0.04, 0.065) * (1 - 0.3 * tt)
            tri = L.rot([(size * 1.8, 0), (-size, size), (-size, -size)], rng.uniform(-0.6, 0.6) + a)
            fr.add(L.sd_poly(X, Y, L.move(tri, r * math.cos(a), r * math.sin(a))), ink=0.012, erode_scale=0.4)
    return fr.pack(clear=f == FRAMES - 1)


# ---------------------------------------------------------------- finisher hit: crescent wave + heavy burst + chunks
def finisher_frame(f, noise):
    t = f / (FRAMES - 1)
    e = 0.0 if f <= 3 else 0.62 * ((f - 3) / 11.0) ** 1.6
    fr = Frame(noise, e, e * 2.2 + (0.0 if f <= 2 else 0.03))
    spikes = [(0, 1.08), (38, 0.6), (82, 0.72), (134, 0.5), (180, 0.42), (226, 0.5), (278, 0.72), (322, 0.6)]
    grow = [0.5, 0.95, 1.0][min(f, 2)] * (1.0 + 0.08 * t)
    fr.add(L.sd_poly(X, Y, burst_poly(0, 0, spikes, 0.2, scale=grow)), ink=0.03)
    fr.add_core(L.sd_poly(X, Y, burst_poly(0, 0, spikes, 0.12, scale=grow * 0.5)))
    if f >= 1:
        tt = ease_out((f - 1) / 10.0)
        radius = 0.32 + 0.66 * tt
        thick = 0.2 - 0.09 * tt
        crown = crescent(0.0, 0.0, radius, thick, 64)
        fr.add(L.sd_poly(X, Y, crown), ink=0.026, erode_scale=0.6)
    rng = random.Random(19)
    if f >= 2:
        tt = ease_out((f - 2) / 13.0)
        for k in range(11):
            a = math.radians(rng.uniform(-70, 70) + (180 if k % 6 == 5 else 0))
            r = 0.3 + rng.uniform(0.0, 0.3) + 0.5 * tt * rng.uniform(0.8, 1.1)
            if k % 6 == 5:
                r *= 0.42  # backward debris stays inside the frame behind the hit point
            size = rng.uniform(0.05, 0.09) * (1 - 0.25 * tt)
            n = rng.choice((3, 4))
            base = [(1.6 * size, 0), (-0.9 * size, size * 1.1), (-0.9 * size, -size * 1.1)] if n == 3 else [(size, size * 0.6), (-size, size), (-size * 1.2, -size * 0.7), (size * 0.6, -size)]
            chunk = L.rot(base, rng.uniform(0, 6.283))
            fr.add(L.sd_poly(X, Y, L.move(chunk, r * math.cos(a), r * math.sin(a))), ink=0.016, erode_scale=0.3)
    if f >= 2:
        tt = ease_out((f - 2) / 12.0)
        for k, ang in enumerate((-26, -14, 14, 26, 0, 8)):
            a = math.radians(ang)
            r0 = 0.5 + 0.45 * tt + (0.1 if k == 4 else 0.0)
            length = 0.32 * (1 - 0.4 * tt)
            fr.add(L.sd_segment(X, Y, (r0 * math.cos(a), r0 * math.sin(a)), ((r0 + length) * math.cos(a), (r0 + length) * math.sin(a)), 0.02, 0.005), ink=0.014, erode_scale=0.45)
    return fr.pack(clear=f == FRAMES - 1)


# ---------------------------------------------------------------- skill hit: striped burst + chevron slices
def stripes(angle_deg, period, width, phase):
    a = math.radians(angle_deg)
    d = (X * math.cos(a) + Y * math.sin(a)) / period + phase
    frac = d - np.floor(d)
    return (np.abs(frac - 0.5) * period - width * 0.5)  # negative inside a cut stripe


def skill_frame(f, noise):
    t = f / (FRAMES - 1)
    e = 0.0 if f <= 6 else 0.5 * ((f - 6) / 9.0) ** 1.8
    fr = Frame(noise, e, e * 1.6)
    rng = random.Random(31)
    angles = [0, 33, 62, 101, 138, 176, 214, 247, 291, 327]
    lengths = [1.02, 0.66, 0.84, 0.58, 0.8, 0.52, 0.7, 0.86, 0.6, 0.76]
    spikes = [(a, l * rng.uniform(0.95, 1.05)) for a, l in zip(angles, lengths)]
    grow = [0.5, 0.86, 0.9][min(f, 2)] * (1.0 + 0.03 * t)
    cut = 0.0 if f <= 3 else 0.2 * ((f - 3) / 12.0) ** 1.2  # stripe cut-out width grows: the burst slices into diagonal bars
    burst = L.sd_poly(X, Y, burst_poly(0, 0, spikes, 0.24, scale=grow))
    cuts = stripes(48, 0.2, 0.022 + cut * 0.9, 0.16 * t * 3)
    sliced = np.maximum(burst, -cuts)
    fr.add(sliced, ink=0.028)
    # the white-hot core is a small burst that stays solid for the first frames only
    core_poly = burst_poly(0, 0, [(a, l * 0.46) for a, l in spikes], 0.1, scale=grow)
    core_sd = L.sd_poly(X, Y, core_poly)
    fr.add_core(np.maximum(core_sd, -(cuts - 0.02)) + (0.0 if f <= 4 else 0.04 * (f - 4)))
    if f >= 3:
        tt = ease_out((f - 3) / 12.0)
        for k in range(3):
            x = 0.38 + 0.4 * tt + k * 0.16 - 0.1
            h = 0.2 - 0.04 * k
            chev = [(0.0, h), (0.17, 0.0), (0.0, -h), (0.075, -h), (0.245, 0.0), (0.075, h)]
            sd = L.sd_poly(X, Y, L.move(chev, x, 0.0, 1.0 - 0.18 * tt))
            fr.add(sd, ink=0.016, erode_scale=1.0)
    return fr.pack(clear=f == FRAMES - 1)


BUILDERS = {
    "fb_hit_basic": (basic_frame, 5, 101),
    "fb_hit_finisher": (finisher_frame, 6, 202),
    "fb_hit_skill": (skill_frame, 5, 303),
}


def build(name):
    builder, cells, seed = BUILDERS[name]
    noise = L.stretch01(L.fbm(SIZE, SIZE, cells, cells, seed, 3))
    frames = [builder(f, noise) for f in range(FRAMES)]
    return L.flipbook(frames, COLS)


def main():
    for name in BUILDERS:
        if ONLY and name not in ONLY:
            continue
        atlas = build(name)
        print(f"LD_SLICE_TEXTURE {name} {atlas.shape[1]}x{atlas.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), atlas))


main()
