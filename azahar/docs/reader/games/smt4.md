# 真・女神転生IV / Shin Megami Tensei IV (Japan)

| | |
| --- | --- |
| Product code | CTR-P-AMXJ |
| Title ID | 0004000000088A00 |
| Verified build | Cartridge, NCCH version 2. Decompressed ExeFS code SHA-256 `b2d31adbf940d040d0a84106fb4f232577c70eec3c6f19ce5f192030aa6e8984` |
| Profile | `src/android/app/src/main/assets/reader/profiles/0004000000088A00.lua` |

The profile refuses any other build (it compares instruction words at its hook
addresses), so an update or another region gets no text rather than wrong text.

## What is reported

Every message shown in a text window, one event per **page**, at the frame the game
puts that page on screen:

- dialogue and narration, with the speaker name;
- the game's furigana, as ruby;
- names and numbers already filled in (the player's name, for example);
- choice lists and selection prompts, on the `choice` channel;
- the voice cue number of the page, in `meta.voice`;
- **where every character of the page is on screen**, for tapping words in the game's
  own picture: taken from the game's own drawing calls, so it follows the text as it
  is typed out. Checked against screenshots for the message window (with furigana,
  two lines, several pages), the prompt above a selection and the selection list on
  the bottom screen.

Checked in the headless harness from a new game through the two dream sequences, the
waking scene and the first town: 60-odd lines compared with screenshots, including
multi-page messages, the name entry prompts and NPC selection.

## What is not

- **Subtitles of the pre-rendered movies.** They are part of the video (the script
  files hold a differently worded text-window version of the same lines).
- **Text drawn without a text window**: menu and list entries, status screens,
  battle numbers. Not investigated yet.
- Battle messages and demon negotiation have not been played in the harness. They
  may well come through the same window code; this is unverified.

## How the game shows text

Messages are `MSG2` files (`*.mbm`, 1,168 of them, about 39,000 entries): an entry
table of `{id, size, offset}` and, per entry, Shift-JIS double-byte text with
two-byte control codes `F8xx` (little-endian arguments) ending in `FFFF`.

| Code | Arguments | Meaning |
| --- | --- | --- |
| `F801` | – | line break |
| `F802` | – | page break (wait for the player, clear the window) |
| `F804` | 2 bytes | text colour |
| `F811` | 2 bytes | insert a variable (0x32 is the name being entered) |
| `F812` … `0000` | – | speaker name, ended by a zero unit |
| `F813` | 8 bytes | voice cue (second 32-bit value) |
| `F816` / `F817` / `F818` | – | ruby: base starts / reading starts / end |
| `F814` | 4 bytes | not identified |
| `F841`–`F843`, `F870`–`F879`, `F87A`, `F87D` | 2 bytes | not identified (insertions, window options) |
| `F87B` | 8 bytes | not identified |
| `F87C` | 4 bytes | not identified |

All entries of all files parse with this table. Unidentified codes are skipped with
their arguments; if one of them inserts text that the expansion step does not fill
in, that text would be missing from the event.

Code (addresses in the loaded program):

| Address | |
| --- | --- |
| `0x00287468` | `TextWindow::SetText(this, style, a, b, [sp] c, [sp+4] text)`. Called once per message for every text window. `style` 4 is the message window, 3 the selection list. |
| `0x001EB398` | called from SetText: copies the message into a work buffer at `0x004DBF08` |
| `0x002D5DFC` | called next: expands variables into `0x004DDF08` and returns the number of 16-bit units, terminator included |
| `0x00287564` | the instruction after that call: the profile's second hook, where `r0` is the unit count and the buffer is final |
| `0x0029728C` | wrapper "set text from MSG2 entry": `[sp+4]` is the loaded file, `[sp+8]` the entry index. Its return address `0x002972D8` is what SetText sees when called through it; the wrapper's own caller identifies the context. |
| `0x002B3590` | `TextWindow::Step(this)`: reveals one more unit (the typewriter), handles control codes |
| `0x002B0AA4` | `TextWindow::Draw(this, flags; s0 = x, s1 = y, s3 = alpha)`: draws the units revealed on the current page. Called once per line of the page, every frame. |
| `0x002AA878` | `DrawGlyph(font, record + 4, colours; s0 = pen x, s1 = y, s2 = glyph width, s3 = height)`: draws one character. All fifteen callers are inside `Draw`. The profile's third hook: the record address says which unit of which window is being drawn. |

`TextWindow` fields used:

| Offset | |
| --- | --- |
| `+0x00` | records, 32 bytes per unit: `+0` the unit's code, `+4` texture, `+8` left bearing (signed), `+9` glyph width, `+10` advance, `+11` height, `+16` cell in the texture, `+28` sheet. They describe glyphs, not positions: positions exist only while drawing. |
| `+0x04` | unit count of the current message |
| `+0x08` | units revealed so far (the typewriter position) |
| `+0x0C` | unit index where the page on screen starts |
| `+0x10` | units revealed on this page |
| `+0x14` | font of the window style (`+0x54`: furigana font) |
| `+0x18`, `+0x1C` | extra spacing between characters and between lines (floats) |
| `+0x34` | flags; `0x10` set once the message is closed |
| `+0x44` | state byte: 1 typing, 2 waiting for the player, 5 closed |

The profile decodes all pages when SetText runs, emits the first, and emits each later
page when `+0x0C` reaches that page's first unit.

## Open items

- Play a battle, a demon negotiation, a shop and the camp menu in the harness and
  record which contexts go through SetText (the event's `meta.context` is the caller).
- Identify the remaining control codes from the game's parser rather than by
  frequency.
- `+0x44 == 2` means the game is waiting for the player: enough to build an
  "advance when the voice has finished" mode in the app.
