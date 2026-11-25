# Shared helpers for the presentation-slice VFX source generators (SourceData/VFX/Blender/slice_*.py).
# Pure numpy (no bpy import) so the generators and the preview script can both use it. Everything is deterministic:
# fixed seeds, no wall-clock or system randomness, PNG bytes written with a fixed zlib level.
#
# Packing convention (same as hit_textures.py): RGB = glow mask (R = G = B), A = coverage.
#   A > 0 and RGB = 0      -> ink only (dark edge band for contrast on a bright ground)
#   RGB ~ GLOW_BODY (0.6)  -> element-colour body
#   RGB = 1.0              -> white-hot core
import hashlib, math, os, struct, zlib
import numpy as np

GLOW_BODY = 0.6
GLOW_CORE = 1.0


# ---------------------------------------------------------------- png / hashing
def write_png(path, rgba):
    data = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    height, width, channels = data.shape
    assert channels == 4
    raw = b"".join(b"\x00" + data[y].tobytes() for y in range(height))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(png)
    return hashlib.sha256(png).hexdigest()


def sha256_file(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def read_png(path):
    """Minimal 8-bit RGBA reader for PNGs written by write_png (filter 0 only)."""
    with open(path, "rb") as handle:
        blob = handle.read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(blob):
        length = struct.unpack(">I", blob[pos:pos + 4])[0]
        tag = blob[pos + 4:pos + 8]
        body = blob[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    raw = np.frombuffer(zlib.decompress(idat), dtype=np.uint8).reshape(height, width * 4 + 1)
    assert not raw[:, 0].any(), "only filter 0 is supported"
    return raw[:, 1:].reshape(height, width, 4).astype(np.float32) / 255.0


# ---------------------------------------------------------------- math
def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def aa(sd, px):
    """Coverage of a signed distance field (negative inside) with a one-pixel anti-alias ramp."""
    return np.clip(0.5 - sd / px, 0.0, 1.0)


def grid(width, height, x0, x1, y0, y1):
    """Pixel-centre coordinates; row 0 is y1 (top), so images are written without a flip."""
    xs = x0 + (np.arange(width, dtype=np.float32) + 0.5) * (x1 - x0) / width
    ys = y1 - (np.arange(height, dtype=np.float32) + 0.5) * (y1 - y0) / height
    return np.meshgrid(xs, ys)


# ---------------------------------------------------------------- noise (tileable value noise)
def value_noise(width, height, px, py, seed):
    """Tileable smooth value noise in [0,1] with px x py lattice cells across the image."""
    rng = np.random.RandomState(seed)
    lattice = rng.rand(py, px).astype(np.float32)
    xs = (np.arange(width, dtype=np.float32) + 0.5) / width * px
    ys = (np.arange(height, dtype=np.float32) + 0.5) / height * py
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    x0 %= px
    y0 %= py
    x1 = (x0 + 1) % px
    y1 = (y0 + 1) % py
    a = lattice[y0][:, x0]
    b = lattice[y0][:, x1]
    c = lattice[y1][:, x0]
    d = lattice[y1][:, x1]
    top = a + (b - a) * fx[None, :]
    bottom = c + (d - c) * fx[None, :]
    return top + (bottom - top) * fy[:, None]


def fbm(width, height, px, py, seed, octaves=4, gain=0.5):
    total = np.zeros((height, width), dtype=np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(width, height, px * 2 ** o, py * 2 ** o, seed + 101 * o)
        norm += amp
        amp *= gain
    return total / norm


def stretch01(a):
    lo, hi = float(a.min()), float(a.max())
    return (a - lo) / max(hi - lo, 1e-6)


# ---------------------------------------------------------------- signed distance primitives (coordinates from grid())
def sd_segment(X, Y, p0, p1, r0, r1=None):
    """Tapered capsule: radius r0 at p0 to r1 at p1."""
    r1 = r0 if r1 is None else r1
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    length2 = max(dx * dx + dy * dy, 1e-12)
    t = np.clip(((X - p0[0]) * dx + (Y - p0[1]) * dy) / length2, 0.0, 1.0)
    cx, cy = p0[0] + t * dx, p0[1] + t * dy
    return np.hypot(X - cx, Y - cy) - (r0 + (r1 - r0) * t)


def sd_poly(X, Y, pts):
    """Signed distance to a simple polygon (negative inside)."""
    n = len(pts)
    dist = np.full(X.shape, 1e9, dtype=np.float32)
    inside = np.zeros(X.shape, dtype=bool)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        wx, wy = X - ax, Y - ay
        t = np.clip((wx * ex + wy * ey) / max(ex * ex + ey * ey, 1e-12), 0.0, 1.0)
        dist = np.minimum(dist, np.hypot(wx - ex * t, wy - ey * t))
        crosses = ((ay <= Y) != (by <= Y)) & (X < (bx - ax) * (Y - ay) / (by - ay + 1e-12) + ax)
        inside ^= crosses
    return np.where(inside, -dist, dist)


def rot(pts, angle, origin=(0.0, 0.0)):
    c, s = math.cos(angle), math.sin(angle)
    return [(origin[0] + (x - origin[0]) * c - (y - origin[1]) * s, origin[1] + (x - origin[0]) * s + (y - origin[1]) * c) for x, y in pts]


def move(pts, dx, dy, scale=1.0):
    return [(x * scale + dx, y * scale + dy) for x, y in pts]


def union(fields):
    out = fields[0]
    for field in fields[1:]:
        out = np.minimum(out, field)
    return out


def pack_layers(ink_cov, body_cov, core_cov):
    """RGB = glow (0 ink, GLOW_BODY body, GLOW_CORE core), A = coverage (the ink layer is a superset of the body)."""
    ink_cov = np.maximum(ink_cov, body_cov)
    core_cov = np.minimum(core_cov, body_cov)
    glow = body_cov * GLOW_BODY + core_cov * (GLOW_CORE - GLOW_BODY)
    return np.dstack([glow, glow, glow, ink_cov])


def worley(width, height, cells, seed):
    """Tileable cellular noise F1 distance, normalised to [0,1] (0 at cell centres)."""
    rng = np.random.RandomState(seed)
    pts = rng.rand(cells, cells, 2).astype(np.float32)
    xs = (np.arange(width, dtype=np.float32) + 0.5) / width * cells
    ys = (np.arange(height, dtype=np.float32) + 0.5) / height * cells
    X, Y = np.meshgrid(xs, ys)
    cx, cy = np.floor(X).astype(int), np.floor(Y).astype(int)
    best = np.full(X.shape, 9.0, dtype=np.float32)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            gx, gy = cx + ox, cy + oy
            p = pts[gy % cells, gx % cells]
            d = np.hypot(X - (gx + p[..., 0]), Y - (gy + p[..., 1]))
            best = np.minimum(best, d)
    return np.clip(best / 0.9, 0.0, 1.0)


def edge_fade(X, Y, x0, x1, y0, y1, width):
    """Soft window: 1 inside, falling to 0 at the frame border, so atlas neighbours never bleed into each other."""
    d = np.minimum(np.minimum(X - x0, x1 - X), np.minimum(Y - y0, y1 - Y))
    return smoothstep(0.0, width, d)


def flipbook(frames, cols):
    rows = (len(frames) + cols - 1) // cols
    h, w = frames[0].shape[:2]
    atlas = np.zeros((rows * h, cols * w, 4), dtype=np.float32)
    for i, frame in enumerate(frames):
        r, c = divmod(i, cols)
        atlas[r * h:(r + 1) * h, c * w:(c + 1) * w] = frame
    return atlas
