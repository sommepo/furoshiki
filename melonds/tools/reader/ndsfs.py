#!/usr/bin/env python3
# SPDX-FileCopyrightText: melonDS Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""List or unpack a DS cartridge's file system (its name and allocation tables), without running it.

    ndsfs.py ROM                     # every file: id, offset, size, path
    ndsfs.py ROM --extract DIR [--match TEXT]...   # unpack (only paths holding a TEXT)
    ndsfs.py ROM --arm9 OUT          # the ARM9's program as loaded (address in the header)

Everything taken from a game goes under private/.
"""
import os
import struct
import sys


def read_tables(rom):
    with open(rom, "rb") as f:
        head = f.read(0x200)
        fnt_off, fnt_size, fat_off, fat_size = struct.unpack_from("<IIII", head, 0x40)
        f.seek(fat_off)
        fat = f.read(fat_size)
        f.seek(fnt_off)
        fnt = f.read(fnt_size)
    alloc = [struct.unpack_from("<II", fat, i) for i in range(0, len(fat), 8)]
    files = []

    def walk(dir_id, prefix):
        entry = (dir_id & 0xFFF) * 8
        sub_off, first_id, _ = struct.unpack_from("<IHH", fnt, entry)
        at, file_id = sub_off, first_id
        while True:
            length = fnt[at]
            at += 1
            if length == 0:
                break
            is_dir, n = length & 0x80, length & 0x7F
            name = fnt[at:at + n].decode("shift_jis", "replace")
            at += n
            if is_dir:
                sub = struct.unpack_from("<H", fnt, at)[0]
                at += 2
                walk(sub, prefix + name + "/")
            else:
                start, end = alloc[file_id]
                files.append((file_id, start, end - start, prefix + name))
                file_id += 1

    walk(0xF000, "")
    return head, files


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 64
    rom = args[0]
    head, files = read_tables(rom)
    if "--arm9" in args:
        out = args[args.index("--arm9") + 1]
        rom_off, entry, load, size = struct.unpack_from("<IIII", head, 0x20)
        with open(rom, "rb") as f:
            f.seek(rom_off)
            data = f.read(size)
        open(out, "wb").write(data)
        print(f"arm9 at {load:08X}, entry {entry:08X}, {size} bytes")
        return 0
    if "--extract" in args:
        out = args[args.index("--extract") + 1]
        matches = [args[i + 1] for i, a in enumerate(args) if a == "--match"]
        with open(rom, "rb") as f:
            for _, start, size, path in files:
                if matches and not any(m in path for m in matches):
                    continue
                f.seek(start)
                target = os.path.join(out, path)
                os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
                open(target, "wb").write(f.read(size))
        return 0
    for file_id, start, size, path in files:
        print(f"{file_id:5d} {start:08X} {size:9d} {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
