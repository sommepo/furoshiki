# Kita e. White Illumination (北へ。White Illumination), T14301M

One disc, build ID A3C799F2. Profile: `reader/profiles/T14301M.lua`.

## What is read

**The message window**: each message once, when the game begins to show it, all of its rows
(one to three), with a place for every character. The hero's name, which the script names
by a placeholder, is given as it was entered.

A message with a voice is typed in time with the voice: it is reported when the typing
begins, so the cells of what is not typed yet are already there.

## What is not read

- **Who speaks**: the window shows no name.
- **The captions** of a day and a place ("７月３１日　札幌"), the menus (where to go, the
  game's own menu), the name entry, the map, the mini games. The game has an object for a
  menu's choices ("MenuSelect", beside the message's in the same list); it was not looked
  into.
- What a command in a row may do to the row's places (see below).

## How the game keeps its text

The game runs on **Windows CE**. The program sees its memory at addresses of its own (the
program at `0x00010000`, its libraries below `0x02000000`), which the system maps page by
page onto the console's memory, differently from one run to the next. The profile follows
the system's page tables, with what the console's memory holds and no help from the
emulator:

- the system's table of address spaces is at `0x8C1618A0` in the console's memory (the
  system's own data, at a fixed place); the game is its entry 7;
- that entry points at 512 pointers, one for each 64 KB of the address space
  (bits 16 to 24 of an address); each to a block whose words from `+0x0C` are the 16 pages'
  entries: bit 8 set for a page that is there, bits 12 to 23 the page's place in the
  console's memory.

By the program's addresses:

- `0x00032D0C` points at an entry of a list of named objects: at `+0x14` the name
  "MessageOut", at `+8` the object. The object's byte at `+9` is 0 while no message is up,
  1 while one is typed and 2 while it waits for the button; its word at `+0x0C` is the
  number of the message.
- `0x00040548` points at the scene's script: at `+4` how many messages, at `+0x10` their
  table, eight bytes each: how many rows, and a pointer to the first row. The rows are
  **Shift-JIS** strings one after the other.
- In a row, a letter or a number between two at signs is a command (`@S@`, `@P@`, `@n@`,
  `@0002@`) and is not drawn; `&主人公&`, `&名前&` and `&主人公名前&` stand for the hero's
  family name (at `0x00033770`), given name (`0x0003377C`) and both.
- Measured in pictures: 24 pixels to a full-width character from x = 20, rows 27 apart
  from y = 370.

## Played by touch

With *Play by touch* (Reader settings on Android) a tap beside the text presses A
(`-- reader: touch press`). Its choices are not read, so they are made with the controller.
Not tried in the game.

## How it was checked

On Linux without a display (the game drawn in software), a new game, 2026-10-10: the first
scenes (the hallway, the hero's room, the airport, the plane, the arrival), some 45
messages, read against pictures as they came; the cells of two-row messages drawn over
pictures; a row with `@S@` and `@P@`, and rows with the hero's name. The same first scene
again **after a new start of the emulator**, with another name: the same addresses held.
**Not looked at**: anything later (other scenes load other scripts: that the pointer at
`0x00040548` follows them was not seen), a loaded save, what `@n@` and the numbered commands
do, whether the game is the system's entry 7 in every way of starting it.

## Tapping an answer (2026-10-16)

Looked for, for playing by touch on a phone, and not done. The named objects are a list from
`0x00032D0C` (the next entry's pointer first): MessageOut, MenuSelect, 3CUTVISUAL. At the
fortune-telling call (はい / いいえ side by side on the phone) nothing in MenuSelect's object or
what it points at changes with the highlight; the only byte that follows it in the program's
memory is at `0x00800210`, on the heap. The command menu after it is a row of pictures (book,
PHS, bag, return), chosen with left and right. Neither has text a tap could pick, so the script
keeps its buttons (up, down, left, right, A, B). Tried on Linux, drawn in software.
