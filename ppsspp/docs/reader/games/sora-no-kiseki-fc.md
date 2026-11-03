# 英雄伝説 空の軌跡FC (PSP)

Nihon Falcom, 2006: the PSP port of the PC role-playing game (The Legend of Heroes: Trails
in the Sky). Not a visual novel: the text is the talk windows over the 3D field, of the
story's scenes and of whoever Estelle talks to, between walking, menus and battles.

| Disc ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- |
| **ULJM-05170** (disc version 1.01) | `6C535B6A` | `assets/reader/profiles/ULJM05170.lua` |

**The profile hooks nothing** (`-- reader: no hooks`): it reads the game's memory once a
frame, so it should run unchanged where hooks are not offered (Adrenaline Reader on the
Vita; not tried there).

## What is read

- **The talk windows**, each page once it is typed out and waits for the button (three
  frames of standing), with its **speaker**: the name on the window's plate. That is the
  character's own name, or the name the script gives for this one window (男性の声 for a
  voice off screen); a window without a plate (a system line such as ５００ミラを貰った。)
  has no speaker. The story's scenes and talking to someone in the field are the same
  window and are both read; the same text shown again (talking to someone twice) is read
  again.
- A row the writer broke stays broken (the game has no wrapping of its own).
- **Furigana**, where the text gives them (`漢字#4Rかんじ#`, Falcom's form): as readings over
  their characters. **Checked with made-up text only** (a line written into the game's
  buffer in the harness); the opening has none in its talk, and the only ones seen in the
  game's memory were in a book, which is not read.
- **Choices** (the script's menus: 休憩する／やめる at Estelle's bed): the answers as one
  event of the channel `choice`, an answer to a line, with a cell for every character, and
  the cursor (`reader.cursor`) following the d-pad.
- A cell for every character (`reader.layout`, the 480x272 screen), in the windows wherever
  the game puts them: below or above the speaker, the face on the left, on the right or
  none, letters of the larger size (`#3S`).
- The game's heart (drawn for the NEC code ㈱) is reported as ♥.

## What is not read

Not asked for, and not the talk window (each would be a piece of work of its own):

- the camp menu (START) and everything in it: items, equipment, orbments, their
  descriptions, the notebook, the status pages;
- battles: their menus, numbers and messages (whether a talk window in a battle, such as
  a tutorial's, is read was not tried);
- **books** (リベール通信, the novels): they have a page of their own, and their text is
  kept compressed;
- shops, the place names that come up on entering an area (エリーズ街道, ブライト家), the
  title screen, the opening film;
- **item names put into a line by the script** (Falcom's code 0x1F and an item number): the
  profile leaves the name out; the line is read without it. None was met in the opening.

Nothing is read under menus: the talk window cannot be open while a menu is (START does
nothing during a talk; seen), and a page is read only while it waits for the button.

There are no voices in the talk (`-- reader: audio never`). The profile does not say
`-- reader: touch press` (a role-playing game is not played by tapping the text) and has no
`-- reader: buttons` line, which is only of use with touch play.

## How the game keeps its text

The executable is not relocatable; the addresses below are this build's.

- **A talk** (the text of one window, all its pages) is kept as Shift-JIS in a buffer of its
  own (in the heap) from `+0x40`, with Falcom's codes in it: `#nnnF` a face, `#nP` where the
  window stands, `#nS` the size of the letters (2 at first), `#nnW` the speed of typing,
  `#nA` (seen once, meaning not known), `漢字#nRかな#` a reading over the `n` half-width cells
  before it, and the bytes `0x01` (a new row), `0x02` (the wait for the button), `0x03`
  (the page is cleared). Before the text: `+0x10` the window's `#nP`, `+0x14` the speaker's
  character object (its name, Shift-JIS, at `+0xe0`), `+0x28` flags (`0x8000` while the
  window closes, `0x40` the face on the right, `0x20000` a name given by the script, which
  is then at `+0x440`).
- **The window** is an object of the class `CMessageWindow` (vtable `0x08a111e0`): `+0x78`
  how far the text has been typed (one past the `0x02` while the page waits), `+0x7c`
  where the page began (at its `0x03`, or at the talk's first byte), `+0x80` the talk's
  buffer, `+0x38` its drawing record. The story's windows are made in the heap (two of
  them, one opening while the other closes) and move between scenes; system lines use one
  at `0x08c39bf4`. **The game's list of the open windows** is at `0x08b87820` (pointers; the
  profile looks at twelve and takes those whose vtable is the class's). In the script, a
  window is opcode `0x5b` (character, text) or `0x5c` (character, name, text).
- **The drawing record**, at `+0x28c8` in the object `+0x38` points to: a byte (4 while the
  window is up, `0x0c` while it closes), then the point the window is placed from (x and y,
  two bytes each, pixels of the screen), the number of half-width cells of its widest row,
  its width and height. The window's place on screen is not kept anywhere else (it is worked
  out again every frame); the text's place follows from that point.
- **The menus** (choices): eight objects of the class `CED6Menu` (vtable `0x08a07c48`) side
  by side from `0x08c399b0`, `0x40` bytes each, each with its drawing record at `+0x38`:
  the answers from `+0x8c`, Shift-JIS, `0x50` bytes apart; the cursor at `+0x2898` (from 0);
  at `+0x28c8` a byte that is 6 or 7 while the menu is up (`0x8d` while it closes, 0
  otherwise), the menu's x and y and the number of answers. In the script the menu is
  opcode `0x5d`, its answers one string with `0x01` between them.

The game draws each row of a window as one picture (the row's string drawn into a texture
by the font), so there is no list of characters with places: the cells follow from the
text and the measurements below.

## Where the characters are

Measured in the game's drawing lists (the rows' quads) and checked over screenshots:

- The first row's first character stands at the record's point **+22 in x** without a
  face, **+64** with the face on the left, **−20** with it on the right (the face is 84
  wide; whether it is on the right is the flag `0x40`; whether there is one at all, a
  `#nnnF` in the talk), and **+37 in y**, with or without a name plate (the name is 24
  above the first row).
- A character of size `n` is `6 + 4n` pixels wide and high: 14 at first, 18 at `#3S` (the
  other sizes are guessed from those two); half-width characters take half. Rows are 8
  pixels apart (22 at the first size; at a larger size the gap is guessed the same).
- Answers of a menu: the first at the menu's point +17, +17; answers 18 apart.

## Buttons

○ talks, examines, advances; START opens the camp menu; × and △ did nothing in the house.
The left stick walks (the d-pad too, turned 45° to the screen). At the title, the cursor
starts on LOAD: up, then ○ for START. LOAD (and starting with the cursor left on it) opens
the PSP's own list of saves; in the harness PPSSPP draws that screen without its text and
it wants a second between presses (× leaves it).

## How it was checked

- **Harness** (the real game, 2026-10-27), all from a new game:
  - The prologue (Estelle waiting for Cassius, the boy he brings home, the boy's first
    words) and the first chapter's opening (the morning, breakfast, the talk before leaving
    for the guild), some 200 pages, circle pressed at each wait with the profile loaded
    and every event kept: each page reported once, none missing against the text the
    window showed, speakers エステル, カシウス, 男の子, ヨシュア, 男性の声 / 男の声 (names the
    script gives), a system line without a speaker (５００ミラを貰った。).
  - From a fresh start of the program (not a saved state): the first pages read, at the
    same addresses.
  - **In the field**: Cassius talked to at the table, a talk of several pages, then again
    (his repeated line read each time); Estelle's bed examined: the choice 休憩する／やめる
    read, the cursor following the d-pad (0, 1, 0), the cells gone when it closed.
  - **Cells drawn over screenshots** (`tools/reader/layout_check.py`) and looked at: a
    window below the speaker with the face on the left (one and two rows), one above the
    speaker, the face on the right (カシウス), no face (男性の声), the larger letters
    (な・ん・か・言・っ・た？), the system line, a field talk, the heart, the choice.
  - Furigana: a made-up line with a reading written into a waiting window's buffer: the
    reading over its two characters, the cells unchanged.
  - START during a talk: nothing opened, nothing read again.
- **Not looked at**: anything past the first chapter's opening; townspeople in Rolent (the
  road from the house was not walked; only Cassius was talked to in the field); a talk
  window in a battle; a choice that comes with a line (the ones seen in the scenario, such
  as 「やるやる！」／「やってみたいけど……」, were not reached); a choice of more than two
  answers; furigana in the real game; half-width characters; colours and item names in a
  line; sizes other than 2 and 3 and a size changed in the middle of a talk; the desktop
  app, Furoshiki's core and any device.
