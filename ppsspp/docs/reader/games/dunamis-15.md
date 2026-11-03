# DUNAMIS15 (PSP)

5pb./MAGES., 2011: デュナミス15, a voiced mystery novel on a school island.

| Disc ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- |
| **ULJM-06119** | `6C2E2A66` | `assets/reader/profiles/ULJM06119.lua` |

**The profile hooks nothing**: it reads the game's memory once a frame, so it should also run
in Adrenaline Reader on the PS Vita, which does not support hooks (not tried there).

## What is read

- **The message window**: each line once it is typed out, waits for the button and has
  stood for four frames, with its **speaker** (the name on the window's plate, as shown:
  `？？？` before a character is introduced; narration and the prologue's voice have none),
  its **furigana**, and a cell for every character where the game drew it.
  - The game gives a reading to each kanji of a word on its own (in its script
    `[く]宮[どう]藤`); readings of characters next to each other are joined into one span
    (宮藤 くどう, 香藤瑞希 かとうみずき), so that a word read as a whole keeps its reading.
  - Rows the game broke because they were full go on without a break. A row that ends with
    room for three more characters would keep its break (a break of the writer's); none was
    met.
  - Words in colour (the TIPS keywords) are read as plain text.
- Nothing is pointable while the window is hidden (×), under the system menu (□), the
  backlog (△), the TIPS (L) or any other of the game's dialogs, nor between scenes; the cells
  come back afterwards without the line being read again.
- Nothing is read of what the skip (START, read text only) runs through; the line it stops
  at is read. Auto mode (SELECT) reads each line once.

## What is not read

- **Choices**: none came up in the first chapter as played (some 1,000 lines). The game's
  scene list names choice scenes (`…*select`) further on; how their answers are kept was
  not looked at. While a choice's dialog is up the message's cells are withdrawn (it is one
  of the game's dialogs), so nothing wrong is pointable, but a tap cannot pick an answer:
  the script names its buttons (`-- reader: buttons`).
- The TIPS (their list and their pages, L), the notice at the top of the screen when one is
  added (「…」が追加されました), the chapter titles, the backlog, the menus, the title screen.
- The name plate's own characters are not given cells (the name is the line's speaker).

## How the game keeps its text

The game is written in Squirrel (a scripting language; its scripts are `.nut` files) on a 2D
engine whose classes are named `M…` (`MTextRender`, `MMotion`, `MPSBFont`). The script hands
a line to the engine as UTF-8, and the engine lays it out into glyphs; **everything is in the
heap**, at places that differ from one start of the game to the next (the player object was
at `0x08abb450` in one run and `0x08ab8f10` in another), so the profile finds its way by the
objects' shapes. The program itself is not relocatable: its vtables are at fixed places.

- **The player** (class `MyPlayer`): a Squirrel instance (`SQInstance`, vtable `0x0897cfa8`)
  of 91 fields (memsize `0x2dc` at +0x28; no other instance is that size), fields from
  +0x2c, 8 bytes each (type, value). The profile finds it by a search of the heap (a few
  milliseconds), checks it every frame, and searches again once it is gone (the game makes a
  new one after going back to its title; searched for every 60 frames while there is none).
  Fields used: [31] `msg` (the window's script object), [34] `msgWaitState` (2 while the
  line is all there and waits for the button, 0 while it is typed), [38] `skipMode`, [46]
  `curSceneName` (only to recognise the object), [76] `msgwin` (the window's motion). Others
  seen: `msgWinState`, `textSkipMode`, `selectWindow`, `histories`, `curTextId`.
- **`msg`**: [1] `_text` and [2] `_name`, script objects whose native object (+0x20, an
  `SQTextRender`) holds the engine's `MTextRender` at +0x34 (vtable `0x0897b6d8`): the
  message (460×80) and the name plate (115×26).
- **`MTextRender`**: +0x114 a vector of rows, each a `std::deque` of glyphs (40 bytes: map,
  map size, start and finish iterators of cur/first/last/node); +0x120 the number of rows,
  +0x124 of glyphs, +0x128 of glyphs shown so far (it counts up while the line is typed);
  +0x144, +0x148 (floats) the text area's top left from the middle of the screen (−227, 52:
  (13, 188) on the 480×272 screen; the name plate's is (20, 160)).
- **A glyph**, 52 bytes, nine to a block of the deque: +0 a pointer to the font cache's
  entry for the character, whose first word is the code point; +4, +8 its place in the text
  area (floats: 17 apart in a row, rows 27 apart, a row after 「 indented by one); +12, +16
  its size (18×18); +20 its colour; +40, +44 a vector of its readings, 16 bytes each: a
  pointer to the reading as UTF-16, where it stands from the character (x, y), its size (8).
- **Whether the window is hidden**: `msgwin`'s native object (+0x20) holds the window's
  `MMotion` (vtable `0x08984eb0`) at +0x44; +0x54 is the clip it plays, as a place in the
  motion's data (+0x5c). Offset `0x1bcc` is the clip that hides the window: under ×, the
  system menu, the backlog and between scenes. `0x2200` is the window at rest, `0x1fbe` its
  coming in, `0x1e0a` the change from line to line (the same offsets after a fresh start).
- **Whether a dialog is over the text**: the story runs in a Squirrel thread (`SQVM`,
  vtable `0x0897e0e0`; the one whose stack holds the player: stack at +0x18, its top at
  +0x30, the root table at +0x38). While a dialog is up the thread waits inside it, so an
  instance of its class is on that stack (`TipsDialog` for L, `CommandDialog` for the menu,
  `HistoryDialog`…). The profile takes every class of the root table whose name ends in
  `Dialog` and looks for one of them on the stack each frame. The TIPS do not hide the
  window (its clip stays `0x2200`), which is why this is needed.
- The script's lines are also in memory as UTF-8 (the scene's string table, sorted by text,
  and a Squirrel string for the line being shown), with the markup `%0#rrggbbaa;…%0#;` for
  colour and `[reading]` before each kanji; the profile does not use them.

## Where the characters are

From the glyphs themselves: a cell is the text area's corner plus the glyph's place, as wide
as the step to the next character (17) and 18 high. Drawn over screenshots
(`tools/reader/layout_check.py`): every character sat in its cell.

## Buttons

○ advances (a press while the line is typed shows it whole), × hides the window, □ the
system menu, △ the backlog (□ there jumps back to a line, after a question), L the TIPS,
SELECT auto mode, START skip (only text read before; it stops at the first unread line).
At the title, START; new game is the first entry. The first start asks whether to look for
system data again (いいえ is lit).

## How it was checked

- **Harness** (the real game, 2026-10-27), all in the first chapter (プロローグ to
  「カフェに行く途中で」):
  - From a new game: the prologue's voiced lines (some 60, no speaker, then `？？？`) and on
    into the first ordinary scenes (高槻東吾, 白大輔, 後藤眞波, 陸七生, 倭一花, 仁和菓,
    香藤瑞希, 櫟井夏來, the 男生徒Ａ–Ｃ), circle pressed every 100–150 frames: runs of 56,
    207 and 91 lines read, each once, none in parts, speakers right; two lines of the same
    text by two speakers in a row both read. One line (「あなたたち仲がいいわね…」) came in
    one run and not in another from the same savestate; the frame-by-frame probe showed the
    game itself did not show it the second time (timing of the presses).
  - From a fresh start of the game (not a savestate): the first line read, the player found
    at another address, the window's clips at the same offsets (× hid the cells and brought
    them back).
  - Furigana: 宮藤 (くどう), names (香藤瑞希, 櫟井夏來, 倭, 仁和, 後藤, 眞波), 転校生 read
    ペリフェリエス.
  - Cells over screenshots: a one-row line with readings and coloured words, a two-row
    spoken line with a name, a two-row narration line, a line with a full-width space.
  - × (hidden and back), □ (the menu and back), △ (the backlog and back), L (a TIPS page,
    its list, back): the cells went and came back, nothing read again. START after jumping
    back through the backlog: the skipped lines not read, the line it stopped at read.
    SELECT: eight lines read once each. Back to the title and a new game: read again.
- **Not looked at**: choices (none reached); a row broken by the writer; half-width
  characters; the game's configuration (text speed, instant text); a game continued from the
  game's own save (only savestates and new games); the desktop app, Furoshiki, any device,
  Adrenaline on the Vita.
- **Tests**: none.

States in `private/dunamis15/states/` (the maintainer's machine): `newgame` (the first line
being typed), `fresh_line1` (the first line, from a fresh start), `line2` (a line of two
rows), `sempai` (a `？？？` line; the hide, menu and backlog checks), `p3` (the first
ordinary scene), `p5` (narration of two rows), `cafe` (a TIPS word in colour, the notice),
`ruby` (宮藤 with its reading), `jumped` (after a backlog jump; the skip check).
