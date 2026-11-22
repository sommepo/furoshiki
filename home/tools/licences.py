#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the licence texts a published package of Furoshiki carries (publish/NOTICES.md says
which is which).

    tools/licences.py --platform windows|android|linux --root REPO --forks FORKS --out DIR [--cores a,b,...]

REPO is the repository (home/publish/licenses, home/fonts, titles/LICENSE); FORKS is the checkout
whose forks the cores were built from, with their submodules (the same folder in a build of a
whole checkout). --cores names the cores the package has (station, flycast, ppsspp, azahar,
pcsx2, armsx2, vita3k, melonds); all of the platform's by default.

Into DIR it writes:
- each core's licence (and PCSX2's or ARMSX2's list of its libraries);
- the texts the program and the app need: the LGPL (Qt on Windows, meikiocr's models on
  Android), the Apache licence, the OFL of every font built in (home/fonts/OFL-*.txt), the
  names data's ODbL and DbCL (titles/LICENSE); on Windows the compiler's runtime exception and
  winpthreads' terms, and the OFL or Apache notice of PPSSPP's fonts beside its core; on Android
  ONNX Runtime's licence and Font Awesome's (PCSX2's font for its icons);
- FFmpeg's licence where Vita3K's core is in (FFmpeg is built into it, from Vita3K's ffmpeg-core);
- third-party/<core>.txt: the licence files of every library bundled in the core's fork (its
  dep/, ext/, deps/, externals/, external/ or 3rdparty/ folder), each library under its name,
  a text met before named once. It covers what the fork bundles, a little more than what a core
  links; a library whose terms are only in its source files' headers is listed as such.
Exit status 1 when a file that must be there is missing.
"""
import argparse
import hashlib
import html
import pathlib
import re
import struct
import sys

# Each fork: its licence files (source, name in the package), and where its bundled libraries are:
# "parents" are folders of libraries (each folder in it one library), "libraries" single ones.
FORKS = {
    "station": {"licences": [("station/LICENSE", "DuckStation-LICENSE.txt")], "parents": ["station/dep"]},
    "flycast": {"licences": [("flycast/LICENSE", "Flycast-LICENSE.txt")], "parents": ["flycast/core/deps"]},
    "ppsspp": {"licences": [("ppsspp/LICENSE.TXT", "PPSSPP-LICENSE.txt")], "parents": ["ppsspp/ext"]},
    "azahar": {"licences": [("azahar/license.txt", "Azahar-license.txt")], "parents": ["azahar/externals"]},
    "pcsx2": {"licences": [("pcsx2/COPYING.GPLv3", "PCSX2-COPYING.GPLv3.txt"),
                           ("pcsx2/bin/docs/ThirdPartyLicenses.html", "PCSX2-ThirdPartyLicenses.html")],
              "parents": ["pcsx2/3rdparty"]},
    "armsx2": {"licences": [("armsx2/COPYING.GPLv3", "ARMSX2-COPYING.GPLv3.txt"),
                            ("armsx2/bin/docs/ThirdPartyLicenses.html", "ARMSX2-ThirdPartyLicenses.html")],
               "parents": ["armsx2/ARMSX2/app/src/main/cpp/3rdparty", "armsx2/3rdparty"]},
    "vita3k": {"licences": [("vita3k/COPYING.txt", "Vita3K-COPYING.txt"),
                            ("vita3k/external/ffmpeg/copyright", "FFmpeg-LGPL-2.1.txt")],
               "parents": ["vita3k/external"]},
    "melonds": {"licences": [("melonds/LICENSE", "melonDS-LICENSE.txt"),
                             ("melonds/freebios/drastic_bios_readme.txt", "melonDS-FreeBIOS.txt")],
                "libraries": ["melonds/src/teakra", "melonds/src/fatfs", "melonds/src/tiny-AES-c",
                              "melonds/src/sha1", "melonds/src/xxhash", "melonds/src/dolphin", "melonds/freebios"]},
}
PLATFORM_CORES = {
    "windows": ["station", "flycast", "ppsspp", "azahar", "pcsx2", "vita3k"],
    "android": ["station", "flycast", "ppsspp", "azahar", "armsx2", "vita3k", "melonds"],
    "linux": ["station", "flycast", "ppsspp", "azahar", "pcsx2", "vita3k"],
}
# Texts kept in this repository: (source under REPO, name in the package, platforms).
TEXTS = [
    ("home/publish/licenses/LGPL-3.0.txt", "LGPL-3.0.txt", ("windows", "android", "linux")),
    ("home/publish/licenses/Apache-2.0.txt", "Apache-2.0.txt", ("windows", "android", "linux")),
    ("titles/LICENSE", "Names-data-ODbL-DbCL.txt", ("windows", "android", "linux")),
    ("home/publish/licenses/GCC-Runtime-Library-Exception-3.1.txt", "GCC-Runtime-Library-Exception-3.1.txt", ("windows",)),
    ("home/publish/licenses/winpthreads-COPYING.txt", "winpthreads-COPYING.txt", ("windows",)),
    ("home/publish/licenses/ONNXRuntime-LICENSE.txt", "ONNXRuntime-LICENSE.txt", ("android",)),
]
LICENCE_NAME = re.compile(r"^(licen[cs]e|copying|copyright|notice|unlicense)([._-].*)?$", re.I)
NOT_A_LICENCE = {".py", ".c", ".h", ".hpp", ".cpp", ".cc", ".cmake", ".in", ".html", ".htm", ".sh",
                 ".yml", ".yaml", ".json", ".xml", ".rs", ".go", ".js", ".ts", ".java", ".kt", ".bat"}
SKIP_DIRS = re.compile(r"^(\.git|tests?|testing|testdata|test_data|docs?|examples?|samples?|benchmarks?|fuzz\w*)$", re.I)
LARGEST = 400_000


def licence_files(library: pathlib.Path) -> list[pathlib.Path]:
    """A library's licence files: in its folder and the folders under it, two levels down."""
    found = []

    def walk(folder: pathlib.Path, depth: int):
        try:
            entries = sorted(folder.iterdir(), key=lambda p: p.name.lower())
        except OSError:
            return
        for entry in entries:
            if entry.is_file() and LICENCE_NAME.match(entry.name) and entry.suffix.lower() not in NOT_A_LICENCE \
                    and entry.stat().st_size <= LARGEST:
                found.append(entry)
        if depth < 2:
            for entry in entries:
                if entry.is_dir() and not entry.is_symlink() and not SKIP_DIRS.match(entry.name):
                    walk(entry, depth + 1)

    walk(library, 0)
    return found


SOURCE_SUFFIXES = (".h", ".hpp", ".hh", ".c", ".cpp", ".cc", ".cxx", ".inl")
COMMENT_LINE = re.compile(r"^\s*(//|/\*|\*|--|;)|^\s*$")


def header_notice(library: pathlib.Path) -> str:
    """Where a library's terms are only in its source files: the comment that holds its
    copyright, from the first of its files (the folder's own, then one level down) that has one."""
    files = sorted((p for p in library.glob("*") if p.suffix.lower() in SOURCE_SUFFIXES),
                   key=lambda p: (p.suffix.lower() not in (".h", ".hpp", ".hh"), p.name.lower()))
    files += sorted((p for p in library.glob("*/*") if p.suffix.lower() in SOURCE_SUFFIXES
                     and not SKIP_DIRS.match(p.parent.name)), key=lambda p: p.as_posix().lower())
    for file in files[:40]:
        try:
            lines = file.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines):
            if re.search(r"copyright|\(c\)|licen[cs]e", line, re.I) and COMMENT_LINE.match(line):
                start = i
                while start > 0 and COMMENT_LINE.match(lines[start - 1]) and lines[start - 1].strip():
                    start -= 1
                end = i
                while end + 1 < len(lines) and COMMENT_LINE.match(lines[end + 1]) and end - start < 80:
                    end += 1
                found = f"--- {file.name} (its header)\n\n" + "\n".join(lines[start:end + 1]).rstrip() + "\n"
                # "See notice at the end of this file" (pugixml, stb): that notice too.
                if re.search(r"end\W+of\W+(this|the)\W+file", "\n".join(lines[start:end + 1]), re.I):
                    for j in range(len(lines) - 1, end, -1):
                        if re.search(r"permission is hereby granted|copyright", lines[j], re.I):
                            first = j
                            while first > end + 1 and lines[first - 1].strip() and COMMENT_LINE.match(lines[first - 1]):
                                first -= 1
                            last = j
                            while last + 1 < len(lines) and COMMENT_LINE.match(lines[last + 1]) and last - first < 60:
                                last += 1
                            found += f"\n--- {file.name} (its end)\n\n" + "\n".join(lines[first:last + 1]).rstrip() + "\n"
                            break
                return found
    return ""


def third_party(forks: pathlib.Path, core: str) -> tuple[str, int]:
    fork = FORKS[core]
    libraries = []
    for parent in fork.get("parents", []):
        folder = forks / parent
        if folder.is_dir():
            # A build folder left in a checkout (name-build, a CMake cache) is no library.
            libraries += sorted((p for p in folder.iterdir() if p.is_dir() and not p.name.startswith(".")
                                 and not p.name.endswith("-build") and p.name != "cmake"
                                 and not (p / "CMakeCache.txt").exists()),
                                key=lambda p: p.name.lower())
    libraries += [forks / lib for lib in fork.get("libraries", []) if (forks / lib).is_dir()]
    parts = [
        f"The libraries bundled in the {core} core's fork, and their licence files as the fork carries them.\n"
        "Each library is under its own terms, as below. The core is distributed, with the rest of\n"
        "Furoshiki, under the GNU General Public License, version 3 (COPYING.txt).\n"
        "This list is made from the fork's folders of bundled libraries: it may name a library the\n"
        "core does not link.\n"]
    seen: dict[str, str] = {}
    unchecked, headers_only = [], []
    for library in libraries:
        name = library.relative_to(forks).as_posix()
        if not any(library.iterdir()):
            unchecked.append(name)
            continue
        files = licence_files(library)
        if not files:
            notice = header_notice(library)
            if notice:
                parts.append("=" * 78 + f"\n{name}\n" + "=" * 78 + "\n" + notice)
            else:
                headers_only.append(name)
            continue
        parts.append("=" * 78 + f"\n{name}\n" + "=" * 78 + "\n")
        for file in files:
            data = file.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            where = file.relative_to(forks).as_posix()
            if digest in seen:
                parts.append(f"--- {where}: the same text as {seen[digest]}\n")
                continue
            seen[digest] = where
            parts.append(f"--- {where}\n\n" + data.decode("utf-8", errors="replace").rstrip() + "\n")
    if headers_only:
        parts.append("=" * 78 + "\nLibraries whose terms are stated in their source files' headers\n" + "=" * 78 + "\n"
                     + "\n".join(headers_only) + "\n")
    if unchecked:
        parts.append("=" * 78 + "\nNot checked out where this list was made (submodules)\n" + "=" * 78 + "\n"
                     + "\n".join(unchecked) + "\n")
        print(f"licences: {core}: {len(unchecked)} empty library folder(s), not checked out: {', '.join(unchecked)}",
              file=sys.stderr)
    return "\n".join(parts), len(libraries)


def font_names(path: pathlib.Path) -> dict[int, str]:
    """A TrueType or OpenType font's name table: copyright (0), licence (13), its address (14)."""
    data = path.read_bytes()
    count = struct.unpack_from(">H", data, 4)[0]
    for i in range(count):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        if tag != b"name":
            continue
        _, records, strings = struct.unpack_from(">HHH", data, offset)
        names: dict[int, str] = {}
        for r in range(records):
            platform, _, language, name_id, length, at = struct.unpack_from(">HHHHHH", data, offset + 6 + 12 * r)
            raw = data[offset + strings + at: offset + strings + at + length]
            if platform == 3 and language == 0x409:
                names[name_id] = raw.decode("utf-16-be", errors="replace")
            elif platform == 1 and name_id not in names:
                names[name_id] = raw.decode("latin-1")
        return names
    return {}


def ofl_body(root: pathlib.Path) -> str:
    text = (root / "home/fonts/OFL-DotGothic16.txt").read_text(encoding="utf-8")
    return text[text.index("-----"):]


def font_notice(font: pathlib.Path, root: pathlib.Path) -> str:
    names = font_names(font)
    head = f"{font.name}\n\n{names.get(0, '').strip()}\n\n{names.get(13, '').strip()}\n{names.get(14, '').strip()}\n\n"
    if "Open Font License" in names.get(13, "") or "OFL" in names.get(14, ""):
        return head + ofl_body(root)
    if "Apache" in names.get(13, ""):
        return head + "The Apache License, version 2.0: Apache-2.0.txt.\n"
    return head


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=sorted(PLATFORM_CORES), required=True)
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--forks", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--cores", default="")
    parser.add_argument("--lenient", action="store_true",
                        help="report missing texts but do not fail (a dev build in a checkout without every submodule)")
    args = parser.parse_args()
    root, forks, out = args.root.resolve(), args.forks.resolve(), args.out
    cores = [c for c in args.cores.split(",") if c] or PLATFORM_CORES[args.platform]
    missing = []
    out.mkdir(parents=True, exist_ok=True)
    (out / "third-party").mkdir(exist_ok=True)

    def put(source: pathlib.Path, name: str):
        if not source.is_file():
            missing.append(str(source))
            return
        (out / name).write_bytes(source.read_bytes())

    for source, name, platforms in TEXTS:
        if args.platform in platforms:
            put(root / source, name)
    for ofl in sorted((root / "home/fonts").glob("OFL-*.txt")):
        put(ofl, ofl.name)
    for core in cores:
        if core not in FORKS:
            missing.append(f"no such core: {core}")
            continue
        for source, name in FORKS[core]["licences"]:
            put(forks / source, name)
        text, count = third_party(forks, core)
        (out / "third-party" / f"{core}.txt").write_text(text, encoding="utf-8")
        print(f"licences: third-party/{core}.txt: {count} libraries")
    if args.platform in ("windows", "linux") and "ppsspp" in cores:
        # PPSSPP's fonts beside its core (cores/ppsspp): Roboto Condensed (Apache) and Inconsolata (OFL).
        for font in sorted((forks / "ppsspp/assets").glob("*.ttf")):
            (out / f"PPSSPP-font-{font.stem}.txt").write_text(font_notice(font, root), encoding="utf-8")
    if args.platform == "android" and "armsx2" in cores:
        # Font Awesome (fa-solid-900.ttf, PCSX2's icons), from the list of PCSX2's libraries.
        listing = forks / "armsx2/bin/docs/ThirdPartyLicenses.html"
        found = re.search(r'<div id="fontawesome">(.*?)</div>', listing.read_text(encoding="utf-8"), re.S) \
            if listing.is_file() else None
        if found:
            text = html.unescape(re.sub(r"<[^>]+>", "", found.group(1))).strip()
            (out / "FontAwesome-LICENSE.txt").write_text(text + "\n", encoding="utf-8")
        else:
            missing.append(f"{listing}: Font Awesome's licence")
    for problem in missing:
        print(f"licences: missing {problem}", file=sys.stderr)
    return 1 if missing and not args.lenient else 0


if __name__ == "__main__":
    sys.exit(main())
