# Contact-sheet previews for the presentation-slice VFX sources (plain Python 3 + numpy + Pillow, no Blender needed):
#   python SourceData/VFX/Blender/slice_preview.py SourceData/VFX/Slice <previewDir>
# A preview approximates the runtime material (emissive = tint x glow, white-hot above GLOW_BODY, ink where glow = 0,
# opacity = A) over three ground swatches sampled from the game: the near-black VFX capture floor, the shaded paving and the
# sunlit warm paving of the street map (refB/m_b_skill.jpg). It is a Blender-side sanity check, not runtime acceptance.
import os, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

GROUNDS = {"dark": (22, 26, 24), "shade": (116, 121, 126), "warm": (189, 181, 166)}
INK = np.array([10, 11, 13], dtype=np.float32)
TINTS = {"white": (255, 255, 255), "water": (0x42, 0xB4, 0xFF), "shock": (0xFF, 0xD2, 0x3D), "orange": (0xFF, 0x6B, 0x1A)}

# id -> (columns, rows, tint used on the sheet)
FLIPBOOKS = {
    "fb_hit_basic": (4, 4, "white"), "fb_hit_finisher": (4, 4, "orange"), "fb_hit_skill": (4, 4, "white"),
    "fb_water_splash": (4, 4, "water"), "fb_shock_spark": (4, 4, "shock"), "fb_shock_field": (4, 4, "shock"),
}
STRIPS = {"tex_slash_edge": "white", "tex_water_jet": "water", "tex_shock_arcs": "shock", "tex_wet_decal": "water"}


def composite(rgba, ground, tint):
    g = rgba[..., 0:1]
    a = rgba[..., 3:4]
    tint = np.array(tint, dtype=np.float32)
    body = tint * np.clip(g / L.GLOW_BODY, 0, 1)
    hot = np.clip((g - 0.75) / 0.2, 0, 1)
    colour = body * (1 - hot) + 255.0 * hot
    colour = INK * (1 - np.clip((g - 0.04) / 0.25, 0, 1)) + colour * np.clip((g - 0.04) / 0.25, 0, 1)
    base = np.array(ground, dtype=np.float32)[None, None, :]
    return base * (1 - a) + colour * a


def to_image(arr):
    return Image.fromarray(np.clip(arr + 0.5, 0, 255).astype(np.uint8), "RGB")


def label(img, text):
    ImageDraw.Draw(img).text((6, 4), text, fill=(255, 0, 255))
    return img


def montage(images, gap=6):
    w = sum(i.width for i in images) + gap * (len(images) - 1)
    h = max(i.height for i in images)
    out = Image.new("RGB", (w, h), (255, 0, 255))
    x = 0
    for i in images:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def sheet(name, out_dir, src_dir):
    cols, rows, tint = FLIPBOOKS[name]
    atlas = L.read_png(os.path.join(src_dir, name + ".png"))
    full = [label(to_image(composite(atlas, GROUNDS[k], TINTS[tint])), f"{name} 1:1 {k}") for k in ("warm", "dark")]
    montage(full).save(os.path.join(out_dir, name + "_sheet_1to1.png"))
    half = []
    for k in ("dark", "shade", "warm"):
        img = to_image(composite(atlas, GROUNDS[k], TINTS[tint]))
        half.append(label(img.resize((img.width // 2, img.height // 2), Image.LANCZOS), f"{k} 50%"))
    # white tint check on the warm ground catches element colours that hide the shape
    img = to_image(composite(atlas, GROUNDS["warm"], TINTS["white"]))
    half.append(label(img.resize((img.width // 2, img.height // 2), Image.LANCZOS), "white on warm"))
    montage(half).save(os.path.join(out_dir, name + "_sheet_gameplay.png"))
    # filmstrip of the whole sequence at the gameplay size over the warm ground (about 128 px per frame)
    fw, fh = atlas.shape[1] // cols, atlas.shape[0] // rows
    scale = 128.0 / fw
    frames = []
    for i in range(cols * rows):
        r, c = divmod(i, cols)
        tile = to_image(composite(atlas[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw], GROUNDS["warm"], TINTS[tint]))
        frames.append(tile.resize((int(fw * scale), int(fh * scale)), Image.LANCZOS))
    strip = Image.new("RGB", (frames[0].width * len(frames), frames[0].height))
    for i, f in enumerate(frames):
        strip.paste(f, (i * f.width, 0))
    strip.save(os.path.join(out_dir, name + "_filmstrip.png"))


def strip_sheet(name, out_dir, src_dir):
    tint = STRIPS[name]
    tex = L.read_png(os.path.join(src_dir, name + ".png"))
    tiles = []
    for k in ("dark", "shade", "warm"):
        img = to_image(composite(tex, GROUNDS[k], TINTS[tint]))
        tiles.append(img)
    h = sum(t.height for t in tiles) + 6 * (len(tiles) - 1)
    out = Image.new("RGB", (tiles[0].width, h), (255, 0, 255))
    y = 0
    for t in tiles:
        out.paste(t, (0, y))
        y += t.height + 6
    out.save(os.path.join(out_dir, name + "_sheet_1to1.png"))


def mesh_sheet(out_dir):
    tiles = []
    for stage in (1, 2, 3):
        path = os.path.join(out_dir, f"raw_mesh_stage{stage}.png")
        if not os.path.exists(path):
            return
        raw = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
        for k in ("warm", "dark"):
            for tint in ("white", "orange"):
                img = to_image(composite(raw, GROUNDS[k], TINTS[tint]))
                tiles.append((stage, k, tint, img))
    big = Image.new("RGB", (768 * 3 + 12, 768 * 2 + 6), (255, 0, 255))
    small = Image.new("RGB", (192 * 3 + 12, 192 * 4 + 18), (255, 0, 255))
    for idx, (stage, k, tint, img) in enumerate(tiles):
        s = stage - 1
        if tint == "orange":
            big.paste(img, (s * 774, 0 if k == "warm" else 774))
        small.paste(img.resize((192, 192), Image.LANCZOS), (s * 198, (0 if k == "warm" else 2) * 198 + (0 if tint == "white" else 198)))
    label(big, "slash meshes 3 stages, orange tint, warm (top) / dark (bottom), Blender top view 2.26 px/cm").save(os.path.join(out_dir, "slash_meshes_1to1.png"))
    label(small, "gameplay scale 0.56 px/cm: rows warm white / warm orange / dark white / dark orange").save(os.path.join(out_dir, "slash_meshes_gameplay.png"))


def main():
    src = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    os.makedirs(out, exist_ok=True)
    only = set(sys.argv[3].split(",")) if len(sys.argv) > 3 else None
    for name in FLIPBOOKS:
        if (not only or name in only) and os.path.exists(os.path.join(src, name + ".png")):
            sheet(name, out, src)
    for name in STRIPS:
        if (not only or name in only) and os.path.exists(os.path.join(src, name + ".png")):
            strip_sheet(name, out, src)
    if not only or "slash_meshes" in only:
        mesh_sheet(out)


main()
