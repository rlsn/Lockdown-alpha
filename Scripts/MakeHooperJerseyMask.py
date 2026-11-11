"""Builds the seat-tint mask of the hooper body texture: the jersey's gold and purple, nothing else.

The Tripo body texture paints a gold and purple team kit. The seat tint (ULDHumanoidVisualComponent::ContenderTint)
replaces exactly these colours, so no seat shows the source kit's team colours; skin (hue 18-27, saturation about 0.5),
the white lettering and the dark shoes keep their texture. Run with --check to confirm the committed mask matches.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "SourceData/Art/Tripo/BasketballHero/tripo_convert_e5a46437-aea5-4d48-ac2f-14ab0faa81e9.fbm/SK_BasketballHero_UE5_basecolor.JPEG"
OUTPUT = ROOT / "SourceData/Art/Tripo/BasketballHero/Hooper/T_Hooper_JerseyMask.png"


def build() -> np.ndarray:
    image = Image.open(SOURCE).convert("RGB")
    hsv = np.asarray(image.convert("HSV")).astype(np.float32) / 255.0
    hue, saturation, value = hsv[..., 0] * 360.0, hsv[..., 1], hsv[..., 2]
    gold = (hue >= 29) & (hue <= 48) & (saturation >= 0.6) & (value >= 0.45)
    purple = (hue >= 245) & (hue <= 315) & (saturation >= 0.25) & (value >= 0.1)
    mask = Image.fromarray(((gold | purple) * 255).astype(np.uint8))
    # Drop single-pixel speckle, then soften the edge so the recolour has no stair steps.
    mask = mask.filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.GaussianBlur(1.0))
    return np.asarray(mask)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when the committed mask differs from a rebuild")
    args = parser.parse_args()
    mask = build()
    if args.check:
        if not OUTPUT.exists():
            print(f"MISSING {OUTPUT}", file=sys.stderr)
            return 1
        committed = np.asarray(Image.open(OUTPUT).convert("L"))
        if committed.shape != mask.shape or np.abs(committed.astype(np.int16) - mask).max() > 1:
            print(f"MISMATCH {OUTPUT} differs from its rebuild", file=sys.stderr)
            return 1
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(mask, mode="L").save(OUTPUT, optimize=False)
    print(f"Wrote {OUTPUT.relative_to(ROOT)} ({mask.shape[1]}x{mask.shape[0]}, {float((mask > 127).mean()):.3f} tinted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
