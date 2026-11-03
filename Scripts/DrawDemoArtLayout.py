"""Draw the Demo art layout map from a CaptureDemoArt.ps1 report.

Top view in the same orientation as the Map-TopDown screenshot (+X right, +Y down, 10 cm per pixel): the approved
D02 floor, walls, spawn/camp/shop/capture places, every decoration footprint coloured by zone, props taller than
150 cm in red, and the floor points where the occlusion sweep found a body hidden behind a prop.
"""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
EXTENT_CM = 7200
SCALE = 0.1
OCCLUDER_CM = 150
ZONE_COLOURS = {"outside": (96, 125, 160), "wall_base": (46, 150, 80), "capture": (150, 70, 190), "shop": (150, 70, 190)}
CODES = {"tree_round": "T", "tree_tall": "T", "tree_pine": "P", "flower_bed": "F", "planter": "p", "bench": "B", "trash_bin": "b",
         "flagpole": "FLAG", "grandstand": "G", "bleachers": "S", "snack_stall": "SHOP", "building_shops": "H", "building_block": "H",
         "street_lamp": "L"}


def px(x, y):
    return ((x + EXTENT_CM) * SCALE, (y + EXTENT_CM) * SCALE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    world = json.loads((ROOT / "SourceData" / "World" / "demo_world.json").read_text(encoding="utf-8"))
    size = int(2 * EXTENT_CM * SCALE)
    image = Image.new("RGB", (size, size + 120), (250, 250, 248))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    surface = world["geometry"]["surfaces"][0]
    draw.rectangle([px(surface["min_cm"]["x"], surface["min_cm"]["y"]), px(surface["max_cm"]["x"], surface["max_cm"]["y"])], fill=(226, 234, 224))
    for blocker in world["geometry"]["blockers"]:
        points = [px(p["x"], p["y"]) for p in blocker["polygon_cm"]]
        draw.polygon(points, fill=(80, 90, 100) if blocker["blocks_sight"] else (190, 150, 90))
    for anchor in world["anchors"]:
        x, y = anchor["position_cm"]["x"], anchor["position_cm"]["y"]
        radius = {"Spawn": 200, "Camp": 500, "Shop": 350, "Capture": 600}[anchor["kind"]]
        colour = {"Spawn": (60, 110, 220), "Camp": (200, 120, 40), "Shop": (150, 70, 190), "Capture": (190, 150, 20)}[anchor["kind"]]
        (cx, cy), r = px(x, y), radius * SCALE
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=colour, width=2)
        draw.text((cx + r + 2, cy - 6), anchor["id"].upper(), fill=colour, font=font)
    for point in report.get("occluded_points", []):
        cx, cy = px(point["x"], point["y"])
        draw.rectangle([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 140, 0))
    tall = []
    for decoration in report["decorations"]:
        (x0, y0, _), (x1, y1, z1) = decoration["min"], decoration["max"]
        high = z1 > OCCLUDER_CM
        on_floor = x1 > surface["min_cm"]["x"] and x0 < surface["max_cm"]["x"] and y1 > surface["min_cm"]["y"] and y0 < surface["max_cm"]["y"]
        colour = (220, 30, 30) if high and on_floor else ZONE_COLOURS[decoration["zone"]]
        draw.rectangle([px(x0, y0), px(x1, y1)], outline=colour, width=2)
        cx, cy = px((x0 + x1) / 2, (y0 + y1) / 2)
        draw.text((cx - 3, cy - 5), CODES.get(decoration["prop"], "?"), fill=colour, font=font)
        if high and on_floor:
            tall.append(f"{decoration['prop']} {z1:.0f} cm")
    occluders = ", ".join(f"{o['prop']} ({o['zone']}, {o['height_cm']:.0f} cm): {o['hidden_points']} points" for o in report.get("occluders", [])) or "none"
    lines = [
        "Demo art layout, top view: +X right, +Y down, 10 cm/px. Grey walls block sight; tan shop stall blocks movement only.",
        "Boxes = decoration footprints. Blue outside the walls, green low wall-base props (<=120 cm), purple capture/shop landmarks,",
        f"red = taller than {OCCLUDER_CM} cm on the arena floor: {', '.join(tall) or 'none'}. Orange = floor points where a body's chest/head is hidden.",
        f"Occluders found by the sweep: {occluders}.",
        "Codes: T tree, P pine, F flower bed, p planter, B bench, b bin, H building, G grandstand, S bleachers, L lamp.",
    ]
    for index, line in enumerate(lines):
        draw.text((10, size + 8 + index * 20), line, fill=(20, 20, 20), font=font)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    print(f"Layout map: {args.out}")


if __name__ == "__main__":
    main()
