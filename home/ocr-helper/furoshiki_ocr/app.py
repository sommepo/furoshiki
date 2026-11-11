# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The helper's service: what Furoshiki asks of an OCR helper, and nothing else.

    python -m furoshiki_ocr --config config.json [--stop-with-stdin]

config.json: {"listen_port": 8794, "source": "furoshiki", "token": "…16 characters or more…",
"state_dir": "state", "engine": "meikiocr"}. It listens on 127.0.0.1 only and answers only this
computer, and only who says the token (Authorization: Bearer). Nothing is sent anywhere unless
the engine chosen is Google Lens, which sends each text area it reads to Google.

    GET  /api/health                      {"status": "ok", "role": "helper"}
    GET  /api/engines                     the engines, and which are loaded
    GET  PUT /api/settings                {"engine", "settle_ms", "min_change"}
    GET  PUT /api/source                  the one picture source there is
    GET  PUT /api/regions                 the games and their text areas (below)
    POST /api/feed/<source>               a JPEG of the game's window, four times a second
    POST /api/ocr/test {"rect"}           what stands in an area now; nothing is kept
    POST /api/ocr/now  {"ask": true}      reads the areas that are read only on request
    GET  /api/lines?since=N&wait=S        the lines read after N (waits up to S seconds for one)
    GET  /api/overlay?after=N&wait=S      the line on the screen now, with each character's place

A game's areas: {"id", "name", "rect": [x, y, w, h] (0-1 of the picture), "read"}. The game's
"active_area" is read whenever it has stopped changing; with it, as one line, every area whose
"read" is "always"; an area whose "read" is "ask" only when asked for.
"""

from __future__ import annotations

import argparse
import asyncio
import io
import json
import logging
import os
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

from aiohttp import web
from PIL import Image

from . import __version__
from .parts import cleaner, engine as engines, stabiliser

log = logging.getLogger("furoshiki_ocr")

DEFAULTS = {"engine": "meikiocr", "settle_ms": 400, "min_change": 0.004}
LIMITS = {"settle_ms": (100, 5000), "min_change": (0.0, 0.2)}
KEPT_LINES = 2000


def clean_rect(rect: Any) -> list[float]:
    x, y, w, h = (float(v) for v in rect)
    x, y = min(max(x, 0.0), 1.0), min(max(y, 0.0), 1.0)
    w, h = min(max(w, 0.0), 1.0 - x), min(max(h, 0.0), 1.0 - y)
    if w < 0.01 or h < 0.01:
        raise ValueError("the area is too small")
    return [round(x, 5), round(y, 5), round(w, 5), round(h, 5)]


def clean_games(value: dict[str, Any]) -> dict[str, Any]:
    games = []
    for game in value.get("games", []):
        areas = []
        for area in game.get("areas", []):
            item = {"id": str(area.get("id") or f"a{len(areas) + 1}")[:32], "name": str(area.get("name") or "Text")[:80],
                    "rect": clean_rect(area["rect"])}
            if area.get("read") in ("always", "ask"):
                item["read"] = area["read"]
            areas.append(item)
        ids = {area["id"] for area in areas}
        active = game.get("active_area")
        games.append({"id": str(game.get("id") or f"g{len(games) + 1}")[:32], "name": str(game.get("name") or "Game")[:120],
                      "page": "", "source": str(game.get("source") or "")[:64], "areas": areas,
                      "active_area": active if active in ids else (areas[0]["id"] if areas else None)})
    ids = [game["id"] for game in games]
    if len(set(ids)) != len(ids):
        raise ValueError("two games have the same id")
    active = value.get("active")
    return {"active": active if active in ids else (ids[0] if ids else None), "games": games}


def crop_box(rect: list[float], width: int, height: int) -> tuple[int, int, int, int]:
    x, y, w, h = rect
    return (round(x * width), round(y * height), round((x + w) * width), round((y + h) * height))


def signature_of(jpeg: bytes, rects: list[list[float]]) -> tuple[Any, int, int]:
    """The areas' look as one small picture, from a half-size decode: a change in any of them is
    a change."""
    image = Image.open(io.BytesIO(jpeg))
    full = image.size
    image.draft("L", (full[0] // 2, full[1] // 2))
    image = image.convert("L")
    parts = [stabiliser.Signature.of(image.crop(crop_box(rect, image.width, image.height))) for rect in rects]
    if len(parts) == 1:
        return parts[0], full[0], full[1]
    stacked = Image.new("L", (parts[0].image.width, sum(part.image.height for part in parts)))
    top = 0
    for part in parts:
        stacked.paste(part.image, (0, top))
        top += part.image.height
    return stabiliser.Signature.of(stacked), full[0], full[1]


def area_png(jpeg: bytes, rect: list[float]) -> bytes:
    image = Image.open(io.BytesIO(jpeg)).convert("RGB")
    out = io.BytesIO()
    image.crop(crop_box(rect, image.width, image.height)).save(out, "PNG", compress_level=1)
    return out.getvalue()


def reading_order(areas: list[dict[str, Any]]) -> list[list[float]]:
    return [area["rect"] for area in sorted(areas, key=lambda a: (a["rect"][1], a["rect"][0]))]


class Helper:
    def __init__(self, config: dict[str, Any]) -> None:
        self.source = str(config.get("source") or "furoshiki")
        self.token = str(config.get("token") or "")
        if len(self.token) < 16:
            raise ValueError("the token wants 16 characters or more")
        self.state = Path(config.get("state_dir") or "state")
        self.state.mkdir(parents=True, exist_ok=True)
        self.settings = {**DEFAULTS, **self._load("settings.json", {})}
        if config.get("engine") in engines.ENGINES and "engine" not in self._load("settings.json", {}):
            self.settings["engine"] = config["engine"]
        self.games = clean_games(self._load("games.json", {}))
        self.engines = {name: engines.Engine(name) for name in engines.ENGINES}
        self.stabiliser = stabiliser.Stabiliser(self._stabiliser_settings())
        self.frame: bytes | None = None
        self.frame_at = 0.0
        self.size: list[int] | None = None
        self.lines: deque[dict[str, Any]] = deque(maxlen=KEPT_LINES)
        self.replaced: dict[int, int] = {}  # a line's id: the id of the line that took its place
        # Above every id of an earlier run: who follows the lines asks for those after one it has.
        self.next_id = int(time.time() * 1000)
        self.on_screen: dict[str, Any] | None = None
        self.overlay_seq = 0
        self.last: dict[str, Any] = {}
        self._area_key: Any = None
        self._new_frame = asyncio.Event()
        self._line_came = asyncio.Event()
        self._overlay_changed = asyncio.Event()
        self._reading = asyncio.Lock()
        self._task: asyncio.Task | None = None

    # ---- what is kept ----

    def _load(self, name: str, default: Any) -> Any:
        try:
            return json.loads((self.state / name).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return default

    def _save(self, name: str, value: Any) -> None:
        part = self.state / (name + ".part")
        part.write_text(json.dumps(value, ensure_ascii=False, indent=1), encoding="utf-8")
        os.replace(part, self.state / name)

    def _stabiliser_settings(self) -> Any:
        return stabiliser.StabiliserSettings(settle_ms=self.settings["settle_ms"], min_change=self.settings["min_change"])

    def set_settings(self, value: dict[str, Any]) -> dict[str, Any]:
        out = dict(self.settings)
        if "engine" in value:
            if value["engine"] not in engines.ENGINES:
                raise ValueError(f"no engine is called {value['engine']}")
            out["engine"] = value["engine"]
        for key, (low, high) in LIMITS.items():
            if key in value:
                item = type(DEFAULTS[key])(value[key])
                if not low <= item <= high:
                    raise ValueError(f"{key} must be between {low} and {high}")
                out[key] = item
        self.settings = out
        self._save("settings.json", out)
        self.stabiliser.settings = self._stabiliser_settings()
        return out

    def set_games(self, value: dict[str, Any]) -> dict[str, Any]:
        self.games = clean_games(value)
        self._save("games.json", self.games)
        return self.games

    def game(self) -> dict[str, Any] | None:
        return next((g for g in self.games["games"] if g["id"] == self.games["active"]), None)

    def read_always(self) -> tuple[dict[str, Any] | None, list[list[float]]]:
        """The game played and what of it is read by itself: its active area and, with it, those
        that are always read."""
        game = self.game()
        if game is None:
            return None, []
        active = [a for a in game["areas"] if a["id"] == game["active_area"]]
        if not active:
            return game, []
        return game, reading_order(active + [a for a in game["areas"] if a.get("read") == "always" and a not in active])

    def read_asked(self, area_id: str | None) -> tuple[dict[str, Any] | None, list[list[float]]]:
        game = self.game()
        if game is None:
            return None, []
        if area_id:
            return game, [a["rect"] for a in game["areas"] if a["id"] == area_id]
        return game, reading_order([a for a in game["areas"] if a.get("read") == "ask"])

    # ---- pictures to lines ----

    def take(self, jpeg: bytes) -> None:
        self.frame = jpeg
        self.frame_at = time.monotonic()
        self._new_frame.set()

    async def read(self, jpeg: bytes, rects: list[list[float]], engine: str | None = None) -> dict[str, Any]:
        """The areas read one after the other as one line: the text, and each character's place in
        the whole picture ([x, y, w, h], 0-1)."""
        name = engine or self.settings["engine"]
        if name not in self.engines:
            raise ValueError(f"no engine is called {name}")
        loop = asyncio.get_running_loop()
        text, layout, raw, ms = "", [], [], 0
        for rect in rects:
            png = await loop.run_in_executor(None, area_png, jpeg, rect)
            result = await self.engines[name].read(png)
            lines = [cleaner.OcrLine(item["text"], *item["box"], vertical=item.get("vertical", False), chars=item.get("chars"))
                     for item in result["lines"]]
            part, boxes = cleaner.join_with_boxes(cleaner.drop_furigana(lines))
            x, y, w, h = rect
            text += part
            layout += [[round(x + left * w, 4), round(y + top * h, 4), round((right - left) * w, 4), round((bottom - top) * h, 4)]
                       for left, top, right, bottom in boxes]
            raw += [item["text"] for item in result["lines"]]
            ms += result.get("ms") or 0
        self.last = {"engine": name, "ms": ms, "text": text}
        return {"text": text, "engine": name, "ms": ms, "raw": raw, "layout": layout}

    def _show(self, line: dict[str, Any] | None) -> None:
        if (line or {}).get("id") != (self.on_screen or {}).get("id"):
            self.on_screen = line
            self.overlay_seq += 1
            self._overlay_changed.set()
            self._overlay_changed = asyncio.Event()

    async def emit(self, jpeg: bytes, game: dict[str, Any], rects: list[list[float]], asked: bool) -> dict[str, Any]:
        async with self._reading:
            try:
                read = await self.read(jpeg, rects)
            except Exception as error:  # an engine that cannot read says why
                log.warning("not read: %s", error)
                self.last = {"error": str(error)}
                return {"added": False, "reason": str(error)}
            previous = self.lines[-1] if self.lines else None
            same_game = previous is not None and previous["game"] == game["name"]
            verdict = cleaner.compare(previous["text"] if same_game else None, read["text"])
            if asked and verdict.action == "drop" and read["text"] and not cleaner.is_junk(read["text"]):
                verdict = cleaner.Verdict("new", "asked for")
            if verdict.action == "drop":
                if verdict.reason == "same as the previous line":
                    self._show(previous)  # that line is on the screen again
                return {"added": False, "reason": verdict.reason, "text": read["text"]}
            replaces = previous["id"] if verdict.action == "replace" and previous and not asked else None
            line = {"id": self.next_id, "time": int(time.time() * 1000), "game": game["name"], "text": read["text"],
                    "engine": read["engine"], "boxes": read["layout"], "source": self.source}
            self.next_id += 1
            if replaces is not None:
                self.replaced[replaces] = line["id"]
            self.lines.append(line)
            self._show(line)
            self._line_came.set()
            self._line_came = asyncio.Event()
            log.info("line %s%s: %s", line["id"], f" (in place of {replaces})" if replaces else "", line["text"])
            return {"added": True, "line": line, "replaces": replaces}

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        while True:
            await self._new_frame.wait()
            self._new_frame.clear()
            jpeg = self.frame
            game, rects = self.read_always()
            if jpeg is None or game is None or not rects:
                continue
            key = (game["id"], tuple(tuple(rect) for rect in rects))
            if key != self._area_key:
                self._area_key = key
                self.stabiliser.reset()
                self._show(None)
            try:
                signature, width, height = await loop.run_in_executor(None, signature_of, jpeg, rects)
            except (OSError, ValueError) as error:
                log.warning("a picture that cannot be read: %s", error)
                continue
            self.size = [width, height]
            decision = self.stabiliser.offer(signature, self.frame_at * 1000)
            if decision.changed:
                self._show(None)
            if decision.read:
                await self.emit(jpeg, game, rects, asked=False)

    async def now(self, ask: bool, area_id: str | None) -> dict[str, Any]:
        game, rects = self.read_asked(area_id) if ask or area_id else self.read_always()
        if game is None or not rects:
            return {"added": False, "reason": "no area is set to be read on request" if ask or area_id else "no text area is set"}
        if self.frame is None or time.monotonic() - self.frame_at > 10:
            return {"added": False, "reason": "there is no current picture of the game"}
        return await self.emit(self.frame, game, rects, asked=True)

    def lines_after(self, since: int, limit: int) -> list[dict[str, Any]]:
        return [line for line in self.lines if line["id"] > since][:limit]


# ---- the web side ----

def from_this_machine(request: web.Request) -> bool:
    peer = request.transport.get_extra_info("peername") if request.transport else None
    address = peer[0] if peer else ""
    name = request.host.rsplit(":", 1)[0] if not request.host.endswith("]") else request.host
    proxied = any(header in request.headers for header in ("X-Forwarded-For", "X-Real-IP", "Forwarded"))
    return address in ("127.0.0.1", "::1") and name in ("127.0.0.1", "localhost", "[::1]") and not proxied


def make_app(helper: Helper) -> web.Application:
    @web.middleware
    async def guard(request: web.Request, handler):
        if not from_this_machine(request):
            raise web.HTTPForbidden(text="the helper answers this computer only")
        if request.path != "/api/health":
            said = request.headers.get("Authorization", "")
            said = said[7:].strip() if said.startswith("Bearer ") else request.headers.get("X-Feed-Token", "")
            if said != helper.token:
                raise web.HTTPForbidden(text="that is not this helper's token")
        try:
            return await handler(request)
        except (KeyError, TypeError, ValueError) as error:
            return web.json_response({"error": str(error)}, status=400)

    async def body(request: web.Request) -> dict[str, Any]:
        if not request.can_read_body:
            return {}
        try:
            data = await request.json()
        except ValueError:
            raise ValueError("the body is not JSON")
        return data if isinstance(data, dict) else {}

    async def health(request: web.Request) -> web.Response:
        return web.json_response({"status": "ok", "role": "helper", "name": "furoshiki-ocr", "version": __version__})

    async def list_engines(request: web.Request) -> web.Response:
        return web.json_response([{"name": name, **about, "loaded": helper.engines[name].instance is not None,
                                   "error": helper.engines[name].error, "chosen": name == helper.settings["engine"]}
                                  for name, about in engines.ENGINES.items()])

    async def get_settings(request: web.Request) -> web.Response:
        return web.json_response(helper.settings)

    async def put_settings(request: web.Request) -> web.Response:
        return web.json_response(helper.set_settings(await body(request)))

    def source() -> dict[str, Any]:
        return {"source": helper.source, "sources": [{"id": helper.source, "name": "Furoshiki: a window", "push": True}]}

    async def get_source(request: web.Request) -> web.Response:
        return web.json_response(source())

    async def put_source(request: web.Request) -> web.Response:
        if (await body(request)).get("source") != helper.source:
            raise ValueError(f"this helper has one picture source, {helper.source}")
        return web.json_response(source())

    async def get_regions(request: web.Request) -> web.Response:
        return web.json_response(helper.games)

    async def put_regions(request: web.Request) -> web.Response:
        return web.json_response(helper.set_games(await body(request)))

    async def feed(request: web.Request) -> web.Response:
        if request.match_info["source"] != helper.source:
            raise web.HTTPNotFound(text="no such picture source")
        jpeg = await request.read()
        if not jpeg:
            raise ValueError("no picture")
        helper.take(jpeg)
        return web.json_response({"taken": True, "in_use": True})

    async def test(request: web.Request) -> web.Response:
        data = await body(request)
        if helper.frame is None:
            return web.json_response({"error": "there is no picture of the game yet"}, status=503)
        try:
            return web.json_response(await helper.read(helper.frame, [clean_rect(data["rect"])], data.get("engine")))
        except RuntimeError as error:
            return web.json_response({"error": str(error)}, status=502)

    async def now(request: web.Request) -> web.Response:
        data = await body(request)
        return web.json_response(await helper.now(bool(data.get("ask")), str(data.get("area") or "") or None))

    async def lines(request: web.Request) -> web.Response:
        since = int(request.query.get("since", "0"))
        limit = min(max(int(request.query.get("limit", "200")), 1), 1000)
        wait = min(max(float(request.query.get("wait", "0")), 0.0), 25.0)
        came = helper._line_came
        found = helper.lines_after(since, limit)
        if not found and wait and since > 0:
            try:
                await asyncio.wait_for(came.wait(), wait)
            except asyncio.TimeoutError:
                pass
            found = helper.lines_after(since, limit)
        return web.json_response({"lines": found, "upto": max([since] + [line["id"] for line in found]),
                                  "replaced": [old for old, new in helper.replaced.items() if old <= since < new] if since > 0 else []})

    async def overlay(request: web.Request) -> web.Response:
        after = request.query.get("after")
        wait = min(max(float(request.query.get("wait", "0")), 0.0), 25.0)
        if after is not None and wait and int(after) == helper.overlay_seq:
            try:
                await asyncio.wait_for(helper._overlay_changed.wait(), wait)
            except asyncio.TimeoutError:
                pass
        _, rects = helper.read_always()
        return web.json_response({"seq": helper.overlay_seq, "source": helper.source, "line": helper.on_screen,
                                  "area": rects[0] if rects else None, "size": helper.size})

    async def start(app: web.Application) -> None:
        helper._task = asyncio.create_task(helper.run(), name="read")
        # The engine chosen is loaded now: the first line does not wait for it.
        loading = asyncio.ensure_future(helper.engines[helper.settings["engine"]].ready())
        loading.add_done_callback(lambda done: done.exception() and log.warning("%s did not load: %s", helper.settings["engine"], done.exception()))

    async def stop(app: web.Application) -> None:
        if helper._task is not None:
            helper._task.cancel()

    app = web.Application(client_max_size=32 * 1024 * 1024, middlewares=[guard])
    r = app.router
    r.add_get("/api/health", health)
    r.add_get("/api/engines", list_engines)
    r.add_get("/api/settings", get_settings)
    r.add_put("/api/settings", put_settings)
    r.add_get("/api/source", get_source)
    r.add_put("/api/source", put_source)
    r.add_get("/api/regions", get_regions)
    r.add_put("/api/regions", put_regions)
    r.add_post("/api/feed/{source}", feed)
    r.add_post("/api/ocr/test", test)
    r.add_post("/api/ocr/now", now)
    r.add_get("/api/lines", lines)
    r.add_get("/api/overlay", overlay)
    app.on_startup.append(start)
    app.on_cleanup.append(stop)
    return app


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="furoshiki_ocr")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--stop-with-stdin", action="store_true",
                        help="end when the standard input does: the program that started this one is gone")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    os.environ.setdefault("LOGURU_COLORIZE", "False")
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    if not Path(config.get("state_dir") or "state").is_absolute():
        config["state_dir"] = str(args.config.resolve().parent / (config.get("state_dir") or "state"))
    port = int(config.get("listen_port") or 8794)

    if args.stop_with_stdin:
        def watch() -> None:
            try:
                while sys.stdin.buffer.read(1):
                    pass
            finally:
                os._exit(0)

        threading.Thread(target=watch, name="stdin", daemon=True).start()

    async def build() -> web.Application:
        return make_app(Helper(config))

    log.info("Furoshiki OCR helper %s on 127.0.0.1:%s", __version__, port)
    web.run_app(build(), host="127.0.0.1", port=port, print=None, access_log=None)


if __name__ == "__main__":
    main()
