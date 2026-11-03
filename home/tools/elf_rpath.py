#!/usr/bin/env python3
"""Sets the library search path (DT_RPATH or DT_RUNPATH) of a 64-bit ELF file in place, for
tools/package-linux.sh: the builds name this machine's folders there, which must not be looked
in where the package runs. There is no patchelf here, so the new path is written over the old
string, which it must not be longer than; a file without one is left as it is.
  elf_rpath.py PATH FILE...      e.g. elf_rpath.py '$ORIGIN/lib' reader-home.bin
  elf_rpath.py --show FILE...    prints each file's path
"""
import struct
import sys

DT_NULL, DT_STRTAB, DT_STRSZ, DT_RPATH, DT_RUNPATH = 0, 5, 10, 15, 29
PT_LOAD, PT_DYNAMIC = 1, 2


def search_path(data):
    """(offset of the path's string in the file, its length, its tag) or None."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    loads, dynamic = [], None
    for i in range(phnum):
        p_type, _, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize)
        if p_type == PT_LOAD:
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == PT_DYNAMIC:
            dynamic = (p_offset, p_filesz)
    if dynamic is None:
        return None
    strtab, path = None, None
    for at in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, value = struct.unpack_from("<qQ", data, at)
        if tag == DT_NULL:
            break
        if tag == DT_STRTAB:
            strtab = value
        elif tag in (DT_RPATH, DT_RUNPATH):
            path = (value, tag)
    if path is None or strtab is None:
        return None
    for vaddr, offset, size in loads:
        if vaddr <= strtab < vaddr + size:
            start = strtab - vaddr + offset + path[0]
            return start, data.index(b"\0", start) - start, path[1]
    raise ValueError("string table outside the loaded segments")


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 64
    show = argv[1] == "--show"
    new = None if show else argv[1].encode()
    status = 0
    for name in argv[2:]:
        with open(name, "rb") as f:
            data = bytearray(f.read())
        try:
            found = search_path(data)
        except ValueError as error:
            print(f"elf_rpath: {name}: {error}", file=sys.stderr)
            status = 1
            continue
        if found is None:
            if show:
                print(f"{name}: (none)")
            continue
        start, length, tag = found
        if show:
            kind = "RPATH" if tag == DT_RPATH else "RUNPATH"
            print(f"{name}: {kind} {data[start:start + length].decode(errors='replace')}")
            continue
        if len(new) > length:
            print(f"elf_rpath: {name}: '{argv[1]}' is longer than its path ({length} bytes)", file=sys.stderr)
            status = 1
            continue
        data[start:start + length] = new + b"\0" * (length - len(new))
        with open(name, "r+b") as f:
            f.write(data)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
