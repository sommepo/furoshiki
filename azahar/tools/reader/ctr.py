#!/usr/bin/env python3
# Copyright Azahar Reader fork contributors
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.
"""Read-only access to a decrypted 3DS cartridge or application image.

Supports NCSD (.3ds/.cci) and bare NCCH (.cxi/.app) containers with the
NoCrypto flag set. Nothing here writes to the source image.

    ctr.py info <image>
    ctr.py exefs <image> <outdir>          # .code is written decompressed
    ctr.py romfs-list <image> [--partition N]
    ctr.py romfs-extract <image> <outdir> [--partition N] [--match GLOB ...]
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass, field

MEDIA_UNIT = 0x200


class CtrError(Exception):
    pass


def lzss_decompress(compressed: bytes, decompressed_size: int) -> bytes:
    """Backward LZSS used for ExeFS .code (same walk as the emulator's loader)."""
    (top_and_bottom,) = struct.unpack_from("<I", compressed, len(compressed) - 8)
    out = decompressed_size
    index = len(compressed) - ((top_and_bottom >> 24) & 0xFF)
    stop = len(compressed) - (top_and_bottom & 0xFFFFFF)
    buf = bytearray(decompressed_size)
    buf[: len(compressed)] = compressed
    while index > stop:
        index -= 1
        control = compressed[index]
        for _ in range(8):
            if index <= stop or index <= 0 or out <= 0:
                break
            if control & 0x80:
                if index < 2:
                    raise CtrError("LZSS: truncated back-reference")
                index -= 2
                seg = compressed[index] | (compressed[index + 1] << 8)
                size = ((seg >> 12) & 15) + 3
                offset = (seg & 0x0FFF) + 2
                if out < size:
                    raise CtrError("LZSS: output underflow")
                for _ in range(size):
                    if out + offset >= decompressed_size:
                        raise CtrError("LZSS: reference outside output")
                    out -= 1
                    buf[out] = buf[out + 1 + offset]
            else:
                if out < 1:
                    raise CtrError("LZSS: output underflow")
                out -= 1
                index -= 1
                buf[out] = compressed[index]
            control = (control << 1) & 0xFF
    return bytes(buf)


@dataclass
class CodeSegment:
    address: int
    pages: int
    size: int


@dataclass
class ExHeader:
    name: str
    compressed_code: bool
    text: CodeSegment
    rodata: CodeSegment
    data: CodeSegment
    stack_size: int
    bss_size: int
    program_id: int


@dataclass
class RomFsFile:
    path: str
    offset: int  # absolute offset in the image
    size: int


@dataclass
class Ncch:
    image: "Image"
    offset: int
    size: int
    program_id: int
    partition_id: int
    product_code: str
    version: int
    flags: bytes
    exheader_size: int
    exefs_offset: int
    exefs_size: int
    romfs_offset: int
    romfs_size: int
    _exheader: ExHeader | None = field(default=None, repr=False)

    @property
    def encrypted(self) -> bool:
        return not (self.flags[7] & 0x04)

    def exheader(self) -> ExHeader | None:
        if not self.exheader_size:
            return None
        if self._exheader is None:
            raw = self.image.read(self.offset + 0x200, 0x400)
            seg = lambda o: CodeSegment(*struct.unpack_from("<III", raw, o))
            self._exheader = ExHeader(
                name=raw[0:8].rstrip(b"\0").decode("ascii", "replace"),
                compressed_code=bool(raw[0x0D] & 1),
                text=seg(0x10),
                stack_size=struct.unpack_from("<I", raw, 0x1C)[0],
                rodata=seg(0x20),
                data=seg(0x30),
                bss_size=struct.unpack_from("<I", raw, 0x3C)[0],
                program_id=struct.unpack_from("<Q", raw, 0x200)[0],
            )
        return self._exheader

    def exefs_files(self) -> dict[str, tuple[int, int]]:
        if not self.exefs_size:
            return {}
        base = self.offset + self.exefs_offset
        header = self.image.read(base, 0x200)
        files = {}
        for i in range(10):
            name = header[i * 16 : i * 16 + 8].rstrip(b"\0").decode("ascii", "replace")
            off, size = struct.unpack_from("<II", header, i * 16 + 8)
            if name and size:
                files[name] = (base + 0x200 + off, size)
        return files

    def exefs_read(self, name: str) -> bytes:
        files = self.exefs_files()
        if name not in files:
            raise CtrError(f"ExeFS has no {name!r}")
        off, size = files[name]
        return self.image.read(off, size)

    def code(self) -> bytes:
        """The program image as loaded at text.address (text + rodata + data)."""
        raw = self.exefs_read(".code")
        exh = self.exheader()
        if exh and exh.compressed_code:
            (extra,) = struct.unpack_from("<I", raw, len(raw) - 4)
            raw = lzss_decompress(raw, len(raw) + extra)
        return raw

    def romfs_files(self) -> list[RomFsFile]:
        if not self.romfs_size:
            return []
        base = self.offset + self.romfs_offset
        ivfc = self.image.read(base, 0x60)
        if ivfc[:4] != b"IVFC":
            raise CtrError("RomFS: missing IVFC header")
        master_hash_size = struct.unpack_from("<I", ivfc, 0x08)[0]
        l3_block_log2 = struct.unpack_from("<I", ivfc, 0x4C)[0]
        block = 1 << l3_block_log2
        l3 = base + ((0x60 + master_hash_size + block - 1) & ~(block - 1))
        h = self.image.read(l3, 0x28)
        (hlen, _dh_off, _dh_len, dm_off, dm_len, _fh_off, _fh_len, fm_off, fm_len,
         data_off) = struct.unpack("<10I", h)
        if hlen != 0x28:
            raise CtrError("RomFS: unexpected level 3 header")
        dirs = self.image.read(l3 + dm_off, dm_len)
        fmeta = self.image.read(l3 + fm_off, fm_len)
        out: list[RomFsFile] = []
        NONE = 0xFFFFFFFF

        def walk(doff: int, prefix: str) -> None:
            stack = [(doff, prefix)]
            while stack:
                doff, prefix = stack.pop()
                (_parent, _sibling, child, first_file, _hash, name_len) = struct.unpack_from(
                    "<6I", dirs, doff)
                name = dirs[doff + 0x18 : doff + 0x18 + name_len].decode("utf-16-le")
                path = prefix + name + "/" if name else prefix
                foff = first_file
                while foff != NONE:
                    (_fparent, fsib, d_off, d_size, _fhash, fname_len) = struct.unpack_from(
                        "<IIQQII", fmeta, foff)
                    fname = fmeta[foff + 0x20 : foff + 0x20 + fname_len].decode("utf-16-le")
                    out.append(RomFsFile(path + fname, l3 + data_off + d_off, d_size))
                    foff = fsib
                sub = child
                subs = []
                while sub != NONE:
                    subs.append(sub)
                    sub = struct.unpack_from("<I", dirs, sub + 4)[0]
                for s in reversed(subs):
                    stack.append((s, path))

        walk(0, "")
        out.sort(key=lambda f: f.path)
        return out


class Image:
    def __init__(self, path: str):
        self.path = path
        self._f = open(path, "rb")
        self.size = os.fstat(self._f.fileno()).st_size
        self.partitions: dict[int, Ncch] = {}
        head = self.read(0x100, 4)
        if head == b"NCSD":
            table = self.read(0x120, 0x40)
            for i in range(8):
                off, size = struct.unpack_from("<II", table, i * 8)
                if size:
                    self.partitions[i] = self._ncch(off * MEDIA_UNIT)
        elif head == b"NCCH":
            self.partitions[0] = self._ncch(0)
        else:
            raise CtrError("not an NCSD or NCCH image")

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > self.size:
            raise CtrError(f"read outside image: 0x{offset:x}+0x{size:x}")
        self._f.seek(offset)
        data = self._f.read(size)
        if len(data) != size:
            raise CtrError("short read")
        return data

    def _ncch(self, offset: int) -> Ncch:
        h = self.read(offset, 0x200)
        if h[0x100:0x104] != b"NCCH":
            raise CtrError(f"no NCCH at 0x{offset:x}")
        u32 = lambda o: struct.unpack_from("<I", h, o)[0]
        return Ncch(
            image=self,
            offset=offset,
            size=u32(0x104) * MEDIA_UNIT,
            partition_id=struct.unpack_from("<Q", h, 0x108)[0],
            program_id=struct.unpack_from("<Q", h, 0x118)[0],
            version=struct.unpack_from("<H", h, 0x112)[0],
            product_code=h[0x150:0x160].rstrip(b"\0").decode("ascii", "replace"),
            flags=h[0x188:0x190],
            exheader_size=u32(0x180),
            exefs_offset=u32(0x1A0) * MEDIA_UNIT,
            exefs_size=u32(0x1A4) * MEDIA_UNIT,
            romfs_offset=u32(0x1B0) * MEDIA_UNIT,
            romfs_size=u32(0x1B4) * MEDIA_UNIT,
        )

    @property
    def main(self) -> Ncch:
        return self.partitions[0]


def cmd_info(args) -> int:
    img = Image(args.image)
    info = {"path": os.path.basename(img.path), "size": img.size, "partitions": {}}
    for i, p in img.partitions.items():
        entry = {
            "offset": p.offset,
            "product_code": p.product_code,
            "program_id": f"{p.program_id:016X}",
            "partition_id": f"{p.partition_id:016X}",
            "version": p.version,
            "encrypted": p.encrypted,
            "exefs": sorted(p.exefs_files()) if not p.encrypted else None,
        }
        exh = p.exheader() if not p.encrypted else None
        if exh:
            code = p.code()
            entry["exheader"] = {
                "name": exh.name,
                "compressed_code": exh.compressed_code,
                "text": [hex(exh.text.address), hex(exh.text.size)],
                "rodata": [hex(exh.rodata.address), hex(exh.rodata.size)],
                "data": [hex(exh.data.address), hex(exh.data.size)],
                "bss_size": hex(exh.bss_size),
            }
            entry["code_size"] = len(code)
            entry["code_sha256"] = hashlib.sha256(code).hexdigest()
        info["partitions"][i] = entry
    json.dump(info, sys.stdout, indent=2)
    print()
    return 0


def _write_new(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    if os.path.exists(path):
        with open(path, "rb") as f:
            if f.read() == data:
                return
        raise CtrError(f"refusing to overwrite a different existing file: {path}")
    with open(path, "wb") as f:
        f.write(data)


def cmd_exefs(args) -> int:
    p = Image(args.image).partitions[args.partition]
    for name in p.exefs_files():
        data = p.code() if name == ".code" else p.exefs_read(name)
        out = os.path.join(args.outdir, "code.bin" if name == ".code" else name.lstrip(".") + ".bin")
        _write_new(out, data)
        print(f"{out}\t{len(data)}")
    return 0


def cmd_romfs_list(args) -> int:
    p = Image(args.image).partitions[args.partition]
    for f in p.romfs_files():
        print(f"{f.size:>12}  {f.path}")
    return 0


def _safe_join(root: str, rel: str) -> str:
    dest = os.path.normpath(os.path.join(root, rel))
    if not (dest == root or dest.startswith(root + os.sep)):
        raise CtrError(f"unsafe path in RomFS: {rel!r}")
    return dest


def cmd_romfs_extract(args) -> int:
    img = Image(args.image)
    p = img.partitions[args.partition]
    root = os.path.abspath(args.outdir)
    count = total = 0
    for f in p.romfs_files():
        if args.match and not any(fnmatch.fnmatch(f.path, m) for m in args.match):
            continue
        dest = _safe_join(root, f.path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if os.path.exists(dest) and os.path.getsize(dest) == f.size:
            continue
        with open(dest, "wb") as out:
            remaining, off = f.size, f.offset
            while remaining:
                chunk = img.read(off, min(remaining, 1 << 22))
                out.write(chunk)
                off += len(chunk)
                remaining -= len(chunk)
        count += 1
        total += f.size
    print(f"extracted {count} files, {total} bytes -> {root}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info"); s.add_argument("image"); s.set_defaults(fn=cmd_info)
    s = sub.add_parser("exefs"); s.add_argument("image"); s.add_argument("outdir")
    s.add_argument("--partition", type=int, default=0); s.set_defaults(fn=cmd_exefs)
    s = sub.add_parser("romfs-list"); s.add_argument("image")
    s.add_argument("--partition", type=int, default=0); s.set_defaults(fn=cmd_romfs_list)
    s = sub.add_parser("romfs-extract"); s.add_argument("image"); s.add_argument("outdir")
    s.add_argument("--partition", type=int, default=0)
    s.add_argument("--match", action="append", help="glob on the RomFS path; repeatable")
    s.set_defaults(fn=cmd_romfs_extract)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except CtrError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
