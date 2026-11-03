#!/usr/bin/env python3
# SPDX-FileCopyrightText: PCSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+
"""Windows does not tell upper case from lower in file names; Linux does. The emulator's
Windows sources name some of Microsoft's headers in a letter case of their own
(<Shobjidl.h> for ShObjIdl.h). This makes a folder of links, one per such name, to the header
that is meant, for the cross-build to search as well. The same for the libraries the build
files name (PowrProf.lib for powrprof.lib), in the folder's `lib`.

    windows-case.py SOURCE_ROOT SDK_ROOT OUT_FOLDER
"""
import os
import re
import sys

source, sdk, out = sys.argv[1:4]
headers = {}  # lower-case name -> real path
for folder in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared",
               "sdk/include/winrt", "sdk/include/cppwinrt"):
    base = os.path.join(sdk, folder)
    for root, _, files in os.walk(base):
        for name in files:
            relative = os.path.relpath(os.path.join(root, name), base)
            headers.setdefault(relative.lower(), []).append(os.path.join(root, name))
have = {}
for paths in headers.values():
    for path in paths:
        have[os.path.basename(path)] = True

include = re.compile(rb'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
wanted = set()
for folder in ("common", "pcsx2", "pcsx2-qt", "updater", "3rdparty", "reader"):
    for root, _, files in os.walk(os.path.join(source, folder)):
        for name in files:
            if not name.endswith((".h", ".hpp", ".c", ".cpp", ".inl")):
                continue
            try:
                data = open(os.path.join(root, name), "rb").read()
            except OSError:
                continue
            for found in include.findall(data):
                wanted.add(found.decode("utf-8", "replace").replace("\\", "/"))

os.makedirs(out, exist_ok=True)
made = 0
for name in sorted(wanted):
    lower = name.lower()
    if lower not in headers:
        continue
    # Already there under this very spelling?
    if any(path.endswith("/" + name) for path in headers[lower]):
        continue
    link = os.path.join(out, name)
    os.makedirs(os.path.dirname(link), exist_ok=True)
    if os.path.islink(link):
        os.remove(link)
    os.symlink(headers[lower][0], link)
    made += 1
print(f"{made} header names linked in {out}")

# The libraries.
libraries = {}
for folder in ("crt/lib/x86_64", "sdk/lib/um/x86_64", "sdk/lib/ucrt/x86_64"):
    base = os.path.join(sdk, folder)
    for name in os.listdir(base) if os.path.isdir(base) else ():
        libraries.setdefault(name.lower(), {})[name] = os.path.join(base, name)
named = re.compile(rb"\b([A-Za-z0-9_]+\.lib)\b", re.I)
wanted = set()
for folder in ("CMakeLists.txt", "cmake", "common", "pcsx2", "pcsx2-qt", "updater", "3rdparty", "reader"):
    start = os.path.join(source, folder)
    walk = os.walk(start) if os.path.isdir(start) else [(source, [], [folder])]
    for root, _, files in walk:
        for name in files:
            if name != "CMakeLists.txt" and not name.endswith(".cmake"):
                continue
            try:
                data = open(os.path.join(root, name), "rb").read()
            except OSError:
                continue
            wanted.update(found.decode() for found in named.findall(data))
os.makedirs(os.path.join(out, "lib"), exist_ok=True)
made = 0
for name in sorted(wanted):
    spellings = libraries.get(name.lower())
    if not spellings or name in spellings:
        continue
    link = os.path.join(out, "lib", name)
    if os.path.islink(link):
        os.remove(link)
    os.symlink(sorted(spellings.values())[0], link)
    made += 1
print(f"{made} library names linked in {out}/lib")
