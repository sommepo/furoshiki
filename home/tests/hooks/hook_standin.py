#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A stand-in for the Windows hook engines, for tests and pictures (docs/hooks.md).

    hook_standin.py textractor            as TextractorCLI.exe: commands in and sentences out
                                          in UTF-16LE, "[handle:pid:addr:ctx:ctx2:name:code] text"
    hook_standin.py luna PID 32|64 [...]  as Furoshiki's LunaHook host: lines of UTF-8 JSON

What it writes is made up from the two programs' documented forms (Textractor's host/CLI/
main.cpp; hook-helper/luna_host.cpp). It is no recording of either: neither was ever run here.
The "game" has three threads: one that says each character twice as it is drawn, the dialogue,
and the window's title.
"""
import json
import sys
import threading
import time

STORY = [
    "怪盗は屋根の上で、月を見ていた。",
    "「今夜の獲物は、あの時計塔だ」",
    "風が強い。\nマントが音を立てる。",
    "警部はまだ、何も知らない。",
]
THREADS = [
    # name, hook code, addr, ctx, ctx2
    ("GetGlyphOutlineA", "HB4@0:gdi32.dll:GetGlyphOutlineA", 0x7FFB1A2C0010, 0x4012A0, 0),
    ("KiriKiri", "HQN-C*0@1B2F40:kaitou.exe", 0x7FF61B2F40, 0x7FF6001B2A, 0),
    ("lstrlenW", "HQ8@0:kernel32.dll:lstrlenW", 0x7FFB1B4410, 0x4310C4, 0x4310C4),
]


def doubled(text):
    return "".join(c + c for c in text.replace("\n", ""))


def sentences(pid):
    """(thread index, text) for ever, as a game being read goes on."""
    yield 2, "怪盗の夜 ver1.02"
    while True:
        for line in STORY:
            yield 0, doubled(line)
            yield 1, line
            time.sleep(0.45)
        time.sleep(1.0)


def textractor():
    out = sys.stdout.buffer
    lock = threading.Lock()

    def say(handle, pid, addr, ctx, ctx2, name, code, text):
        line = "[%X:%X:%X:%X:%X:%s:%s] %s\n" % (handle, pid, addr, ctx, ctx2, name, code, text)
        with lock:
            out.write(line.encode("utf-16-le"))
            out.flush()

    def console(text):
        say(0, 0, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, "Console", "HB0@0", text)

    def game(pid):
        console("Textractor: pipe connected")
        for index, text in sentences(pid):
            name, code, addr, ctx, ctx2 = THREADS[index]
            say(index + 2, pid, addr, ctx, ctx2, name, code, text)

    out.write("Usage: {'attach'|'detach'|hookcode} -Pprocessid\n".encode("utf-16-le"))
    out.flush()
    pending = b""
    while True:
        chunk = sys.stdin.buffer.read1(4096) if hasattr(sys.stdin.buffer, "read1") else sys.stdin.buffer.read(2)
        if not chunk:
            return 0
        pending += chunk
        text = pending[: len(pending) - len(pending) % 2].decode("utf-16-le")
        while "\n" in text:
            line, text = text.split("\n", 1)
            words = line.split(" -P")
            # The real CLI ends at a line it cannot read.
            if len(words) != 2 or not words[1].strip().isdigit():
                return 0
            pid = int(words[1])
            if words[0] == "attach":
                threading.Thread(target=game, args=(pid,), daemon=True).start()
            elif words[0] == "detach":
                return 0
            else:
                console("hook inserted: " + words[0])
        pending = text.encode("utf-16-le") + pending[len(pending) - len(pending) % 2 :]


def luna(pid):
    def write(what):
        sys.stdout.write(json.dumps(what, ensure_ascii=False) + "\n")
        sys.stdout.flush()

    def fields(index):
        name, code, addr, ctx, ctx2 = THREADS[index]
        return {"name": name, "code": code, "pid": pid, "addr": "%X" % addr, "ctx": "%X" % ctx, "ctx2": "%X" % ctx2}

    def game():
        write({"attached": pid})
        write({"said": "engine KiriKiri"})
        for index in range(len(THREADS)):
            write({"thread": "new", **fields(index)})
        for index, text in sentences(pid):
            write({"text": text, **fields(index)})

    threading.Thread(target=game, daemon=True).start()
    sys.stdin.buffer.read()  # until Furoshiki lets go
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "textractor":
        sys.exit(textractor())
    if len(sys.argv) >= 3 and sys.argv[1] == "luna":
        sys.exit(luna(int(sys.argv[2])))
    sys.exit(2)
