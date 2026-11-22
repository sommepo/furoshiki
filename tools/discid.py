#!/usr/bin/env python3
"""tools/discid.py IMAGE: the console and ID a disc image says it is (PSP, PS2, PlayStation,
Dreamcast), read from its first 256 MB without unpacking it; a .zip is read from inside (its
largest file). Run it under a memory cap (docs/adding-a-game.md, step 2)."""
import re, sys, zipfile
PAT = [
    ("PSP UMD_DATA.BIN", re.compile(rb"([A-Z]{4}-\d{5})\|[0-9A-F]{16}\|")),
    ("PS2 SYSTEM.CNF BOOT2", re.compile(rb"BOOT2\s*=\s*cdrom0:\\?([A-Z]{4}[_-]\d{3}\.\d{2})")),
    ("PS1 SYSTEM.CNF BOOT", re.compile(rb"BOOT\s*=\s*cdrom:\\?([A-Z]{4}[_-]\d{3}\.\d{2})")),
    ("Dreamcast IP.BIN", re.compile(rb"SEGA SEGAKATANA .{48}(.{10})", re.S)),
]
path = sys.argv[1]
if path.lower().endswith(".zip"):
    z = zipfile.ZipFile(path)
    f = z.open(max(z.infolist(), key=lambda i: i.file_size))
else:
    f = open(path, "rb")
tail, seen = b"", 0
while seen < (256 << 20):
    block = f.read(4 << 20)
    if not block:
        break
    data, seen = tail + block, seen + len(block)
    for what, pat in PAT:
        m = pat.search(data)
        if m:
            print(what, m.group(1).decode("ascii", "replace").strip())
            sys.exit(0)
    tail = data[-4096:]
print("no ID found: a compressed image (CHD), or another console")
sys.exit(1)
