#!/usr/bin/env python3
"""
Generate touch button icon PNGs for the Android overlay.

Generates 128x128 white-on-transparent PNG icons for each button.
Requires Pillow: pip install Pillow

Usage:
    python3 gen_touch_icons.py [output_dir]

Output directory defaults to ./touch_icons/
"""

import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

ICON_SIZE = 128
ICONS = {
    "btn_secondary": "2nd",
    "btn_flare":     "FLR",
    "btn_bomb":      "BMB",
    "btn_map":       "MAP",
    "btn_rear":      "RVW",
    "btn_menu":      "ESC",
    "btn_fire":      "FIRE",
    "btn_cycle":     "CYC",
}

def make_icon(label, size=ICON_SIZE):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Draw circle outline
    margin = 8
    draw.ellipse(
        [margin, margin, size - margin, size - margin],
        outline=(255, 255, 255, 200),
        width=3
    )

    # Draw text centered
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 28)
    except (OSError, IOError):
        font = ImageFont.load_default()

    bbox = draw.textbbox((0, 0), label, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (size - tw) // 2
    ty = (size - th) // 2
    draw.text((tx, ty), label, fill=(255, 255, 255, 230), font=font)

    return img


def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "touch_icons"
    os.makedirs(output_dir, exist_ok=True)

    for name, label in ICONS.items():
        img = make_icon(label)
        path = os.path.join(output_dir, f"{name}.png")
        img.save(path)
        print(f"Generated {path}")

    print(f"\nAll icons generated in {output_dir}/")


if __name__ == "__main__":
    main()
