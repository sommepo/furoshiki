# レイトン教授VS逆転裁判 / Professor Layton vs. Phoenix Wright: Ace Attorney (Japan)

| | |
| --- | --- |
| Product code | CTR-P-AVSJ |
| Title ID | 0004000000078B00 |
| Verified build | Cartridge, NCCH version 2. Decompressed ExeFS code SHA-256 `02f1d848d5e43c15274ee798af91600ce4bf186efcf4ba090b961b5b5709b1ee` |
| Profile | `src/android/app/src/main/assets/reader/profiles/0004000000078B00.lua` |
| Title record | `titles/layton-vs-ace-attorney.json` (VNDB v15119), guide `layton-vs-ace-attorney` |

The profile refuses any other build (it compares instruction words at its hook addresses).

## What is reported

One event per text box, at the frame the game starts showing it, **with the place of every
character on screen** (`reader.layout`), so that a word can be tapped in the game's own picture:

- dialogue and narration in the message box on the top screen, with the speaker's name;
- the subtitles of the animated scenes (white text on the lower screen), with the speaker;
- the banners as an investigation begins (the place, レイトンの研究室, and the task,
  本棚を調べてみよう) and what is said when a spot is examined;
- a puzzle's texts: its title (魔女のナゾ), its question on the lower screen, the instructions
  page on the upper one, and the task in the bar over the puzzle (それぞれのお城へ導こう！);
- the game's furigana, as ruby; names and numbers put into a message (`%s`), filled in.

Checked in the headless harness (2026-11-14 and 15) from a new game: the name entry, the opening
film, the prologue's first scene, the animated scenes in Layton's study and on the aeroplane, the
talk in the study, the investigation tutorial, examining the bookshelf, and Puzzle 01 up to its
board, about 70 texts compared with screenshots: text, line breaks, furigana and speakers right,
one event per box. The cells were drawn over screenshots with `tools/reader/layout_check.py` for
the message box (two lines, furigana), a subtitle on the lower screen, the puzzle's question
(seven lines with a blank one) and instructions (in a frame on the upper screen) and the task bar
(which slides in: its place is followed every frame): every cell on its character. A box no
longer drawn is closed within three frames.

## What is not

- **Court.** Testimony, the つきつける/ゆさぶる prompts and the answers to choose from were not
  reached in the harness: the Layton half of the prologue has five puzzles to solve by stylus
  first (the guide, `layton-vs-ace-attorney`, lists them), and no way to jump ahead was found (the
  script loads were not caught by the path-building functions). What is known: every text box
  is filled by the one SetText the profile hooks, whose only caller is the script's set-text
  command (all call sites were listed); the court's strings are in the same script files with
  the same markup and the speaker before each line (`txt/jp/11/11_110070.xs`: ガードナー's
  testimony, 「～事件当夜の見回り～」, then each statement). An answer to choose from is a string
  of its own (`<T>窃盗と傷害`), so it would come as a line of its own on the dialogue channel, not
  on `choice`; that, and where the answers' boxes are, is unverified.
- Text drawn as pictures: the chapter titles on the book's pages, the name plates, the
  puzzles' "PUZZLE! ナゾ01" card and the ひらめき指数 board.
- Text of size tags (`<YS>`, `<YL>`) is placed by the same 16-pixel grid as the rest; such a line
  was not seen on screen.
- The puzzles' hints and answers screens were not opened.

## How the game shows text

All the game's data is in one archive, `vs1.fa` in the RomFS: Level-5's **XFSA** (a header of
ten words: magic, the offsets of the directory table, the directory hashes, the file table, the
names and the data, the directory and file counts; each table is in Level-5's compression, a
word of `size << 3 | method` first: 0 none, 1 LZ10, 2/3 Huffman, 4 RLE). A directory entry is
16 bytes (`crc32`, `fileNameStart << 14 | fileCount`, `firstFile:u16`, `firstDir:u16`,
`nameOffset << 14 | dirCount`), a file entry 12 (`crc32`, `name_hi << 26 | offset >> 4`,
`name_lo << 23 | size`, the name nine bits: `name_hi << 9 | name_lo`, relative to its
directory's first name).

The scripts are `txt/jp/<chapter>/<chapter>_<scene>.xs` (1,494 files, Level-5's **XSCR**). The
word at `+0x10`, times four, is the offset of the string table, LZ10-compressed: Shift-JIS strings
ended by a zero byte, in the order the script uses them. A message is one string:

| Markup | Meaning |
| --- | --- |
| `\n` | line break (a message has at most two lines in the box; up to nine on the puzzles' instruction pages) |
| `[漢字/かんじ]` | furigana |
| `[btn_000]`, `[hint01]` | a picture of a button or a hint coin, nothing to read |
| `<W30>` | a pause in frames |
| `<R0.2>` … `</R>` | the typing speed |
| `<CR>` … `</C>`, `<CN>`, `<C#FF>` | text colours |
| `<YS>`, `<YL>`, `<YWS>`, `<YWL>` … | text size and shaking |
| `<ME"マヨイ"/1/1/1>`, `<M6/6/6>`, `<MS"こまり">` | faces and gestures of the people on screen |
| `<V00010>`, `<S212>`, `<F>`, `<K>`, `<T>`, `<Q>`, `<SEQ…>`, `<BGM…>` | voice, sound, flash, thought, page start, and sequence and music commands |

The decoder drops every `<...>` tag and keeps `[base/reading]` as ruby. The strings of the
animated scenes' subtitles are not in these files (they come with the scenes' sequences); they
come through the same call at run time.

Speakers: the name plate is a picture, but the script keeps the name as a string of its own. In
the investigation scenes the message is a script variable (argument type 4: a number,
`0x002233E0` finds its value) and the variable before it holds the name; elsewhere the message is
the string itself and the name is the string kept right before it in the table (`チェルミー`, then
the line). The first string of a table has the table's index of string addresses before it, not a
zero. Either is taken when it is a bare name (double-byte characters only, at most twelve). The
script names the speaker again for every line; narration, banners and puzzles have none.

Code (addresses in the loaded program):

| Address | |
| --- | --- |
| `0x00194D94` | The script's set-text command: `r1` the node to fill, `r2` the script's context, `r3` its arguments (the message: a string, or type 4, a variable), `[sp]` how many. Its callers are the script's command table (`0x001934BC`, `0x00193C10`). |
| `0x002089EC` | The wrapper it calls SetText through: `r0` the node, `node+0x138` the window. The only caller of SetText; its return address is `0x00208A20`. |
| `0x001C68E0` | `MessageWindow::SetText(window, pos, text, alignment, ...)`. With a `%s` argument it first formats the text into a buffer in the window (`+0x4C4`). |
| `0x001C69A8` | inside it, `bl` to the layout (`0x001C91B0`) with `r0` the window and `r2` the final string: where the profile reads the message. `0x001C69AC`, right after, is where the layout is done. |
| `0x001D2BE4` | `AddQuad(mesh, &topleft, &bottomright, &uv)`: one call per character drawn (furigana too), in the order of the window's character list, with the glyph's corners in the window's own pixels. |
| `0x001CAF9C` | `MessageWindow::Draw(window, ...)`: every frame the window is shown. |
| `0x002233E0` | The script machine's variable lookup (ranges from `0x3E9`, `0x7D0`, `0xBB8`, `0xFA0`). |
| `0x001C2C78`, `0x001C8800` | the two parsers of the markup (they compare with `[`, `/`, `<`, `>`), the first measuring, the second building the character list. |

Window fields: `+0x94` the character list (8-byte entries: type 0 a character, 1 a character
under furigana, 2 a furigana character, 3 the end of furigana, 5 a line break, 6 a pause, 12 a
speed, 13 the end), `+0x9C` how many; `+0xF8`/`+0xFA` and `+0xFC`/`+0xFE` the laid-out text's
offset and size. Node fields: `+0x28`/`+0x2C` its own translation, `+0x58`/`+0x5C` the one it is
drawn with (its parents' included: a puzzle's instructions sit in a frame), `+0x12D` the screen
(0 upper, 1 lower), `+0x138` the window. A character is 16 pixels wide and a line 24 high; the
profile snaps each character's quad to that grid from its line's left edge, from the line's
baseline up.

## The harness, for this game

`drag X0 Y0 X1 Y1 [frames] [after]` (added for it) draws a stroke on the lower screen: the
investigation's magnifier only moves with one (about 1.4 pixels on the upper screen for each pixel
of stroke). `actions` and `action N` run a profile's `reader.action`, as in Eden Reader's harness.
`layout_check.py` takes the engine's layouts as they are now (no `screen` for the upper one).
A harness built in a `git worktree` stamps its states with the revision `UNKNOWN` and refuses
to load them again: give its build's `src/common/scm_rev.cpp` the commit's hash (the build is
configured once, so it stays) or build in the main checkout.

States in `private/lvp/states/`: the name entry, the prologue (before the first line), the first
line, an animated scene, the study, the investigation, the magnifier on the bookshelf
(`atbooks`), and Puzzle 01's board.

## Open items

- Court: reach it (solve the prologue's puzzles, or find where the next scene is chosen) and look
  at the testimony box, the prompts and the answers; put the answers on the `choice` channel.
- Lines with size tags, the hints screen, the Story and Puzzle Index.
