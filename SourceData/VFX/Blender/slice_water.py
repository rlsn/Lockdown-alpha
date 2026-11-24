# Water assets for active_hose_down (and the wet status) in the presentation slice:
#   tex_water_jet.png   512 x 128, tileable along U: turbulent jet strip (RGB glow / A coverage)
#   fb_water_splash.png 4 x 4 frames of 256 x 256: forward-biased splash crown on the target
#   tex_wet_decal.png   512 x 512: plan-view puddle with an ink rim (ground decal mask)
# Run from the repository root (prints each file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_water.py -- SourceData/VFX/Slice
import math, os, random, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))


# ---------------------------------------------------------------- jet strip (tileable along U = image X)
def periodic_edge(width, seed, amp):
    """1D tileable edge displacement: a few integer-cycle sines (so the strip loops) plus a high-frequency wobble."""
    rng = np.random.RandomState(seed)
    u = (np.arange(width, dtype=np.float32) + 0.5) / width
    out = np.zeros(width, dtype=np.float32)
    for cycles, weight in ((2, 1.0), (5, 0.7), (11, 0.45), (23, 0.3), (41, 0.18)):
        out += weight * np.sin(2 * math.pi * (cycles * u + rng.rand()))
    return out / 2.6 * amp


def water_jet(width=512, height=128):
    v = (1.0 - 2.0 * (np.arange(height, dtype=np.float32) + 0.5) / height)[:, None]  # +1 top .. -1 bottom
    top = 0.44 + periodic_edge(width, 3, 0.17)[None, :]
    bottom = 0.44 + periodic_edge(width, 4, 0.17)[None, :]
    half = np.where(v >= 0, top, bottom)
    px = 2.0 / height
    body_sd = np.abs(v) - half
    ink_w = 0.11
    # white-hot centre band that wobbles with the jet, plus bright flow streaks stretched along U
    core_sd = np.abs(v) - (0.075 + periodic_edge(width, 5, 0.04)[None, :])
    streak = L.fbm(width, height, 6, 22, 51, 3)
    inner = L.smoothstep(0.03, 0.14, -body_sd)
    hot_lines = L.smoothstep(0.60, 0.66, streak) * inner
    # droplets torn off the edges: stretched cells of a tileable cellular field, kept in a band outside the body
    cell = L.worley(width, height, 11, 61)
    drop_zone = L.smoothstep(0.03, 0.10, body_sd) * (1.0 - L.smoothstep(0.30, 0.46, body_sd))
    droplets = (1.0 - L.smoothstep(0.20, 0.27, cell)) * drop_zone * L.smoothstep(0.35, 0.6, L.fbm(width, height, 3, 2, 71, 2))
    halo = (1.0 - L.smoothstep(0.27, 0.37, cell)) * drop_zone * L.smoothstep(0.35, 0.6, L.fbm(width, height, 3, 2, 71, 2))  # thin ink ring around each droplet
    body = np.maximum(L.aa(body_sd, px), droplets)
    ink = np.maximum.reduce([L.aa(body_sd - ink_w, px), halo, body])
    core = np.maximum(np.minimum(L.aa(core_sd, px), L.aa(body_sd, px)), hot_lines)
    return L.pack_layers(ink, body, core)


# ---------------------------------------------------------------- splash crown flipbook
SIZE, COLS, FRAMES = 256, 4, 16
X0, X1, Y0, Y1 = -0.6, 1.4, -1.0, 1.0
PX = (X1 - X0) / SIZE
X, Y = L.grid(SIZE, SIZE, X0, X1, Y0, Y1)


def ease_out(t):
    return 1.0 - (1.0 - t) ** 3


def splash_frame(f, noise):
    t = f / (FRAMES - 1)
    e = 0.0 if f <= 5 else 0.6 * ((f - 5) / 10.0) ** 1.5
    body = np.full((SIZE, SIZE), 9.0, dtype=np.float32)
    ink = body.copy()
    core = body.copy()
    rng = random.Random(77)
    en = e * noise

    def add(sd, ink_w, core_sd=None):
        nonlocal body, ink, core
        s = sd + en
        body = np.minimum(body, s)
        ink = np.minimum(ink, s - ink_w * max(0.0, 1.0 - (e / 0.3) ** 2))
        if core_sd is not None:
            core = np.minimum(core, core_sd + e * 2.0 * noise)

    # small irregular splat at the hit point (dissolves early; the lasting mark is the wet decal)
    pts = []
    radius = [0.11, 0.16, 0.2][min(f, 2)] * (1.0 + 0.03 * f) * max(0.12, 1.0 - 0.11 * max(0, f - 6))
    for i in range(16):
        a = 2 * math.pi * i / 16
        r = radius * (0.75 + 0.45 * ((i * 7) % 5) / 4.0) * (1.3 if math.cos(a) > 0.3 else 0.85)
        pts.append((r * math.cos(a), r * math.sin(a)))
    add(L.sd_poly(X, Y, pts), 0.02)
    # crown: 15 curved tapered blades fanned over the forward half-plane (long / short alternate), each with a white-hot
    # vein along its centre line and a detached drop beyond the tip
    if f >= 1:
        tt = ease_out(min(1.0, f / 5.0))
        fall = max(0.0, (f - 7) / 8.0)
        for k in range(15):
            lead = k % 2 == 0
            a0 = math.radians(-118 + 236 * k / 14 + rng.uniform(-4, 4))
            bend = math.radians(rng.uniform(-24, 24))
            forward = 1.0 if abs(math.degrees(a0)) < 75 else 0.72
            reach = (rng.uniform(0.62, 0.82) if lead else rng.uniform(0.4, 0.55)) * forward
            r0 = 0.1
            r1 = r0 + (reach - r0) * tt
            prev, vein_prev = None, None
            n = 6
            for s_ in range(n + 1):
                q = s_ / n
                a = a0 + bend * q
                r = r0 + (r1 - r0) * q
                p = (r * math.cos(a), r * math.sin(a))
                if prev is not None:
                    q0 = q - 1.0 / n
                    wid = 0.062 if lead else 0.046
                    add(L.sd_segment(X, Y, prev, p, wid * (1 - q0) + 0.006, wid * (1 - q) + 0.006), 0.014,
                        L.sd_segment(X, Y, prev, p, 0.014 * (1 - q0) + 0.002, 0.014 * (1 - q) + 0.002) if (q <= 0.8 and tt > 0.5) else None)
                prev = p
            if f >= 3 and lead:
                dist = r1 + 0.07 + 0.2 * ease_out(min(1.0, (f - 3) / 8.0))
                a = a0 + bend
                size = 0.032 * (1.0 - 0.6 * fall)
                p = (dist * math.cos(a), dist * math.sin(a) + 0.1 * fall * fall * rng.choice((-1, 1)))
                add(L.sd_segment(X, Y, p, (p[0] + 0.05 * math.cos(a), p[1] + 0.05 * math.sin(a)), size, size * 0.6), 0.01)
    if f == FRAMES - 1:
        return np.zeros((SIZE, SIZE, 4), dtype=np.float32)
    window = L.edge_fade(X, Y, X0, X1, Y0, Y1, 0.05)
    return L.pack_layers(L.aa(ink, PX) * window, L.aa(body, PX) * window, L.aa(np.maximum(core, body), PX) * window)


def water_splash():
    noise = L.stretch01(L.fbm(SIZE, SIZE, 6, 6, 404, 3))
    return L.flipbook([splash_frame(f, noise) for f in range(FRAMES)], COLS)


# ---------------------------------------------------------------- wet ground decal
def wet_decal(size=512):
    X, Y = L.grid(size, size, -1, 1, -1, 1)
    px = 2.0 / size
    rng = np.random.RandomState(88)
    theta = np.arctan2(Y, X)
    lobes = np.zeros_like(X)
    for freq, amp in ((2, 0.07), (3, 0.075), (4, 0.05), (6, 0.03), (9, 0.016)):
        lobes += amp * np.sin(freq * theta + rng.rand() * 6.283)
    sd = np.hypot(X, Y) - (0.5 + lobes)
    # run-off tongues with blunt rounded ends and a few satellite puddles keep the outline organic, not a disc or a star
    r = random.Random(5)
    for k in range(5):
        a = 2 * math.pi * k / 5 + r.uniform(-0.3, 0.3)
        length = r.uniform(0.22, 0.34)
        p0 = (0.4 * math.cos(a), 0.4 * math.sin(a))
        p1 = ((0.4 + length) * math.cos(a), (0.4 + length) * math.sin(a))
        sd = np.minimum(sd, L.sd_segment(X, Y, p0, p1, 0.1, 0.055))
    for k in range(5):
        a = r.uniform(0, 6.283)
        d = r.uniform(0.8, 0.93)
        sd = np.minimum(sd, L.sd_segment(X, Y, (d * math.cos(a), d * math.sin(a)), (d * math.cos(a), d * math.sin(a)), r.uniform(0.02, 0.04)))
    body = L.aa(sd, px)
    ink = np.maximum(L.aa(sd - 0.022, px), body)
    # glints: short curved highlights on the water surface (white-hot) so it reads as wet, not as a stain
    core_sd = np.full_like(X, 9.0)
    for cx, cy, a, ln in ((-0.18, 0.12, 0.5, 0.26), (0.12, -0.16, -0.35, 0.2), (0.22, 0.2, 0.9, 0.12)):
        c, s_ = math.cos(a), math.sin(a)
        core_sd = np.minimum(core_sd, L.sd_segment(X, Y, (cx - c * ln * 0.5, cy - s_ * ln * 0.5), (cx + c * ln * 0.5, cy + s_ * ln * 0.5), 0.016, 0.004))
    core = L.aa(np.maximum(core_sd, sd + 0.05), px)
    # wet-edge shading: darker (lower glow) near the rim, full body glow in the middle of the puddle
    shade = 0.38 + (L.GLOW_BODY - 0.38) * L.smoothstep(0.0, 0.16, -sd)
    glow = body * shade * (1.0 - core) + core * L.GLOW_CORE
    return np.dstack([glow, glow, glow, ink])


def main():
    out = {}
    out["tex_water_jet"] = water_jet()
    out["fb_water_splash"] = water_splash()
    out["tex_wet_decal"] = wet_decal()
    for name, img in out.items():
        print(f"LD_SLICE_TEXTURE {name} {img.shape[1]}x{img.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), img))


main()
