# Element sources of the electric field's charged floor (LDDemoVfxNiagara field candidate F3):
#   field_web            a tileable, isotropic, branching electric crack web: jittered points joined to their neighbours
#                        by kinked strokes with short forks, rendered as Blender curve geometry (slice_render.py)
#   field_energy_noise   tileable utility noise: R soft energy blobs, G a second blob phase, B the web's cell id (one
#                        random value per web point's cell, for cells that flare), A = 1
# The web and the noise share one tile and one point set, so B lines up with the web's cells when both are sampled with
# the same UV. Fixed seeds; run from the repository root:
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_field.py -- SourceData/VFX/Slice
#
# Encoding (slice_lib): field_web RGB = glow (0.6 body, 1.0 white-hot core), A = coverage, linear.
import math, os, random, sys

sys.dont_write_bytecode = True  # a __pycache__ folder under SourceData/VFX fails the importer whitelist
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import slice_lib as L
import slice_render as R

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
CELLS = 6          # web points per tile side
WEB_SIZE = 512
NOISE_SIZE = 256


def points(seed=401):
    """One jittered point per cell of a CELLS x CELLS grid over the unit tile."""
    rng = random.Random(seed)
    return [[((i + 0.5 + rng.uniform(-0.38, 0.38)) / CELLS, (j + 0.5 + rng.uniform(-0.38, 0.38)) / CELLS)
             for i in range(CELLS)] for j in range(CELLS)]


def kinked(a, b, rng, segments, kink):
    """A polyline from a to b through segments - 1 interior points pushed sideways by up to kink of the length."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / max(length, 1e-9), dx / max(length, 1e-9)
    out = [a]
    for k in range(1, segments):
        t = k / segments + rng.uniform(-0.25, 0.25) / segments
        s = rng.uniform(-kink, kink) * length
        out.append((a[0] + dx * t + nx * s, a[1] + dy * t + ny * s))
    out.append(b)
    return out


def blur_wrap(a, sigma):
    r = int(math.ceil(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2).astype(np.float32)
    k /= k.sum()
    out = np.pad(a, ((0, 0), (r, r)), mode="wrap")
    out = np.apply_along_axis(lambda row: np.convolve(row, k, mode="valid"), 1, out)
    out = np.pad(out, ((r, r), (0, 0)), mode="wrap")
    return np.apply_along_axis(lambda col: np.convolve(col, k, mode="valid"), 0, out).astype(np.float32)


def field_web():
    """Tileable crack web: each point joined to its right and lower neighbour and to one diagonal (alternating), about a
    fifth of the joins left out so it never reads as a grid, every join a 4-6 segment kinked stroke with a hot core,
    and short forks off a third of them. Every stroke is drawn with its eight wrapped copies, so the tile is seamless."""
    rng = random.Random(402)
    P = points()
    strokes = []
    for j in range(CELLS):
        for i in range(CELLS):
            a = P[j][i]
            joins = [(1, 0), (0, 1), (1, 1) if (i + j) % 2 == 0 else (-1, 1)]
            for di, dj in joins:
                if rng.random() < 0.2:
                    continue
                ii, jj = i + di, j + dj
                # The neighbour across the tile edge is its wrapped copy one tile over.
                b = P[jj % CELLS][ii % CELLS]
                b = (b[0] + math.floor(ii / CELLS), b[1] + math.floor(jj / CELLS))
                line = kinked(a, b, rng, rng.randint(4, 6), 0.16)
                width = rng.uniform(0.8, 1.15)
                strokes.append((line, width))
                if rng.random() < 0.35:
                    k = rng.randint(1, len(line) - 2)
                    base = line[k]
                    ang = math.atan2(line[k + 1][1] - base[1], line[k + 1][0] - base[0]) + rng.choice((-1, 1)) * rng.uniform(0.6, 1.1)
                    reach = math.hypot(b[0] - a[0], b[1] - a[1]) * rng.uniform(0.25, 0.45)
                    tip = (base[0] + math.cos(ang) * reach, base[1] + math.sin(ang) * reach)
                    strokes.append((kinked(base, tip, rng, 3, 0.2), width * 0.6))
    px = 1.0 / WEB_SIZE
    c = R.Canvas(WEB_SIZE, WEB_SIZE, 0.0, 1.0, 0.0, 1.0)
    for line, width in strokes:
        n = len(line)
        taper = [0.55 + 0.45 * math.sin(math.pi * (k / (n - 1)) * 0.9 + 0.15) for k in range(n)]
        for ox in (-1.0, 0.0, 1.0):
            for oy in (-1.0, 0.0, 1.0):
                pts = [(x + ox, y + oy) for x, y in line]
                if max(x for x, _ in pts) < -0.02 or min(x for x, _ in pts) > 1.02 or max(y for _, y in pts) < -0.02 or min(y for _, y in pts) > 1.02:
                    continue
                c.stroke(pts, [1.5 * px * width * t for t in taper], R.BODY)
                c.stroke(pts, [0.6 * px * width * t for t in taper], R.CORE)
    img = c.render()
    g, a = img[..., 0], img[..., 3]
    # A soft energy halo round the strokes (wrapped, so the tile stays seamless).
    halo = blur_wrap(a, 2.5) * 0.35
    out_a = np.maximum(a, halo)
    glow = np.where(out_a > 1e-4, (g * a + 0.45 * (out_a - a)) / np.maximum(out_a, 1e-4), 0.0)
    return np.dstack([glow, glow, glow, out_a]).astype(np.float32)


def field_energy_noise():
    """R and G: soft tileable energy blobs at two phases; B: the random value of the nearest web point (wrapped), so each
    web cell has its own id; A = 1."""
    soft = L.stretch01(L.fbm(NOISE_SIZE, NOISE_SIZE, 3, 3, 411, 4))
    soft2 = L.stretch01(L.fbm(NOISE_SIZE, NOISE_SIZE, 4, 4, 412, 4))
    rng = random.Random(413)
    P = points()
    ids = [[rng.random() for _ in range(CELLS)] for _ in range(CELLS)]
    ys, xs = np.mgrid[0:NOISE_SIZE, 0:NOISE_SIZE].astype(np.float32)
    u, v = (xs + 0.5) / NOISE_SIZE, 1.0 - (ys + 0.5) / NOISE_SIZE   # row 0 on top, like the web render
    best = np.full(u.shape, 9.0, dtype=np.float32)
    cell = np.zeros(u.shape, dtype=np.float32)
    for j in range(CELLS):
        for i in range(CELLS):
            px, py = P[j][i]
            dx = np.abs(u - px); dx = np.minimum(dx, 1.0 - dx)
            dy = np.abs(v - py); dy = np.minimum(dy, 1.0 - dy)
            d = dx * dx + dy * dy
            closer = d < best
            best = np.where(closer, d, best)
            cell = np.where(closer, ids[j][i], cell)
    return np.dstack([soft, soft2, cell, np.ones_like(soft)]).astype(np.float32)


ASSETS = {"field_web": field_web, "field_energy_noise": field_energy_noise}


def main():
    only = set(ARGS[1].split(",")) if len(ARGS) > 1 else None
    for name, make in ASSETS.items():
        if only and name not in only:
            continue
        img = make()
        print(f"LD_SLICE_TEXTURE {name} {img.shape[1]}x{img.shape[0]} sha256=" + L.write_png(os.path.join(OUT, name + ".png"), img))


main()
