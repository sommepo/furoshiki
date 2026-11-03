# Segagaga (セガガガ), HDR-0151

One disc, build ID 042E25DC. Profile: `reader/profiles/HDR-0151.lua`.

## What is read

**The message window** of the story's conversations: each run of text once, when the game
has typed it out and waits for the button, with a place for every character.

## What is not read

- **Who speaks**: the window shows no name. (The text names a character and a face by
  number, `@c11`, `@f14`; nothing is made of that.)
- **Everything else**: the title menu and its description line, the name entry, chapter
  cards, the role-playing battles, the development simulation's menus and reports, shops,
  the reference room. None of it was looked into.
- Text in another size or colour, and anything the conversation commands other than those
  below may do to the window.

## How the game keeps its text

- A conversation is one **Shift-JIS** text with commands that begin with an at sign: `@n` a
  new row; `@p` and `@k` wait for the button, after which the window starts afresh (measured:
  after `@k` the next text begins at the window's top, as after `@p`); others carry a number
  (`@v1006` a voice, `@c`, `@f`, `@w`, `@m`, `@e`).
- The game reads it through a stream at `0x8CAC5E54`: a pointer to the text, the offset it
  has read to, and the offset where the run it is showing began. While it waits, the read
  offset stands on the `@p` or `@k`. The profile reports the run between the wait before and
  this one when the offset has stood still on a wait for four frames.
- The window's grid, measured in pictures: 24 pixels to a full-width character from x = 42,
  rows 30 apart from y = 344.

## How it was checked

On Linux without a display (the game drawn in software), a new game, 2026-10-10: the first
conversation of chapter 1 (the president's speech), some ten windows: each reported once as
it stood complete, read against pictures; the cells of a three-row window drawn over a
picture. **Not looked at**: anything after that conversation; a window while the game skips
or the text is typed; a loaded save; whether the stream is at that address in every part of
the game (the simulation and the battles may use others).
