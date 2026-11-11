# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The helper end to end over its own web side, with an engine that says what it is told to.

    python -m unittest discover -s tests       (from this folder; needs aiohttp and Pillow)
"""

from __future__ import annotations

import asyncio
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from aiohttp.test_utils import TestClient, TestServer  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

from furoshiki_ocr import app as helper_app  # noqa: E402

TOKEN = "a-token-of-sixteen-or-more"
AUTH = {"Authorization": "Bearer " + TOKEN}


def picture(marks: int) -> bytes:
    """A dark picture with `marks` light blocks in its lower part: another number, another look."""
    image = Image.new("RGB", (640, 360), (10, 10, 30))
    draw = ImageDraw.Draw(image)
    for i in range(marks):
        draw.rectangle((80 + i * 60, 270, 120 + i * 60, 310), fill=(240, 240, 240))
    out = io.BytesIO()
    image.save(out, "JPEG", quality=90)
    return out.getvalue()


class Told:
    """An engine in the engines' place: each area read is the next text it was told."""

    def __init__(self) -> None:
        self.texts: list[str] = []
        self.read_areas = 0
        self.instance = self
        self.error = None

    async def ready(self):
        return self

    async def read(self, image: bytes) -> dict:
        self.read_areas += 1
        text = self.texts.pop(0) if self.texts else ""
        step = 1 / max(1, len(text))
        return {"engine": "meikiocr", "ms": 3, "width": 10, "height": 10,
                "lines": [{"text": text, "box": [0.0, 0.2, 1.0, 0.8], "vertical": False,
                           "chars": [[char, n * step, 0.2, (n + 1) * step, 0.8] for n, char in enumerate(text)]}] if text else []}


GAMES = {"active": "g", "games": [{"id": "g", "name": "A game", "active_area": "text", "areas": [
    {"id": "text", "name": "Text", "rect": [0.05, 0.7, 0.9, 0.25]},
    {"id": "menu", "name": "Menu", "rect": [0.6, 0.05, 0.35, 0.4], "read": "ask"},
]}]}


class HelperTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.helper = helper_app.Helper({"token": TOKEN, "state_dir": self.folder.name})
        self.told = Told()
        self.helper.engines = {name: self.told for name in self.helper.engines}
        self.helper.set_settings({"settle_ms": 100})
        self.client = TestClient(TestServer(helper_app.make_app(self.helper), host="127.0.0.1"))
        await self.client.start_server()

    async def asyncTearDown(self) -> None:
        await self.client.close()
        self.folder.cleanup()

    async def show(self, marks: int, seconds: float = 0.5) -> None:
        """The game shows that picture for a while, sent as Furoshiki sends it."""
        for _ in range(max(2, int(seconds / 0.05))):
            answer = await self.client.post("/api/feed/furoshiki", data=picture(marks), headers=AUTH)
            self.assertEqual((await answer.json())["taken"], True)
            await asyncio.sleep(0.05)

    async def test_who_is_answered(self) -> None:
        self.assertEqual((await (await self.client.get("/api/health")).json())["status"], "ok")
        self.assertEqual((await self.client.get("/api/regions")).status, 403)
        self.assertEqual((await self.client.get("/api/regions", headers={"Authorization": "Bearer another"})).status, 403)
        self.assertEqual((await self.client.get("/api/regions", headers={**AUTH, "X-Forwarded-For": "10.0.0.2"})).status, 403)
        self.assertEqual((await self.client.get("/api/regions", headers=AUTH)).status, 200)
        self.assertEqual((await self.client.post("/api/feed/another", data=b"x", headers=AUTH)).status, 404)

    async def test_lines_from_pictures(self) -> None:
        await self.client.put("/api/regions", json=GAMES, headers=AUTH)
        first = await (await self.client.get("/api/lines?since=0&limit=1&source=furoshiki", headers=AUTH)).json()
        self.assertEqual(first["lines"], [])
        # A line comes once the area has stood still; the same picture again is not read again.
        self.told.texts = ["おはよう、今日はいい天気だね。"]
        await self.show(3)
        await self.show(3)
        self.assertEqual(self.told.read_areas, 1)
        answer = await (await self.client.get("/api/lines?since=1&wait=2&source=furoshiki", headers=AUTH)).json()
        self.assertEqual([line["text"] for line in answer["lines"]], ["おはよう、今日はいい天気だね。"])
        line = answer["lines"][0]
        self.assertEqual(answer["upto"], line["id"])
        self.assertEqual(len(line["boxes"]), len(line["text"]))
        self.assertTrue(all(0.05 <= box[0] <= 0.95 and 0.7 <= box[1] <= 0.95 for box in line["boxes"]))
        overlay = await (await self.client.get("/api/overlay?source=furoshiki", headers=AUTH)).json()
        self.assertEqual(overlay["line"]["id"], line["id"])
        self.assertEqual(overlay["size"], [640, 360])
        # Another look, another line; nothing waits where a line already came.
        self.told.texts = ["この先の森には近づかないほうがいい。"]
        await self.show(5)
        answer = await (await self.client.get(f"/api/lines?since={line['id']}&wait=2&source=furoshiki", headers=AUTH)).json()
        self.assertEqual([each["text"] for each in answer["lines"]], ["この先の森には近づかないほうがいい。"])
        self.assertEqual(answer["replaced"], [])

    async def test_a_line_that_was_still_being_written(self) -> None:
        await self.client.put("/api/regions", json=GAMES, headers=AUTH)
        self.told.texts = ["勇者よ、よくぞ"]
        await self.show(2)
        self.told.texts = ["勇者よ、よくぞ来てくれた！"]
        await self.show(4)
        ids = [line["id"] for line in self.helper.lines]
        self.assertEqual(len(ids), 2)
        answer = await (await self.client.get(f"/api/lines?since={ids[0]}&source=furoshiki", headers=AUTH)).json()
        self.assertEqual(answer["replaced"], [ids[0]])
        self.assertEqual(answer["lines"][0]["text"], "勇者よ、よくぞ来てくれた！")

    async def test_an_area_read_only_on_request(self) -> None:
        nothing = await (await self.client.post("/api/ocr/now", json={"ask": True}, headers=AUTH)).json()
        self.assertFalse(nothing["added"])
        await self.client.put("/api/regions", json=GAMES, headers=AUTH)
        self.told.texts = ["はじめから"]
        await self.show(1)
        self.assertEqual([line["text"] for line in self.helper.lines], ["はじめから"])
        # The menu's area was not read with it; asked for, it is, and takes no line's place.
        self.told.texts = ["つづきから"]
        said = await (await self.client.post("/api/ocr/now", json={"ask": True}, headers=AUTH)).json()
        self.assertTrue(said["added"])
        self.assertIsNone(said["replaces"])
        self.assertTrue(all(box[0] >= 0.6 and box[1] <= 0.45 for box in said["line"]["boxes"]))
        self.assertEqual([line["text"] for line in self.helper.lines], ["はじめから", "つづきから"])

    async def test_two_areas_as_one_line(self) -> None:
        games = {"active": "g", "games": [{"id": "g", "name": "A game", "active_area": "text", "areas": [
            {"id": "text", "name": "Text", "rect": [0.05, 0.7, 0.9, 0.25]},
            {"id": "name", "name": "Name", "rect": [0.05, 0.55, 0.3, 0.1], "read": "always"}]}]}
        await self.client.put("/api/regions", json=games, headers=AUTH)
        self.told.texts = ["村人", "おはよう。"]  # the name's area is above: read first
        await self.show(3)
        self.assertEqual([line["text"] for line in self.helper.lines], ["村人おはよう。"])

    async def test_settings_and_what_is_kept(self) -> None:
        self.assertEqual((await self.client.put("/api/settings", json={"engine": "nothing"}, headers=AUTH)).status, 400)
        chosen = await (await self.client.put("/api/settings", json={"engine": "oneocr"}, headers=AUTH)).json()
        self.assertEqual(chosen["engine"], "oneocr")
        await self.client.put("/api/regions", json=GAMES, headers=AUTH)
        again = helper_app.Helper({"token": TOKEN, "state_dir": self.folder.name})
        self.assertEqual(again.settings["engine"], "oneocr")
        self.assertEqual(again.games["games"][0]["areas"][1]["read"], "ask")
        self.assertEqual((await self.client.put("/api/regions", json={"games": [{"areas": [{"rect": [0, 0, 0, 0]}]}]}, headers=AUTH)).status, 400)
        with self.assertRaises(ValueError):
            helper_app.Helper({"token": "short", "state_dir": self.folder.name})


if __name__ == "__main__":
    unittest.main()
