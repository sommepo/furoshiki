#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The licences of the libraries a Linux package carries in its lib folder.

    tools/lib_licences.py SOURCES LICENSES-FOLDER [--qt-licence FILE] [--pcsx2-licences FILE]

SOURCES is package-linux.sh's lib-sources.txt: each library's name in lib and the file it was
copied from. Each is looked up where it came from:
- a conda prefix (a folder with conda-meta/): the package whose files list it, its "license"
  field and the texts in its info/licenses (or info/LICENSE*);
- this computer's /usr (dpkg -S) or a folder of unpacked .deb files beside a debs/ folder: the
  Debian package and its usr/share/doc/<package>/copyright;
- PCSX2's own builds of rapidyaml and shaderc (in its prefix, no package): PCSX2's list of
  third-party licences (FILE);
- the official Qt (a gcc_64/lib folder): Qt's LGPL-3.0 (FILE), and for the ICU it carries, the
  ICU licence of a conda icu package found in the prefixes already seen.
The texts go to LICENSES-FOLDER/libraries/<package>/, the list to LICENSES-FOLDER/LIBRARIES.txt.
Exit 1 when a library has no licence found or one that cannot go into a GPL-3.0 package
(a licence named non-free, proprietary, unknown, OpenSSL's or SSLeay's old one, BSD-4-Clause).
"""
import json
import pathlib
import re
import shutil
import subprocess
import sys

# Licences that do not go with the GPL version 3 in one package, or say nothing.
REFUSED = re.compile(r"non-?free|proprietary|unknown|LicenseRef|\bOpenSSL\b(?!-standalone)|SSLeay|BSD-4-Clause|"
                     r"\bOther\b", re.I)

conda_index: dict[pathlib.Path, dict[str, dict]] = {}


def conda_prefix(path: pathlib.Path):
    for parent in path.parents:
        if (parent / "conda-meta").is_dir():
            return parent
    return None


def conda_package(prefix: pathlib.Path, relative: str):
    if prefix not in conda_index:
        files = {}
        for meta in (prefix / "conda-meta").glob("*.json"):
            record = json.loads(meta.read_text(encoding="utf-8"))
            for name in record.get("files", []):
                files[name] = record
        conda_index[prefix] = files
    return conda_index[prefix].get(relative)


# Package caches seen (where a package's unpacked folder is): another environment's cache may
# still hold a package whose own environment's cache was cleaned.
caches: set[pathlib.Path] = set()

# Libraries built by PCSX2's own dependency script, not from a package: PCSX2's list of its
# third-party licences (licenses/PCSX2-ThirdPartyLicenses.html) has their texts.
PCSX2_BUILT = {"libryml": ("rapidyaml", "MIT"), "libshaderc_shared": ("shaderc", "Apache-2.0")}


def conda_texts(record: dict) -> list[pathlib.Path]:
    folder = pathlib.Path(record.get("extracted_package_dir") or record.get("link", {}).get("source", ""))
    if folder.is_dir():
        caches.add(folder.parent)
    else:
        for cache in sorted(caches):
            if (cache / folder.name).is_dir():
                folder = cache / folder.name
                break
    found = []
    if (folder / "info" / "licenses").is_dir():
        found = sorted(p for p in (folder / "info" / "licenses").rglob("*") if p.is_file())
    if not found and (folder / "info").is_dir():
        found = sorted(p for p in (folder / "info").glob("LICENSE*") if p.is_file())
    return found


def deb_licences(copyright_file: pathlib.Path) -> str:
    names = []
    for line in copyright_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("License:"):
            name = line.split(":", 1)[1].strip()
            if name and name not in names:
                names.append(name)
    return " AND ".join(names) if names else "(see copyright)"


deb_files: dict[pathlib.Path, dict[str, str]] = {}


def unpacked_deb(path: pathlib.Path):
    """A file of a root folder unpacked from .deb files kept in a debs/ folder beside it."""
    for parent in path.parents:
        debs = parent.parent / "debs"
        if parent.name == "root" and debs.is_dir():
            if parent not in deb_files:
                files = {}
                for deb in debs.glob("*.deb"):
                    package = deb.name.split("_")[0]
                    listing = subprocess.run(["dpkg-deb", "-c", str(deb)], capture_output=True, text=True).stdout
                    for line in listing.splitlines():
                        name = line.split()[-1] if " -> " not in line else line.split(" -> ")[0].split()[-1]
                        files[name.lstrip(".")] = package
                deb_files[parent] = files
            relative = "/" + str(path.relative_to(parent))
            return parent, deb_files[parent].get(relative)
    return None, None


def main(argv) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    sources, out = pathlib.Path(argv[0]), pathlib.Path(argv[1])
    qt_licence = pathlib.Path(argv[argv.index("--qt-licence") + 1]) if "--qt-licence" in argv else None
    pcsx2_list = pathlib.Path(argv[argv.index("--pcsx2-licences") + 1]) if "--pcsx2-licences" in argv else None
    libraries = out / "libraries"
    if libraries.exists():
        shutil.rmtree(libraries)
    libraries.mkdir(parents=True)
    rows, problems, icu_wanted = [], [], []
    entries = [line.split("\t", 1) for line in sources.read_text(encoding="utf-8").splitlines() if line.strip()]
    # The package caches of every environment first, so that one cleaned cache can be stood in for.
    for _, origin in entries:
        if prefix := conda_prefix(pathlib.Path(origin)):
            conda_package(prefix, "")
            for record in {id(r): r for r in conda_index[prefix].values()}.values():
                folder = pathlib.Path(record.get("extracted_package_dir") or "")
                if folder.is_dir():
                    caches.add(folder.parent)
    for line in sources.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        name, origin = line.split("\t", 1)
        path = pathlib.Path(origin)
        package = version = licence = None
        texts: list[pathlib.Path] = []
        if prefix := conda_prefix(path):
            record = conda_package(prefix, str(path.relative_to(prefix)))
            if record:
                package, version, licence = record["name"], record["version"], record.get("license", "")
                texts = conda_texts(record)
                if not texts and package in ("libgcc", "libstdcxx", "libgcc-ng", "libstdcxx-ng"):
                    # conda-forge's GCC runtime packages carry no text: the GPL-3.0 is COPYING.txt,
                    # and the GCC Runtime Library Exception is in Debian's gcc-N-base copyright.
                    texts = sorted(pathlib.Path("/usr/share/doc").glob("gcc-*-base/copyright"))[-1:]
            elif (known := PCSX2_BUILT.get(name.split(".so")[0])) and pcsx2_list:
                package, licence = known
                version = "PCSX2's build"
                texts = [pcsx2_list]
        elif path.parent.name == "lib" and path.parent.parent.name == "gcc_64":
            if name.startswith("libicu"):
                icu_wanted.append(name)
                continue
            package, version, licence = "qt", path.parent.parent.parent.name, "LGPL-3.0-only"
            texts = [qt_licence] if qt_licence else []
        else:
            root, deb = unpacked_deb(path)
            if deb:
                package, licence = deb, None
                copyright_file = root / "usr" / "share" / "doc" / deb / "copyright"
            elif str(path).startswith("/usr/"):
                answer = subprocess.run(["dpkg", "-S", str(path)], capture_output=True, text=True).stdout
                package = answer.split(":", 1)[0].strip() if answer else None
                copyright_file = pathlib.Path("/usr/share/doc") / (package or "-") / "copyright"
            else:
                copyright_file = pathlib.Path("-")
            if package:
                if not copyright_file.is_file():
                    # A library's -dev or other package of the same source may hold the text.
                    copyright_file = next(iter(sorted(copyright_file.parent.parent.glob(package.rstrip("0123456789.-") + "*/copyright"))), copyright_file)
                if copyright_file.is_file():
                    licence = deb_licences(copyright_file)
                    texts = [copyright_file]
                version = "deb"
        if not package:
            problems.append(f"{name}: no package found for {origin}")
            continue
        if not texts:
            problems.append(f"{name}: no licence text in {package} {version}")
        if not licence or REFUSED.search(licence):
            problems.append(f"{name}: {package} {version}: licence \"{licence}\" needs a look before it is published")
        folder = libraries / package
        folder.mkdir(exist_ok=True)
        for text in texts:
            target = folder / text.name
            if not target.exists():
                shutil.copyfile(text, target)
        rows.append((name, package, version, licence or "?", f"libraries/{package}" if texts else "-"))
    if icu_wanted:
        # Qt's own ICU: its licence text from any conda icu package seen (the same Unicode licence).
        icu = None
        for files in conda_index.values():
            for record in files.values():
                if record["name"] == "icu" and conda_texts(record):
                    icu = record
        folder = libraries / "icu-qt"
        folder.mkdir(exist_ok=True)
        if icu:
            for text in conda_texts(icu):
                shutil.copyfile(text, folder / text.name)
        else:
            problems.append("ICU (Qt's): no licence text found")
        for name in icu_wanted:
            rows.append((name, "icu (Qt's)", "73", "Unicode-3.0 (ICU)", "libraries/icu-qt" if icu else "-"))
    width = [max(len(r[i]) for r in rows + [("Library", "Package", "Version", "Licence", "Text")]) for i in range(5)]
    lines = ["The libraries in lib/ and their licences (from each one's package metadata).", ""]
    for row in [("Library", "Package", "Version", "Licence", "Text")] + sorted(rows):
        lines.append("  ".join(cell.ljust(width[i]) for i, cell in enumerate(row)).rstrip())
    (out / "LIBRARIES.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"lib_licences: {len(rows)} libraries, {len(list(libraries.iterdir()))} packages' texts in {libraries}")
    for problem in problems:
        print(f"lib_licences: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
