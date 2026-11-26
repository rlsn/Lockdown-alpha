"""Builds the Lobby seat portraits (SourceData/UI/lobby_seat_1..8.png) from the UI coverage capture.

Input: a CaptureUICoverage.ps1 match run, whose Ref_Seat<N>_1080_100.png shots show the host's blue seat-1 hooper of the
opening rest held in pose N, from above, on the plain rest-room floor. Each seat's jersey is painted in its seat ink. The floor (bright, unsaturated, connected to the image
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
AREA = (60, 0, 1860, 1080)     # where the body may stand in the 1920x1080 shot
FRAME = (460, 690)             # the portrait frame cut around it: centred on the body, a little air over the head
OUT_SIZE = (544, 816)
# Seat inks (LDBroadcast::SeatInk). Every shot is the blue seat-1 body: its jersey hue is swapped for the seat's ink,
# shading kept.
INKS = ["6896F6", "F6B368", "72F67C", "D172F6", "F6EA68", "68EEF6", "F66DBD", "F3F3F6"]


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
    # The own body's foot marker (a seat-coloured disc with a yellow rim) lies on the floor under the feet: the portrait
    # ends where its rim begins.
    rim = (a[..., 0] > 0.8) & (a[..., 1] > 0.72) & (a[..., 2] < 0.66) & (a[..., 1] > a[..., 0] * 0.8) & (sat > 0.25)
    rim[: a.shape[0] // 2] = False
    rows = np.nonzero(rim.sum(1) > 8)[0]
    if len(rows):
        alpha[max(rows[0] - 3, 0):] = 0
    out = image.convert("RGBA")
    out.putalpha(Image.fromarray(alpha).filter(ImageFilter.GaussianBlur(1.0)))
    return out


def repaint(image, ink):
    rgba = np.asarray(image).astype(np.float32) / 255.0
    rgb = rgba[..., :3]
    top, low = rgb.max(2), rgb.min(2)
    sat = (top - low) / np.maximum(top, 1e-3)
    jersey = (rgb[..., 2] >= top - 1e-6) & (sat > 0.22) & (rgb[..., 2] > rgb[..., 0] * 1.15)
    target = np.array([int(ink[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0
    lit = float(np.percentile(top[jersey], 92)) if jersey.any() else 0.96
    shade = top / lit                        # the blue jersey's own light, 1 where it is lit
    if target.min() > 0.9:                   # a white jersey keeps its shading on a near-white cloth
        painted = np.clip(0.18 + 0.86 * shade, 0, 1)[..., None] * target
    else:
        painted = np.clip(shade, 0, 1.05)[..., None] * target
    soft = ndimage.gaussian_filter(jersey.astype(np.float32), 0.8)[..., None]
    rgba[..., :3] = rgb * (1 - soft) + np.clip(painted, 0, 1) * soft
    return Image.fromarray((rgba * 255).round().astype(np.uint8), "RGBA")


def main(folder):
    manifest_path = UI / "ui_images.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    images = [row for row in manifest["images"] if not row["id"].startswith("lobby_seat_")]
    for seat in range(1, 9):
        shot = Path(folder) / f"Ref_Seat{seat}_1080_100.png"
        cut = key(Image.open(shot).crop(AREA))
        # The frame hangs from the head: a little air over it, centred on the head's own columns (the top fifth of the
        # body), so every card shows the face in its upper part whatever the pose. Room is added where the shot ends.
        mask = np.asarray(cut.getchannel("A")) > 128
        rows = np.nonzero(mask.any(1))[0]
        top, bottom = int(rows[0]), int(rows[-1])
        head = np.nonzero(mask[top:top + max((bottom - top) // 5, 40)].any(0))[0]
        centre = int(head[0] + head[-1]) // 2
        x0, y0 = centre - FRAME[0] // 2, top - 36
        room = Image.new("RGBA", (cut.width + 2 * FRAME[0], cut.height + 2 * FRAME[1]), (0, 0, 0, 0))
        room.paste(cut, (FRAME[0], FRAME[1]))
        portrait = room.crop((x0 + FRAME[0], y0 + FRAME[1], x0 + 2 * FRAME[0], y0 + 2 * FRAME[1])).resize(OUT_SIZE, Image.LANCZOS)
        if seat != 1:
            portrait = repaint(portrait, INKS[seat - 1])
        name = f"lobby_seat_{seat}.png"
        portrait.save(UI / name, optimize=True)
        images.append({
            "id": f"lobby_seat_{seat}", "path": name,
            "sha256": hashlib.sha256((UI / name).read_bytes()).hexdigest(),
            "width": OUT_SIZE[0], "height": OUT_SIZE[1],
            "purpose": f"Lobby seat card {seat}: portrait of the hooper in pose {seat}, jersey in seat {seat}'s ink",
            "provenance": "Rendered in-game from the project's own hooper model and clips (rest-room capture by "
                          "Scripts/CaptureUICoverage.ps1, LDUICoverageProbe Ref_Seat shots), cut out and jersey-painted by "
                          "Scripts/MakeLobbySeats.py. No third-party art.",
        })
    manifest["images"] = images
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("wrote 8 seat portraits")


if __name__ == "__main__":
    main(sys.argv[1])
