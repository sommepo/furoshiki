#!/usr/bin/env python3
# SPDX-FileCopyrightText: melonDS Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unpack Cing's .wpf/.tbl archives (Wish Room's data/local/jp/textdata and the like).

    wpf.py ARCHIVE [--extract DIR] [--match TEXT] [--text]

ARCHIVE is the archive without its extension (data/local/jp/textdata): its .tbl lists the files
(0x20 bytes each: a name of 0x18, then size and offset) and its .wpf holds them, each after a
0x20-byte header with its name and a 0x10-byte one: 12 3D DA, then 01 packed or 00 stored, the
size unpacked, the size packed. Packed: a flag byte, low bit first, 1 a byte as it is, 0 a copy
of three bytes, a 16-bit place p and a length n + 4, which starts (out - (p + 0x103)) & 0xFFFF
bytes back (zeros before the start). Lists the files; --extract writes them (unpacked) into DIR;
--text prints a script (.txt: a count, offsets, pages of Shift-JIS) one page a line.
"""
import argparse
import pathlib
import struct
import sys


def unpack(data: bytes, size: int) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < size and i < len(data):
        flags = data[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags >> bit & 1:
                out.append(data[i])
                i += 1
            else:
                place = struct.unpack_from("<H", data, i)[0]
                length = data[i + 2] + 4
                i += 3
                back = (len(out) - (place + 0x103)) & 0xFFFF or 0x10000
                for _ in range(length):
                    at = len(out) - back
                    out.append(out[at] if at >= 0 else 0)
    return bytes(out)


def entries(archive: str):
    table = pathlib.Path(archive + ".tbl").read_bytes()
    blob = pathlib.Path(archive + ".wpf").read_bytes()
    for k in range(0, len(table), 0x20):
        name = table[k:k + 0x18].split(b"\0")[0].decode("ascii", "replace").lstrip("\\")
        if not name:
            continue
        _, offset = struct.unpack_from("<II", table, k + 0x18)
        head = blob[offset + 0x20:offset + 0x30]
        unpacked, packed = struct.unpack_from("<II", head, 4)
        body = blob[offset + 0x30:offset + 0x30 + packed]
        yield name, unpack(body, unpacked) if head[3] == 1 else blob[offset + 0x30:offset + 0x30 + unpacked]


def pages(script: bytes):
    count = struct.unpack_from("<I", script, 0)[0]
    start = 4 + 4 * count
    if start > len(script):
        return
    for k in range(count):
        at = start + struct.unpack_from("<I", script, 4 + 4 * k)[0]
        end = script.find(b"\0\0", at)
        yield script[at:end if end >= 0 else len(script)].replace(b"\0", b"/").decode("cp932", "replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("archive")
    parser.add_argument("--extract", metavar="DIR")
    parser.add_argument("--match", default="")
    parser.add_argument("--text", action="store_true")
    args = parser.parse_args()
    for name, data in entries(args.archive):
        if args.match not in name:
            continue
        if args.text and name.endswith(".txt"):
            for page in pages(data):
                print(f"{name}\t{page}")
        elif args.extract:
            out = pathlib.Path(args.extract) / name
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(data)
        else:
            print(f"{len(data):8d} {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
