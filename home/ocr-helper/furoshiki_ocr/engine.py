"""The OCR sidecar: owocr's engines in a process of their own, behind a Unix socket (or, where
there are none, a loopback TCP port: --listen 127.0.0.1:PORT).

    POST /ocr?engine=meikiocr   (body: a PNG or JPEG)  ->  {"engine", "ms", "lines": [...]}
    GET  /engines                                     ->  which engines there are and are loaded

Only owocr's engine classes (owocr.ocr) are used: owocr's command-line program wants a desktop
(tray icon, clipboard, keyboard hooks, PyGObject) and always listens on every interface, which
a server should not. Each engine is loaded on first use and then kept; its calls run one at a
time on a thread of its own, so the event loop stays free.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

from aiohttp import web

log = logging.getLogger("console_ocr.engine")

# Engines offered, by owocr's names. Google Lens sends the picture to Google; the others read
# on this machine. OneOCR is Windows' own (the Snipping Tool's engine, Windows 11, or its three
# files put in place by hand); Screen AI is Chrome's, whose library owocr fetches at first use.
ENGINES = {
    "meikiocr": {"label": "meikiocr (on this machine)", "local": True},
    "oneocr": {"label": "OneOCR (Windows' own, on this machine)", "local": True},
    "screenai": {"label": "Chrome Screen AI (on this machine)", "local": True},
    "glens": {"label": "Google Lens (sends the text area to Google)", "local": False},
}


def engine_class(name: str) -> Any:
    from owocr import ocr  # heavy: numpy, onnxruntime

    return {"meikiocr": ocr.MeikiOCR, "oneocr": ocr.OneOCR, "screenai": ocr.ChromeScreenAI,
            "glens": ocr.GoogleLens}[name]


# Why an engine that did not load most likely did not, in words for the person choosing it.
NOT_AVAILABLE = {
    "oneocr": "OneOCR is not available: it needs Windows 11's Snipping Tool (or oneocr.dll, "
              "oneocr.onemodel and onnxruntime.dll in .config/oneocr) and the oneocr package",
    "screenai": "Chrome Screen AI is not available: its library could not be fetched or loaded",
    "glens": "Google Lens is not available (missing dependencies?)",
}


class Engine:
    def __init__(self, name: str) -> None:
        self.name = name
        self.instance: Any = None
        self.error: str | None = None
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix=f"ocr-{name}")
        self.loading: asyncio.Future | None = None

    def _load(self) -> Any:
        instance = engine_class(self.name)()
        if not instance.available:
            raise RuntimeError(NOT_AVAILABLE.get(self.name, f"{self.name} is not available (missing dependencies?)"))
        return instance

    async def ready(self) -> Any:
        if self.instance is not None:
            return self.instance
        loop = asyncio.get_running_loop()
        if self.loading is None:
            self.loading = loop.run_in_executor(self.executor, self._load)
        try:
            self.instance = await self.loading
            self.error = None
        except Exception as error:  # an engine that cannot load says why, and may be retried
            self.error = str(error)
            self.loading = None
            raise
        return self.instance

    async def read(self, image: bytes) -> dict[str, Any]:
        instance = await self.ready()
        loop = asyncio.get_running_loop()
        started = time.monotonic()
        ok, result = await loop.run_in_executor(self.executor, instance, image)
        ms = round((time.monotonic() - started) * 1000)
        if not ok:
            raise RuntimeError(str(result))
        return {"engine": self.name, "ms": ms, **to_lines(result)}


def _char_boxes(words: list, vertical: bool, separators: bool = True) -> list[list]:
    """Each character of the line's words with its box ([char, left, top, right, bottom], 0-1
    of the image). A word of several characters (Google Lens) is shared out evenly along its
    box; the space between two words gets an empty box at the end of the first."""
    out: list[list] = []
    for index, word in enumerate(words):
        box = word.bounding_box
        left, top, right, bottom = box.left, box.top, box.right, box.bottom
        chars = list(word.text)
        for n, char in enumerate(chars):
            if vertical:
                step = (bottom - top) / len(chars)
                out.append([char, left, top + n * step, right, top + (n + 1) * step])
            else:
                step = (right - left) / len(chars)
                out.append([char, left + n * step, top, left + (n + 1) * step, bottom])
        if separators and index < len(words) - 1:
            separator = word.separator if word.separator is not None else " "
            for char in separator:
                end = (left, bottom, right, bottom) if vertical else (right, top, right, bottom)
                out.append([char, *end])
    return out


def to_lines(result: Any) -> dict[str, Any]:
    """owocr's OcrResult as plain lines with boxes in 0-1 coordinates of the image, and each
    character's box when the engine gives word boxes (meikiocr: one per character)."""
    lines = []
    for paragraph in result.paragraphs:
        vertical = paragraph.writing_direction == "TOP_TO_BOTTOM"
        for line in paragraph.lines:
            line_vertical = vertical or line.writing_direction == "TOP_TO_BOTTOM"
            chars = _char_boxes(line.words, line_vertical) if line.words else []
            if line.text is not None:
                text = line.text
                # meikiocr: one word per character and no separators; the line's text is theirs.
                joined = _char_boxes(line.words, line_vertical, separators=False) if line.words else []
                if "".join(char for char, *_ in joined) == text:
                    chars = joined
            else:
                text = "".join(char for char, *_ in chars)
            box = line.bounding_box
            item = {
                "text": text,
                "box": [box.left, box.top, box.right, box.bottom],
                "vertical": line_vertical,
            }
            # Only when the characters are the line's text, one for one.
            if chars and "".join(char for char, *_ in chars) == text:
                item["chars"] = [[char, *(round(v, 5) for v in rest)] for char, *rest in chars]
            lines.append(item)
    return {"width": result.image_properties.width, "height": result.image_properties.height,
            "lines": lines}


def make_app(preload: list[str]) -> web.Application:
    engines = {name: Engine(name) for name in ENGINES}
    app = web.Application(client_max_size=32 * 1024 * 1024)

    async def ocr(request: web.Request) -> web.Response:
        name = request.query.get("engine", "meikiocr")
        engine = engines.get(name)
        if engine is None:
            return web.json_response({"error": f"unknown engine {name}"}, status=400)
        image = await request.read()
        if not image:
            return web.json_response({"error": "no picture"}, status=400)
        try:
            return web.json_response(await engine.read(image))
        except Exception as error:
            log.warning("%s failed: %s", name, error)
            return web.json_response({"error": str(error)}, status=502)

    async def list_engines(request: web.Request) -> web.Response:
        return web.json_response([
            {"name": name, **info, "loaded": engines[name].instance is not None,
             "error": engines[name].error}
            for name, info in ENGINES.items()
        ])

    async def warm(app: web.Application) -> None:
        for name in preload:
            if name in engines:
                asyncio.ensure_future(engines[name].ready()).add_done_callback(
                    lambda f, name=name: f.exception() and log.warning("%s did not load: %s", name, f.exception()))

    app.router.add_post("/ocr", ocr)
    app.router.add_get("/engines", list_engines)
    app.on_startup.append(warm)
    return app


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="console_ocr engine")
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--socket", type=Path)
    where.add_argument("--listen", metavar="127.0.0.1:PORT",
                       help="a loopback TCP port in place of the Unix socket")
    parser.add_argument("--preload", default="meikiocr",
                        help="engines to load at start, comma-separated")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    # owocr logs through loguru; keep its INFO lines but no colours in the journal.
    os.environ.setdefault("LOGURU_COLORIZE", "False")
    preload = [name for name in args.preload.split(",") if name]
    if args.listen:
        from .config import engine_host_port

        try:
            host, port = engine_host_port(args.listen)
        except ValueError as error:
            parser.error(str(error))
        web.run_app(make_app(preload), host=host, port=port, print=None, access_log=None)
        return
    args.socket.parent.mkdir(parents=True, exist_ok=True)
    args.socket.unlink(missing_ok=True)
    web.run_app(make_app(preload), path=str(args.socket), print=None)
