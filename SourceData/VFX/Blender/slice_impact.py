# Impact-burst flipbook of the skill tier (fb_hit_skill); the basic and finisher hits are generated Niagara systems.
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


def ease_out(t):
    return 1.0 - (1.0 - t) ** 3


# ---------------------------------------------------------------- basic hit: directional sparkle + speed-line fan
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
