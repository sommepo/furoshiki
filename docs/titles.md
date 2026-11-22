# Titles: one record per game or show, and its names

The maintainer, 2026-11-08: a proper-names dictionary for every game (as
characterdictionary.tokyo makes them), a Notebook of people, places and things, Furochibi's
playthrough guides, and "new games added to reader forks need to have their proper names added
to the dictionary from VNDB and this notebook page filled out too". The plan:
`https://claude.ai/artifact/3tJMjHaAxoNmwqxV8iEaHb`.

Four things name a game differently: Furoshiki by disc serial or title ID (one per disc),
Furochibi by an OCR game's id or `tv-<hash>` for a show, VNDB by `v2002`, the guides by a slug.
A **title record** ties them together. This file is the format every reader of it follows:
`tools/titles.py`, the shared engine's name dictionaries, Furochibi's Notebook (Furochibi only: the maintainer, 2026-11-08),
Furochibi's service and guides.

## `titles/<key>.json`

`<key>` is the title's slug, the guide's slug where it has one (`steins-gate`).

```json
{
  "title": "STEINS;GATE",
  "english": "STEINS;GATE",
  "ids": ["ULJM05718", "PCSG00160"],
  "furochibi": ["ocr-3f9a0c1d"],
  "jellyfin": [],
  "vndb": "v2002",
  "anilist": null,
  "guide": "steins-gate"
}
```

- `english` (2026-11-10): the title in Latin letters, as it is known in English or else in
  romaji (`Danganronpa V3: Killing Harmony`, `Kamaitachi no Yoru`), written by hand. Furoshiki 3's
  shelf sets every title in one display face, which has no Japanese (the maintainer: "if the
  japanese titles wont show in the same font, just show english titles"), so a title without it
  stands there in the Japanese text face instead. A new title gets one with its record.
- `jiten` (2026-11-10): the title's deck on Jiten (`113260`), which is how the maintainer's study
  tracker names it; Furoshiki 3 asks the tracker what was read of the title by that number
  (`home/docs/furoshiki-3.md`, "The study tracker"). `tracker_title` instead, the tracker's own
  `originalTitle` exactly, for a title the tracker has that Jiten does not. Set by hand, or by an
  exact match of the names; a title that is one part of what the tracker tracks (the first game of
  a collection) is linked by hand and said so here: `great-ace-attorney-chronicles` is the
  tracker's *The Great Ace Attorney: Adventures*, `apollo-justice-trilogy` its *Apollo Justice: Ace
  Attorney*, `eve-burst-error-plus` its *EVE Burst Error*.
- `screenscraper` (optional): the title's games on ScreenScraper by console,
  `{"psp": 103341, "psv": 574275}` (the consoles as below), which `tools/titles.py spines` and
  `covers` then go to at once for that console's box side and front; a bare number in an older
  record is the game of whichever console that game is. Written by those commands when they find
  a console's game by an exact match (one of the title's IDs of that console among the roms'
  serials there, or a game of that console whose name there is exactly the title's `english` or
  `title`, searched also with the punctuation made spaces); set by hand, after looking at the
  game's names and roms there, for a title ScreenScraper names otherwise (`digital-devil-saga`,
  `utawarerumono`, `kita-e`...). Never a looser match: a search for Persona 3 finds Persona 4 first.
- `libretro` (optional): the exact name, without `.png`, of the title's box front in
  libretro-thumbnails' `Named_Boxarts` of its console (`{"ps2": "..."}` where it has several), by
  hand, where the file's name before its brackets is none of the title's names.
- `aliases` (optional): other exact names of the title, searched for and matched like `english`.

**Box pictures** (2026-11-10; not in Git, as they are not ours to publish). **A spine or a cover
is only ever the box of the console the game is played on** (the maintainer, of a Switch shelf
with Muv-Luv's Xbox 360 spine and Trails in the Sky FC's Vita one: "important the console spine
and cover is not wrong"): a title has one of each for every console its `ids` are of, named by
that console as `spine_platform()` says it (`swi`, `ps1`, `ps2`, `psp`, `psv`, `drc`, `n3d`), and
none where that console's box is not found; another console's is never taken in its place.
`tools/titles.py spines [KEY...]` makes `titles/build/spines/<key>.<console>.jpg` (1000 high) from
ScreenScraper's `box-2D-side` of that console's game, else the tallest narrow `pkgside` of the
title's Japanese releases of that console on VNDB; `tools/titles.py covers [KEY...]` makes
`titles/build/covers/<key>.<console>.jpg` (at most 1400 high, its shape kept) from ScreenScraper's
`box-2D` of that console's game, else VNDB's `pkgfront` of a Japanese release of that console,
else libretro-thumbnails' box art of that console. A VNDB release of that console alone comes
before one also of another; a VNDB entry's own picture, which is no console's box, is never
taken. Japan's box comes first: where ScreenScraper has only another country's, libretro's
Japanese file or VNDB's Japanese release of that console is taken instead; another country's
box of the right console is better than none. Both need ffmpeg; ScreenScraper needs the
maintainer's account in `home/private/screenscraper.json` (in a worktree, the main checkout's);
libretro needs `gh`. A picture already there is kept: delete it to fetch it again. Furoshiki
builds both folders in and shows a game the pictures of its own console.
- `lines` (2026-11-10, optional, `true`): the title is also read from lines in Furoshiki (another
  program's WebSocket, the clipboard: `home/docs/furoshiki-3.md`, "Sources"). Its game ID there is
  made from the key, `LINES-<KEY>` (`LINES-STEINS-GATE`), which is not written in `ids` (no profile
  has it): `titles.py build` puts it in the names dictionary's scope, so the title's names are
  looked up while it is read that way. A title added in the app under exactly this record's
  `title` or `english` gets that ID whether or not the record says `lines`; without it only the
  names are missing.
  A window read by OCR (Furoshiki's wizard, `home/docs/furoshiki-3.md`, 8) is such a title too:
  the same `LINES-<KEY>` for a name that is exactly the record's, and no new field here (which
  window and where its text is are the device's own, in its `program/sources.json`).
- `ids`: every Furoshiki game ID of this title, all discs and editions, as the profiles name
  them (`<ID>.lua`). One ID belongs to one title only.
- `furochibi`: Furochibi's OCR game ids (its settings' `games`, as `ocr-<id>`), for games read by
  OCR. Filled in when the game gets its area (`tools/titles.py check --service <url>` lists the
  service's games without a title). A reader app's line is matched by its game's ID (`ids`); lines
  the service stored before it kept IDs were given theirs by hand (`python -m console_ocr
  game-ids`, its `HISTORY_GAME_IDS`).
- `jellyfin`: series names as Jellyfin gives them, for shows (names from `anilist`).
- `vndb`, `anilist`: where names come from; `null` when the title is on neither.
- `vndb_also` (only when there are more): further VNDB entries the title holds, for a collection
  of several games on one disc or cartridge (逆転裁判123 is `v711` with `v715` and `v716`). Their
  characters are fetched together: each takes its biggest role and least spoiler among them.
- `guide`: the guide's slug in `guides/src/guides/`, or `null`.

## `titles/names/<key>.json`: the names (may be published)

```json
{
  "source": {"vndb": "v2002", "fetched": "2026-11-08"},
  "checked": false,
  "entries": [
    {
      "id": "c6487",
      "kind": "person",
      "name": "牧瀬 紅莉栖",
      "family": "牧瀬", "given": "紅莉栖",
      "reading": "まきせ くりす",
      "family_reading": "まきせ", "given_reading": "くりす",
      "romaji": "Makise Kurisu",
      "aliases": [{"text": "助手", "reading": "じょしゅ"}, {"text": "クリスティーナ", "reading": "クリスティーナ"}],
      "role": "primary",
      "sex": "f",
      "spoiler": 0,
      "needs_check": []
    }
  ]
}
```

- `id`: VNDB's character id (`c6487`) for an entry that came from VNDB; an entry added by hand has
  `<kind>:<romaji slug>` (`place:akihabara`, `person:hazuki-ryou`). `fetch` rewrites the VNDB
  entries and keeps every other one as it is.
- `kind`: `person`, `place` or `thing`.
- `name` is written as the game writes it, with a space between family and given name when
  VNDB has one; dictionaries are made without the space as well.
- Readings are hiragana for Japanese names and the name itself for kana ones. They are made
  from VNDB's romanisation, which spells the reading out (`Okabe Rintarou`: おかべ りんたろう). A
  romanisation that does not convert plainly (macrons, a part count that differs from the
  written name) leaves the reading out and names it in `needs_check`: never guessed. A kanji
  alias is read with JMdict's first reading (助手: じょしゅ), or flagged `alias:<text>` when
  JMdict does not have it.
- `role`: VNDB's for this title (`main`, `primary`, `side`, `appears`); places and things have
  none.
- `spoiler`: 0 none, 1 minor, 2 major (VNDB's for this character in this title).
- `checked`: true once a person has looked the file over; `tools/titles.py check` lists the rest.
- `readings` (optional, at the top): readings of kanji words the aliases use, for whoever fills
  them in (`{"鳳凰院": "ほうおういん", "凶真": "きょうま"}`, `{"超高校級": "ちょうこうこうきゅう"}`).
  `fetch` reads an alias piece by piece: kana as written, kanji from this table, from the cast's
  own family and given names, or from JMdict for a word of two kanji or more; brackets and dots
  are left out of the reading (超高校級の「探偵」: ちょうこうこうきゅうのたんてい).
- A reading written in by hand (a person's, or an alias's) is kept when `fetch` runs again and
  VNDB's romanisation still does not give one: correct the file, not the tool's output.

A romanisation is converted part by part: a part written in kana is its own reading (桑田 怜恩,
"Kuwata Leon": れおん is not made from "Leon", so it is flagged; 城ケ崎 ノア gives じょうがさき ノア),
a name written without a space whose romanisation has several words is read as one (御剣怜侍:
みつるぎれいじ), and the kana a name is written with must be in the reading made, or it is
flagged (ジュース東郷 is not じゅうすとうごう).

Names, readings and aliases are VNDB's database (Open Database License, contents under the
Database Contents License: attribution in `home/publish/NOTICES.md` and in each dictionary's
`index.json`).

## `titles/notes/<key>.json`: descriptions and pictures (dev line only)

```json
{"c6487": {"description": "…", "image": "https://t.vndb.org/ch/34/97234.jpg"}}
```

VNDB says images and character descriptions "may be subject to separate license conditions", so
they are never published: `titles/notes/**` is in `publish.json`'s private paths. Descriptions
are kept spoiler-free: VNDB's `[spoiler]…[/spoiler]` passages are dropped.

A description is **the first paragraph that describes** (`clean_description`, since 2026-11-09):
VNDB's pages often open with something the character says, in quotation marks, with or without
who said it after, and that used to be all a note had (18 of Danganronpa V3's 81: 真宮寺 是清 was
"So… I have an interest in it…"). `is_quotation` passes such a paragraph over.
`tools/titles.py notes [KEY…]` fetches only the notes again (the people VNDB has; names, readings,
`checked` and notes written by hand are left alone).

**Pictures**: `tools/titles.py portraits [KEY…]` fetches each note's `image` once, makes it small
(200 px tall at most, with ffmpeg) and keeps it in `titles/build/portraits/<key>/<id>.jpg`: not
in Git, and never in a public dictionary. A new checkout or worktree has none until the command
is run there (898 pictures, 9 MB, some minutes); a dictionary built without them only lacks
them.

## Games VNDB does not have

RPGs and action games (Final Fantasy, Dragon Quest, Trails, Shenmue, Persona, Xenoblade...) are
not on VNDB. Their names start from the speakers already in the backlog (`reader.db`'s `lines`)
and from Japanese wiki pages; readings are drafted and the file stays `checked: false` until a
person has read it. Such a file's `source` says so:
`{"vndb": null, "drafted": "2026-11-08", "from": ["https://ja.wikipedia.org/wiki/…", "backlog speakers"]}`.
A title VNDB has without characters (July, 綾波育成計画, Twilight Syndrome) keeps its VNDB source
and gets its people by hand the same way. A reading no source confirmed is left out and named
in `needs_check`. Their notes are one or two sentences written for the Notebook, never copied.

`tools/titles.py check` also says when a names file does not follow this format (`bad entry`)
and when a record lists an ID no profile has (`no profile`).

## The dictionary made from it

`tools/titles.py search TEXT --platform psp --key KEY` lists VNDB's matches with their platforms
and the title already using each, and prints the `add` command for each hit.

`tools/titles.py build` makes one Yomitan zip per title, `names-<key>.zip`, titled `名前 · <title>`,
whose `index.json` has `"furoshikiScope": [ids...]` and `"furoshikiTitle": "<key>"`. Entries: the
full name (with and without the space), family name, given name, every alias, and the given name
and aliases with さん, ちゃん, くん, 様, 先輩, 先生; each tagged `名前`.

**An entry is a card** (since 2026-11-09, `card_gloss`; before: three plain senses, the name,
"Also: …" and a description). Its one glossary item is Yomitan structured content whose root
`div` carries, as `data`, what Furoshiki's popup draws (`shared/popup/README.md`, "A name's card"):

```json
{"type": "structured-content", "content": {"tag": "div",
  "data": {"furoshiki": "name", "id": "c53392", "kind": "person", "name": "真宮寺 是清",
           "reading": "しんぐうじ これきよ", "romaji": "Shinguuji Korekiyo", "role": "Main character",
           "also": "超高校級の「民俗学者」", "about": "In order to study folklore, …", "image": "img/c53392.jpg"},
  "content": [{"tag": "div", "content": "真宮寺 是清（しんぐうじ これきよ）"}, {"tag": "div", "content": "Main character"},
              {"tag": "div", "content": "Also: 超高校級の「民俗学者」"}, {"tag": "div", "content": "In order to study folklore, …"}]}}
```

`role` is VNDB's in words (main: Protagonist, primary: Main character, side: Side character,
appears: Appears), or Place or Thing; `also` the other names joined with 、. The `content` says
the same in plain lines, which is what every other reader of the format shows, the classic popup,
Furochibi's page and an Anki card's glossary. `about` (the note's first sentences, never for a
spoiler) and `image` (a picture fetched with `portraits`, in the zip as `img/<id>.jpg`, stored
not deflated) are the dev line's only: a `--public` zip has neither.

A zip's revision is `<key>-<date>-c1` and then `-dev` on the dev line, and `-p<N>.<hash>` when it
carries N pictures: a public zip's never has `-dev`, so a device that had the dev line's gets the
public one in its place and the other way round, and a zip with other pictures is taken as new.
The same names, notes and pictures make the same bytes.

**In the engine** (2026-11-08): a dictionary's `furoshikiScope` is kept with it (`reader_dict.db`
version 2, `dictionaries.scope`, `"|ID|ID|"`; the desktop, Android and Furochibi's bridge share the
file's form, and the Vita's read-only lookup reads either version). A lookup is told the running
game's ID: a scoped dictionary is used only while one of its games runs, an unscoped one always.
Among words of the same length a name of the running game comes first, and in a headword its
entries come before other dictionaries'; a longer word still wins (ダル does not hide だるい). The
popup tags such an entry 名前 (`shared/popup/README.md`); a card made from it is like any other.
Furochibi's bridge takes the game as a lookup's `"game"` (a Furoshiki game ID or Furochibi's own
id, as `ids` and `furochibi` list them).

## How the program gets the dictionaries

**Installed or brought up to date by the program itself, as a game starts** (`ReaderApp::Names`
in `shared/reader/desktop/names.*`, `NamesDictionaries.kt`): it looks for a `names-*.zip` whose
scope has the game, first in the user's profiles folder, then among what the build comes with, and
imports it unless the dictionary of that title is there at that revision (one of another revision
is replaced, switched on or off as the user had it). Where the zips come from:

- **A dev build** comes with all of them: Furoshiki's CMake runs `titles.py build` as it is
  configured (again when a title's files change) and puts them in as `:/reader/names/`; Furoshiki
  for Android's Gradle runs it too (`readerNames`) and puts them in the assets' `reader/names/`.
- **The published script lists** carry them: `tools/public-tree.py` makes each title's public zip
  (`titles.py`'s `make_zip`, no notes) beside its games' scripts in each console's folder, and
  `tools/scripts-index.py` names it in each of those scripts' entries (`"names": {file, size,
  sha256}`). The apps install it with the script (`Scripts::Folder::Install`, `ScriptStore`); when
  the list is read, an installed script's names that are missing or changed are fetched by
  themselves. A title none of whose games is in the list (each held back, or needing a character
  table Git has not) has no zip there: its names stay out of the public tree as its scripts do.

The Notebook is Furochibi's (the maintainer, 2026-11-08: "notebook should be in furochibi, not
furoshiki"): its service reads `titles/` itself (`furochibi/server/console_ocr/titles.py`).
