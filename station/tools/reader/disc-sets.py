#!/usr/bin/env python3
# SPDX-FileCopyrightText: Station Reader fork contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes reader/core/disc_sets.inc: for each later disc of a game on several discs, the
first disc's serial, from DuckStation's game database (data/database/gamedb.json).

    tools/reader/disc-sets.py

The database names each disc of a set "<name> (Disc N)" and lists the serials a disc has
been sold under ("codes", the same release in the same place on every disc of it: FF9's
SCPS-45501, SLPM-87389, SLPS-02001 beside disc 1's SCPS-45500, SLPM-87388, SLPS-02000). A
later disc's serial goes to disc 1's of the same place in the list; where the lists differ
in length, to disc 1's serial of the same four letters, else to disc 1's first. Sets the
database has no disc 1 of are left out. Run it again when the database changes.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DISC = re.compile(r" \(Disc (\d+)\)")


def main():
    entries = json.loads((ROOT / "data/database/gamedb.json").read_text(encoding="utf-8"))
    sets = {}
    for entry in entries:
        found = DISC.search(entry["name"])
        if found:
            name = DISC.sub("", entry["name"])
            codes = entry.get("codes") or [entry["serial"]]
            sets.setdefault(name, {})[int(found.group(1))] = codes
    pairs = {}
    for name, discs in sets.items():
        first = discs.get(1)
        if not first:
            continue
        for number, codes in discs.items():
            if number == 1:
                continue
            for i, code in enumerate(codes):
                if len(codes) == len(first):
                    target = first[i]
                else:
                    target = next((c for c in first if c[:4] == code[:4]), first[0])
                if code != target and code not in first:
                    pairs.setdefault(code, target)
    lines = [
        "// Written by tools/reader/disc-sets.py from data/database/gamedb.json: do not edit.",
        "// A later disc's serial, then its game's first disc's (sorted for a binary search).",
    ]
    lines += [f'{{"{later}", "{first}"}},' for later, first in sorted(pairs.items())]
    out = ROOT / "reader/core/disc_sets.inc"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{out.relative_to(ROOT)}: {len(pairs)} later discs", file=sys.stderr)


if __name__ == "__main__":
    main()
