#!/usr/bin/env python3
"""Titles: one record per game or show, its names from VNDB, and the names dictionaries.

The format is docs/titles.md. Commands:

  titles.py list                      every profile's game, and the title it belongs to
  titles.py search TEXT [--platform P] [--results N] [--page N] [--key KEY]
                                      VNDB visual novels matching TEXT (to find a vndb id),
                                      with their platforms and the title already using each;
                                      --key prints the add command for each hit
  titles.py add KEY --title T --ids ID [ID...] [--vndb vN] [--also vN...] [--guide SLUG]
                                      write titles/KEY.json, then fetch its names; --also adds
                                      more VNDB entries the title holds (a trilogy's games)
  titles.py fetch KEY [KEY...]        (re)fetch names and notes from VNDB for these titles
  titles.py notes [KEY...]            fetch only the notes (descriptions, picture addresses) of
                                      the people VNDB has again; names and readings are left alone
  titles.py spines [KEY...]           fetch each title's box spine, one for each console it is played
                                      on and only that console's, into titles/build/spines/
                                      <key>.<console>.jpg (not in Git), for Furoshiki 3's shelf
  titles.py covers [KEY...]           the same for the box fronts, into titles/build/covers/
  titles.py portraits [KEY...]        fetch the notes' pictures, made small, into titles/build/
                                      portraits/ (not in Git): a dev build's dictionaries carry them
  titles.py build [KEY...] [--out DIR] [--public] [--quiet]
                                      one Yomitan dictionary per title (名前 · <title>),
                                      names-KEY.zip; --public leaves out the notes (descriptions)
  titles.py check [--service URL]     profiles without a title, names not checked, guides
                                      without a title, and with --service Furochibi's OCR
                                      games without one; exits 1 if a game has no title

Network: VNDB's API (api.vndb.org/kana), politely (one request a second). Kanji aliases are
read with JMdict (input/JMdict_english.zip) when it has the word.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TITLES = ROOT / "titles"
NAMES = TITLES / "names"
NOTES = TITLES / "notes"
BUILD = TITLES / "build"
PORTRAITS = BUILD / "portraits"
GUIDES = ROOT / "guides" / "src" / "guides"
def _main_checkout() -> Path:
    """The main checkout when this runs in a git worktree (its input/ is not in Git)."""
    git = ROOT / ".git"
    if git.is_file():  # a worktree: "gitdir: <checkout>/.git/worktrees/<name>"
        m = re.match(r"gitdir:\s*(.+)", git.read_text(encoding="utf-8").strip())
        if m:
            common = Path(m.group(1)).resolve().parent.parent
            if common.name == ".git":
                return common.parent
    return ROOT


JMDICT = next((p for p in (ROOT / "input" / "JMdict_english.zip", _main_checkout() / "input" / "JMdict_english.zip")
               if p.is_file()), ROOT / "input" / "JMdict_english.zip")
API = "https://api.vndb.org/kana"
AGENT = "furoshiki-titles/1 (private reading tools)"

# Every folder of game scripts and its console (publish.json's list, and Eden's, which is private).
PROFILE_FOLDERS = {
    "station/reader/profiles": "ps1",
    "flycast/reader/profiles": "dreamcast",
    "ppsspp/assets/reader/profiles": "psp",
    "pcsx2/reader/profiles": "ps2",
    "azahar/src/android/app/src/main/assets/reader/profiles": "3ds",
    "vita3k/reader/profiles": "vita",
    "melonds/reader/profiles": "nds",
    "eden/src/android/app/src/main/assets/reader/profiles": "switch",
}
KINDS = ("person", "place", "thing")
ROLES = ("main", "primary", "side", "appears")
HONORIFICS = ["さん", "ちゃん", "くん", "君", "様", "さま", "先輩", "先生", "氏"]
ROLE_SCORE = {"main": 30, "primary": 20, "side": 10, "appears": 0}


# ---------------------------------------------------------------- profiles and records

def profiles() -> dict[str, dict]:
    """Every game script: its ID, console and title from its header."""
    out: dict[str, dict] = {}
    for folder, console in PROFILE_FOLDERS.items():
        for lua in sorted((ROOT / folder).glob("*.lua")):
            title = ""
            for line in lua.read_text(encoding="utf-8", errors="replace").splitlines()[:12]:
                m = re.match(r"--\s*reader:\s*title\s+(.*)", line)
                if m:
                    title = m.group(1).strip()
            out.setdefault(lua.stem, {"id": lua.stem, "console": console, "title": title, "path": str(lua.relative_to(ROOT))})
    return out


def records() -> dict[str, dict]:
    return {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted(TITLES.glob("*.json"))}


def write_json(path: Path, value, compact: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=None if compact else 1)
    path.write_text(text + "\n", encoding="utf-8", newline="\n")


# ---------------------------------------------------------------- VNDB

_last_call = 0.0


def vndb(endpoint: str, body: dict) -> dict:
    global _last_call
    wait = 1.0 - (time.monotonic() - _last_call)
    if wait > 0:
        time.sleep(wait)
    req = urllib.request.Request(f"{API}/{endpoint}", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json", "User-Agent": AGENT})
    with urllib.request.urlopen(req, timeout=30) as r:
        _last_call = time.monotonic()
        return json.loads(r.read())


def vndb_ids(rec: dict) -> list[str]:
    """The VNDB entries a title holds: `vndb`, then `vndb_also` (a trilogy's other games)."""
    ids = [rec["vndb"]] if rec.get("vndb") else []
    return ids + [v for v in rec.get("vndb_also", []) if v not in ids]


def vndb_characters(vid: str | list[str]) -> list[dict]:
    vids = [vid] if isinstance(vid, str) else vid
    vfilter = ["vn", "=", ["id", "=", vids[0]]] if len(vids) == 1 else \
        ["or"] + [["vn", "=", ["id", "=", v]] for v in vids]
    chars, page = [], 1
    while True:
        res = vndb("character", {
            "filters": vfilter,
            "fields": "name,original,aliases,description,image.url,image.sexual,sex,vns.id,vns.role,vns.spoiler",
            "results": 100, "page": page})
        chars += res["results"]
        if not res.get("more"):
            return chars
        page += 1


# ---------------------------------------------------------------- readings

_ROMAJI = {
    "kya": "きゃ", "kyu": "きゅ", "kyo": "きょ", "sha": "しゃ", "shu": "しゅ", "sho": "しょ", "she": "しぇ",
    "cha": "ちゃ", "chu": "ちゅ", "cho": "ちょ", "che": "ちぇ", "nya": "にゃ", "nyu": "にゅ", "nyo": "にょ",
    "hya": "ひゃ", "hyu": "ひゅ", "hyo": "ひょ", "mya": "みゃ", "myu": "みゅ", "myo": "みょ",
    "rya": "りゃ", "ryu": "りゅ", "ryo": "りょ", "gya": "ぎゃ", "gyu": "ぎゅ", "gyo": "ぎょ",
    "bya": "びゃ", "byu": "びゅ", "byo": "びょ", "pya": "ぴゃ", "pyu": "ぴゅ", "pyo": "ぴょ",
    "ja": "じゃ", "ju": "じゅ", "jo": "じょ", "je": "じぇ", "jya": "じゃ", "jyu": "じゅ", "jyo": "じょ",
    "tsu": "つ", "shi": "し", "chi": "ち", "dzu": "づ", "dji": "ぢ",
    "ka": "か", "ki": "き", "ku": "く", "ke": "け", "ko": "こ", "sa": "さ", "su": "す", "se": "せ", "so": "そ",
    "ta": "た", "te": "て", "to": "と", "na": "な", "ni": "に", "nu": "ぬ", "ne": "ね", "no": "の",
    "ha": "は", "hi": "ひ", "fu": "ふ", "he": "へ", "ho": "ほ", "ma": "ま", "mi": "み", "mu": "む", "me": "め", "mo": "も",
    "ya": "や", "yu": "ゆ", "yo": "よ", "ra": "ら", "ri": "り", "ru": "る", "re": "れ", "ro": "ろ",
    "wa": "わ", "wo": "を", "ga": "が", "gi": "ぎ", "gu": "ぐ", "ge": "げ", "go": "ご",
    "za": "ざ", "ji": "じ", "zu": "ず", "ze": "ぜ", "zo": "ぞ", "da": "だ", "de": "で", "do": "ど",
    "ba": "ば", "bi": "び", "bu": "ぶ", "be": "べ", "bo": "ぼ", "pa": "ぱ", "pi": "ぴ", "pu": "ぷ", "pe": "ぺ", "po": "ぽ",
    "a": "あ", "i": "い", "u": "う", "e": "え", "o": "お",
}


def romaji_to_kana(text: str) -> str | None:
    """VNDB's romanisation of a Japanese name back to hiragana; None when it is not plain
    (macrons, letters that are not Japanese sounds), so the reading is never guessed."""
    if "'" in text or "-" in text:  # Jun'ichi: the n ends a syllable of its own
        pieces = [romaji_to_kana(p) for p in re.split(r"['-]", text) if p]
        return "".join(pieces) if pieces and all(pieces) else None
    s = text.lower()
    if not s or any(ch not in "abcdefghijkmnoprstuvwyz" for ch in s):
        return None
    out, i = [], 0
    while i < len(s):
        if i + 1 < len(s) and s[i] == s[i + 1] and s[i] not in "aeioun":
            out.append("っ")
            i += 1
            continue
        if s.startswith("tch", i):
            out.append("っ")
            i += 1
            continue
        if s[i] == "n" and (i + 1 == len(s) or s[i + 1] not in "aeiouy"):
            out.append("ん")
            i += 1
            continue
        for size in (3, 2, 1):
            piece = s[i:i + size]
            if piece in _ROMAJI:
                out.append(_ROMAJI[piece])
                i += size
                break
        else:
            return None
    return "".join(out)


def is_kana(text: str) -> bool:
    return bool(text) and all("぀" <= c <= "ヿ" or c in "ー・" for c in text)


def has_japanese(text: str) -> bool:
    return any("぀" <= c <= "ヿ" or "㐀" <= c <= "鿿" or c in "々〆" for c in text)


def to_hiragana(text: str) -> str:
    return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text)


_jmdict: dict[str, tuple[str, int]] | None = None


def jmdict_reading(word: str) -> str | None:
    """A word's first reading in JMdict, for kanji aliases such as 助手."""
    global _jmdict
    if _jmdict is None:
        _jmdict = {}
        if JMDICT.is_file():
            with zipfile.ZipFile(JMDICT) as z:
                for name in z.namelist():
                    if name.startswith("term_bank_"):
                        for row in json.loads(z.read(name)):
                            # The reading JMdict ranks first (助手 is じょしゅ, not たすけて).
                            best = _jmdict.get(row[0])
                            if best is None or row[4] > best[1]:
                                _jmdict[row[0]] = (row[1] or row[0], row[4])
    found = _jmdict.get(word)
    # A row with no reading gives the word itself back: only a reading in kana is one.
    return found[0] if found and is_kana(found[0].replace(" ", "")) else None


_QUOTED = re.compile(r'["“「『«][^"“”「」『』«»]*["”」』»]')


def is_quotation(paragraph: str) -> bool:
    """A paragraph that is only something the character says (VNDB's descriptions often open
    with a catchphrase), with or without who said it after: no description of anyone."""
    text = paragraph.strip()
    if not text or text[0] not in '"“「『«':
        return False
    # What is left without the quoted words: nothing, or who said them (- Name, ~Name, (Name)).
    rest = _QUOTED.sub("", text).strip()
    return rest != text and re.fullmatch(r"(?:[-–—~〜(（,]\s*[^.!?。]{1,60}[)）]?)?", rest) is not None


def clean_description(text: str | None) -> str:
    """The first paragraph that describes: spoilers and markup out, quotations passed over."""
    if not text:
        return ""
    text = re.sub(r"\[spoiler\].*?\[/spoiler\]", "", text, flags=re.S | re.I)
    text = re.sub(r"\[url=[^\]]*\](.*?)\[/url\]", r"\1", text, flags=re.S | re.I)
    text = re.sub(r"\[/?[a-z]+\]", "", text, flags=re.I)
    for paragraph in re.split(r"\n\s*\n", text.strip()):
        paragraph = re.sub(r"\s+", " ", paragraph).strip()
        if paragraph and not is_quotation(paragraph):
            return paragraph
    return ""


def name_reading(parts: list[str], latin_parts: list[str]) -> list[str] | None:
    """The reading of a written name, part by part, from VNDB's romanisation. A part written in
    kana is its own reading (桑田 怜恩 is "Kuwata Leon": レオン is not romaji). A name written
    without a space whose romanisation has several words (御剣怜侍, "Mitsurugi Reiji") is read
    as one. None when anything does not convert plainly, or when the kana the name is written
    with is not in the reading made (ジュース東郷 is not じゅうすとうごう)."""
    # A name with a middle dot (ストールマン・孔) is read part by part and keeps the dot.
    dotted = len(parts) == 1 and "・" in parts[0] and len(parts[0].split("・")) == len(latin_parts)
    if dotted:
        parts = parts[0].split("・")
    if len(parts) == len(latin_parts):
        kana = []
        for part, latin in zip(parts, latin_parts):
            kana.append(part if is_kana(part) else romaji_to_kana(latin))
        if not all(kana):
            return None
        return ["・".join(kana)] if dotted else kana
    if len(parts) == 1 and len(latin_parts) > 1:
        joined = [romaji_to_kana(p) for p in latin_parts]
        if not all(joined):
            return None
        reading = "".join(joined)
        runs = re.findall(r"[぀-ヿー]+", parts[0])
        if not any("㐀" <= c <= "鿿" or c in "々ヶケ" for c in parts[0]):
            return None  # nothing in it needs reading: a description, not a name
        if all(to_hiragana(r) in reading for r in runs):
            return [reading]
    return None


def person(char: dict, vid: str | list[str]) -> tuple[dict, dict]:
    """A VNDB character as a names entry and its note."""
    original = (char.get("original") or "").strip()
    latin = (char.get("name") or "").strip()
    written = original or latin
    parts = written.split()
    latin_parts = latin.split()
    entry: dict = {"id": char["id"], "kind": "person", "name": written}
    check: list[str] = []
    if len(parts) == 2:
        entry["family"], entry["given"] = parts
    if is_kana(written.replace(" ", "")):
        entry["reading"] = written
        if len(parts) == 2:
            entry["family_reading"], entry["given_reading"] = parts
    elif original and (kana := name_reading(parts, latin_parts)):
        entry["reading"] = " ".join(kana)
        if len(parts) == 2:
            entry["family_reading"], entry["given_reading"] = kana
    else:
        check.append("reading")
    entry["romaji"] = latin
    aliases = []
    for alias in char.get("aliases") or []:
        alias = alias.strip()
        if not has_japanese(alias) or alias.replace(" ", "") == written.replace(" ", ""):
            continue
        if is_kana(alias.replace(" ", "")):
            aliases.append({"text": alias, "reading": alias})
        else:
            # An alias that is the family or given name alone takes that part's reading.
            own = {entry.get("family"): entry.get("family_reading"), entry.get("given"): entry.get("given_reading")}
            reading = own.get(alias) or jmdict_reading(alias)
            aliases.append({"text": alias, "reading": reading} if reading else {"text": alias})
            if not reading:
                check.append(f"alias:{alias}")
    entry["aliases"] = aliases
    vids = [vid] if isinstance(vid, str) else vid
    rels = [v for v in char.get("vns", []) if v.get("id") in vids]
    # In a title of several games: the biggest role, and the least spoiler of those games.
    rel = max(rels, key=lambda v: ROLE_SCORE.get(v.get("role") or "appears", 0), default={})
    if rels:
        rel = dict(rel, spoiler=min(v.get("spoiler", 0) for v in rels))
    entry["role"] = rel.get("role")
    sex = char.get("sex")
    entry["sex"] = sex[0] if isinstance(sex, list) and sex else None
    entry["spoiler"] = rel.get("spoiler", 0)
    entry["needs_check"] = check
    return entry, note_of(char)


def note_of(char: dict) -> dict:
    """A VNDB character's note: what describes them, and their picture's address unless VNDB
    marks the picture as sexual."""
    note = {"description": clean_description(char.get("description"))}
    image = char.get("image") or {}
    if image.get("url") and (image.get("sexual") or 0) < 1:
        note["image"] = image["url"]
    return note


# ---------------------------------------------------------------- commands

def cmd_list(_args) -> int:
    owner = {i: k for k, r in records().items() for i in r.get("ids", [])}
    for pid, p in sorted(profiles().items(), key=lambda kv: (kv[1]["console"], kv[0])):
        print(f'{p["console"]:9} {pid:18} {owner.get(pid, "-"):28} {p["title"]}')
    return 0


def slug(text: str) -> str:
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def cmd_search(args) -> int:
    text = " ".join(args.text)
    filters: list = ["search", "=", text]
    if args.platform:
        filters = ["and", filters, ["platform", "=", args.platform]]
    res = vndb("vn", {"filters": filters, "fields": "id,title,alttitle,released,platforms",
                      "results": args.results, "page": args.page})
    used = {v: k for k, r in records().items() for v in vndb_ids(r)}
    rec = records().get(args.key, {}) if args.key else {}
    for v in res["results"]:
        plats = ",".join(v.get("platforms") or [])
        owner = f'  [{used[v["id"]]}]' if v["id"] in used else ""
        print(f'{v["id"]:8} {v.get("released") or "":10} {plats[:28]:28} {v["title"]}  {v.get("alttitle") or ""}{owner}')
        if args.key:
            ids = " ".join(rec.get("ids", [])) or "<ID...>"
            title = (v.get("alttitle") or v["title"]).replace("'", "")
            print(f"    tools/titles.py add {args.key} --title '{rec.get('title', title)}' --ids {ids} --vndb {v['id']}")
    if not res["results"]:
        print(f'no hits for "{text}"' + (f" on {args.platform}" if args.platform else ""))
        if args.platform:
            # VNDB lists a game's platforms by its releases, and a port's console is often missing.
            print("    try again without --platform: VNDB may not list this console for the title")
        else:
            print("    try the Japanese title, the romaji, or a shorter part of the name")
    if res.get("more"):
        print(f"(more: --page {args.page + 1})")
    return 0


def read_compound(text: str, known: dict[str, str]) -> str | None:
    """An alias read piece by piece: kana as written, kanji from `known` (this title's name parts
    and its names file's `readings`), or JMdict for a word of two or more kanji
    (超高校級の「探偵」: ちょうこうこうきゅうのたんてい once 超高校級 is known). Brackets and dots
    are left out of the reading; anything else unread makes it None."""
    out = []
    for run in re.findall(r"[㐀-鿿々〆ヶ]+|[぀-ヿー]+|[ 　]+|[「」『』・･]+|.", text):
        if is_kana(run):
            out.append(run)
        elif run.isspace():
            out.append(" ")
        elif re.fullmatch(r"[「」『』・･]+", run):
            continue
        elif "㐀" <= run[0] <= "鿿" or run[0] in "々〆ヶ":
            reading = known.get(run) or (jmdict_reading(run) if len(run) > 1 else None)
            if not reading:
                return None
            out.append(reading)
        else:
            return None
    reading = re.sub(r" +", " ", "".join(out)).strip()
    return reading or None


def fill_readings(entries: list[dict], table: dict[str, str], old: list[dict]) -> None:
    """Readings left out by the conversion, filled from what is known: a reading someone wrote
    in by hand before this fetch (kept by the entry's id), then the title's own name parts and
    `readings` table for aliases. Whatever is filled is no longer flagged."""
    before = {e.get("id"): e for e in old}
    known = dict(table)
    for e in entries:
        for part, read in ((e.get("family"), e.get("family_reading")), (e.get("given"), e.get("given_reading"))):
            if part and read and not is_kana(part):
                known.setdefault(part, read)
    for e in entries:
        prev = before.get(e["id"], {})
        flags = list(e.get("needs_check", []))
        if "reading" in flags and is_kana((prev.get("reading") or "").replace(" ", "")):
            for field in ("reading", "family_reading", "given_reading"):
                if prev.get(field):
                    e[field] = prev[field]
            flags.remove("reading")
        prev_alias = {a["text"]: a["reading"] for a in prev.get("aliases", [])
                      if is_kana((a.get("reading") or "").replace(" ", ""))}
        for a in e.get("aliases", []):
            if a.get("reading"):
                continue
            reading = prev_alias.get(a["text"]) or read_compound(a["text"], known)
            if reading:
                a["reading"] = reading
                if f'alias:{a["text"]}' in flags:
                    flags.remove(f'alias:{a["text"]}')
        e["needs_check"] = flags


def fetch(key: str) -> None:
    rec = records()[key]
    if not rec.get("vndb"):
        print(f"{key}: no vndb id; names are added by hand (docs/titles.md)")
        return
    vid = vndb_ids(rec)
    chars = vndb_characters(vid)
    names_path, notes_path = NAMES / f"{key}.json", NOTES / f"{key}.json"
    old = json.loads(names_path.read_text(encoding="utf-8")) if names_path.is_file() else {"entries": []}
    # Entries added by hand (places, things, people VNDB lacks) are kept.
    kept = [e for e in old.get("entries", []) if not re.fullmatch(r"c\d+", str(e.get("id", "")))]
    entries, notes = [], {}
    for c in chars:
        if not (c.get("original") or c.get("name")):
            continue
        entry, note = person(c, vid)
        entries.append(entry)
        if note.get("description") or note.get("image"):
            notes[c["id"]] = note
    entries.sort(key=lambda e: (-ROLE_SCORE.get(e.get("role") or "appears", 0), e["id"]))
    fill_readings(entries, old.get("readings", {}), old.get("entries", []))
    source = {"vndb": vid[0], "fetched": dt.date.today().isoformat()}
    if len(vid) > 1:
        source["vndb_also"] = vid[1:]
    names = {"source": source, "checked": False}
    if old.get("readings"):
        names["readings"] = old["readings"]
    names["entries"] = entries + kept
    write_json(names_path, names)
    # Notes of the entries added by hand are kept with them.
    old_notes = json.loads(notes_path.read_text(encoding="utf-8")) if notes_path.is_file() else {}
    kept_ids = {e.get("id") for e in kept}
    notes.update({k: v for k, v in old_notes.items() if k in kept_ids})
    write_json(notes_path, notes)
    unsure = sum(1 for e in entries if e["needs_check"])
    print(f"{key}: {len(entries)} people from VNDB {', '.join(vid)}, {len(kept)} kept by hand, {unsure} to check")


def cmd_add(args) -> int:
    path = TITLES / f"{args.key}.json"
    rec = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}
    taken = {i: k for k, r in records().items() if k != args.key for i in r.get("ids", [])}
    clash = [i for i in args.ids if i in taken]
    if clash:
        raise SystemExit(f"already in another title: {', '.join(f'{i} ({taken[i]})' for i in clash)}")
    rec.update({
        "title": args.title or rec.get("title", args.key),
        "ids": sorted(set(rec.get("ids", [])) | set(args.ids)),
        "furochibi": rec.get("furochibi", []),
        "jellyfin": rec.get("jellyfin", []),
        "vndb": args.vndb if args.vndb is not None else rec.get("vndb"),
        **({"vndb_also": args.also} if args.also else {"vndb_also": rec["vndb_also"]} if rec.get("vndb_also") else {}),
        "anilist": rec.get("anilist"),
        "guide": args.guide if args.guide is not None else rec.get("guide"),
    })
    if rec["guide"] and not (GUIDES / rec["guide"] / "guide.json").is_file():
        raise SystemExit(f'no guide "{rec["guide"]}" in guides/src/guides')
    write_json(path, rec)
    print(f"{path.relative_to(ROOT)} written")
    if rec["vndb"]:
        fetch(args.key)
    return 0


def cmd_fetch(args) -> int:
    for key in args.keys or [k for k, r in records().items() if r.get("vndb")]:
        fetch(key)
    return 0


def refetch_notes(key: str) -> tuple[int, int]:
    """Only the notes of the people VNDB has, fetched again (a better reading of their
    descriptions); names, readings and notes written by hand stay as they are. Returns how many
    notes there are and how many changed."""
    rec = records()[key]
    if not rec.get("vndb"):
        return 0, 0
    notes_path = NOTES / f"{key}.json"
    old = json.loads(notes_path.read_text(encoding="utf-8")) if notes_path.is_file() else {}
    notes = {k: v for k, v in old.items() if not re.fullmatch(r"c\d+", k)}
    for c in vndb_characters(vndb_ids(rec)):
        note = note_of(c)
        if note.get("description") or note.get("image"):
            notes[c["id"]] = note
    changed = sum(1 for k, v in notes.items() if old.get(k) != v) + sum(1 for k in old if k not in notes)
    if notes != old:
        write_json(notes_path, notes)
    return len(notes), changed


def cmd_notes(args) -> int:
    for key in args.keys or [k for k, r in records().items() if r.get("vndb")]:
        total, changed = refetch_notes(key)
        print(f"{key}: {total} notes, {changed} changed")
    return 0


def portrait_path(key: str, entry_id: str) -> Path:
    return PORTRAITS / key / (re.sub(r"[^A-Za-z0-9_-]", "_", entry_id) + ".jpg")


def fetch_portraits(key: str) -> tuple[int, int]:
    """The pictures the notes name, each made small (200 px tall at most) with ffmpeg and kept
    in titles/build/portraits/<key>/ (not in Git: VNDB's pictures are not ours to publish). One
    that is there is not fetched again. Returns how many are there and how many came now."""
    notes_path = NOTES / f"{key}.json"
    if not notes_path.is_file():
        return 0, 0
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("portraits: ffmpeg is needed to make the pictures small")
    notes = json.loads(notes_path.read_text(encoding="utf-8"))
    have = came = 0
    for entry_id, note in notes.items():
        url = note.get("image")
        if not url:
            continue
        out = portrait_path(key, entry_id)
        if out.is_file():
            have += 1
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": AGENT})
            with urllib.request.urlopen(req, timeout=30) as r:
                raw = r.read()
            time.sleep(0.25)
            done = subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "image2pipe", "-i", "-", "-vf", "scale=-2:'min(200,ih)'",
                 "-frames:v", "1", "-q:v", "5", "-map_metadata", "-1", "-bitexact", "-f", "mjpeg", "-"],
                input=raw, capture_output=True, timeout=60)
            if done.returncode != 0 or not done.stdout:
                continue
            out.write_bytes(done.stdout)
            have += 1
            came += 1
        except Exception as error:  # a picture that cannot be had is left out, quietly
            print(f"  {key} {entry_id}: {error}", file=sys.stderr)
    return have, came


SPINES = BUILD / "spines"
COVERS = BUILD / "covers"


def spine_platform(game_id: str) -> str | None:
    """VNDB's name of the console a Furoshiki game ID is of, as far as the ID says."""
    i = game_id.upper()
    if re.match(r"(ULJ|NPJ|UCJ|ULA)", i):
        return "psp"
    if re.match(r"PCS", i):
        return "psv"
    if re.fullmatch(r"0004[0-9A-F]{12}", i):
        return "n3d"
    if re.fullmatch(r"[0-9A-F]{16}", i):
        return "swi"
    # A DS cartridge's game code: four letters or figures (BSKJ, AWIJ).
    if re.fullmatch(r"[A-Z0-9]{4}", i):
        return "nds"
    # SCPS-10… is the PlayStation's (Aconcagua, Double Cast); the PS2's are SCPS-15…, 55…, 56…
    if re.match(r"(SLPS-?2|SLPM-?[5-7]|SCPS-?(1[5-9]|[5-7])|SLPS-?7)", i):
        return "ps2"
    if re.match(r"(SLPS-?[0189]|SLPM-?8|SCPS-?[14]0|SCPS-?45)", i):
        return "ps1"
    # The Dreamcast's: T-…, HDR-…, MK-…, and Sega's own seven figures (610-7179 as 6107179).
    if re.match(r"(T-?\d|HDR-|MK-)", i) or re.fullmatch(r"\d{7}", i):
        return "drc"
    # The DS's cartridges: a four-letter game code (AWIJ), which no other console's ID is.
    if re.fullmatch(r"[A-Z0-9]{4}", i):
        return "nds"
    return None


SCREENSCRAPER = ROOT / "home" / "private" / "screenscraper.json"
SCREENSCRAPER_SYSTEMS = {"psp": 61, "ps1": 57, "ps2": 58, "psv": 62, "drc": 23, "n3d": 17, "swi": 225, "nds": 15}
# The regions of a box, the Japanese one first: the games are read in Japanese.
REGION_ORDER = {"jp": 0, "wor": 1, "ss": 2, "us": 3, "eu": 4, "uk": 5, "fr": 6, "de": 7}


def plain_name(text: str) -> str:
    return re.sub(r"[\W_]+", "", unicodedata.normalize("NFKC", text or "").casefold())


def plain_serial(text: str) -> str:
    return re.sub(r"[^0-9A-Z]", "", (text or "").upper())


def screenscraper(page: str, **query) -> dict | None:
    """One question to ScreenScraper (api.screenscraper.fr), with the maintainer's developer and
    member details from home/private/screenscraper.json (never in Git); None without that file,
    or when it has no such game."""
    # In a worktree, private/ is the main checkout's.
    file = next((f for f in (SCREENSCRAPER, _main_checkout() / "home" / "private" / "screenscraper.json") if f.is_file()), None)
    if file is None:
        return None
    who = json.loads(file.read_text(encoding="utf-8"))
    if not who.get("devid") or not who.get("devpassword"):
        return None
    query.update(devid=who["devid"], devpassword=who["devpassword"], softname=who.get("softname") or "Furoshiki",
                 output="json", ssid=who.get("ssid", ""), sspassword=who.get("sspassword", ""))
    url = f"https://api.screenscraper.fr/api2/{page}?" + urllib.parse.urlencode(query)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "Furoshiki"}), timeout=40) as r:
            return json.loads(r.read().decode("utf-8", "replace")).get("response")
    except Exception:
        return None
    finally:
        time.sleep(1.0)


def name_variants(rec: dict) -> list[str]:
    """What to search for a title by: its English and own names as written, then with their
    punctuation made spaces (ScreenScraper's search finds "Star Ocean The Second Story R" but not
    "Star Ocean: The Second Story R"). Only searches: a found game must still be named exactly."""
    out: list[str] = []
    for name in [rec.get("english"), rec.get("title")] + list(rec.get("aliases") or []):
        if not name:
            continue
        spaced = re.sub(r"\s+", " ", re.sub(r"[^\w]+", " ", unicodedata.normalize("NFKC", name))).strip()
        for text in (name, spaced):
            if text and text not in out:
                out.append(text)
    return out


def ss_serials(game: dict) -> set[str]:
    found: set[str] = set()
    for rom in [game.get("rom") or {}] + list(game.get("roms") or []):
        for serial in (rom.get("serials") or []) + [rom.get("romserial") or ""]:
            if serial:
                found.add(plain_serial(serial))
    return found


def art_platforms(rec: dict) -> list[str]:
    """The consoles a title is played on here, as spine_platform() names them: one picture each."""
    return sorted({spine_platform(i) for i in rec.get("ids", [])} & set(SCREENSCRAPER_SYSTEMS))


def ss_system_of(game: dict) -> str | None:
    system = str((game.get("systeme") or {}).get("id"))
    return next((p for p, s in SCREENSCRAPER_SYSTEMS.items() if str(s) == system), None)


def stored_screenscraper(rec: dict) -> dict[str, int]:
    """The record's ScreenScraper games by console: `"screenscraper": {"psp": 103341}`, or a bare
    number (older records), which is the game of whichever console it turns out to be."""
    value = rec.get("screenscraper")
    if isinstance(value, dict):
        return {p: int(v) for p, v in value.items() if v}
    return {"?": int(value)} if value else {}


_SS_GAMES: dict[tuple[str, str], tuple[dict, str] | None] = {}


def screenscraper_game(key: str, rec: dict, platform: str) -> tuple[dict, str] | None:
    """The title's game on ScreenScraper for one console (its `systemeid`: never another console's
    game), with how it was found. Never a looser match than these, in this order: the game the
    record names for that console (`screenscraper`); a game ScreenScraper finds by one of the
    title's IDs of that console whose roms carry that very serial; a game of that console whose
    name there is exactly the record's English or own title (a search for Persona 3 finds Persona
    4 first). A game found by serial or name is written into the record, so the next run goes to
    it at once."""
    if (key, platform) in _SS_GAMES:
        return _SS_GAMES[(key, platform)]
    system = SCREENSCRAPER_SYSTEMS[platform]
    chosen = None
    stored = stored_screenscraper(rec)
    for where, number in stored.items():
        if where not in (platform, "?"):
            continue
        found = screenscraper("jeuInfos.php", gameid=number)
        game = (found or {}).get("jeu")
        if game and ss_system_of(game) == platform:
            chosen = (game, f"record {number}")
            break
    known = platform in stored
    if chosen is None and not known:
        names = {plain_name(n) for n in [rec.get("english"), rec.get("title")] + list(rec.get("aliases") or []) if n} - {""}
        for game_id in [i for i in rec.get("ids", []) if spine_platform(i) == platform]:
            if platform == "swi":
                break  # ScreenScraper's Switch roms carry no title IDs: by name below
            found = screenscraper("jeuInfos.php", systemeid=system, romtype="iso", romnom=f"{game_id}.iso", serialnum=game_id)
            game = (found or {}).get("jeu")
            if game and ss_system_of(game) == platform and plain_serial(game_id) in ss_serials(game):
                chosen = (game, f"serial {game_id}")
                break
        for search in ([] if chosen else name_variants(rec)):
            found = screenscraper("jeuRecherche.php", systemeid=system, recherche=search)
            games = [g for g in (found or {}).get("jeux", []) if g and g.get("id") and ss_system_of(g) == platform and
                     names & {plain_name(n.get("text", "")) for n in g.get("noms", [])}]
            if games:
                chosen = (games[0], f"name {search!r}")
                break
    if chosen and (not known or "?" in stored):
        # Kept in the record by console.
        path = TITLES / f"{key}.json"
        if path.is_file():
            record = json.loads(path.read_text(encoding="utf-8"))
            have = stored_screenscraper(record)
            if "?" in have and str(have["?"]) == str(chosen[0]["id"]):
                del have["?"]
            have[platform] = int(chosen[0]["id"])
            record["screenscraper"] = {p: v for p, v in sorted(have.items())}
            write_json(path, record)
            rec["screenscraper"] = record["screenscraper"]
    _SS_GAMES[(key, platform)] = chosen
    return chosen


def ss_name(game: dict) -> str:
    names = {n.get("region"): n.get("text", "") for n in game.get("noms", [])}
    return names.get("jp") or names.get("ss") or names.get("wor") or next(iter(names.values()), "")


def screenscraper_media(key: str, rec: dict, platform: str, kind: str) -> tuple[bytes, str, str] | None:
    """A picture of the title's box for one console from ScreenScraper (`box-2D-side` its spine,
    `box-2D` its front), Japan's before any other region's; with where it came from and its region."""
    found = screenscraper_game(key, rec, platform)
    if not found:
        return None
    game, how = found
    pictures = [m for m in game.get("medias", []) if m.get("type") == kind and m.get("url")]
    pictures.sort(key=lambda m: REGION_ORDER.get(m.get("region"), 9))
    for picture in pictures[:2]:
        try:
            with urllib.request.urlopen(urllib.request.Request(picture["url"], headers={"User-Agent": "Furoshiki"}), timeout=60) as r:
                raw = r.read()
            if raw[:1] in (b"{", b"<") or len(raw) < 2000:
                continue  # ScreenScraper's answer that it has none, not a picture
            return raw, f"screenscraper {game['id']} {ss_name(game)!r} ({picture.get('region')}, by {how})", picture.get("region") or ""
        except Exception:
            continue
        finally:
            time.sleep(1.0)
    return None


def keep_picture(raw: bytes, out: Path, tallest: int) -> bool:
    """A picture made at most `tallest` high, its own shape kept, as a small JPEG at `out`."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("titles: ffmpeg is needed to make the pictures small")
    done = subprocess.run(
        [ffmpeg, "-v", "error", "-y", "-f", "image2pipe", "-i", "-", "-vf", f"scale=-2:'min({tallest},ih)',format=yuvj444p",
         "-frames:v", "1", "-q:v", "3", "-map_metadata", "-1", "-bitexact", "-f", "mjpeg", "-"],
        input=raw, capture_output=True, timeout=60)
    if done.returncode != 0 or not done.stdout:
        return False
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(done.stdout)
    return True


def download(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": AGENT}), timeout=60) as r:
        return r.read()


_VNDB_RELEASES: dict[str, list[dict]] = {}


def vndb_release_picture(rec: dict, platform: str, kind: str, accept) -> tuple[str, str] | None:
    """The largest picture of a kind (`pkgside`, `pkgfront`) that `accept(dims)` takes, of the
    title's Japanese releases on VNDB for that console only (a release of that console alone
    before one that is also another's: its box may be the other's)."""
    best = None
    for vid in vndb_ids(rec):
        if vid not in _VNDB_RELEASES:
            found = vndb("release", {"filters": ["and", ["vn", "=", ["id", "=", vid]], ["lang", "=", "ja"]],
                                     "fields": "title,platforms,images{type,url,dims,sexual}", "results": 100})
            _VNDB_RELEASES[vid] = found.get("results", [])
        for release in _VNDB_RELEASES[vid]:
            platforms = release.get("platforms") or []
            if platform not in platforms:
                continue
            for image in release.get("images") or []:
                dims = image.get("dims") or [0, 0]
                if image.get("type") != kind or not dims[0] or not accept(dims) or (image.get("sexual") or 0) >= 1:
                    continue
                rank = (len(platforms) == 1, dims[1])
                if best is None or rank > best[0]:
                    best = (rank, image["url"], f"vndb {vid} {release.get('id')} {release.get('title')!r} {kind} {platforms}")
    return (best[1], best[2]) if best else None


def art_file(folder: Path, key: str, platform: str) -> Path:
    return folder / f"{key}.{platform}.jpg"


def fetch_spine(key: str, rec: dict, platform: str) -> str:
    """A title's box spine on one console, for Furoshiki 3's shelf: ScreenScraper's `box-2D-side`
    of that console's game, else the tallest narrow `pkgside` of its Japanese releases of that
    console on VNDB; never another console's box. Made 1000 high, as
    titles/build/spines/<key>.<platform>.jpg (not in Git: the pictures are not ours to publish).
    Says what it did: "have", "new (…)" or "none"."""
    out = art_file(SPINES, key, platform)
    if out.is_file():
        return "have"
    found = screenscraper_media(key, rec, platform, "box-2D-side")
    if found and keep_picture(found[0], out, 1000):
        return f"new ({found[1]})"
    # A spine stands at least four times as tall as it is wide: a box's whole side spread out, or a
    # disc's edge lying down, is not one.
    picture = vndb_release_picture(rec, platform, "pkgside", lambda dims: dims[1] >= 4 * dims[0])
    if picture and keep_picture(download(picture[0]), out, 1000):
        return f"new ({picture[1]})"
    return "none"


# libretro-thumbnails' repositories of box fronts, by console (their Named_Boxarts, named as Redump
# and No-Intro name the games: "Otogirisou - Sosei Hen (Japan)").
LIBRETRO_SYSTEMS = {"ps1": "Sony_-_PlayStation", "ps2": "Sony_-_PlayStation_2", "psp": "Sony_-_PlayStation_Portable",
                    "psv": "Sony_-_PlayStation_Vita", "drc": "Sega_-_Dreamcast", "n3d": "Nintendo_-_Nintendo_3DS",
                    "nds": "Nintendo_-_Nintendo_DS"}
_LIBRETRO: dict[str, list[str]] = {}


def libretro_boxarts(repo: str) -> list[str]:
    """The names of a libretro-thumbnails repository's box fronts (GitHub's API through `gh`)."""
    if repo not in _LIBRETRO:
        names: list[str] = []
        gh = shutil.which("gh")
        try:
            if gh:
                root = json.loads(subprocess.run([gh, "api", f"repos/libretro-thumbnails/{repo}/git/trees/HEAD"],
                                                 capture_output=True, text=True, timeout=60, check=True).stdout)
                sha = next(t["sha"] for t in root.get("tree", []) if t.get("path") == "Named_Boxarts")
                tree = json.loads(subprocess.run([gh, "api", f"repos/libretro-thumbnails/{repo}/git/trees/{sha}"],
                                                 capture_output=True, text=True, timeout=120, check=True).stdout)
                names = [t["path"] for t in tree.get("tree", []) if t.get("path", "").endswith(".png")]
        except Exception:
            names = []
        _LIBRETRO[repo] = names
    return _LIBRETRO[repo]


def libretro_cover(key: str, rec: dict, platform: str, japan_only: bool = False) -> tuple[str, str] | None:
    """A box front from libretro-thumbnails' repository of that console: the file whose name
    before its brackets is exactly one of the title's names (its English or own name, or its name
    on ScreenScraper for that console), or the record's `libretro`; Japan's before any other."""
    repo = LIBRETRO_SYSTEMS.get(platform)
    if not repo:
        return None
    names = {plain_name(n) for n in [rec.get("english"), rec.get("title")] + list(rec.get("aliases") or []) if n}
    found = screenscraper_game(key, rec, platform)
    if found:
        names |= {plain_name(n.get("text", "")) for n in found[0].get("noms", [])}
    names -= {""}
    libretro = rec.get("libretro")
    if isinstance(libretro, dict):
        libretro = libretro.get(platform)
    hits = [f for f in libretro_boxarts(repo) if f == f"{libretro}.png" or plain_name(re.split(r"\s+\(", f[:-4], maxsplit=1)[0]) in names]
    hits.sort(key=lambda f: (0 if "(Japan" in f else 1, len(f)))
    if japan_only:
        hits = [f for f in hits if "(Japan" in f]
    if not hits:
        return None
    url = f"https://raw.githubusercontent.com/libretro-thumbnails/{repo}/master/Named_Boxarts/" + urllib.parse.quote(hits[0])
    return url, f"libretro {repo} {hits[0]!r}"


def fetch_cover(key: str, rec: dict, platform: str) -> str:
    """A title's box front on one console, for Furoshiki 3's stage and its open spine:
    ScreenScraper's `box-2D` of that console's game, else the largest `pkgfront` of its Japanese
    releases of that console on VNDB, else libretro-thumbnails' box art of that console; never
    another console's box (and never a VNDB entry's own picture, which is no console's). Japan's box
    first: where ScreenScraper has only another country's, libretro's or VNDB's Japanese one of that
    console is taken instead. Made at most 1400 high, its shape kept, as
    titles/build/covers/<key>.<platform>.jpg (not in Git). Says what it did: "have", "new (…)" or "none"."""
    out = art_file(COVERS, key, platform)
    if out.is_file():
        return "have"
    found = screenscraper_media(key, rec, platform, "box-2D")
    front = lambda dims: dims[1] >= dims[0] * 0.9
    if found and found[2] not in ("jp", "wor", "asi"):
        picture = libretro_cover(key, rec, platform, japan_only=True) or vndb_release_picture(rec, platform, "pkgfront", front)
        if picture and keep_picture(download(picture[0]), out, 1400):
            return f"new ({picture[1]})"
    if found and keep_picture(found[0], out, 1400):
        return f"new ({found[1]})"
    picture = vndb_release_picture(rec, platform, "pkgfront", front)
    if picture and keep_picture(download(picture[0]), out, 1400):
        return f"new ({picture[1]})"
    picture = libretro_cover(key, rec, platform)
    if picture and keep_picture(download(picture[0]), out, 1400):
        return f"new ({picture[1]})"
    return "none"


def cmd_art(args, fetch) -> int:
    recs = records()
    for key in args.keys or sorted(recs):
        platforms = art_platforms(recs[key])
        if not platforms:
            print(f"{key}: none (no console among its ids)", flush=True)
        for platform in platforms:
            print(f"{key}.{platform}: {fetch(key, recs[key], platform)}", flush=True)
    return 0


def cmd_spines(args) -> int:
    return cmd_art(args, fetch_spine)


def cmd_covers(args) -> int:
    return cmd_art(args, fetch_cover)


def cmd_portraits(args) -> int:
    for key in args.keys or sorted(p.stem for p in NOTES.glob("*.json")):
        have, came = fetch_portraits(key)
        if have or came:
            print(f"{key}: {have} portraits, {came} new")
    return 0


ROLE_WORDS = {"main": "Protagonist", "primary": "Main character", "side": "Side character", "appears": "Appears"}
KIND_WORDS = {"place": "Place", "thing": "Thing"}


def role_words(entry: dict) -> str:
    """What an entry is, in words, for its card: a person's part in the story, else its kind."""
    if entry.get("kind", "person") != "person":
        return KIND_WORDS.get(entry.get("kind"), "")
    return ROLE_WORDS.get(entry.get("role") or "", "")


def card_gloss(entry: dict, note: dict, public: bool, image: str | None) -> dict:
    """A name's one glossary item: Yomitan structured content whose root holds the card (its
    `data`: what Furoshiki's popup draws, docs/titles.md) and, as its content, the same in plain
    lines for every other reader of the format."""
    full = entry["name"]
    reading = entry.get("reading") or ""
    others = [a["text"] for a in entry.get("aliases", [])]
    role = role_words(entry)
    about = "" if public or entry.get("spoiler") else short(note.get("description") or "", 320)
    data = {"furoshiki": "name", "id": str(entry.get("id", "")), "kind": entry.get("kind", "person"), "name": full}
    if reading and reading != full:
        data["reading"] = reading
    if entry.get("romaji"):
        data["romaji"] = entry["romaji"]
    if role:
        data["role"] = role
    if others:
        data["also"] = "、".join(others)
    if about:
        data["about"] = about
    if image:
        data["image"] = image
    lines = [f"{full}（{reading}）" if reading and reading != full else full]
    if role:
        lines.append(role)
    if others:
        lines.append("Also: " + "、".join(others))
    if about:
        lines.append(about)
    return {"type": "structured-content",
            "content": {"tag": "div", "data": data, "content": [{"tag": "div", "content": line} for line in lines]}}


def short(text: str, limit: int = 220) -> str:
    """The first sentences of a description, as many as fit in a popup line or two."""
    out = ""
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        if out and len(out) + len(sentence) > limit:
            break
        out = f"{out} {sentence}".strip()
    return out[:limit]


def dictionary_rows(rec: dict, names: dict, notes: dict, public: bool,
                    images: dict[str, str] | None = None) -> list[list]:
    """The dictionary's term rows. `images`: an entry's id to its picture's path in the zip."""
    images = images or {}
    rows, seq = [], 0
    seen: set[tuple[str, str]] = set()

    def add(expression: str, reading: str, gloss: list[str], score: int, tags: str = "名前") -> None:
        key = (expression, reading)
        if not expression or key in seen:
            return
        seen.add(key)
        rows.append([expression, reading if reading != expression else "", tags, "", score, gloss, seq, ""])

    for e in names.get("entries", []):
        seq += 1
        score = ROLE_SCORE.get(e.get("role") or "appears", 0) + (5 if e.get("kind") == "person" else 0)
        full = e["name"]
        reading = to_hiragana(e.get("reading", "")) if e.get("reading") else ""
        gloss = [card_gloss(e, notes.get(e["id"], {}), public, images.get(e["id"]))]
        for spaced in dict.fromkeys([full, full.replace(" ", "")]):  # in this order every time (a set is not)
            add(spaced, reading.replace(" ", "") if spaced == full.replace(" ", "") else reading, gloss, score + 2)
        parts = []
        if e.get("kind") == "person" and not (e.get("family") or e.get("given")):
            # A name of one part (パパス) takes the honorifics itself.
            parts.append((full, reading.replace(" ", "")))
        if e.get("family"):
            parts.append((e["family"], to_hiragana(e.get("family_reading", "") or "")))
        if e.get("given"):
            parts.append((e["given"], to_hiragana(e.get("given_reading", "") or "")))
        for alias in e.get("aliases", []):
            parts.append((alias["text"], to_hiragana(alias.get("reading", "") or "")))
        for text, read in parts:
            add(text, read, gloss, score)
            if e.get("kind") == "person":
                for h in HONORIFICS:
                    add(text + h, (read + to_hiragana(h)) if read and is_kana(h) else "", gloss, score - 1)
    return rows


def make_zip(key: str, rec: dict, names: dict, notes: dict | None, public: bool) -> bytes:
    """One title's names dictionary as the bytes of its zip (docs/titles.md). `notes` (short
    descriptions in the glosses) are left out of a public one: names, readings and aliases only."""
    notes = {} if public or not notes else notes
    # A dev line's zip carries the pictures that were fetched (titles.py portraits), as img/<id>.jpg.
    pictures: dict[str, bytes] = {}
    if not public:
        for e in names.get("entries", []):
            path = portrait_path(key, str(e.get("id", "")))
            if notes.get(e.get("id"), {}).get("image") and not e.get("spoiler") and path.is_file():
                pictures[e["id"]] = path.read_bytes()
    images = {entry_id: f"img/{portrait_path(key, entry_id).name}" for entry_id in pictures}
    rows = dictionary_rows(rec, names, notes, public, images)
    source = names.get("source", {})
    # What the zip holds besides the names' own date: the card's form (c1), the dev line's notes,
    # and its pictures (a zip with other pictures is another revision, so a device takes it).
    marks = "-c1" + ("" if public else "-dev")
    if pictures:
        digest = hashlib.sha256(b"".join(pictures[k] for k in sorted(pictures))).hexdigest()[:8]
        marks += f"-p{len(pictures)}.{digest}"
    from_vndb = bool(rec.get("vndb"))
    index = {
        "title": f'名前 · {rec["title"]}',
        "revision": f'{key}-{source.get("fetched") or source.get("drafted") or "hand"}' + marks,
        "format": 3,
        "author": ("VNDB contributors" if from_vndb else "Furoshiki's contributors") + "; made by Furoshiki's titles.py",
        "url": f'https://vndb.org/{rec["vndb"]}' if from_vndb else "",
        "description": f'The names of {rec["title"]}: its people, places and things, for this game only.',
        "attribution": "Character data from VNDB (vndb.org), Open Database License; contents under the "
                       "Database Contents License." if from_vndb else
                       "Names drafted by hand from the game and Japanese reference pages; not from VNDB.",
        # A title also read from lines (docs/titles.md, "lines") has the ID Furoshiki makes for that.
        "furoshikiScope": rec.get("ids", []) + rec.get("furochibi", []) + ([f"LINES-{key.upper()}"] if rec.get("lines") else []),
        "furoshikiTitle": key,
    }
    out = io.BytesIO()
    # A fixed date in every entry: the same names make the same bytes (the script list's SHA-256).
    def put(z: zipfile.ZipFile, name: str, text: str) -> None:
        z.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), text, zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        put(z, "index.json", json.dumps(index, ensure_ascii=False))
        put(z, "tag_bank_1.json", json.dumps([["名前", "name", -5, "A name in this game", 0]], ensure_ascii=False))
        put(z, "term_bank_1.json", json.dumps(rows, ensure_ascii=False))
        for entry_id in sorted(pictures):
            # Stored, not deflated: a JPEG does not shrink, and the popup reads one at a time.
            z.writestr(zipfile.ZipInfo(images[entry_id], (2026, 1, 1, 0, 0, 0)), pictures[entry_id], zipfile.ZIP_STORED)
    return out.getvalue()


def zip_name(key: str) -> str:
    """The file a title's names dictionary is, beside the game scripts and in the program."""
    return f"names-{key}.zip"


def cmd_build(args) -> int:
    out = Path(args.out) if args.out else BUILD
    out.mkdir(parents=True, exist_ok=True)
    recs = records()
    for key in args.keys or list(recs):
        rec = recs[key]
        names_path = NAMES / f"{key}.json"
        if not names_path.is_file():
            continue
        names = json.loads(names_path.read_text(encoding="utf-8"))
        notes_path = NOTES / f"{key}.json"
        notes = json.loads(notes_path.read_text(encoding="utf-8")) if notes_path.is_file() and not args.public else {}
        data = make_zip(key, rec, names, notes, args.public)
        zpath = out / zip_name(key)
        if not zpath.is_file() or zpath.read_bytes() != data:
            zpath.write_bytes(data)
        if not args.quiet:
            print(f"{zpath.relative_to(ROOT) if zpath.is_relative_to(ROOT) else zpath}: "
                  f"{len(dictionary_rows(rec, names, notes, args.public))} terms")
    return 0


def validate(names: dict) -> list[str]:
    """What in a names file does not follow docs/titles.md."""
    problems, seen = [], set()
    for e in names.get("entries", []):
        who = e.get("name") or e.get("id")
        if e.get("id") in seen:
            problems.append(f"{who}: id {e.get('id')} twice")
        seen.add(e.get("id"))
        if e.get("kind") not in KINDS:
            problems.append(f"{who}: kind {e.get('kind')!r}")
        if not e.get("name"):
            problems.append(f"{e.get('id')}: no name")
        if e.get("role") not in (None,) + ROLES:
            problems.append(f"{who}: role {e.get('role')!r}")
        if e.get("spoiler", 0) not in (0, 1, 2):
            problems.append(f"{who}: spoiler {e.get('spoiler')!r}")
        for field in ("reading", "family_reading", "given_reading"):
            r = (e.get(field) or "").replace(" ", "")
            if r and not is_kana(r):
                problems.append(f"{who}: {field} {e.get(field)!r} is not kana")
        for a in e.get("aliases", []):
            r = (a.get("reading") or "").replace(" ", "")
            if not a.get("text") or (r and not is_kana(r)):
                problems.append(f"{who}: alias {a!r}")
    return problems


def cmd_check(args) -> int:
    recs = records()
    owner = {i: k for k, r in recs.items() for i in r.get("ids", [])}
    missing = [p for pid, p in profiles().items() if pid not in owner]
    for p in missing:
        print(f'no title: {p["console"]} {p["id"]} {p["title"]}  ({p["path"]})')
    for key, rec in recs.items():
        names_path = NAMES / f"{key}.json"
        if not names_path.is_file():
            print(f"no names: {key}")
            continue
        names = json.loads(names_path.read_text(encoding="utf-8"))
        for problem in validate(names):
            print(f"bad entry: {key}: {problem}")
        unsure = [e["name"] for e in names.get("entries", []) if e.get("needs_check")]
        if not names.get("checked"):
            print(f"not checked: {key} ({len(names.get('entries', []))} entries, {len(unsure)} flagged)")
        # The public repository has no guide library: a guide is checked only where guides/ is.
        if rec.get("guide") and GUIDES.is_dir() and not (GUIDES / rec["guide"] / "guide.json").is_file():
            print(f'missing guide: {key} -> {rec["guide"]}')
    known = profiles()
    # The public repository has no eden/: a Switch title ID (16 hex digits from 01) is not
    # looked for where the Switch's profiles folder is not.
    no_switch = not (ROOT / "eden/src/android/app/src/main/assets/reader/profiles").is_dir()
    for key, rec in recs.items():
        if not isinstance(rec.get("lines", False), bool):
            print(f'bad record: {key}: "lines" is true or left out')
        for i in rec.get("ids", []):
            if i not in known and not (no_switch and re.fullmatch(r"01[0-9A-F]{14}", i)):
                print(f"no profile: {key} lists {i}")
    linked = {r.get("guide") for r in recs.values()}
    for g in sorted(p.parent.name for p in GUIDES.glob("*/guide.json")):
        if g not in linked:
            print(f"guide without a title: {g}")
    unmapped = check_service(args.service, recs) if getattr(args, "service", None) else []
    return 1 if missing or unmapped else 0


def check_service(base: str, recs: dict) -> list[str]:
    """Furochibi's games with no title: each OCR game of the service at [base] (its 範囲 games)
    must be in a title's `furochibi`, or its lines have no Notebook, names or guide."""
    try:
        with urllib.request.urlopen(base.rstrip("/") + "/api/regions", timeout=20) as answer:
            games = json.load(answer).get("games", [])
    except Exception as error:  # noqa: BLE001
        print(f"service not read: {base}: {error}")
        return []
    mapped = {i for r in recs.values() for i in r.get("furochibi", [])}
    unmapped = []
    for game in games:
        gid = f'ocr-{game.get("id")}'
        if gid not in mapped and game.get("name") not in ("", "Game"):
            unmapped.append(gid)
            print(f'no title: Furochibi OCR game {gid} {game.get("name")}  (add it to a title\'s "furochibi")')
    return unmapped


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list").set_defaults(fn=cmd_list)
    s = sub.add_parser("search"); s.add_argument("text", nargs="+"); s.add_argument("--platform")
    s.add_argument("--results", type=int, default=10); s.add_argument("--page", type=int, default=1)
    s.add_argument("--key"); s.set_defaults(fn=cmd_search)
    a = sub.add_parser("add"); a.add_argument("key"); a.add_argument("--title"); a.add_argument("--ids", nargs="+", default=[])
    a.add_argument("--vndb"); a.add_argument("--also", nargs="+", metavar="vN"); a.add_argument("--guide")
    a.set_defaults(fn=cmd_add)
    f = sub.add_parser("fetch"); f.add_argument("keys", nargs="*"); f.set_defaults(fn=cmd_fetch)
    b = sub.add_parser("build"); b.add_argument("keys", nargs="*"); b.add_argument("--out"); b.add_argument("--public", action="store_true")
    b.add_argument("--quiet", action="store_true")
    b.set_defaults(fn=cmd_build)
    n = sub.add_parser("notes"); n.add_argument("keys", nargs="*"); n.set_defaults(fn=cmd_notes)
    pt = sub.add_parser("portraits"); pt.add_argument("keys", nargs="*"); pt.set_defaults(fn=cmd_portraits)
    sp = sub.add_parser("spines"); sp.add_argument("keys", nargs="*"); sp.set_defaults(fn=cmd_spines)
    cv = sub.add_parser("covers"); cv.add_argument("keys", nargs="*"); cv.set_defaults(fn=cmd_covers)
    c = sub.add_parser("check"); c.add_argument("--service", metavar="URL",
        help="also a Furochibi service at URL: its OCR games without a title")
    c.set_defaults(fn=cmd_check)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
