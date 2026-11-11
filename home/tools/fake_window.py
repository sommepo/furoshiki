#!/usr/bin/env python3
"""A stand-in for a game's window (docs/furoshiki-3.md, "A window read by OCR"): Furochibi's
fake game (../furochibi/tools/fake_feeder.py: a dialogue box whose text types itself out) drawn
into a picture file again and again, which Furoshiki reads as a window's picture when
READER_HOME_FAKE_WINDOWS names the file.

    tools/fake_window.py FILE [--fps 4] [--seconds 0] [--lines 3] [--at SECONDS]

Each line is printed as "LINE <text>" once it has finished typing, as the fake feeder prints
it, so that a test can compare. With --at, one picture as the game is that many seconds in,
and nothing more (a wizard's picture). Run with a Python that has Pillow (Furochibi's .venv).
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "furochibi" / "tools"))
import fake_feeder  # noqa: E402


def write(image, path: Path) -> None:
    """Whole or not at all: the reader of the file never sees half a picture."""
    part = path.with_name(path.name + ".part")
    part.write_bytes(fake_feeder.jpeg(image))
    os.replace(part, path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=Path)
    parser.add_argument("--fps", type=float, default=4.0)
    parser.add_argument("--seconds", type=float, default=0.0, help="stop after this long (0: never)")
    parser.add_argument("--lines", type=int, default=len(fake_feeder.SCRIPT), help="the script's first lines")
    parser.add_argument("--first", type=int, default=0, help="the script's first line used")
    parser.add_argument("--at", type=float, default=None)
    parser.add_argument("--cps", type=float, default=22.0)
    parser.add_argument("--hold", type=float, default=4.0)
    parser.add_argument("--gap", type=float, default=1.0)
    args = parser.parse_args()
    scene = fake_feeder.Scene(fake_feeder.SCRIPT[args.first:args.first + args.lines], args.cps, args.hold, args.gap)
    if args.at is not None:
        write(scene.frame(scene.started + args.at), args.file)
        return
    print("fake window", args.file, flush=True)
    started = time.monotonic()
    while not args.seconds or time.monotonic() - started < args.seconds:
        began = time.monotonic()
        write(scene.frame(began), args.file)
        sys.stdout.flush()
        time.sleep(max(0.0, 1 / args.fps - (time.monotonic() - began)))


if __name__ == "__main__":
    main()
