# Element sources of the generated Niagara effects that are not the chain or the field (LDDemoVfxNiagara): each element
# gets its own surface, not a recolour of another's. Pure numpy rasters with fixed seeds; run from the repository root:
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_elements.py -- SourceData/VFX/Slice
#
# Encoding (slice_lib): RGB = glow (0 = the palette's edge colour, 0.6 the element body, 1.0 the white-hot core or a
# specular glint), A = coverage, linear. A glow of 0 with coverage darkens what is under it (a wet floor).
#   water_flow   a stream's surface, tileable along U: a translucent body with long flow streaks and bright glints,
#                ragged soft sides that break into beads
#   water_drop   one droplet: a translucent bead with a thicker rim and a highlight towards the upper left
#   water_wet    a wet floor, tileable in both axes: dark soaked blotches with a few sparse glints
#   soft_puff    a soft cloud puff (mist, steam, dust, smoke): a lumpy round coverage, glow 0.3 (between edge and body)
#   fire_tongue  four flame tongues in a 2 x 2 atlas (base at the bottom): a hot core low in the tongue inside an orange
#                body whose edge licks upward and breaks into wisps
#   fire_char    a scorched floor, tileable in both axes: soft soot blotches (glow 0) crossed by thin glowing ember cracks
#   ice_shard    four ice crystals in a 2 x 2 atlas: faceted shards, each facet its own brightness, bright ridges, see-through
#   ice_frost    a frosted floor, tileable in both axes: a pale sheen crossed by thin branching frost needles
#   rot_murk     a rotten murk, tileable in both axes: soft lumpy clouds with darker cores and scattered bubble rims
#   rot_bubble   one bubble: a thin rim with a highlight, a nearly clear middle
import math, os, sys

sys.dont_write_bytecode = True  # a __pycache__ folder under SourceData/VFX fails the importer whitelist
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import slice_lib as L

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))


def blur_wrap(a, sigma):
    r = int(math.ceil(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2).astype(np.float32)
    k /= k.sum()
    out = np.pad(a, ((0, 0), (r, r)), mode="wrap")
    out = np.apply_along_axis(lambda row: np.convolve(row, k, mode="valid"), 1, out)
    out = np.pad(out, ((r, r), (0, 0)), mode="wrap")
    return np.apply_along_axis(lambda col: np.convolve(col, k, mode="valid"), 0, out).astype(np.float32)


def water_flow(width=256, height=128):
    """U runs along the stream and tiles; V across it, centre 0.5. The noise lattices have whole cells across U, so the
    left and right columns meet."""
    v = (np.arange(height, dtype=np.float32) + 0.5) / height * 2.0 - 1.0          # -1 .. 1 across
    V = np.repeat(v[:, None], width, axis=1)
    # Ragged sides: the half width breathes along the stream.
    side = L.fbm(width, height, 6, 1, 711, octaves=3)
    half = 0.62 + 0.22 * (side - 0.5) * 2.0
    edge = np.abs(V) - half
    body = L.smoothstep(0.12, -0.08, edge)
    # Beads breaking off the sides: cellular blobs just outside the edge.
    cells = L.worley(width, height, 10, 733)
    sparse = L.smoothstep(0.55, 0.75, L.fbm(width, height, 7, 3, 741, octaves=2))
    beads = L.smoothstep(0.24, 0.12, cells) * L.smoothstep(0.22, 0.0, np.abs(edge - 0.06)) * sparse
    # Long flow streaks: noise stretched along U (few cells along, many across).
    streak = L.fbm(width, height, 3, 22, 757, octaves=3)
    streak2 = L.fbm(width, height, 5, 34, 769, octaves=2)
    lines = L.smoothstep(0.62, 0.8, streak) * 0.7 + L.smoothstep(0.7, 0.86, streak2) * 0.6
    glints = L.smoothstep(0.78, 0.9, streak2) * L.smoothstep(0.86, 0.94, L.fbm(width, height, 8, 16, 787, octaves=2))
    centre = 1.0 - np.abs(V)
    glow = np.clip(0.48 + 0.14 * centre + 0.3 * lines, 0.0, 0.92) * np.where(body > 0.0, 1.0, 0.0)
    glow = np.maximum(glow, np.clip(glints * 1.6, 0.0, 1.0) * body)
    glow = np.maximum(glow, beads * 0.7)
    # Water is see-through: thin in the middle, thicker towards the sides and where it streaks.
    alpha = body * np.clip(0.42 + 0.25 * np.abs(V) / np.maximum(half, 0.1) + 0.3 * lines, 0.0, 0.95)
    alpha = np.maximum(alpha, beads * 0.85)
    return np.dstack([glow, glow, glow, alpha])


def water_drop(size=64):
    X, Y = L.grid(size, size, -1.0, 1.0, -1.0, 1.0)
    r = np.hypot(X, Y)
    cover = L.smoothstep(0.82, 0.7, r)
    rim = L.smoothstep(0.45, 0.78, r) * cover
    hl = L.smoothstep(0.32, 0.08, np.hypot(X + 0.3, Y - 0.32))
    glow = np.clip(0.5 + 0.12 * rim + 0.6 * hl, 0.0, 1.0) * cover
    alpha = cover * np.clip(0.45 + 0.5 * rim + 0.4 * hl, 0.0, 1.0)
    return np.dstack([glow, glow, glow, alpha])


def water_wet(size=256):
    blot = L.fbm(size, size, 8, 8, 811, octaves=4)
    blot = L.stretch01(blot)
    cover = L.smoothstep(0.42, 0.6, blot)
    # A soaked floor is darker in its middle, its edges a thin damp band.
    alpha = cover * (0.3 + 0.3 * L.smoothstep(0.6, 0.85, blot))
    sparkle = L.smoothstep(0.9, 0.97, L.fbm(size, size, 24, 24, 829, octaves=2)) * L.smoothstep(0.62, 0.8, blot)
    glow = np.clip(sparkle * 1.4, 0.0, 1.0)
    alpha = np.maximum(alpha, sparkle * 0.8)
    return np.dstack([glow, glow, glow, alpha])


def soft_puff(size=128):
    X, Y = L.grid(size, size, -1.0, 1.0, -1.0, 1.0)
    lumps = L.fbm(size, size, 5, 5, 853, octaves=4)
    r = np.hypot(X, Y) + 0.35 * (lumps - 0.5)
    cover = L.smoothstep(0.92, 0.25, r) * (0.55 + 0.45 * lumps)
    cover *= L.edge_fade(X, Y, -1.0, 1.0, -1.0, 1.0, 0.08)
    glow = np.full(cover.shape, 0.3, dtype=np.float32) * np.where(cover > 0.0, 1.0, 0.0)
    return np.dstack([glow, glow, glow, np.clip(cover, 0.0, 1.0)])


def fire_tongue(cell=128):
    """One tongue per cell (cell wide, 2 x cell tall), base at the bottom centre. Soft sides (coverage falls off across the
    tongue, never a hard flat fill), rising streaks inside, a hot soft core low in the middle, and a tip that licks sideways
    and thins into wisps."""
    tiles = []
    for k in range(4):
        X, Y = L.grid(cell, 2 * cell, -1.0, 1.0, 0.0, 4.0)
        h = Y / 4.0
        warp = L.fbm(cell, 2 * cell, 3, 6, 901 + 17 * k, octaves=4) - 0.5
        lean = 0.22 * math.sin(1.7 * k + 0.6) * h ** 1.6
        x = X - lean - 0.55 * warp * h
        width = 0.8 * np.sqrt(np.clip(h * 6.0, 0.0, 1.0)) * (1.0 - h) ** 0.75 + 1e-3
        u = np.abs(x) / width
        streak = L.fbm(cell, 2 * cell, 6, 3, 931 + 17 * k, octaves=3)
        side = L.smoothstep(1.05, 0.25, u)
        rise = L.smoothstep(0.0, 0.06, h) * (1.0 - L.smoothstep(0.5, 0.95, h + 0.25 * (streak - 0.5)))
        alpha = side * rise * (0.6 + 0.55 * streak)
        core = 0.85 * L.smoothstep(0.6, 0.0, np.hypot(u * 1.2, (h - 0.18) / 0.24))
        glow = np.clip(0.3 + 0.38 * side * (1.0 - h) ** 0.6 + 0.2 * (streak - 0.5) + 0.5 * core, 0.0, 1.0)
        alpha = np.clip(alpha, 0.0, 1.0) * L.edge_fade(X, Y, -1.0, 1.0, 0.0, 4.0, 0.06)
        tiles.append(np.dstack([glow * (alpha > 0.0), glow * (alpha > 0.0), glow * (alpha > 0.0), alpha]))
    top = np.concatenate([tiles[0], tiles[1]], axis=1)
    bottom = np.concatenate([tiles[2], tiles[3]], axis=1)
    return np.concatenate([top, bottom], axis=0)


def worley2(size, cells, seed):
    """Tileable cellular noise: F2 - F1 (0 on the borders between cells), in cell units."""
    rng = np.random.RandomState(seed)
    pts = rng.rand(cells, cells, 2).astype(np.float32)
    xs = (np.arange(size, dtype=np.float32) + 0.5) / size * cells
    X, Y = np.meshgrid(xs, xs)
    cx, cy = np.floor(X).astype(int), np.floor(Y).astype(int)
    f1 = np.full(X.shape, 9.0, dtype=np.float32)
    f2 = np.full(X.shape, 9.0, dtype=np.float32)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            gx, gy = cx + ox, cy + oy
            p = pts[gy % cells, gx % cells]
            d = np.hypot(X - (gx + p[..., 0]), Y - (gy + p[..., 1]))
            f2 = np.where(d < f1, f1, np.minimum(f2, d))
            f1 = np.minimum(f1, d)
    return f2 - f1


def fire_char(size=256):
    soot = L.stretch01(L.fbm(size, size, 6, 6, 971, octaves=5))
    cover = L.smoothstep(0.32, 0.6, soot)
    edge = worley2(size, 6, 977)
    crack = L.smoothstep(0.07, 0.0, edge) * L.smoothstep(0.45, 0.7, soot)
    flicker = L.smoothstep(0.35, 0.65, L.fbm(size, size, 10, 10, 983, octaves=2))
    seam = crack * flicker
    glow = np.clip(seam * 1.1, 0.0, 1.0)
    alpha = np.clip(cover * 0.7 + seam * 0.5, 0.0, 1.0)
    return np.dstack([glow, glow, glow, alpha])


def ice_shard(cell=128):
    tiles = []
    rng = np.random.RandomState(1013)
    for k in range(4):
        X, Y = L.grid(cell, cell, -1.0, 1.0, -1.0, 1.0)
        # A long hexagonal prism seen side on: a tip at the top, a broken base, an off-centre ridge.
        w = 0.32 + 0.12 * rng.rand()
        tip = 0.92 - 0.1 * rng.rand()
        lean = (rng.rand() - 0.5) * 0.3
        pts = [(-w, -0.85), (-w * 1.05, 0.35), (lean, tip), (w * 1.05, 0.4), (w, -0.85), (0.1, -0.95)]
        sd = L.sd_poly(X, Y, pts)
        cover = L.aa(sd, 2.0 / cell)
        ridge = lean * (Y + 0.85) / (tip + 0.85) + 0.06 * np.sin(3.0 * Y + k)
        left = X < ridge
        facet = np.where(left, 0.5 + 0.15 * (Y + 1.0) / 2.0, 0.72 - 0.1 * (Y + 1.0) / 2.0)
        edge = L.smoothstep(0.05, 0.0, -sd) * cover
        crest = L.smoothstep(0.035, 0.0, np.abs(X - ridge)) * cover * L.smoothstep(-0.8, 0.6, Y)
        glint = L.smoothstep(0.18, 0.0, np.hypot(X - ridge * 0.5 + 0.12, Y - 0.45)) * cover
        glow = np.clip(facet + 0.35 * edge + 0.45 * crest + 0.6 * glint, 0.0, 1.0) * cover
        alpha = cover * np.clip(0.55 + 0.35 * edge + 0.3 * crest, 0.0, 1.0)
        tiles.append(np.dstack([glow, glow, glow, alpha]))
    return np.concatenate([np.concatenate(tiles[:2], 1), np.concatenate(tiles[2:], 1)], 0)


def ice_frost(size=256):
    sheen = L.stretch01(L.fbm(size, size, 5, 5, 1031, octaves=4))
    base = L.smoothstep(0.25, 0.7, sheen) * 0.28
    needles = np.zeros((size, size), dtype=np.float32)
    for k, (cells, width) in enumerate(((5, 0.05), (9, 0.04), (16, 0.03))):
        e = worley2(size, cells, 1037 + 7 * k)
        needles = np.maximum(needles, L.smoothstep(width, 0.0, e) * (1.0 - 0.3 * k))
    needles *= L.smoothstep(0.3, 0.6, sheen)
    glow = np.clip(0.55 + 0.45 * needles, 0.0, 1.0) * (np.maximum(base, needles) > 0.0)
    alpha = np.clip(base + 0.75 * needles, 0.0, 1.0)
    return np.dstack([glow, glow, glow, alpha])


def rot_murk(size=256):
    cloud = L.stretch01(L.fbm(size, size, 4, 4, 1051, octaves=5))
    cover = L.smoothstep(0.3, 0.7, cloud)
    cells = L.worley(size, size, 9, 1061)
    rims = L.smoothstep(0.05, 0.0, np.abs(cells - 0.22)) * L.smoothstep(0.55, 0.75, cloud)
    glow = np.clip(0.25 + 0.35 * (1.0 - cloud) + 0.4 * rims, 0.0, 1.0) * ((cover + rims) > 0.0)
    alpha = np.clip(cover * 0.6 + rims * 0.5, 0.0, 1.0)
    return np.dstack([glow, glow, glow, alpha])


def rot_bubble(size=64):
    X, Y = L.grid(size, size, -1.0, 1.0, -1.0, 1.0)
    r = np.hypot(X, Y)
    rim = L.smoothstep(0.12, 0.0, np.abs(r - 0.72))
    hl = L.smoothstep(0.2, 0.0, np.hypot(X + 0.3, Y - 0.35))
    inside = L.smoothstep(0.78, 0.7, r) * 0.18
    glow = np.clip(0.5 + 0.2 * rim + 0.5 * hl, 0.0, 1.0) * ((rim + inside + hl) > 0.0)
    alpha = np.clip(rim * 0.85 + inside + hl * 0.8, 0.0, 1.0)
    return np.dstack([glow, glow, glow, alpha])


def main():
    for name, image in (("water_flow", water_flow()), ("water_drop", water_drop()), ("water_wet", water_wet()), ("soft_puff", soft_puff()),
                        ("fire_tongue", fire_tongue()), ("fire_char", fire_char()), ("ice_shard", ice_shard()), ("ice_frost", ice_frost()),
                        ("rot_murk", rot_murk()), ("rot_bubble", rot_bubble())):
        digest = L.write_png(os.path.join(OUT, name + ".png"), image)
        print(name, image.shape[1], "x", image.shape[0], digest)


main()
