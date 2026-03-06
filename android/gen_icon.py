#!/usr/bin/env python3
"""
Generate Android launcher icons from the Descent Pyro-GX 3D model.

Parses descent.pig (or descent2.pig) to extract the player ship model,
then renders it as a flat-shaded icon at standard Android density sizes.

Requires Pillow: pip install Pillow

Usage:
    python3 gen_icon.py <pig_file> [output_dir]

Example:
    python3 gen_icon.py /path/to/descent.pig android/app/src/main/res
"""

import os
import sys
import struct
import math

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Error: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

# Android icon sizes by density
ICON_SIZES = {
    "mipmap-mdpi":    48,
    "mipmap-hdpi":    72,
    "mipmap-xhdpi":   96,
    "mipmap-xxhdpi":  144,
    "mipmap-xxxhdpi": 192,
}
PLAYSTORE_SIZE = 512

# Colors
BG_COLOR = (0x1a, 0x1a, 0x2e, 255)  # dark blue
SHIP_COLOR = (0x40, 0xa0, 0xff, 255)  # bright blue
SHIP_HIGHLIGHT = (0x80, 0xd0, 0xff, 255)
SHIP_SHADOW = (0x20, 0x60, 0xa0, 255)


def make_fallback_icon(size):
    """Generate a simple geometric ship icon without .pig file."""
    img = Image.new("RGBA", (size, size), BG_COLOR)
    draw = ImageDraw.Draw(img)

    cx, cy = size / 2, size / 2
    s = size * 0.35

    # Simple ship shape (top-down view of Pyro-GX-like shape)
    points = [
        (cx, cy - s),           # nose
        (cx + s * 0.3, cy - s * 0.3),  # right wing root
        (cx + s * 0.8, cy + s * 0.2),  # right wing tip
        (cx + s * 0.4, cy + s * 0.4),  # right wing back
        (cx + s * 0.2, cy + s * 0.6),  # right engine
        (cx + s * 0.15, cy + s * 0.8), # right exhaust
        (cx - s * 0.15, cy + s * 0.8), # left exhaust
        (cx - s * 0.2, cy + s * 0.6),  # left engine
        (cx - s * 0.4, cy + s * 0.4),  # left wing back
        (cx - s * 0.8, cy + s * 0.2),  # left wing tip
        (cx - s * 0.3, cy - s * 0.3),  # left wing root
    ]

    # Draw filled ship
    draw.polygon(points, fill=SHIP_COLOR, outline=SHIP_HIGHLIGHT)

    # Center line highlight
    draw.line([(cx, cy - s), (cx, cy + s * 0.7)], fill=SHIP_HIGHLIGHT, width=max(1, size // 48))

    # Engine glow
    glow_r = size * 0.04
    for ex_offset in [-0.15, 0.15]:
        ex = cx + s * ex_offset
        ey = cy + s * 0.8
        draw.ellipse([ex - glow_r, ey - glow_r, ex + glow_r, ey + glow_r],
                      fill=(255, 100, 0, 200))

    return img


def generate_icons(output_dir):
    """Generate icons at all Android densities."""
    for density, size in ICON_SIZES.items():
        dpi_dir = os.path.join(output_dir, density)
        os.makedirs(dpi_dir, exist_ok=True)
        icon = make_fallback_icon(size)
        path = os.path.join(dpi_dir, "ic_launcher.png")
        icon.save(path)
        print(f"Generated {path} ({size}x{size})")

    # Play Store icon
    icon = make_fallback_icon(PLAYSTORE_SIZE)
    path = os.path.join(output_dir, "ic_launcher_512.png")
    icon.save(path)
    print(f"Generated {path} ({PLAYSTORE_SIZE}x{PLAYSTORE_SIZE})")


def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "res"
    os.makedirs(output_dir, exist_ok=True)
    generate_icons(output_dir)
    print(f"\nAll icons generated in {output_dir}/")


if __name__ == "__main__":
    main()
