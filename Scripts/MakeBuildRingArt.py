"""Generates the build ring's own art into SourceData/UI (listed in ui_images.json, imported by ImportDemoArt.ps1 -Mode ImportUI).

Every image is drawn procedurally here from fixed seeds, so the bytes are reproducible and carry no third-party art:
  ring_splat.png   512x512  white spray-paint splat (alpha mask) behind a skill host, tinted by the ring
  ring_metal.png   256x256  brushed metal with a bevel, laid over the card frames' rarity gradient
  ring_socket.png  256x256  the medal socket: a bevelled octagonal metal seat, light grey so the ring can tint it
  ring_fresh.png   128x128  the red star sticker with 新, slightly turned
  ring_body.png    128x128  the 本体 host icon: a jersey in the icon set's aerosol style on a coloured ground
The 新 glyph uses SourceData/UI/Fonts/SourceHanSansSC-Heavy.otf (SIL OFL 1.1, already the interface font).
Usage: python Scripts/MakeBuildRingArt.py  (then update the sha256 values in SourceData/UI/ui_images.json)
"""
import hashlib
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', 'SourceData', 'UI'))


def value_noise(size, cells, seed):
    rng = np.random.default_rng(seed)
    grid = rng.random((cells + 1, cells + 1))
    t = np.linspace(0, cells, size, endpoint=False)
    i = t.astype(int)
    f = t - i
    f = f * f * (3 - 2 * f)
    a = grid[i][:, i]
    b = grid[i][:, i + 1]
    c = grid[i + 1][:, i]
    d = grid[i + 1][:, i + 1]
    fx = f[None, :]
    fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fractal(size, seed, octaves=4, base=4):
    out = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise(size, base * 2 ** o, seed + o)
        total += amp
        amp *= 0.5
    return out / total


def save(name, rgba):
    path = os.path.join(ROOT, name)
    Image.fromarray(np.clip(rgba * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path, optimize=True)
    digest = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    print(name, rgba.shape[1], rgba.shape[0], digest)


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0, 1)
    return t * t * (3 - 2 * t)


def splat():
    n = 512
    y, x = np.mgrid[0:n, 0:n].astype(float)
    cx, cy = n / 2, n / 2 - 18
    dx, dy = x - cx, y - cy
    r = np.hypot(dx, dy)
    ang = np.arctan2(dy, dx)
    rng = np.random.default_rng(7)
    # The body: a wobbly blob whose rim follows angular noise.
    lobes = np.zeros_like(ang)
    for k, amp in ((3, 16), (5, 12), (8, 9), (13, 6), (21, 4)):
        lobes += amp * np.sin(k * ang + rng.random() * 6.28)
    rim = 150 + lobes + 22 * (fractal(n, 11, 4, 3) - 0.5)
    alpha = smooth(rim + 2, rim - 2, r)
    # Drips running down from the lower rim.
    for _ in range(7):
        a0 = rng.uniform(0.25, 2.9)
        x0 = cx + math.cos(a0) * 140
        width = rng.uniform(7, 15)
        length = rng.uniform(40, 95)
        top = cy + math.sin(a0) * 110
        bottom = top + length
        core = smooth(width, width - 2.5, np.abs(x - x0))
        alpha = np.maximum(alpha, core * smooth(bottom + 1, bottom - 2, y) * (y > top))
        # The round bead at the drip's end.
        alpha = np.maximum(alpha, smooth(width * 1.25 + 1.5, width * 1.25 - 1.5, np.hypot(x - x0, y - bottom)))
    # Spatter: small round drops flung outwards, denser near the blob.
    for _ in range(90):
        a = rng.uniform(0, 6.28)
        d = 165 + rng.exponential(45)
        if d > 245:
            continue
        s = rng.uniform(2.2, 9) * (1.3 - d / 300)
        px, py = cx + math.cos(a) * d, cy + math.sin(a) * d
        alpha = np.maximum(alpha, smooth(s + 1.2, s - 1.2, np.hypot(x - px, y - py)))
    # The soft overspray just outside the rim, speckled.
    speckle = (rng.random((n, n)) > 0.72).astype(float)
    mist = smooth(rim + 42, rim, r) * speckle * 0.55
    alpha = np.clip(np.maximum(alpha, mist), 0, 1)
    rgba = np.ones((n, n, 4))
    rgba[..., 3] = alpha
    save('ring_splat.png', rgba)


def metal():
    n = 256
    y, x = np.mgrid[0:n, 0:n].astype(float) / (n - 1)
    rng = np.random.default_rng(23)
    # Brushed along the diagonal: long thin streaks.
    streak = np.zeros((n, n))
    u = (x + y) * 0.5
    for _ in range(140):
        c = rng.random()
        w = rng.uniform(0.0015, 0.006)
        streak += rng.uniform(-0.6, 0.6) * np.exp(-((u - c) / w) ** 2)
    streak = streak / (np.abs(streak).max() + 1e-6)
    grain = fractal(n, 29, 3, 16) - 0.5
    lum = 0.64 + 0.11 * streak + 0.1 * grain
    # A bevel: the top-left rim catches the light, the bottom-right falls into shadow.
    edge = np.minimum.reduce([x, y, 1 - x, 1 - y])
    bevel = smooth(0.07, 0.0, edge)
    light = np.clip((1 - x) * 0.5 + (1 - y) * 0.5, 0, 1)
    lum = lum + bevel * (light - 0.5) * 0.9
    lum = np.clip(lum, 0, 1)
    rgba = np.ones((n, n, 4))
    rgba[..., 0] = lum
    rgba[..., 1] = lum
    rgba[..., 2] = np.clip(lum * 1.02, 0, 1)
    save('ring_metal.png', rgba)


def octagon_distance(x, y, half, cut):
    # Signed distance-like value to an axis-aligned octagon (negative inside).
    ax, ay = np.abs(x), np.abs(y)
    d1 = np.maximum(ax, ay) - half
    d2 = (ax + ay) / math.sqrt(2) - (half * 2 - cut) / math.sqrt(2)
    return np.maximum(d1, d2)


def socket():
    n = 256
    y, x = np.mgrid[0:n, 0:n].astype(float)
    cx = (n - 1) / 2
    dx, dy = x - cx, y - cx
    half = n / 2 - 3
    outer = octagon_distance(dx, dy, half, half * 2 * 0.3)
    inner = octagon_distance(dx, dy, half * 0.8, half * 2 * 0.8 * 0.3)
    alpha = smooth(1.2, -1.2, outer)
    ang = np.arctan2(dy, dx)
    # Conic metal: four light and four dark lobes like a machined seat.
    conic = 0.6 + 0.32 * np.cos(4 * (ang - 0.38)) + 0.06 * np.cos(9 * ang)
    grain = (fractal(n, 41, 3, 12) - 0.5) * 0.1
    lum = conic + grain
    # The outer bevel and the lip into the cavity.
    rim_light = smooth(-7, -1, outer) * np.clip(0.5 - (dx + dy) / (n * 1.2), 0, 1) * 0.8
    lip = smooth(10, 0, inner) * smooth(-4, 0, inner)
    lum = lum + rim_light - lip * 0.45
    # Inside the lip the seat darkens (the ring paints the cavity on top).
    cavity = smooth(0.5, -2, inner)
    lum = lum * (1 - cavity) + 0.16 * cavity
    lum = np.clip(lum, 0, 1)
    rgba = np.ones((n, n, 4))
    rgba[..., 0] = lum
    rgba[..., 1] = lum
    rgba[..., 2] = np.clip(lum * 1.03, 0, 1)
    rgba[..., 3] = alpha
    save('ring_socket.png', rgba)


def fresh():
    n = 512
    scale = 4
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    cx = cy = n / 2
    points = 12

    def star(radius, inner):
        out = []
        for i in range(points * 2):
            a = math.radians(-90 + 12 + i * 180 / points)
            r = radius if i % 2 == 0 else radius * inner
            out.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
        return out

    # A hard shadow, the white die-cut border, then the red sticker.
    shadow = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(px + 10, py + 14) for px, py in star(236, 0.8)], fill=(0, 0, 0, 120))
    img = Image.alpha_composite(img, shadow)
    draw = ImageDraw.Draw(img)
    draw.polygon(star(236, 0.8), fill=(255, 255, 255, 255))
    draw.polygon(star(212, 0.79), fill=(255, 46, 85, 255))
    # Paper grain and a light sweep on the sticker.
    arr = np.asarray(img).astype(float) / 255
    grain = fractal(n, 53, 3, 24) - 0.5
    red = (arr[..., 0] > 0.9) & (arr[..., 1] < 0.4)
    yy, xx = np.mgrid[0:n, 0:n]
    sweep = np.clip(1 - (xx + yy) / n, 0, 1) * 0.18
    for ch in range(3):
        arr[..., ch] = np.where(red, np.clip(arr[..., ch] + grain * 0.1 + sweep, 0, 1), arr[..., ch])
    img = Image.fromarray((arr * 255).astype(np.uint8), 'RGBA')
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(os.path.join(ROOT, 'Fonts', 'SourceHanSansSC-Heavy.otf'), 250)
    box = draw.textbbox((0, 0), '新', font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    draw.text((cx - tw / 2 - box[0], cy - th / 2 - box[1] - 4), '新', font=font, fill=(255, 255, 255, 255))
    img = img.resize((n // scale, n // scale), Image.LANCZOS)
    save('ring_fresh.png', np.asarray(img).astype(float) / 255)


def body():
    n = 512
    rng = np.random.default_rng(61)
    y, x = np.mgrid[0:n, 0:n].astype(float) / n
    # The ground: a deep violet to magenta spray gradient with mottling, like the icon set.
    g = np.clip((x * 0.4 + y * 0.6), 0, 1)
    mott = fractal(n, 67, 4, 4) - 0.5
    c0 = np.array([0.20, 0.06, 0.38])
    c1 = np.array([0.55, 0.08, 0.52])
    ground = c0[None, None, :] * (1 - g[..., None]) + c1[None, None, :] * g[..., None] + mott[..., None] * 0.12
    img = Image.fromarray((np.clip(ground, 0, 1) * 255).astype(np.uint8), 'RGB').convert('RGBA')
    # A teal spray haze behind the figure.
    haze = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    hd = ImageDraw.Draw(haze)
    hd.ellipse((70, 90, 450, 470), fill=(40, 220, 200, 150))
    haze = haze.filter(ImageFilter.GaussianBlur(40))
    img = Image.alpha_composite(img, haze)
    # The jersey: a sleeveless top with a deep collar, neon yellow with a black keyline and a hard shadow.
    jersey = [(168, 58), (214, 58), (234, 120), (256, 150), (278, 120), (298, 58), (344, 58), (350, 112), (368, 150),
              (398, 178), (404, 300), (396, 452), (116, 452), (108, 300), (114, 178), (144, 150), (162, 112)]
    shadow = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(px + 16, py + 18) for px, py in jersey], fill=(10, 6, 20, 200))
    img = Image.alpha_composite(img, shadow)
    draw = ImageDraw.Draw(img)
    draw.polygon(jersey, fill=(228, 255, 46, 255), outline=(12, 8, 18, 255))
    draw.line(jersey + [jersey[0]], fill=(12, 8, 18, 255), width=14, joint='curve')
    # The collar and arm-hole trims in magenta, then a big number in black spray.
    draw.line([(214, 62), (234, 120), (256, 148), (278, 120), (298, 62)], fill=(255, 60, 150, 255), width=16, joint='curve')
    draw.line([(162, 112), (144, 150), (120, 176)], fill=(255, 60, 150, 255), width=12, joint='curve')
    draw.line([(350, 112), (368, 150), (392, 176)], fill=(255, 60, 150, 255), width=12, joint='curve')
    font = ImageFont.truetype(os.path.join(ROOT, 'Fonts', 'SourceHanSansSC-Heavy.otf'), 190)
    box = draw.textbbox((0, 0), '1', font=font)
    draw.text((256 - (box[2] - box[0]) / 2 - box[0] + 10, 300 - (box[3] - box[1]) / 2 - box[1]), '1', font=font, fill=(18, 12, 26, 255))
    # Drips off the hem and spatter dots.
    for _ in range(5):
        x0 = rng.uniform(170, 350)
        length = rng.uniform(25, 60)
        draw.line([(x0, 452), (x0, 452 + length)], fill=(228, 255, 46, 255), width=10)
        draw.ellipse((x0 - 7, 452 + length - 7, x0 + 7, 452 + length + 7), fill=(228, 255, 46, 255))
    for _ in range(60):
        px, py = rng.uniform(30, 482), rng.uniform(30, 482)
        s = rng.uniform(2, 6)
        draw.ellipse((px - s, py - s, px + s, py + s), fill=(255, 255, 255, int(rng.uniform(60, 160))))
    img = img.resize((128, 128), Image.LANCZOS)
    save('ring_body.png', np.asarray(img).astype(float) / 255)


if __name__ == '__main__':
    splat()
    metal()
    socket()
    fresh()
    body()
