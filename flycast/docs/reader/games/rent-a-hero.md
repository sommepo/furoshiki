# Rent a Hero No.1 (レンタヒーロー No.1), Japanese version, HDR-0074

One disc, build ID 4CE8F65A. Profile: `reader/profiles/HDR-0074.lua`.

**A fan-made English version of this disc has the same product number and the same header**,
so the app cannot tell the two apart: the profile runs on it as well and reports nonsense
there. Switch the reader's text off for that copy, or do not expect anything of it.

## What is read

**The message window**: the narration and what people say, each message once when it stands
complete, with a place for every character. A name the player typed is put in where the
message has it.

## What is not read

- **Who speaks**: no name is shown with a message.
- **Everything else**: the title menu, the name entry, the memory card screens, the menus of
  the game itself (items, the hero's jobs, shops), anything in a fight.
- **A window in another place**: the profile puts the text where the opening's messages are
  (see below). If the game moves the window, the text is still read but the places are wrong.

## How the game keeps its text

- A message is a row of **16-bit codes**. Below 0x8000 a code is a character counted through
  the JIS X 0208 table with its empty places left out: 10 for the first character of row 1,
  then rows 1 to 7 and 16 to 84 in order, without row 8 (so あ is 220, コ 320, 父 2918).
  0x8001 is a new row and 0x8000 the end. 0x9000 + n stands for the n-th name of a table at
  `0x8C1BEFBC` (18 bytes each, the same codes; 2 is the family name, 3 the given name).
  0x4D00 to 0x4DFF are half-width Latin characters, 0x7D above their ASCII codes (seen:
  capitals in "MIC TEST"; 0x4DA7 is the space in it).
- All of a scene's messages are in memory at once. The one on screen: `0x8C1E29DC` points to
  its first code and `0x8C1E29E0` to where the game has got to, which is the end code once
  it is all shown; `0x8C1E29C8` is not zero while the window is up.
- The window's text, measured in pictures of the opening: 24 pixels to a character from
  x = 70, rows 24 apart from y = 350; a half-width character takes 12.

## How it was checked

On Linux without a display (the game drawn in software), a new game, 2026-10-10: the
opening's narration and the father's speech at the party, nine messages, each reported once
and read against pictures; the cells of two of them drawn over pictures (one with half-width
letters). **Not looked at**: anything after that; choices; messages with other codes above
0x8000 (they are left out); the other names of the table; a loaded save.
