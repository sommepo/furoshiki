# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The three parts the reading is made of: the engines (owocr's, each loaded at first use), the
stabiliser (when an area has stopped changing and is to be read) and the cleaner (from what an
engine saw to one line, and whether it is a new one).

A published tree and an installed helper have them beside this file. In the repository they come
from, they are kept once, where they were written (furochibi/server/console_ocr), and taken from
there.
"""

from __future__ import annotations

try:
    from . import cleaner, engine, stabiliser  # noqa: F401
except ImportError:
    import sys
    from pathlib import Path

    _server = Path(__file__).resolve().parents[3] / "furochibi" / "server"
    if not (_server / "console_ocr" / "engine.py").is_file():
        raise
    sys.path.insert(0, str(_server))
    from console_ocr import cleaner, engine, stabiliser  # noqa: F401
