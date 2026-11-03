# July (T35401M)

Fortyfive's adventure (1998), the Japanese disc; header checksum `481991C9`. Profile:
`reader/profiles/T35401M.lua` (memory read once a frame).

## What is read

The message window, each message once when it is typed out, with who speaks (the name in
the window's box), and every character's place; the thoughts and choices the game writes in
the middle of the picture (a question in red and its answers below) as a message and a
choice. Not read: the map screen, the scenario menu, the date line at the top.

## How the game keeps its text

Found on this disc (2026-10-19) in dumps of the console's memory:

- The message being shown is a C string at `0x8C226D60`: Shift-JIS, two tabs before each
  line, and codes beginning with `%`: `%N` a new line, `%<` … `%>` text in the middle of the
  picture (a thought or a choice's question; after `%>` come the answers, one a line with a
  blank line between), `%Q2` a choice of two answers, others (`%C1`, `%K`, `%+20`, `%B18`,
  `%S0`) for colour, waiting and place.
- The window's object at `0x8C0BFD00`: `+0x24` (u32) where the typing has got to in the
  message (0 while a message is not typed, as a choice's), `+0x54` (u32) who speaks (0: no
  name), `+0x58` (u32) a pointer to their name in the game's table of names.
- The script of the scene is in memory too (`0x8CE06A00`, messages between `00 FF`), not used.
- Places, in the 640×480 picture: the window's lines at x 66, y 343, 31 apart; the middle's
  at x 240, y 80, 32 apart; characters 28 apart (measured on the picture).

## How it was checked

On Linux without a display (2026-10-19), without a BIOS: Start, Makoto's scenario from the
map, its opening. The messages were reported as the pictures show them, with their speakers
(誠, えり子) as the backlog lists them; the first choice (ここはどこだ？ / 俺の部屋か・・・ / 寝よう・・・)
as a message and a choice; a click on 屋 in the window opened its entry over the glyph.

**Not seen**: further than the first scene, the other scenarios, the map screen's place
names, voices (whether a card's sound is the line's), a loaded save. The answers are
reported when the question comes up, a press before the game shows them.
