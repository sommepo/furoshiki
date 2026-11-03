#!/usr/bin/env python3
# Copyright Azahar Reader fork contributors
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.
"""Draw the layouts a profile reported over a harness screenshot.

    layout_check.py SHOT.png OUT.png [--scale 3] < layouts

Reads layout JSON lines (the harness's `layouts` command) from standard input and
draws, for the latest layout of every window still on screen, the text box in blue
and each character cell in red. The picture shows at a glance whether the cells sit
on the characters. Needs Pillow (the analysis Python, $READER_RE_PYTHON).
"""
import argparse
import json
import sys

from PIL import Image, ImageDraw

# Harness screenshots: top screen 400x240, then the 320x240 bottom screen centred below it.
ORIGIN = {0: (0, 0), 1: (40, 240)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args()

    current = {}
    for line in sys.stdin:
        line = line.strip()
        if not line.startswith("{"):
            continue
        layout = json.loads(line)
        if layout.get("closed"):
            current.pop(layout["window"], None)
        else:
            current[layout["window"]] = layout

    scale = args.scale
    image = Image.open(args.shot).convert("RGB")
    image = image.resize((image.width * scale, image.height * scale), Image.NEAREST)
    draw = ImageDraw.Draw(image)

    def rect(screen, r, colour):
        ox, oy = ORIGIN[screen]
        x, y, w, h = r
        draw.rectangle([(ox + x) * scale, (oy + y) * scale,
                        (ox + x + w) * scale - 1, (oy + y + h) * scale - 1], outline=colour)

    for layout in current.values():
        if layout.get("box"):
            rect(layout["screen"], layout["box"], (40, 120, 255))
        cells = [g for g in layout["glyphs"] if g]
        for g in cells:
            rect(layout["screen"], g, (255, 40, 40))
        print(f"window {layout['window']} seq {layout['seq']} screen {layout['screen']}: "
              f"{len(cells)} of {len(layout['glyphs'])} characters placed")
    image.save(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
