# Hit-feel VFX masks for Lockdown (SourceData/VFX). Each mask is modelled as flat emissive geometry, rendered by
# EEVEE through an orthographic camera with a transparent film, then softened and packed in numpy:
#   RGB = glow mask (white where the effect burns hottest), A = coverage. Tint and brightness are runtime parameters.
# Run from the repository root (writes SourceData/VFX/<id>.png and prints each file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/hit_textures.py -- SourceData/VFX
import bpy, bmesh, hashlib, math, os, random, struct, sys, zlib
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(__file__), ".."))
ONLY = set(ARGS[1].split(",")) if len(ARGS) > 1 else None
TMP = os.path.join(bpy.app.tempdir or OUT, "ld_hit_textures")
os.makedirs(TMP, exist_ok=True)


# ---------------------------------------------------------------- scene
def reset(width, height):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.filter_size = 1.2
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "16"
    try:
        scene.eevee.taa_render_samples = 64
    except AttributeError:
        pass
    world = bpy.data.worlds.new("Black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0
    scene.world = world
    camera = bpy.data.cameras.new("Ortho")
    camera.type = "ORTHO"
    camera.ortho_scale = 2.0 * max(1.0, height / width)
    rig = bpy.data.objects.new("Ortho", camera)
    rig.location = (0, 0, 10)
    scene.collection.objects.link(rig)
    scene.camera = rig
    material = bpy.data.materials.new("Emit")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (1, 1, 1, 1)
    emit.inputs["Strength"].default_value = 1.0
    out = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(emit.outputs[0], out.inputs[0])
    return scene, material


def add_polys(material, polys, name="Shape"):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    for poly in polys:
        verts = [bm.verts.new((x, y, 0.0)) for x, y in poly]
        if len(verts) >= 3:
            bm.faces.new(verts)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(material)
    bpy.context.scene.collection.objects.link(obj)


def render(scene, material, polys, width, height):
    for obj in list(scene.collection.objects):
        if obj.type == "MESH":
            bpy.data.objects.remove(obj)
    add_polys(material, polys)
    path = os.path.join(TMP, "frame.png")
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(path, check_existing=False)
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    # Blender stores rows bottom-up; coverage is the film alpha.
    return np.flipud(pixels.reshape(height, width, 4))[:, :, 3].copy()


# ---------------------------------------------------------------- shapes (camera spans [-1, 1] on the short side)
def ngon(cx, cy, r, n=64, start=0.0):
    return [(cx + r * math.cos(start + 2 * math.pi * i / n), cy + r * math.sin(start + 2 * math.pi * i / n)) for i in range(n)]


def annulus(r0, r1, n=128, wobble=None):
    polys = []
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        w0 = wobble(a0) if wobble else 0.0
        w1 = wobble(a1) if wobble else 0.0
        polys.append([(r0 * math.cos(a0), r0 * math.sin(a0)), (r1 * (1 + w0) * math.cos(a0), r1 * (1 + w0) * math.sin(a0)),
                      (r1 * (1 + w1) * math.cos(a1), r1 * (1 + w1) * math.sin(a1)), (r0 * math.cos(a1), r0 * math.sin(a1))])
    return polys


def ray(angle, inner, outer, width):
    c, s = math.cos(angle), math.sin(angle)
    px, py = -s, c
    mid = inner + 0.18 * (outer - inner)
    return [(inner * c, inner * s), (mid * c + px * width, mid * s + py * width), (outer * c, outer * s),
            (mid * c - px * width, mid * s - py * width)]


def segment(x0, y0, x1, y1, w0, w1):
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy) or 1.0
    px, py = -dy / length, dx / length
    return [(x0 + px * w0, y0 + py * w0), (x1 + px * w1, y1 + py * w1), (x1 - px * w1, y1 - py * w1), (x0 - px * w0, y0 - py * w0)]


def polyline(points, w0, w1):
    polys = []
    for i in range(len(points) - 1):
        t0, t1 = i / (len(points) - 1), (i + 1) / (len(points) - 1)
        polys.append(segment(*points[i], *points[i + 1], w0 + (w1 - w0) * t0, w0 + (w1 - w0) * t1))
        polys.append(ngon(*points[i + 1], w0 + (w1 - w0) * t1, 10))
    return polys


def jagged(rng, x0, y0, angle, length, steps, spread):
    points = [(x0, y0)]
    for i in range(1, steps + 1):
        angle += rng.uniform(-spread, spread)
        x0 += math.cos(angle) * length / steps
        y0 += math.sin(angle) * length / steps
        points.append((x0, y0))
    return points


def outline(points, width):
    polys = []
    for i in range(len(points)):
        polys += polyline([points[i], points[(i + 1) % len(points)]], width, width)
    return polys


def shape_star(rng):
    polys = [ngon(0, 0, 0.13)]
    for i in range(4):
        polys.append(ray(math.pi / 2 * i + 0.05, 0.0, 0.98, 0.075))
        polys.append(ray(math.pi / 2 * i + math.pi / 4, 0.0, 0.58, 0.05))
    for _ in range(14):
        a = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(0.5, 0.64)
        polys.append(segment(r0 * math.cos(a), r0 * math.sin(a), 0.96 * math.cos(a), 0.96 * math.sin(a), 0.012, 0.002))
    return polys


def shape_cross(rng):
    polys = [ngon(0, 0, 0.16)]
    for i in range(4):
        polys.append(ray(math.pi / 2 * i, 0.0, 1.0, 0.13))
        polys.append(ray(math.pi / 2 * i + math.pi / 4, 0.0, 0.34, 0.07))
    return polys


def shape_ring(rng):
    return annulus(0.8, 0.92)


def shape_ripple(rng):
    polys = annulus(0.86, 0.93) + annulus(0.56, 0.6)
    for _ in range(10):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.66, 0.8)
        polys.append(ngon(r * math.cos(a), r * math.sin(a), rng.uniform(0.012, 0.025), 12))
    return polys


def shape_burn(rng):
    phases = [rng.uniform(0, 2 * math.pi) for _ in range(3)]
    wobble = lambda a: 0.05 * math.sin(7 * a + phases[0]) + 0.03 * math.sin(13 * a + phases[1]) + 0.02 * math.sin(23 * a + phases[2])
    polys = annulus(0.56, 0.86, 160, wobble)
    for _ in range(18):
        a = rng.uniform(0, 2 * math.pi)
        polys.append(ray(a, 0.8, rng.uniform(0.9, 1.0), 0.03))
    return polys


def shape_sigil(rng):
    polys = annulus(0.9, 0.95) + annulus(0.8, 0.825) + annulus(0.27, 0.3)
    for turn in (math.pi / 2, -math.pi / 2):
        tri = [(0.8 * math.cos(turn + 2 * math.pi * i / 3), 0.8 * math.sin(turn + 2 * math.pi * i / 3)) for i in range(3)]
        polys += outline(tri, 0.016)
    for i in range(6):
        a = math.pi / 2 + math.pi / 3 * i
        polys.append(ngon(0.875 * math.cos(a), 0.875 * math.sin(a), 0.035, 16))
    return polys


def crack_paths(rng):
    paths = []
    for i in range(9):
        a = 2 * math.pi * i / 9 + rng.uniform(-0.2, 0.2)
        main = jagged(rng, 0.0, 0.0, a, rng.uniform(0.7, 0.95), 7, 0.35)
        paths.append((main, 1.0))
        for _ in range(2):
            k = rng.randint(2, 5)
            x, y = main[k]
            paths.append((jagged(rng, x, y, a + rng.choice((-1, 1)) * rng.uniform(0.5, 1.0), rng.uniform(0.15, 0.3), 4, 0.4), 0.55))
    return paths


def shape_crack(rng, glow):
    polys = [ngon(0, 0, 0.12 if glow else 0.2, 24)]
    for points, weight in crack_paths(rng):
        w0 = (0.022 if glow else 0.05) * weight
        polys += polyline(points, w0, w0 * 0.15)
    return polys


def shape_bolt(rng):
    # A vertical strike: zig-zag kinks around the centre line, never wider than the frame.
    polys, main, x = [], [], 0.0
    for i in range(17):
        main.append((x, 3.9 - 7.8 * i / 16))
        x = max(-0.55, min(0.55, -0.6 * x + rng.uniform(-0.45, 0.45)))
    polys += polyline(main, 0.11, 0.07)
    for k in (4, 8, 11):
        x, y = main[k]
        side = 1 if x < 0 else -1
        branch = [(x, y), (x + side * 0.35, y - 0.35), (x + side * 0.2, y - 0.75), (x + side * 0.55, y - 1.1)]
        polys += polyline(branch, 0.05, 0.015)
    return polys


def shape_hex(rng):
    # One tile of a pointy-top hex grid: the camera spans [-sqrt3/2*s .. ] chosen so edges wrap.
    polys = []
    s = 0.5  # hex radius; tile = sqrt(3)*s wide, 3*s tall
    for row in range(-2, 3):
        for col in range(-2, 3):
            cx = col * math.sqrt(3) * s + (row % 2) * math.sqrt(3) * s / 2
            cy = row * 1.5 * s
            hexagon = [(cx + s * math.cos(math.pi / 6 + math.pi / 3 * i), cy + s * math.sin(math.pi / 6 + math.pi / 3 * i)) for i in range(6)]
            polys += outline(hexagon, 0.02)
    return polys


# ---------------------------------------------------------------- post (numpy)
def blur(mask, radius):
    if radius <= 0:
        return mask
    sigma = radius / 2.0
    xs = np.arange(-radius, radius + 1, dtype=np.float32)
    kernel = np.exp(-0.5 * (xs / sigma) ** 2)
    kernel /= kernel.sum()
    out = np.apply_along_axis(lambda row: np.convolve(row, kernel, mode="same"), 1, mask)
    return np.apply_along_axis(lambda col: np.convolve(col, kernel, mode="same"), 0, out)


def wrap_blur(mask, radius):
    pad = radius * 2
    tiled = np.pad(mask, pad, mode="wrap")
    return blur(tiled, radius)[pad:-pad, pad:-pad]


def write_png(path, rgba):
    data = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    height, width, _ = data.shape
    raw = b"".join(b"\x00" + data[y].tobytes() for y in range(height))
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)
    return hashlib.sha256(png).hexdigest()


def pack(glow, coverage):
    return np.dstack([glow, glow, glow, coverage])


def soft(sharp, radius, halo):
    return np.clip(np.maximum(sharp, halo * blur(sharp, radius)), 0.0, 1.0)


# ---------------------------------------------------------------- textures
def build(name, width, height):
    scene, material = reset(width, height)
    rng = random.Random(name)
    if name == "hit_crack":
        thick = render(scene, material, shape_crack(random.Random(name), False), width, height)
        thin = render(scene, material, shape_crack(random.Random(name), True), width, height)
        coverage = np.clip(blur(thick, 3) * 1.1, 0, 1)
        return pack(soft(thin, 6, 0.8), coverage)
    if name == "hit_hex":
        scene.camera.data.ortho_scale = 3 * 0.5  # vertical span = 3s: the tile repeats exactly in both axes
        sharp = render(scene, material, shape_hex(rng), width, height)
        glow = np.clip(np.maximum(sharp, 0.6 * wrap_blur(sharp, 5)), 0, 1)
        return pack(glow, glow)
    if name == "hit_bolt":
        sharp = render(scene, material, shape_bolt(rng), width, height)
        glow = soft(sharp, 10, 0.9)
        return pack(np.clip(sharp + 0.4 * blur(sharp, 3), 0, 1), glow)
    shapes = {"hit_star": shape_star, "hit_cross": shape_cross, "hit_ring": shape_ring, "hit_ripple": shape_ripple,
              "hit_burn": shape_burn, "hit_sigil": shape_sigil}
    sharp = render(scene, material, shapes[name](rng), width, height)
    halo = {"hit_star": (12, 0.85), "hit_cross": (14, 0.9), "hit_ring": (8, 0.9), "hit_ripple": (6, 0.8),
            "hit_burn": (10, 0.9), "hit_sigil": (6, 0.75)}[name]
    coverage = soft(sharp, *halo)
    return pack(np.clip(sharp + 0.25 * blur(sharp, 4), 0, 1), coverage)


def screen_edge(width, height):
    ys, xs = np.mgrid[0:height, 0:width].astype(np.float32)
    u = (xs + 0.5) / width
    v = (ys + 0.5) / height
    edge = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))
    mask = np.clip(1.0 - edge / 0.22, 0.0, 1.0) ** 2.2
    return pack(mask, mask)


TEXTURES = [("hit_star", 256, 256), ("hit_cross", 256, 256), ("hit_ring", 256, 256), ("hit_ripple", 256, 256),
            ("hit_burn", 256, 256), ("hit_sigil", 512, 512), ("hit_crack", 512, 512), ("hit_bolt", 128, 512),
            ("hit_hex", 222, 384), ("screen_edge", 256, 256)]

for name, width, height in TEXTURES:
    if ONLY and name not in ONLY:
        continue
    rgba = screen_edge(width, height) if name == "screen_edge" else build(name, width, height)
    digest = write_png(os.path.join(OUT, name + ".png"), rgba)
    print(f"LD_HIT_TEXTURE {name} {width}x{height} sha256={digest}")
