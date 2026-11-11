#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A throwaway WebSocket server that sends lines as a texthooker does, for tools/test-lines.sh.

    tools/lines_server.py PORT        (127.0.0.1 only; ends after one client has had its lines)

Plain text frames and JSON with "sentence", a line sent piece by piece as a game writes it,
the same line twice, and a line with nothing Japanese in it. The standard library only.
"""
import base64
import hashlib
import json
import socket
import struct
import sys
import time

# (seconds to wait before it, the frame's text)
LINES = [
    (0.6, "山道は細く、"),
    (0.03, "山道は細く、曲がり"),
    (0.03, "山道は細く、曲がりくねっている。"),
    (0.6, json.dumps({"sentence": "助手席の奈美は、まだ眠っていた。", "process_path": "C:/game/made-up.exe"}, ensure_ascii=False)),
    (0.4, "助手席の奈美は、まだ眠っていた。"),
    (0.4, "Now Loading..."),
    (0.4, json.dumps({"text": "雨が降り始めた。\nワイパーを動かす。"}, ensure_ascii=False)),
]


def frame(text: str) -> bytes:
    payload = text.encode("utf-8")
    head = bytes([0x81])
    if len(payload) < 126:
        head += bytes([len(payload)])
    else:
        head += bytes([126]) + struct.pack(">H", len(payload))
    return head + payload


def main() -> int:
    port = int(sys.argv[1])
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", port))
        server.listen(1)
        server.settimeout(60)
        print(f"lines server on ws://127.0.0.1:{port}", flush=True)
        client, _ = server.accept()
        with client:
            request = b""
            while b"\r\n\r\n" not in request:
                request += client.recv(4096)
            key = next(line.split(b":", 1)[1].strip() for line in request.split(b"\r\n")
                       if line.lower().startswith(b"sec-websocket-key"))
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            client.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            for wait, text in LINES:
                time.sleep(wait)
                client.sendall(frame(text))
            print(f"sent {len(LINES)} frames", flush=True)
            # Until the client goes (or half a minute): a texthooker stays.
            client.settimeout(30)
            try:
                while client.recv(4096):
                    pass
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
