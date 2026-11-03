# Shin Seiki Evangelion: Ayanami Ikusei Keikaku (新世紀エヴァンゲリオン 綾波育成計画), T46303M

One disc, build ID 98FD76CA. Profile: `reader/profiles/T46303M.lua`.

## What is read

**The message window**: each message once, when the game has typed all of it, in the rows
the game broke it into (up to four of fourteen characters), with a place for every
character, wherever the window stands. Who speaks is part of the text as the game writes it
(`リツコ「…」`).

## What is not read

- **The raising part's own screens**: the week's schedule, the icons, the figures, the
  shop, the computer, the album. They were not looked into; the icons are pictures.
- **The name entry's keyboard.**
- Any other text object the game may put up beside the message window: the profile takes
  the first one it finds.

## How the game keeps its text

- The window's text is an object on the game's heap. Nothing in the program's fixed data
  points at it (what does is on the stack of the routine that waits for the button), so the
  profile looks for it: the heap begins where the pointer at `0x8C2C4FD0` says, and the text
  follows a mark of sixteen bytes: four of `0xFF`, the floats 14.0 and 12.0 (the text's
  inset in its window) and a zero word. It looks through the heap's first 256 KB every
  thirty frames until it has the object, and checks the mark every frame after.
- The text is a **Shift-JIS** string with a line feed (`0x0A`) where the game broke a row.
- Before the text: at `-0x48` and `-0x44` where it is drawn (floats, in the 640x480
  picture), at `-0x50` 1 while the window is up, at `-0x54` 1 once all of the text is typed.
- Measured in a picture: 24 pixels to a full-width character, rows 26 apart.

## How it was checked

On Linux without a display (the game drawn in software), a new game, 2026-10-10: the whole
opening up to the first week's screen, some 120 messages, read as they came and many
against pictures; the cells of a four-row message drawn over a picture; the window at its
two places; nothing reported on the week's screen and during changes of scene (the flag at
`-0x50` was found by comparing sixty moments with and without the window). The game's
pointer is moved with the stick: the development channel got a `stick` command for it.
**Not looked at**: anything after the first week's screen appears (events, lessons, the
weeks' reports), a loaded save, whether the object is always within the heap's first
256 KB.
