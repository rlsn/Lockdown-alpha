"""Builds the Lobby seat portraits (SourceData/UI/lobby_seat_1..8.png) from the UI coverage capture.

Input: a CaptureUICoverage.ps1 match run, whose Ref_Seat<N>_1080_100.png shots show one seat-tinted hooper of the
opening rest from above the front, on the plain rest-room floor. The floor (bright, unsaturated, connected to the image
border) and its soft shadow are keyed out; the body is cropped to the card's portrait frame and written as RGBA PNG.
The manifest SourceData/UI/ui_images.json gets each file's size and SHA-256 (the images are then imported by
ImportDemoArt.ps1 -Mode ImportUI). Usage: python Scripts/MakeLobbySeats.py <capture folder>
"""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "SourceData/UI"
CROP = (620, 60, 1300, 1080)   # the body's frame in the 1920x1080 shot (the camera centres the chest)
OUT_SIZE = (544, 816)
# Seat inks (LDBroadcast::SeatInk). A seat whose shot is spoiled (a wall behind it, a worn skill effect, the probing
# player's decal, no hooper in frame) is painted from seat 3's clean green jersey: its hue swapped for the ink, shading kept.
INKS = ["6896F6", "F6B368", "72F67C", "D172F6", "F6EA68", "68EEF6", "F66DBD", "F3F3F6"]
REPAINT = {1: 3, 6: 3, 8: 3}


def key(image):
    a = np.asarray(image.convert("RGB")).astype(np.float32) / 255.0
    top, low = a.max(2), a.min(2)
    sat = (top - low) / np.maximum(top, 1e-3)
    floor = (top > 0.42) & (sat < 0.14)
    labels, _ = ndimage.label(floor)
    border = set(np.unique(np.concatenate([labels[0], labels[-1], labels[:, 0], labels[:, -1]]))) - {0}
    background = np.isin(labels, list(border))
    # Floor seen between the legs: large bright islands in the lower third.
    islands, count = ndimage.label(floor & ~background)
    for index in range(1, count + 1):
        ys, _ = np.nonzero(islands == index)
        if len(ys) > 400 and ys.mean() > a.shape[0] * 0.66:
            background |= islands == index
    background = ndimage.binary_opening(background, iterations=2)
    body, _ = ndimage.label(~background)
    sizes = ndimage.sum(np.ones_like(body), body, range(1, body.max() + 1))
    keep = 1 + int(np.argmax(sizes)) if len(sizes) else 0
    alpha = (body == keep).astype(np.uint8) * 255
    out = image.convert("RGBA")
    out.putalpha(Image.fromarray(alpha).filter(ImageFilter.GaussianBlur(1.0)))
    return out


def repaint(image, ink):
    rgba = np.asarray(image).astype(np.float32) / 255.0
    rgb = rgba[..., :3]
    top, low = rgb.max(2), rgb.min(2)
    sat = (top - low) / np.maximum(top, 1e-3)
    green = (rgb[..., 1] >= top - 1e-6) & (sat > 0.22) & (rgb[..., 1] > rgb[..., 0] * 1.15)
    target = np.array([int(ink[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0
    shade = top / 0.96                       # the green jersey's own light, 1 at its brightest
    if target.min() > 0.9:                   # a white jersey keeps its shading on a near-white cloth
        painted = np.clip(0.18 + 0.86 * shade, 0, 1)[..., None] * target
    else:
        painted = np.clip(shade, 0, 1.1)[..., None] * target
    soft = ndimage.gaussian_filter(green.astype(np.float32), 0.8)[..., None]
    rgba[..., :3] = rgb * (1 - soft) + np.clip(painted, 0, 1) * soft
    return Image.fromarray((rgba * 255).round().astype(np.uint8), "RGBA")


def main(folder):
    manifest_path = UI / "ui_images.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    images = [row for row in manifest["images"] if not row["id"].startswith("lobby_seat_")]
    for seat in range(1, 9):
        source = REPAINT.get(seat, seat)
        shot = Path(folder) / f"Ref_Seat{source}_1080_100.png"
        portrait = key(Image.open(shot).crop(CROP)).resize(OUT_SIZE, Image.LANCZOS)
        if source != seat:
            portrait = repaint(portrait, INKS[seat - 1])
        name = f"lobby_seat_{seat}.png"
        portrait.save(UI / name, optimize=True)
        images.append({
            "id": f"lobby_seat_{seat}", "path": name,
            "sha256": hashlib.sha256((UI / name).read_bytes()).hexdigest(),
            "width": OUT_SIZE[0], "height": OUT_SIZE[1],
            "purpose": f"Lobby seat card {seat}: front portrait of the hooper in seat {seat}'s jersey tint",
            "provenance": "Rendered in-game from the project's own hooper model (rest-room capture by Scripts/CaptureUICoverage.ps1, "
                          "LDUICoverageProbe Ref_Seat shots) and cut out by Scripts/MakeLobbySeats.py (seats 1, 6 and 8 "
                          "repainted from seat 3's jersey). No third-party art.",
        })
    manifest["images"] = images
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("wrote 8 seat portraits")


if __name__ == "__main__":
    main(sys.argv[1])
