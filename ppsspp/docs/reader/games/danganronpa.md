# ダンガンロンパ 希望の学園と絶望の高校生 (Danganronpa, PSP)

Spike, 2010: the first Danganronpa, voiced in part. An adventure: story scenes in the message
box, free-roam investigation in first person (a reticle moved with the analog stick, ○ to
look or talk), and class trials. This page is about the PlayStation Store release.

| ID | `EBOOT.BIN` (`reader.build_id`) | Profile |
| --- | --- | --- |
| **NPJH-50372** | `DCE18276` | `assets/reader/profiles/NPJH50372.lua` |

**The profile hooks nothing** (`-- reader: no hooks`): it reads the game's memory once a
frame, so it should run unchanged where hooks are not carried out (Adrenaline Reader on the
Vita). Nothing in the reader assumes UL* IDs: the ID pattern is `[A-Z]{4}[0-9]{5}` in
`console.h` and `Console.kt`, and save sync's naming (`naming_psp.cpp`) takes any four
capitals and five digits (`NPJH50372` is `4E504A4800050372` there; not tried against a
service).

## What is read

- **The message box**, each message once it is typed out and has stood for eight frames
  with × not held, with its **speaker**: the name on the box's plate (the plates are
  pictures; the profile has their names by the speaker's number, `NAMES`: the fifteen
  students, モノクマ, the others met later, and `？？？` for a speaker not yet introduced,
  which is also what the prologue's narration shows). The tutorial's voice and a plate that
  says ナレーション are read without a speaker. This is the same box for the story, for what
  Naegi says when something is looked at in free roam, for talk with the other students and
  for the tutorial; the class trials' talk outside the debates goes through it too (not
  reached here).
- Every row break in this game is the script's own. A break after the end of a sentence
  (`。！？」』）…‥”`) is kept; one in the middle of a sentence is left out, so that a
  sentence that runs over two rows reads as one. A full-width space that indents a row (in
  the letters quoted with 『』) is left out of the text.
- **A cell for every character**, from the game's own font: each character is as wide as the
  font says (16 for most, 8 for “ ”), rows 24 apart from x 17, y 208.
- **Choices** (the investigation's 外に出ますか？ with はい／いいえ): the answers as one event of
  the channel `choice`, an answer to a line, with their cells and the cursor
  (`reader.cursor`, 0 for the first; the first is lit when they come up). The question stays
  in the box, read and pointable, while the answers are up.
- Nothing is read while × is held (it runs through the text, stopping six frames or so on
  each message), and the cells go under the game's SYSTEM page (START, the help) and come
  back afterwards without the text being read again.

## What is not read, and why

- **Furigana**: none were met in the prologue; the box's text has no readings in it. Where
  the game shows any later is not known.
- **The nonstop debates, the minigames and the trials' other screens** (not asked for, not
  reached): their floating statements are drawn by other parts of the game and are not in the
  message box's buffer. The profile reads nothing there unless the box is up.
- **The title screen, the options, the game's own save screen** (セーブデータを保存しますか？
  with its PROLOGUE card: the question is in the box's buffer, but the box never says it
  waits, so it is not read), **the films** (the opening, the prologue's fall into the dark).
- **The free-roam markers and the map** (△): pictures.
- **No message log** was found: △, □, SELECT, L and R do nothing while a message waits.

## How the game keeps its text

Everything below is at fixed places in the executable's data (it is not relocatable), except
the script's state, which a fixed pointer leads to.

- **The message**, `0x08c9a260`: two bytes, then the speaker's number (two bytes, `+2`),
  then from `+4` five rows of 0x6c bytes, each **UTF-16LE** ended by a zero, as the box shows
  them; from `+0x220` a byte per character, its colour (white, and yellow for the words the
  game marks). The whole message is there from the moment it begins to be typed out, and it
  is cleared when the next one begins. (The script itself, also UTF-16, with `<CLT n>` colour
  commands and line feeds, is in the scene's file in memory, at `0x08e35740` in the prologue;
  the rows of the message are also copied to `0x08c85e60`, 0x6c bytes apart, for drawing.)
- `0x08c9a591`, a byte: 1 once the message is typed out and waits for the button, 0 as soon
  as the next one begins. The byte before it counts how far the typing has got. (○ held
  advances once only; × held runs through.)
- **The speaker's number**: 0 苗木 to 14 不二咲 in the order of the game's roster, 15
  モノクマ, 16 江ノ島 (again), 17 アルターエゴ, 18 ジェノサイダー, 19 学園長, 20–22 苗木's
  mother, father and sister, 23 ナレーション, 24 イシマル・オオワダ, 25–29 大和田大亜, 30
  `？？？`, 31 and above no plate (31 is the tutorial's voice). Each was seen by writing the
  number into a waiting message's `+2` and looking at the plate.
- `0x08993fb6`, a byte: 1 while the SYSTEM page is over everything.
- `0x08990538`: the buttons the game sees held (PSP button bits; × is 0x4000).
- `0x08994d20`: the game's mode (2 a story scene, 5 free roam waiting for input, 1 or 2 while
  walking a corridor, 6 looking around a room...). Not used by the profile.
- **The font**, at `0x08a1de6c`: a header `SpFt` (`tFpS` in memory), at `+0x20` a pointer
  to its glyphs (16 bytes each: the character, its place in the font's picture, its width and
  height), at `+0x24` a pointer to a table of two bytes per UTF-16 unit, the glyph's number
  (0xffff for none). A second font follows it at `0x08a1de94` (the larger one: names,
  menus). The profile reads widths only, and only of characters it lays out.
- **The choice**: the script's state is an object of the game's heap that `0x08995434`
  points to (it was at `0x08f14fe0`). At `+0x776c` a byte: how many answers; `+0x7774` the
  choice's state (1 to 3 while the answers come up, 4 while they wait, 7 and 8 while the
  cursor moves, 5 and 6 once one was picked, 0 when there is none); `+0x7778` the answer the
  cursor is on, from 0; from `+0x7958` the answers' texts, UTF-16, 0x6c bytes apart.
- In a corridor the player's place is three floats at `0x08a239b8` (x, height, z) and the
  direction they face at `0x08a239c8` (degrees). Not used by the profile; it is what the
  harness walk below steered by.

## Where the characters are

Measured in screenshots (480x272): the box's first character's left edge at x 17, the
first row's top at y 208, rows 24 apart, characters 20 high; widths from the font. The
answers of a choice (two) are on bars 25 apart from y 93; an answer's text begins at x 226,
the lit one's at x 186 once its bar has slid out (some ten frames after the cursor moves;
until then the lit answer's cells are a little to the left of its text). Where three or
more answers stand was not seen.

## Buttons

○ advances (and examines or talks at the reticle in free roam), × held runs through the
text, START opens the SYSTEM page (its help; × closes it), the analog stick moves the
reticle in a room and walks in a corridor, the d-pad and L/R turn the view, △ shows the map in
a corridor, × leaves a room (with a はい／いいえ question). **Played by touch** a tap beside
the text is ○, which is right for the story and the talk; free roam needs the analog stick,
which the touch buttons do not have, so investigation cannot be played by touch alone.

## How it was checked

- **Harness only** (the real game, 2026-10-27). From the title: New game (the difficulty
  screen left as it is), the opening film, then circle at each wait through the prologue's
  narration in front of the school (some 95 messages, the text written down at each from the
  memory above and compared with screenshots), the game's own save question (cross through
  the PSP's save screen), the classroom: the tutorial's lines, the desk, the camera, the
  monitor, the clock, the window plates (and the 17 lines Naegi says then), the leave-room
  question; the corridors (the map's tutorial, the red door, 絶望ホテル's sign) walked to the
  entrance hall; there the meeting with the other students (some 40 messages, the first
  ordinary dialogue) and 石丸's introduction (talk in free roam).
- With the profile loaded: every one of those messages reported once, with the plate's
  speaker (`？？？`, ナエギ　マコト, イシマル　キヨタカ, the tutorial's none); a message
  that only a press stopped was never reported twice; nothing while × was held from the desk
  through six messages; the cells went under the SYSTEM page and came back; the leave-room
  choice: the answers, the cursor following the d-pad (0, then 1), the question kept, cells
  and cursor gone once はい was picked. From a fresh start of the game (not a saved state):
  the first seven messages read.
- **Cells drawn over screenshots** (`tools/reader/layout_check.py`) and looked at: narration
  of two rows with yellow marked words, the “ ” rows (half-width quotes), a speaker's first
  line, a quoted letter with its indented second row, the hall's talk (rows with `　` in the
  middle, `！！`, full-width digits), the tutorial's coloured rows, and both cursor positions
  of the choice.
- **Not looked at**: a trial (its talk, the debates, the minigames), choices of three or
  more answers, furigana (none met), what happens to the box's buffer in the later chapters'
  scenes that show text over pictures, the options' text speed, the desktop app and any
  device (the PS Vita through Adrenaline Reader included).
