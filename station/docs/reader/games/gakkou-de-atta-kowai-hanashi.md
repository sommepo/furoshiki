# Gakkou de Atta Kowai Hanashi S (SLPS-00404)

Banpresto's horror sound novel (1996), the Japanese disc; boot program `8A7E3290`. Profile:
`reader/profiles/SLPS-00404.lua` (no hooks).

## What is read

The story's text, which the game writes down the page in columns from the right over its
photographs: each paragraph once, when the game stops at its end, with every character's
place; earlier paragraphs of the page stay clickable until the page turns. A choice's
question as a paragraph and its answers ("１・知ってる", "２・知らない") as a choice, each
answer in its column, and which answer the game's cursor is on, so that in the Android app's
*Play by touch* (and with the desktop's) a tap on an answer picks it. The choice's text lists
the answers from the left (`２・知らない` first): the engine counts a cursor that left and right
move from the left, and this game's first answer stands at the right. The player's own words, which the script has for a boy and for a girl
(`￥ｆ僕￥私￥`), and the player's name come as the game shows them. No voices are read; the
game has none in its text. Not read: the menus (card, save slots, name entry).

## How the game keeps its text

Found on this disc (2026-10-19) by searching dumps of the console's memory:

- The scene's script is in memory as Shift-JIS, with codes of its own (`￥ｆA￥B￥` a word for
  a boy or a girl, `￥ｐ` a page's end, CR LF a paragraph's).
- The game lays out a whole page at once at `0x80061858`: one 16-bit Shift-JIS code per
  character, choices of words already made, `FF03` where a column wraps (every 13
  characters), `0D0A` a paragraph's end (and a new column), `FF02` a paragraph's end where a
  column had just wrapped, `FF01` the page's end.
- `0x80060D18` (u16) is how many of the page's codes are shown; it stops at a paragraph's end
  while the game waits for a press.
- A choice's question is laid out the same way at `0x80061058`, ending with a zero. The
  answers are drawn one by one through `0x80061858` ("２・知らない" filled out with `FF04`, ending
  with a zero), so only the last is left; their number is its first character. Their words
  are read from the script: `0x80060CB8` (u32) is where the game is in it, and at a choice it
  stands on the zero that ends the question; each answer is then a zero, three bytes, its
  words.
- `0x80060DE8` (u8) is the answer the game's cursor is on, 0 the first (at the right). Left
  moves it to the next answer, right back; up and down do nothing (the script's
  `-- reader: buttons left right circle` line, a note of what picks an answer with the
  controller). Found at the first choice: of the bytes that followed left, right, left,
  left (0, 1, 0, 1, 1), this one held still between presses, frame by frame, where others
  (`0x80060DD8`, `0x80007264`) flickered.
- The picture is 320×224. Columns are 30 pixels apart from x 288, characters 15 pixels
  apart from y 9 (measured on the picture). The engine is told the picture's size by the
  emulator now (`reader.picture()`), which is what the profile's places are in.

## How it was checked

On Linux without a display (2026-10-19), with the user's SCPH-5500 BIOS, from a new game (a
boy, a name typed in the name entry): the opening's pages and the first story's first pages,
paragraph by paragraph, read against the pictures; the page turns withdrawing the old text;
the first choice (内山浩太のこと、知ってるわよね？ / １・知ってる / ２・知らない) as a question and a
choice, and the story after it; lookups on 確かめる (down a column) and on an answer.

**The cursor and touch play** (Linux without a display, 2026-10-19, *Play by touch* on in
the desktop app, a savestate at the first choice, 内山浩太のこと、知ってるわよね？): the choice
came as `２・知らない` / `１・知ってる`, the cursor reported as 1 (horizontal) on １. A click on ２
pressed left once and confirmed it: the story went on with ………知らないの？ and the cursor was
withdrawn. From the same savestate, the cursor moved to ２ by hand, a click on １ pressed right
and confirmed: そう。. Not on a device.

**Not seen**: a girl's game, other storytellers, an ending, a choice with more than two
answers, the scenes with pictures in other places of the page. The choice of who speaks
next (誰の話を聞く？, six photographs) is no text: a tap there presses ○ for the one lit.
