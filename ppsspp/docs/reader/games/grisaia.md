# グリザイアの果実 -LE FRUIT DE LA GRISAIA- (PSP)

Prototype/Frontwing, 2013. Two discs, one game:

| Disc | What is on it | Disc ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- | --- | --- |
| 1 | The install data (pictures, music, sound, part of the voices) | **ULJM-06232** | `CAB4E2B4` | `assets/reader/profiles/ULJM06232.lua` |
| 2 | The other voices (`VOICE0.PAK`, 1.3 GB) and the films (opening, the five endings) | **ULJM-06233** | `B234BF0B` | `assets/reader/profiles/ULJM06233.lua` |

**How the game is played.** Disc 1 installs its `INSDIR` onto the memory stick (the save
folder `ULJM06232PAK`, 645 MB; in the harness that took no time to speak of) and then says
「DISC2をセットして下さい。」. With disc 2 in the drive the title comes up (はじめから,
つづきから, 環境設定, アンインストール) and the whole story is played from there: **the story
is not on either disc as a part of its own**. The script of the whole game is inside the
program (it is in memory from the start, every route's scenes in it). Disc 2 started first
says 「DISC1をセットして下さい。」. In PPSSPP that is *Swap UMD* in the pause menu, once the
game asks.

**The two programs are one program built twice**: the two `EBOOT.BIN`s differ, but the code
in memory is the same word for word and so are the places of everything a profile reads
(compared in the harness). The game's ID that PPSSPP (and the reader) go by stays the one of
the disc that was started, also after a swap, so either profile can be the one that reads:
**the two profiles are the same file below the lines that name the disc**, and each accepts
both builds. Start disc 1 (it installs, then asks for disc 2) and its profile reads.

**No hooks**: the profile reads memory once a frame (`-- reader: no hooks`), so it is fit for
Adrenaline Reader on the PS Vita as it is.

## What is read

- **The message window**: each line once it is typed out and has stood for four frames, with
  the **speaker** (the game draws the name in front of the line, `雄二「…」`; the name is not
  part of the text) and a cell for every character. A line is one event however many rows
  it takes (one to three): the game breaks rows by itself.
- **Choices**: the answers as one event of the channel `choice`, an answer to a line, with a
  cell for every character, and the answer the cursor is on (`reader.cursor`), so that a tap
  on an answer picks it in touch play. The message window is away while the answers are up.
- The game's own marks come out as they are: `!!`, `!?` and `!!!` (one byte each, drawn
  together in about one cell) stay `!!`, `!?`, `!!!`; the full-width space between
  sentences stays.

## What is not read

- **Furigana**: none was met. The line's text has no readings in it in what was played, and
  the game's text parser knows no command for them (below).
- An older line the game shows again: **L and ↑ step back through the story** (the game
  goes back to that line, picture and voice too); ○ and ↓ step forward again. Those lines
  were read when they were new and are not read again, and they get no cells.
- The message log (□), the menu (△), the settings, the quick load, the yes/no questions:
  nothing is read under them and the line's cells are taken away while one is up; the line
  comes back without being read again.
- What a skip runs through (a line has to stand four frames once typed out).
- The title screen, the install screens, the films, the eye-catches between scenes.

## How the game keeps its text

Everything is at a fixed place: the game's objects are global (the classes' names are in
the program, `cObj_MessageWin`, `cObj_SelectWin`...), and the program is not relocatable.
**Its module begins at `0x08804018`**, not `0x08804000` as most PSP games, which is the base
the profile counts from (`reader.base - 0x08804018`).

- **The line**, `0x08dcc290`: the line on screen, NUL-ended, Shift-JIS with two
  differences: **the hiragana are the single bytes of half-width katakana** (`ｱ` is あ,
  `ｶ` is か, `ｦ` を, `ｯ` っ, `ｰ` ー; the hiragana with dakuten are ordinary two-byte
  Shift-JIS: が, げ), and `｡｢｣､･` are 。「」、・. A speaker is `$t`, the name, `@` in front
  of the line. The script itself (all of it in memory from `0x089ae574` on) is shorter
  still: `` `24@ `` is the speaker number 24 and `~8` a word from a table (警官); the line at
  `0x08dcc290` has them written out.
- **The message window**, `0x08de4850` (`cObj_MessageWin`):
  - `+0x08`: 1 while the window is in use; 0 for one frame between two lines, and while
    there is no window (films, scene changes).
  - `+0x3c`: where the line is in the script (a pointer). The profile tells a line from the
    same text shown again by it.
  - `+0x1f8`: 0x30 while the line is being typed out, 0 once it is.
  - `+0x200` (1 or 2 once typed out), `+0x204` (a frame count while typing), `+0x208`: not
    used.
- **Stepping back**, `0x08e179dc`: 1 while the game shows an older line again (L, ↑), 0
  once it is back at the newest.
- **The other windows**, each alive while the byte at `+8` is not 0: `cObj_SystemMenu`
  `0x08df9340` (△), `cObj_BackLogWin` `0x08de41e8` (□), `cObj_ConfigWin` `0x08df66f8`,
  `cObj_QuickLoad` `0x08df8e80`, `cObj_AskWin` `0x08dccad8` and `0x08dcd4b8`.
- **The choice window**, `cObj_SelectWin` `0x08de72d8`: `+0x08` is 1 while the answers are
  up, `+0x668` how many there are, `+0x680` the one the cursor is on (from 0; ↓ from the last
  goes round to the first), `+0x9b1` the answers as the script has them, NUL-ended, with
  `$d` between them (ordinary Shift-JIS there, without the single-byte hiragana). The first
  answer is lit when they come up.

**The script** is a row of commands of two bytes and arguments, the second byte saying how
many pairs of bytes the command takes in all (`13 15 …` is a line of 42 bytes). `0x13` is a
line, `0x12` a voiced line, `0x14` a choice (its handler in the interpreter is at
`0x088afa44`; the table of handlers at `0x088ebdf4`, from `0x12` on). The script in memory has
**12 choices**, numbered by the game from 0 to 11; the first two (幸を手伝う／様子をみる,
then three answers about a nickname) are in the common part, some 8,400 and 9,600 lines in.

The text parser (at `0x08829f34`) turns the script's `` ` ``, `~` and `$` into the line;
after `$` it knows `d`, `t`, `v` and `V`. Met: `$t` (the speaker), `$d` (a line break, in
the answers), and `$S` with three digits (the size of the letters: `$S022` a shout, `$S012` a
whisper; `$S` alone puts it back; four lines of the whole script, and one answer, have it).
The profile says so in its log for any other.

## Where the characters are

From the vertices the game draws its characters with (read out of its display lists in the
harness), and screenshots:

- The first cell is at x 98, y 201; rows are 24 pixels apart (y 201, 225, 249), a cell 17
  high. A row is 368 pixels wide (23 full-width characters).
- A full-width character is 16 pixels wide, **the full-width space 8**, `!` 4.5; other
  characters of one byte are taken as 8 (`?` looked right with it; none other was met).
- The game breaks a row when the next character does not fit, **unless it is 、 。 or 」**
  (seen), which then stands after the full row as a 24th character; the profile does the
  same for 』 ） ！ ？ ， ． (not met).
- The speaker's name takes the first cells of the first row (`雄二「` is three cells); the
  line's cells begin after it.
- **A line in large or small letters** (`$S`) gets cells of its letters' size, which are only
  near: the game spreads such a row out to fill its width and puts the next row lower. The
  one met (`$S022`, 「たーーーすけて…」) was looked at; its cells are off by up to half a
  letter.
- **The answers** of a choice are in letters of 18 pixels, each answer centred at x 240; their
  rows are 56 pixels apart around y 122 (two answers at y 94 and 150, three at 66, 122 and
  178: both measured).

## Buttons

○ (and ↓) go on; on a line being typed out the first press finishes it. △ is the menu
(セーブ, ロード, 環境設定, タイトル, クイックセーブ, クイックロード; × closes it), □ the
message log (× closes it), SELECT the auto mode, L and ↑ step back a line, R held runs forward
again (as far as the story has been, and no further: it is the skip of what was read). ×
goes on like ○. START showed nothing in the harness. At a choice ↑ ↓ move the cursor and ○
picks.

**Played by touch** (`-- reader: touch press`): a tap beside the text presses ○, a tap on an
answer moves the cursor to it and presses ○ (the cursor is reported). Not tried on a
device.

## How it was checked

- **Harness** (the real game, on 2026-10-27), with the program of disc 1 running and disc 2
  in the drive (how the harness got there: below):
  - From はじめから: the first 40 lines one by one with a screenshot each (the opening
    narration, the policeman, 雄二), then 150 more (the bag snatcher, the police station),
    every line read once, the speakers right (警官, 雄二, 若い女性, 男, 刑事, 年配の刑事).
  - Then ○ pressed eight frames apart with the profile loaded, some 27,000 presses: about
    9,400 lines more, up to the second choice (the nickname, a little after the
    karaoke/焼肉 scenes), 46 speakers (雄二, 天音, みちる, 蒔菜, 幸, 由美子, 学園長, ＪＢ,
    カルビ将軍, マグロマン…). No line twice running but one that the game shows twice
    (由美子「………」), no line of more than three rows, no `$` command but `$t` and one
    `$S022`, nothing read on the title, the install and disc screens, the eye-catches.
  - **Both choices met**: 幸を手伝う／様子をみる and the three answers about a nickname. Each
    read once with its cells, the cursor followed ↑ ↓ (0, 1, 2 and round to 0), the answer
    picked and the next line read; under the menu over a choice the cells went and came back
    without the answers being read again.
  - **Cells drawn over screenshots** (`tools/reader/layout_check.py`) and looked at: lines of
    one, two and three rows, narrated and spoken; rows that end with a hanging 、 。 」; the
    lines with `!!`, `!!!` and `!?`; both choices (two and three answers); the line in
    large letters (near only, above).
  - Under the menu and the log the cells went and came back; L and ↑ stepped back several
    lines and ○ went forward again, and R held ran forward to where the story had been:
    nothing was read twice, the next new line was read.
  - **With disc 2's own program** (build `B234BF0B`, as when disc 2 is started first and the
    discs are swapped as it asks): the title, a new game, the first 60 lines with their
    cells, read by `ULJM06233.lua`. That new game begins with the window up and an older
    text of the game's (ボタン操作…) in the line for a few frames, before it has a place in
    the script: the profile waits for the place, and nothing is read then.
  - **Disc 1's program** (build `CAB4E2B4`) loads `ULJM06232.lua` (the build check passes)
    on its disc screen; its story was played in the harness as said above.
- **Disc 1 by itself**: started from a new memory stick, it installed and asked for disc 2.
- **Disc 2 by itself**: started with the install data in place, it asked for disc 1, and
  given disc 1 asked for disc 2.
- **The swap of discs was not done the way a player does it**: the harness has no command
  for it. A state of the program waiting for the other disc was loaded into a harness with
  that disc in the drive, and the game's flag that the disc changed (its UMD callback sets
  the byte `0x08e15bd5`) was set by hand; from there on everything was the game's own. So in
  the harness of disc 2 the reader took disc 1's program for disc 2's (the ID and build are
  the ones it started with), which is how `ULJM06233.lua` read most of the above; the code
  and places are the same (compared word for word), and disc 1's own start loads
  `ULJM06232.lua`.
- **Not looked at**: the ten later choices (one of them has an answer in letters of 16
  pixels, `$S016`, which the profile lays out at that size), any route, the endings and
  films, the menus' own text, the game's save and load screens, a skip over lines read in an
  earlier playthrough (only the run forward after stepping back), the auto mode with the
  profile, a state loaded in the middle of a line being typed; PPSSPP's own *Swap UMD*; the
  desktop app and any device.
- **Tests**: none of the profile itself.
