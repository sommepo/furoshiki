#!/usr/bin/env python3
# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The title records as the page reads them (docs/titles.md), and their box pictures.

  titles-index.py OUT

writes OUT/front/titles.json, `{"<game ID>": {key, title, english, jiten, tracker_title, guide,
people, more, spine, cover}}`, one entry for every ID a record names: what the desktop's shelf
and stage know of a title (home/src/shelf_page.inc: RecordOf, CastOf, SpineOf). The people are a
title's first names (titles/names/<key>.json), the leads first, none that is a spoiler. The box
pictures are copied beside it (OUT/front/spines, OUT/front/covers) where a checkout has them
(titles/build, tools/titles.py spines and covers: not in Git), each the console's own, never
another console's.
"""
import importlib.util
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PEOPLE = 8
RANK = {"main": 0, "primary": 1, "side": 2}


def titles_tool():
    spec = importlib.util.spec_from_file_location("titles_tool", ROOT / "tools" / "titles.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def people_of(key: str):
    file = ROOT / "titles" / "names" / f"{key}.json"
    if not file.is_file():
        return [], 0
    entries = json.loads(file.read_text(encoding="utf-8")).get("entries", [])
    cast = [e for e in entries if e.get("kind") == "person" and not e.get("spoiler")]
    cast.sort(key=lambda e: RANK.get(e.get("role"), 3))
    shown = [{"name": e.get("name", ""), "romaji": e.get("romaji", "")} for e in cast[:PEOPLE]]
    return shown, max(0, len(cast) - PEOPLE)


def main() -> int:
    out = Path(sys.argv[1]) / "front"
    # A published checkout has no tools/titles.py and no box pictures (titles/build): the records alone.
    platform_of = titles_tool().spine_platform if (ROOT / "tools" / "titles.py").is_file() else (lambda _: "")
    index = {}
    for kind in ("spines", "covers"):
        shutil.rmtree(out / kind, ignore_errors=True)
    for file in sorted((ROOT / "titles").glob("*.json")):
        record = json.loads(file.read_text(encoding="utf-8"))
        key = file.stem
        people, more = people_of(key)
        for game_id in record.get("ids", []):
            entry = {
                "key": key,
                "title": record.get("title", ""),
                "english": record.get("english", ""),
                "jiten": record.get("jiten") or 0,
                "tracker_title": record.get("tracker_title", ""),
                "guide": record.get("guide") or "",
                "people": people,
                "more": more,
            }
            platform = platform_of(game_id)
            for kind in ("spines", "covers"):
                picture = ROOT / "titles" / "build" / kind / f"{key}.{platform}.jpg"
                if platform and picture.is_file():
                    (out / kind).mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(picture, out / kind / picture.name)
                    entry[kind[:-1]] = f"{kind}/{picture.name}"
            index[game_id] = entry
    out.mkdir(parents=True, exist_ok=True)
    (out / "titles.json").write_text(json.dumps(index, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
