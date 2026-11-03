# Sakura Taisen (サクラ大戦), HDR-0072

Two discs with one product number: build ID 94AC613E (disc 1) and F73F4DE9 (disc 2).
Profile: `reader/profiles/HDR-0072.lua`.

## What is read

- **The message window** of the adventure parts: each message once, when the game has
  printed all of it, with a place for every character. A message that is printed in time
  with its voice is reported when the printing ends.
- **The answers of a timed choice** (LIPS), as one choice with an answer to a row, with a
  place for every character.

## What is not read

- **Who speaks**: the window shows a face and no name.
- **The battles**, the theatre's map, the chapter cards, the previews of the next chapter,
  the menus, the save screens, the mini games. None of it was looked into.
- A choice of another kind than a list of answers (the game has ones with a gauge, and
  ones where a spot in the picture is pointed at: what the hero thinks of the spot comes
  through the message window and is read).

## How the game keeps its text

- A message is a **Shift-JIS** string with `$$` where a new row begins. Two characters are
  the game's own: `0x81AC` is drawn as "!?" and `0x81B8` as "!!" in one cell each (seen in
  pictures); the profile gives them as ⁉ and ‼.
- The program's pointer at `0x8C1D726C` leads to the message window's record (on the heap):
  at `+0x74` a pointer to the message being printed (a copy on the heap, 0 while there is
  none), at `+0x78` how far the printing has got, at `+0x6C` 1 while it prints. The profile
  reports a message when the flag is 0 and the second pointer stands on the string's end.
- A timed choice has a record at `0x8C1F7AD4`: at `+4` an object's pointer while the choice
  is up (0 otherwise), from `+0xC` the answers, 100 bytes apart, at `+0x138` how many. The
  answers of the last choice stay there after it; the pointer is what says one is up.
- Measured in pictures: the message window's characters are 30 wide from x = 128, rows 32
  apart from y = 338. A choice's rows are centred on x = 320, 29 to a character and narrowed
  to fit 404; with two answers they are 60 apart and with three 45, about y = 196 (the rule
  in the profile, 180 / (answers + 1), fits those two and is a guess for any other number).

## Played by touch

With *Play by touch* (Reader settings on Android, on unless switched off) the game is played by
touch alone (`-- reader: touch press`): a tap beside the text presses A, a tap on an answer of
a timed choice moves the cursor to it with the d-pad and presses A. The answers are reported
as a choice now (the `choice` channel), not as dialogue. The cursor is the word at `+0x64` of
the choice's object (the pointer at `+4` of its record), 0 on the first answer: found on
2026-10-16 on Linux without a display, by moving the cursor at a choice of three answers and
comparing memory. Checked there: the profile said 0, 1, 2, 1 as the cursor went down, down
and up; the app's presses for a tap on the first answer (A) and on the third (down, down, A)
led to different replies, each the one its answer asks for. The timer runs while the cursor
moves, so a pick is a few presses quicker than the time a choice gives. In the desktop app too (2026-10-16, by clicks over the development channel): a click beside a
message went on to the choice, a click on the third answer picked it (its reply came). Not
checked: a choice of four answers, a choice with a gauge, a device, Windows.

## How it was checked

On Linux without a display (the game drawn in software), disc 1 from "ディスク1の最初から",
2026-10-10: some 250 messages of chapter 1, from the order of transfer to the theatre's
reception, each reported once, many read against pictures; the cells of a two-row message
and of a choice with two and one with three answers drawn over pictures; the choice gone
from the report once an answer was taken. **Disc 2 was not played**: it wants a save from
disc 1's end. Its program in memory is the same as disc 1's up to `0x8C13000C` and names
both addresses as often, which is why the profile takes it. **Not looked at**: a loaded
save, the battles, a choice with four answers or with a gauge, a message while the game
skips.
