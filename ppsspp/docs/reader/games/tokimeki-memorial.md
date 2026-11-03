# Tokimeki Memorial Forever with You (PSP)

Konami, 2006: the 1995 dating simulation (the PlayStation version, voiced) on the PSP, with a
choice of frames around the picture at the first start.

| Disc ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- |
| **ULJM-05078** | `04BDCD25` | `assets/reader/profiles/ULJM05078.lua` |

> **The profile hooks one address** (`0x08826ae0`, the game's routine that prints a text), so
> **it reads nothing in Adrenaline Reader**, which runs scripts without hooks. No place in the
> game's main memory holds the text that is shown: [why](#why-a-hook). Everything else it uses
> (where each text stands, the typing, the choice's cursor, the frame) is read from memory
> once a frame.

## What is read

- **The message window**: each message once it is typed out and has stood for four frames,
  with its **speaker** (the name left of the text: the hero's family name for his own lines,
  藤崎, 如月, 好雄, 伊集院, 女生徒…) and a cell for every character. The opening bracket the
  game puts before a line (「 for others, 『 for the hero, （ for his thoughts) is part of
  the text, as shown; the game does not close 「 and 『. The rows are the writer's (the
  script breaks them itself), and stay broken in the text.
  - A message of more than three rows scrolls while it is typed: it is read whole, and the
    rows that have gone up have no cells.
  - A message the game finishes after a pause (藤崎「あっ、ああ君。 … 今帰り？) is read as two
    texts, each when it is typed out, with the same speaker.
  - The hero's thought on the command screens ((明日から何をしようかな？) and
    (今日は何をしようかな？)) is read each time it is shown.
  - A sound word printed in the window (ドン, printed twice as its own shadow) is read once,
    without a speaker.
- **Choices** in the window (『一緒に帰らない？』／『そうだよ。じゃお先に。』): the answers as
  one event of the channel `choice`, an answer to a line with its brackets, a cell for every
  character, and the cursor (`reader.cursor`) for touch play.
- Cells follow the frame chosen at the first start (full screen, "TYPE A", and enlarged,
  "TYPE B", were checked).

## What is not read

- **Furigana**: the game has none.
- The name entry and its confirmation, the album (save) screens, the command screens' icons
  and parameters, the date, the place names (printed off the screen), the PSP's own dialogs:
  menus, not asked for. Calendars, the status screens and the girls' lists were not looked at.
- **After a savestate is loaded, the message on screen is not read** (it was printed before
  the profile could see it); the next one is.

## Why a hook

The game is the PlayStation original, ported with its ways kept: the PlayStation's 2 MB of
memory are a block of the PSP's (`0x08d8acc0` is the PlayStation's `0x80000000`; the routine at
`0x088532bc` turns one address into the other), and its text drawing is the PlayStation's:

- A text is printed with `print(x, y, colour, text, …)` at `0x08826ae0`. It makes the next
  entry of a list of printed texts and turns each character into a picture at once: the
  character's 12x12 picture from the font (`0x08a2fe34`, 18 bytes a character, Shift-JIS
  turned into the font's order by the ranges at `0x08a289e0`) is drawn into a 16-slot buffer
  and copied into video memory (around `0x04134240`), where the screen is drawn from. **The
  character codes are kept nowhere**: neither the list nor anything else holds the text or a
  pointer to it once `print` has returned.
- Where the text comes from is different for each kind of scene:
  - **The opening and the command screens** are code, each scene its own: the opening's
    lines are a table at `0x08a57de0` (one pointer a line, a style byte each at `0x08a57e8c`)
    read with a counter at `0x091ad574` that only that scene uses. They go through the
    message routine `0x08834784` (the message with spaces between rows, `P` and `G` for the
    hero's names), which makes the rows (`0x08834910`) in its own stack and prints them; the
    name is printed by `0x088346e4`. Some 240 places in the code call it, each with its own
    table and counter.
  - **The events** (meeting 如月 in the library, 藤崎 at the gate…) are scripts loaded into
    the PlayStation's memory (the library's at `0x08f25…`), their lines pointed to by the
    PlayStation's addresses. The script's place is kept only on the main thread's stack
    (`0x09fff38c` in the scene looked at), and the line is again composed on the stack and
    printed.
- So a script without hooks would have to know every scene's table and counter, or follow
  the stack, or recognise the characters' pictures in video memory (which Adrenaline Reader,
  reading main memory only, cannot see). None of these was attempted.

## How the game keeps its text

Everything below is at fixed places (the program is not relocatable).

- **The list of printed texts**, `0x08f8ad30`, 8 bytes an entry (at most `0x34`): x and y
  (two bytes each, signed, in the PlayStation's screen of 320x224 from its middle), a byte
  (the colour or style), the number of characters, a byte, and a byte that is 1 while the
  entry is shown. `0x08f8aed0` (two bytes): how many entries there are; the next `print`
  makes entry number that. `0x08f8aed2`: how many characters they have together.
- **The typing**: `0x08f8aed8` (two bytes), how many characters, counted over the whole list,
  are shown. A new message sets it to its first row's first character (the name and the
  bracket are shown at once); it counts up as the line is spoken and stands at the end while
  the game waits for ○.
- **The message window**: the name at x −128 to −120 on the first row (y 50 in the opening
  and command screens, 48 in events), the bracket at −86, the rows at −72 (y 50/66/82 or
  48/64/80); in events the bracket is an entry of its own (`「 `, with a space). A fourth row
  is printed on the third row's place and the entries move up (the first row's entry is
  hidden) as it is typed. Entry 0 is the place's name, printed off the screen (x −206,
  y −152).
- **Choices** in the window: each answer is a bracket (`『`, at x −128) and the answer at
  −114, one row each. The choice is the game's second menu: `0x08fca329` how many entries it
  has, `0x08fca32e` the one the cursor is on (from 0; the first answer is lit from the start).
  `0x088332d8` sets a menu up (its entries, 16 bytes each, near `0x08fca228`; the first
  menu is the command screen's 14 icons). The menu's count stays after the choice is gone: the profile
  takes the choice to be up while its entries are in the list.
- **The picture on the PSP's screen**, `0x0908bdd0`: x, y, width, height (two bytes each):
  0, 0, 480, 272 for "TYPE A" (full screen), 48, 0, 384, 272 for "TYPE B" (enlarged). The
  PlayStation's (x, y) is at (x + 160) · width/320, (y + 112) · height/224 from there.

## Buttons

○ goes on (the opening goes on by itself after each spoken line; a line still being spoken
does not take ○ at once). On the command screens the d-pad steps through the icons, ○ picks
one. The name entry is a hand moved with the d-pad, ○ types the character under it, START
ends a field; the confirmation (これでいい？) took START, not ○. △, □, START, SELECT, L and R
did nothing in an event (there is no menu, backlog or skip there). At the first start the
game asks for a frame (◀ ▶, ○, then はい).

## How it was checked

- **Harness** (the real game, 2026-10-27), frame "TYPE A" unless said:
  - From a new game (name, birthday, 藤崎's birthday left empty): the opening's first two
    lines read with the hero's name in them; the opening from its third line to the legend
    of the tree (42 messages) from a saved state: every message once, speakers ぎぎ, 好雄,
    女子Ａ, 女子Ｂ, 伊集院, 藤崎, messages of four and five rows read whole.
  - Some six weeks of play from the first command screen, ○ every two seconds: the command
    screens' thoughts, 伊集院 in the library, the 如月 event (12 lines), the hallway (女生徒,
    ドン), 藤崎 at the gate: the two-part message, the choice, the cursor 0 → 1 with ▼, each
    answer picked once and the next line read. Lines compared with what the game printed
    (a probe that logged every `print`): none missing, none twice.
  - ○ tapped every eight frames through the hallway event: each line still read once.
  - **Cells drawn over screenshots** (`tools/reader/layout_check.py`) and looked at: a
    message of four rows after it scrolled (the first row without cells), an event line with
    the bracket as an entry of its own, the two-part message, the choice; and in "TYPE B" an
    opening message of four rows.
- **Not looked at**: dates, phone calls, the girls' events past the first weeks, a choice of
  three answers or one in a scene that is code (the opening's kind: none came), a choice
  answered by touch, frames "TYPE C" and "TYPE D", the endings; whether anything else is
  printed into the window (a menu over it, a tutorial); the desktop and Android apps and any
  device. Adrenaline Reader: not possible (the hook).
- **Tests**: none of the profile itself.

Harness states (in `private/tokimemo/states/`, not in Git): `title.state` (press START),
`confirm_name.state` (the new game's confirmation: START three times to the opening),
`prologue2.state` (the opening's second line), `command1.state` (the first command screen),
`week2.state` (the 如月 event), `gate_oi.state` (藤崎 at the gate, before her two-part line),
`choice_up.state` (the choice, first answer lit), `frameB_prologue.state` (a game in
"TYPE B").
