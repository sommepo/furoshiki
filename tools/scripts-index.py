#!/usr/bin/env python3
# SPDX-FileCopyrightText: Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make the list of game scripts that the apps and Furoshiki install from (Reader, Game scripts).

    tools/scripts-index.py FOLDER --system NAME [--check]

The forks' tools/reader/scripts-index.py for any console: Station's and Azahar's, which have
none, and Furoshiki's published tree (tools/public-tree.py makes its lists with this). Reads
every <ID>.lua in FOLDER and writes FOLDER/index.json: for each script the game's ID, what
its first lines say of it (title, reads, version, api), and the name, size and SHA-256 of
the script and of every data file it asks for with reader.data("name") (<ID>.name beside
it). A script whose data file is not in the folder (a character table made from the game,
which is not published) is left out of the list and named on standard error.

A game's names dictionary (names-<title>.zip, made by tools/titles.py: docs/titles.md) beside
the scripts is named in the entry of every script its index.json's furoshikiScope has ("names"),
and the apps install it with the script. A zip no listed script names is left out of the list
(public-tree.py leaves it out of the folder too). With --check
nothing is written: the list is printed. NAME is the console as the app's
Reader::ConsoleTraits::ScriptSystem says it (ps1, ps2, psp, dreamcast, 3ds).

An ID is what the console's game IDs look like in the forks: a disc's serial (SLPS-01794,
SLPM-65550, ULJM05718, HDR-0072, T14301M) or a 3DS title ID (0004000000088A00), capitals,
digits and hyphens, four to sixteen, with a digit among them; or a DS cartridge's game code,
four capitals (AWIJ).

The apps fetch the list from the published repository and each file from beside it, and check
every file against its size and SHA-256 before installing it (shared/reader/desktop/
script_index.*, shared/android/.../ScriptStore.kt). The format is theirs: change both with it.
"""
import hashlib
import io
import json
import re
import sys
import zipfile
from pathlib import Path

FORMAT = 1
NAME = re.compile(r"^((?=[A-Z-]*[0-9])[A-Z0-9][A-Z0-9-]{3,15}|[A-Z]{4})\.lua$")
HEADER = re.compile(r"^--\s*reader:\s*(\S+)\s+(.*?)\s*$")
DATA = re.compile(r"""reader\.data\(\s*["']([A-Za-z0-9_.-]+)["']\s*\)""")
NAMES = re.compile(r"^names-[a-z0-9][a-z0-9-]*\.zip$")


def names_scopes(files):
    """{game ID: the names zip whose furoshikiScope has it} of the zips among FILES."""
    out = {}
    for name in sorted(files):
        if not NAMES.match(name):
            continue
        try:
            with zipfile.ZipFile(io.BytesIO(files[name])) as z:
                index = json.loads(z.read("index.json").decode("utf-8"))
        except (KeyError, ValueError, zipfile.BadZipFile):
            continue
        for game in index.get("furoshikiScope", []):
            out.setdefault(str(game).strip().upper(), name)
    return out


def header(source):
    """The lines "-- reader: <key> <value>" among the comments a script begins with."""
    said = {}
    for raw in source.splitlines():
        line = raw.strip()
        if not line:
            continue
        if not line.startswith("--"):
            break
        match = HEADER.match(line)
        if match:
            said.setdefault(match.group(1), match.group(2))
    return said


def file_entry(name, data):
    return {"file": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def make(files, system):
    """The list from FILES ({name: bytes} of one folder). Returns (index, left out): the left
    out are (ID, the data files it asks for that are not there)."""
    scripts, left_out = [], []
    names = names_scopes(files)
    for name in sorted(files):
        match = NAME.match(name)
        if not match:
            continue
        game = match.group(1)
        source = files[name].decode("utf-8")
        said = header(source)
        data, missing = [], []
        for wanted in sorted(set(DATA.findall(source))):
            data_name = f"{game}.{wanted}"
            (data.append(file_entry(data_name, files[data_name])) if data_name in files
             else missing.append(data_name))
        if missing:
            left_out.append((game, missing))
            continue
        entry = {
            "id": game,
            "title": said.get("title", game),
            "reads": said.get("reads", ""),
            "version": said.get("version", ""),
            "api": int(said.get("api", "1")),
            **file_entry(name, files[name]),
        }
        if data:
            entry["data"] = data
        if game in names:
            entry["names"] = file_entry(names[game], files[names[game]])
        scripts.append(entry)
    return {"format": FORMAT, "system": system, "scripts": scripts}, left_out


def text(index):
    return json.dumps(index, ensure_ascii=False, indent=2) + "\n"


def main(argv):
    if not argv or argv[0].startswith("-") or "--system" not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    folder = Path(argv[0])
    system = argv[argv.index("--system") + 1]
    files = {path.name: path.read_bytes() for path in folder.iterdir() if path.is_file()}
    index, left_out = make(files, system)
    for game, missing in left_out:
        print(f"left out {game}: {', '.join(missing)} not in {folder}", file=sys.stderr)
    if "--check" in argv[1:]:
        sys.stdout.write(text(index))
    else:
        (folder / "index.json").write_text(text(index), encoding="utf-8")
        print(f"{folder / 'index.json'}: {len(index['scripts'])} scripts", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
