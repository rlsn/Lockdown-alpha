"""Generates the build ring's own art into SourceData/UI (listed in ui_images.json, imported by ImportDemoArt.ps1 -Mode ImportUI).

The image is drawn procedurally here from fixed seeds, so the bytes are reproducible and carry no third-party art:
  ring_body.png    128x128  the 本体 host icon: a jersey in the icon set's aerosol style on a coloured ground
The 1 glyph uses SourceData/UI/Fonts/SourceHanSansSC-Heavy.otf (SIL OFL 1.1, already the interface font).
Usage: python Scripts/MakeBuildRingArt.py  (then update the sha256 values in SourceData/UI/ui_images.json)
"""
import hashlib
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
    body()
