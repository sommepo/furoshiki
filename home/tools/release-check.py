#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Whether a Windows package or an Android APK of Furoshiki may be published.

    tools/release-check.py ZIP-OR-APK
    tools/release-check.py --folder FOLDER

Refuses (exit 1, each finding on a line) a package that has
- Eden's core (reader_core_eden*) or the driver hooks it loads Turnip with (adrenotools'):
  the maintainer's alone, never published;
- the text server (its WebSocket handshake), the dev line's;
- a game script (.lua) or a character table (.charset), as files or built into the program
  (a Qt resource's name in reader-home.exe): a published build installs them from the
  published lists;
- the maintainer's recorded menu sounds built in (sounds/, private);
- in any program or library: a save sync service's address (a private network's address with
  the service's port, a Tailscale name), the maintainer's address or devices' serials; and, in
  a core, save sync of its own (the words "save sync" and the like, as the reader forks' public
  builds were checked with strings); in the program, the pairing link (a published build has
  no save sync: HOME_PUBLIC; the names of its settings stay, in the shared settings);
- no licence, notices or source offer (COPYING.txt, NOTICES.txt, SOURCE.txt).
In the private repository the markers of ../../publish.json are looked for as well. Paths
of the machine the build was made on (where a library's asserts keep their source file's
name) are counted and said, not refused: the forks' published packages have them too.
"""
import json
import pathlib
import re
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
POLICY = HERE.parents[1] / "publish.json"

# A save sync service's address: a private or Tailscale network's address with its port
# (8642), or any *.ts.net name. Examples of the code and its tests (192.168.1.5, .50,
# example.ts.net, your-tailnet) are not addresses of anyone's.
ADDRESS = re.compile(rb"(?:\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7]))"
                     rb"\.\d{1,3}\.\d{1,3}:8642\b|\b[A-Za-z0-9-]+\.[A-Za-z0-9-]+\.ts\.net\b)")
EXAMPLES = re.compile(rb"192\.168\.1\.50?:|example|your-tailnet", re.I)
SAVE_SYNC = re.compile(rb"save[ _-]?sync", re.I)
# What only the private line's program has: the pairing link.
PAIRING = re.compile(rb"readersync:", re.I)
BUILD_PATH = re.compile(rb"(?:/srv/[a-z][a-z0-9_-]*/|/home/[a-z][a-z0-9_-]*/)")
PRIVATE_SOUNDS = {b"move.wav", b"select.wav", b"back.wav"}
NEEDED = ("COPYING.txt", "NOTICES.txt", "SOURCE.txt")
# The text server answers a WebSocket handshake with this key's hash (RFC 6455): nothing else
# of the program's or the app's own does.
WEBSOCKET = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
# What only Eden's core loads on Android (its driver, through adrenotools).
EDEN_HOOKS = ("libhook_impl.so", "libmain_hook.so", "libfile_redirect_hook.so", "libgsl_alloc_hook.so")


def markers():
    """publish.json's markers (pattern, what), where this is the private repository."""
    if not POLICY.is_file():
        return []
    rules = json.loads(POLICY.read_text(encoding="utf-8"))
    allowed = [re.compile(re.escape(word).encode(), re.I) for word in rules.get("allowed", [])]
    found = []
    for marker in rules["markers"]:
        # The machine's paths are build paths in a binary (counted below), not findings.
        if marker["what"] == "a path of the maintainer's machine":
            continue
        found.append((re.compile(marker["pattern"].encode()), marker["what"], allowed))
    return found


def runs(data: bytes):
    """The text in a binary as strings(1) finds it: ASCII, UTF-16LE and UTF-16BE runs."""
    for match in re.finditer(rb"[\x20-\x7e]{5,}", data):
        yield match.group()
    for encoding, pattern in (("utf-16-le", rb"(?:[\x20-\x7e]\x00){5,}"), ("utf-16-be", rb"(?:\x00[\x20-\x7e]){5,}")):
        for match in re.finditer(pattern, data):
            yield match.group().decode(encoding).encode()


def check_binary(name: str, data: bytes, personal) -> tuple[list, int]:
    problems, paths = [], 0
    core = "reader_core_" in name and (re.search(r"(^|/)cores/[^/]+\.dll$", name) is not None or name.endswith(".so"))
    # The program: Windows' executable, and on Android the app's native library and its code.
    program = name.endswith(("Furoshiki.exe", "reader-home.exe", "libfuroshiki.so")) or re.search(r"(^|/)classes\d*\.dex$", name) is not None
    sync_words = 0
    for text in runs(data):
        if ADDRESS.search(text) and not EXAMPLES.search(text):
            problems.append(f"{name}: a save sync address: {text[:100].decode(errors='replace')}")
        for pattern, what, allowed in personal:
            if pattern.search(text) and not any(word.search(text) for word in allowed):
                problems.append(f"{name}: {what}: {text[:100].decode(errors='replace')}")
        if BUILD_PATH.search(text):
            paths += 1
        if SAVE_SYNC.search(text):
            sync_words += 1
        if program and WEBSOCKET in text:
            problems.append(f"{name}: the text server built in (its WebSocket handshake)")
        # A script's name in the app's code is an example of one (Console.kt); a resource's
        # name in a native program is a file built in.
        if program:
            if not name.endswith(".dex") and re.fullmatch(rb"[A-Za-z0-9_.-]+\.(lua|charset)", text):
                problems.append(f"{name}: a game script or character table built in: {text.decode()}")
            if text in PRIVATE_SOUNDS:
                problems.append(f"{name}: the maintainer's recorded sound built in: {text.decode()}")
            if PAIRING.search(text):
                problems.append(f"{name}: save sync's pairing built in: {text[:100].decode(errors='replace')}")
    if core and sync_words:
        problems.append(f"{name}: save sync in a core ({sync_words} strings): the cores carry none of their own")
    if program and sync_words:
        print(f"release-check: {name}: {sync_words} save sync strings (the shared settings' names, no address in it)")
    return problems, paths


def check(members) -> int:
    """MEMBERS: (name, read()) of the package's files, names relative to its folder."""
    personal = markers()
    problems, paths, names = [], 0, []
    for name, read in members:
        names.append(name)
        base = name.rsplit("/", 1)[-1]
        if "reader_core_eden" in base.lower():
            problems.append(f"{name}: Eden's core")
        if base in EDEN_HOOKS:
            problems.append(f"{name}: the driver hooks of Eden's core")
        if base.lower().endswith((".lua", ".charset")):
            problems.append(f"{name}: a game script or character table")
        if base.lower().endswith((".exe", ".dll", ".so", ".dex")):
            found, count = check_binary(name, read(), personal)
            problems += found
            paths += count
    for needed in NEEDED:
        if not any(n == needed or n.endswith("/" + needed) for n in names):
            problems.append(f"no {needed} in the package")
    for problem in dict.fromkeys(problems):
        print(problem)
    if paths:
        print(f"release-check: {paths} paths of the build machine in the binaries (source file names of asserts)")
    print(f"release-check: {len(names)} files, {len(dict.fromkeys(problems))} problem(s)", file=sys.stderr)
    return 1 if problems else 0


def main(argv) -> int:
    if len(argv) == 2 and argv[0] == "--folder":
        root = pathlib.Path(argv[1])
        files = sorted(p for p in root.rglob("*") if p.is_file())
        return check((p.relative_to(root).as_posix(), p.read_bytes) for p in files)
    if len(argv) == 1:
        with zipfile.ZipFile(argv[0]) as archive:
            infos = [info for info in archive.infolist() if not info.is_dir()]
            return check((info.filename, (lambda info=info: archive.read(info))) for info in infos)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
