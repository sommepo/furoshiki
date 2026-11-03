# Shenmue II (シェンムーII), Japanese version, HDR-0164

Four discs, one product number (`HDR-0164`), a build ID each: 7C69F1A3, 1FFADD74, 885B3B06,
D8DC84DA (discs 1 to 4). One profile, `reader/profiles/HDR-0164.lua`.

## What is read

**The subtitles**: what is said in cutscenes and in conversations with people in the street,
each line once when it comes up, with a place for every character so that a word can be
clicked. A subtitle does not wait for the player: the reader's "pause the game" (a
controller's back button, Ctrl+G) holds the picture so that the line can be read and its
words looked up.

## What is not read

- **Who speaks.** The game does not show a name, and the line it keeps for display has none.
  (The scene's data names a character by an internal code before each line, `RYO_` and the
  like; nothing is made of that.)
- **Everything that is not a subtitle**: the Start Menu's descriptions (not text in memory:
  searched for and not found, so probably pictures), the notebook, item names, shop and
  signboard text, the Options screens, the "Now Loading" cards with place and date, quick
  time event prompts. None of these was looked into.
- **A subtitle of more than one row, or with half-width characters**: none was met. The
  profile puts every line on one row, a full-width character 24 wide and anything below
  U+0080 half of that.

## How the game keeps its text

- Text is **EUC-JP** (not Shift-JIS): `text.eucjp` was added to the engine for it.
- A scene's lines are loaded with the scene, in blocks tagged `CHID` (who), then the text,
  then `CLIP` (mouth movements), `ENDC`. They are all there at once, so they do not say
  what is on screen.
- **The line on screen** is copied into the data of the scene task. The program's word at
  `0x8C278B20` points to that data, which begins with the tag `SCEN`; the text is at `+0x84`,
  zero-terminated, and its first byte is zero while no subtitle is up; `+0x54` points to the
  text's end. The task's data is in the game's heap (blocks tagged `TASK`), which is why it
  is reached through the pointer: its address was `0x8C4160A0` in a new game on disc 1 and
  the block did not exist yet at the Start Menu.
- A subtitle is drawn centred, 24 pixels to a full-width character, its glyphs from about
  y = 422 to 445 of the 640x480 picture (measured in pictures of three lines). Ryo's lines
  have a blue edge and other people's an orange one: the same place either way.

## How it was checked

On Linux without a display (the game drawn in software), disc 1, a new game, 2026-10-10:

- The opening: the three subtitles of the letter scene were reported as they came up
  (「ワンチャイ…紅南街…＆恩田楼…」「桃李少老師は、ここに…」「まずは、ワンチャイか。」) and
  withdrawn when they went, read against pictures of the game.
- Free roaming at Aberdeen: a street musician's line (「旅のお方、１曲きいていきなされ。」).
  With the game paused on it by the reader, a click on its first word (sent over the
  development channel) opened the popup for 旅, the word marked; the cells of 「まずは、
  ワンチャイか。」 were drawn over a picture and the measurements corrected from it.
- **Discs 2, 3 and 4** were only started to their title screens: same product number, the
  pointer at the same address holds a heap address there too, and it leads to another task
  (`WNDT`) at that point, where the profile rightly reports nothing. **No line of discs 2 to
  4 was read.**
- **Not looked at**: anything beyond the first minutes; lines while the game's own menus
  are open; a loaded save or savestate in the middle of a line (the profile forgets what it
  showed when a state is loaded); long lines that might break into two rows; quick time
  events; the "Shenmue Collection" and "Digest Movie" entries of the Start Menu.
