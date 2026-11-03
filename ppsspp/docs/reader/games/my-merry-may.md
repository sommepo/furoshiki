# My Merry May with be (PSP)

KID/CYBERFRONT, 2009: My Merry May and its sequel My Merry Maybe on one disc, with the
"Beginning" prologue before the title.

| Disc ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- |
| **ULJM-05603** | `A0EDB17F` | `assets/reader/profiles/ULJM05603.lua` |

The profile hooks nothing: it reads the game's memory once a frame.

## What is read

- **The story's text**, each message once it is typed out (or faded in) and has stood for
  four frames, with its **speaker** (the name the box shows on its plate; narration has
  none) and a cell for every character. The game shows its text in two ways, and both are
  read:
  - **the message box** at the bottom of the screen (a box of four rows, cleared after each
    message);
  - **the page**: narration over the whole screen, message under message until the page is
    cleared. Each message of a page is a text of its own, and the earlier ones stay
    pointable until the page goes.
- A row the game broke because it was full goes on without a break; a row the writer broke
  stays broken.
- **Choices**: the answers as one event of the channel `choice`, an answer to a line, with a
  cell for every character, and the cursor (`reader.cursor`) for touch play. The message
  under the choice stays on screen and stays pointable.
- Nothing is pointable under the game's menu (□), in its message log (△) or while × hides
  the window; the cells come back afterwards without the text being read again.

## What is not read

- The chapter's title at the top of a page (オープニング／プロローグ), the title screen, the
  menus, the backlog, the films and their eye-catches, the PSP's own save screen.

## How the game keeps its text

Everything below is at fixed places (the program is not relocatable).

- **The shown text**, `0x08a49b5c`: Shift-JIS, everything the box or the page has shown
  since it was last cleared, the script's percent commands still in it. A message ends in
  `%K` (the wait for the button), then `%N` (the next row: on the page), `%P` (the box is
  cleared), `%p` (the same, between scenes) or `%O030` (the page is cleared). A message in
  the box begins with `【name】`. Other commands are taken out: `%S060` (the typing speed),
  `%FS`…`%FE` (fading in), `%LC` (a centred row), `%L1`, `%T090`, `%CG<colour>`…`%CE`,
  `%X66`…`%XE`, `%W`. **One message can be two strings of the script** (a voice changes in
  the middle): `0x08e68328`, the string being typed, then has only the second part, which
  is why the profile reads the whole shown text.
- `0x08a493c4`, a byte: 1 once the message waits for the button. With `%FS` the whole
  message fades in and the byte is 1 at once.
- `0x08a492b4`, a float: how opaque the page is: 1 while the text is on the page, 0 while it
  is in the box. (The byte after the "waiting" one looked like this at first and is not.)
- `0x08a49b44`, two bytes: 1 while the text window is in use; 0 in films, eye-catches and
  between scenes.
- `0x08a4a93c`: frames since the message began; it begins anew with every message.
- **The cursor**: `0x08e12f24` its row (from 0, the box's or the page's first), `0x08e12ecc`
  its column. While a message waits, the cursor stands after its last character: the `%N`
  after `%K` is carried out when the next message begins. The game breaks a row lazily (when
  a character no longer fits), but after a character that hangs over a full row (`。`) it
  begins the next row at once, so a `%N` after one leaves an empty row.
- `0x08a492c4`, two bytes: each 1 while the text can be seen; the first is 0 under the menu
  and the log, the second while × hides the window. (A float at `0x08a492c8` is the box's
  opacity.)
- `0x08a49b40`: twelve bytes about the window; the two at `+0x0a` count the messages of a
  scene (not used).
- **The choice** comes after a message that ends in `%K%p`. Its window is at `0x08a493d0`:
  the first byte is 1 while it is up, the float at `+4` its opacity, `+0x22` (two bytes) the
  answer the cursor is on, from 0, and `+0x24` how many answers there are; from `+0x38`, 16
  bytes an answer, the answer's number among the scene's texts and its length. The answers'
  texts are in the scene's script right after that message; the game's task `select_call`
  (an object in the game's heap with its name, `task select_call`, in it; it was at
  `0x08f8e69c`) points to each from 0x20 bytes after the name, 12 bytes apart, and the
  profile finds the task by its name between `0x08f80000` and `0x08fa0000` when a choice
  comes up. The window says it is not in use (`0x08a49b44` is 0) from two frames before the
  choice comes up, which is why the profile lets a message go only after eight such frames.
  **The first answer is lit from the start**: ○ picks it at once.

The game draws its text into a picture: there is no list of characters with places, and the
characters' codes are nowhere but in the texts above.

## Where the characters are

Measured in screenshots (480x272), and followed from the text:

| | first cell's left | step | cells in a row | first row's top | between rows |
| --- | --- | --- | --- | --- | --- |
| **box** | 28 | 16 | 26 | 200 | 18 |
| **page** | 32 | 16.5 | 25 | 31 | 18.17 |

Answers are centred at x 239, rows 19 apart around y 122, a character 17 wide (measured
with two answers; where three or more stand was not seen).

A row ends as soon as it is full, unless a `、 。 」 』 ）` follows, which then stands as one
more character after it. Characters of one byte
count as half a cell (none was met). **The page can begin lower down** (the prologue's first
page begins two rows down, after a message of two `%N`s that the shown text no longer has),
so the profile lays the text out from the top and then moves it so that its last row is the
game's cursor's.

## Buttons

Circle advances; on a line still being spoken the first press stops the voice and the second
goes on. Start switches the auto mode on and off, △ is the message log, □ the menu (save,
load, quick save and load, options, title), × hides the window. Start at the title opens its menu (New game, Continue, Append story, System data,
Special); New game asks for Beginning, My Merry May or My Merry Maybe. Beginning is also what
plays when the disc is left at its title. After Beginning the game makes its system save
through the PSP's own save screen (in the harness: circle, then cross, a second apart).

## How it was checked

- **Harness** (the real game, on 2026-10-25):
  - Beginning, from its first page to the opening film (some 260 messages), and My Merry
    May from its first page through the first days (some 1,100 messages more), circle
    pressed whenever the game waited, the shown text written down at each: no choice came
    up.
  - With the profile loaded, circle pressed at every wait: 100 presses from My Merry May's
    first page (98 messages, as two presses only stopped a voice): each reported once, none
    missing against the written-down text, speakers (恭介, ひとえ, レゥ, ？？, 店員Ａ…), a
    message of two script strings reported whole, a box of three messages each after a wait
    (１．２．３．, kept together), pages of three and four messages. The prologue's first page
    (it begins two rows down) and the first choice (あっているよ／それは違う, after some 1,400
    messages): the answers, the cursor following the d-pad (0, then 1), the message under it
    kept, the second answer picked and the next line read. From a fresh start of the game
    (not a saved state): the prologue's first five messages read.
  - Under the log (△) and with the window hidden (×) the cells went and came back; nothing
    was read again.
  - **Cells drawn over screenshots** (`tools/reader/layout_check.py`) and looked at: a page
    of three messages (one that ends on a hanging `。` and leaves an empty row), the next
    page, the prologue's page that begins lower down, a box of two rows without a speaker and
    one with a speaker, the box of three numbered messages, the choice.
- **Tests**: the catalog test knows the ID; nothing of the profile itself.
- **Not looked at**: a choice of three or more answers (their rows are guessed), one whose
  message is on the page; My Merry Maybe; Append story; a centred row (`%LC`), coloured or
  large text; a message of the box that waits twice; the game's skip and auto modes; the
  backlog and the menus (whether the cells go away under them); a state loaded in the middle
  of a page; the desktop app and any device.
