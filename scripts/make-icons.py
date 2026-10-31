#!/usr/bin/env python3
"""Draws the two launcher icons of PB Store: res/icons/pbstore.bmp and pbstore_f.bmp (122 x 122, 8 bit grey, like the icons of the
sibling apps). A plain shopping-bag glyph, new artwork: black on white, and white on black for the focused icon. No PocketBook logo.

    python3 scripts/make-icons.py [out-dir]     (needs Pillow)
"""
import os
import sys

from PIL import Image, ImageDraw

SIZE = 122
SCALE = 4   # draw large, scale down for smooth edges


def glyph(ink, paper):
    s = SIZE * SCALE
    img = Image.new("L", (s, s), paper)
    d = ImageDraw.Draw(img)
    u = s / 122.0
    # the bag: a rounded rectangle, a bit narrower at the top
    left, right, top, bottom = 25 * u, 97 * u, 44 * u, 106 * u
    d.rounded_rectangle([left, top, right, bottom], radius=8 * u, outline=ink, width=int(6 * u))
    # the handle: an arch over the opening
    d.arc([42 * u, 18 * u, 80 * u, 66 * u], start=180, end=360, fill=ink, width=int(6 * u))
    # two small dots where the handle meets the bag
    for x in (46 * u, 76 * u):
        d.ellipse([x - 4 * u, 56 * u - 4 * u, x + 4 * u, 56 * u + 4 * u], fill=ink)
    # a plus sign on the bag: something to get
    cx, cy, arm = 61 * u, 80 * u, 12 * u
    d.line([cx - arm, cy, cx + arm, cy], fill=ink, width=int(6 * u))
    d.line([cx, cy - arm, cx, cy + arm], fill=ink, width=int(6 * u))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res", "icons")
    os.makedirs(out, exist_ok=True)
    glyph(0, 255).save(os.path.join(out, "pbstore.bmp"))
    glyph(255, 0).save(os.path.join(out, "pbstore_f.bmp"))


if __name__ == "__main__":
    main()
