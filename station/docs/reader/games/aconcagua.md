# Aconcagua (SCPS-10131, SCPS-10132)

Sony Computer Entertainment's adventure on the mountain (2000), the Japanese release on two
discs; both discs have the same boot program, `SCPS_101.31` / `SCPS_101.32`, build
`9108E514`. Profiles: `reader/profiles/SCPS-10131.lua` and `SCPS-10132.lua`, the same script
under each disc's name (no hooks).

## What is read

The game's message windows, page by page, with every character's place: what the survivors
say to each other and to the player (the window under a speaker's portrait), what the hero
thinks and finds, the scenes the game plays in its own pictures (their pages that go on by
themselves after some frames are taken like the others, by what the code says; not seen), and
the small window that names what was picked up (手袋を／手に入れた). Each page once, when the window waits; a window that slides in or out is
followed; when it closes its text is withdrawn. A yes/no question (この深雪を進んでいきますか？)
is read as a line of its own.

Not read:

- **The movies' subtitles.** The opening and the other movies (`ST*/SE*.STR`) have their
  Japanese lines drawn into the pictures of the movie: there is no text in memory to read.
- **Speakers' names.** The game shows a portrait, no name, so none is reported. The script
  knows the speaker of each page (`0x11` and a digit in a page's head: 1 カトウ, 2 パチャママ,
  3 ジュリア, 4 a man born in Los Angeles, 5 the dying Spanish speaker, as the lines themselves say), which a
  later version could report.
- **A yes/no question's answers.** As the place's code reads, the window waits in a mode of
  its own and the answer is taken from two buttons (bits 21 and 22 of the pad word the place
  keeps, most likely ○ and ×), not from a cursor; what the game draws for them was not seen. So no choice and no cursor are
  reported, and a tap beside the text, with *Play by touch*, presses ○.
- Menus, the chapter titles (a program of their own), the item list, the map.

## How the game keeps its text

Found on disc 1 (2026-10-25) in dumps of the console's memory, the game's code read from them
and from the disc:

- **The game is many programs.** The boot program only loads the others: `PROGRAM.BIN` (the
  same on both discs) holds 142 PS-X executables, compressed (LZSS: a flag byte, its bits from
  the lowest, 0 a literal byte, 1 a reference of two bytes, little-endian, `0x1000 - (w &
  0xFFF)` back, `(w >> 12) + 3` long), each loaded at `0x80026000` over the one before. 27 of
  them are the game's places (the field), and each has its own copy of the message windows'
  code and its own three windows, at addresses of its own (for the first place, program 12 at
  `0x527000` in the file: windows `0x80162CD0`, `0x80162E40`, `0x80163030`).
- **So the profile looks for them.** The routine that sets a window up for a piece of script
  (`window, script`; at `0x8005CE48` in the first place) begins the same way in every place:
  in 25 of them compiled plainly (30 instructions, no address among them), in two (programs
  39 and 40) compiled for speed (22 instructions). The profile finds that routine in what is
  loaded, then every call of it, and takes the window from the instructions before the call
  that set `a0` (`lui`/`addiu` into `a0`, or into a register that is then moved to `a0`). It
  does this again when the first 64 bytes at `0x80026000` change (another program), then every
  second for ten seconds (the program may still be arriving), and every second while none is
  found. A window is believed only while what it holds looks like a page (its place in the
  script in main memory, a wait, valid Shift-JIS and attributes, a place on the picture), so
  that the windows of a place whose code a smaller program left behind give nothing. Tried on all 142 programs, unpacked from the disc and handed to
  the profile's own code: three windows in each of the 27 places, none in the others.
- **A window** (0x170 bytes): `+0x08`, `+0x0A` (s16) where its first glyph is drawn, in the
  game's picture; `+0x50` (u32) where it is in the script (0 once it is done); `+0x54` (s32)
  what it waits for (-1 a press, more than 0 frames before it goes on by itself, 0 while the
  page is being made); `+0x58` the kind of wait (0 a page, 1 a yes/no question, 3 the end);
  `+0x68` the page as drawn, Shift-JIS, with a space where a line ends; `+0xE8` a byte of
  attributes for each byte (`0x20` or `0x10`, `0x80` and a colour added; 0 for a line's end);
  `+0x168` (u32) its length. The whole page is made at once, not a glyph at a time.
- **The script** is Shift-JIS with codes below `0x20`: a page's head `10 '0' 11 <speaker> 12
  <face> 13 '=4095;' 03`, `0A` a new line, `0B` a new page after a press, `0C` a yes/no
  question, `10 '=70;' 01` a wait of 70 frames, `10 '=8195;' 05` something the scene does, 00
  the end.
- **Where it is drawn.** The game draws a picture 208 lines high (its drawing area ends at
  y 207), which the console shows in the middle of 224 lines: 8 lines lower. A full-width glyph
  is 16 pixels wide, a line 17 lower than the one before: in the first place the message
  window's glyphs are sprites at x 32, 48, … and y 151 and 168 (found in the game's list of
  things to draw).
- Single-byte characters (the one Spanish line of the first place, `%Ah! Esta llave`, where
  `%` is likely the game's `¡`) are taken to be 8 pixels wide; not seen drawn.

## How it was checked

On Linux without a display (2026-10-25), in a test home of its own, the user's SCPH-5500 BIOS:

- **Disc 1, from a new game**: the title, NEW GAME, the opening movies (their subtitles in the
  pictures; nothing reported during them or at the title), then the first place: the profile
  found its three windows as the place's program arrived (`message windows of this program:
  80162CD0 80162E40 80163030` in the log, during play, not from a savestate).
- Katoh by the dead climber, three pages in the message window, each reported as the picture
  showed it and when it showed it: お、おい、大丈夫か！ / だめだ死んでいる・・・ / 俺は雪がクッションになって
  ⏎ 助かったのか・・・; withdrawn when the window slid away. A click on 大丈夫 opened 丈夫 (じょうぶ)
  with the word marked over its glyphs.
- Katoh at his rucksack: よかった、俺のザックだ and よし！⏎中身は無事だ in the message window, then
  手袋を⏎手に入れた in the item window (`80163030`), followed as it slid in; a click on 手袋 opened
  てぶくろ, the mark on its two glyphs. When it closed, the item window kept its page and its
  place in the script and was slid out to x 318: at first its text stayed reported over the
  scene; the profile now counts only glyphs inside the picture, and it was withdrawn.
- **Every place, from the disc**: the 142 programs of `PROGRAM.BIN` unpacked and handed, one
  by one, to the profile's own finding code inside the running app (as data files of a test
  copy): three windows in each of the 27 places, nothing in the 115 other programs. Only the
  first place was played.
- **Disc 2**: booted; its profile loaded (`SCPS-10132`, build `9108E514`); the game went from
  its title to its memory card page (it starts from a save of disc 1, which this test home
  does not have), nothing reported. None of its places was played.
- A yes/no question (この深雪を進んでいきますか？) was only seen by making the game's message window
  read it in the first place (a test copy of the profile set the window's script pointer):
  the question was reported as a line. What the game draws for its answers was not seen.

Seen by the way: where the reader puts the marks of a word low in the picture. A glyph of the
message window (drawn at y 159 to 175 of 224) is drawn by the app at y 511 to 557 of its
720-pixel window, the reader's cell for it is at y 493 to 541 (`status`), about five lines of
the game's picture higher; in the middle of the picture (the item window) the two agree. The
profile's places are the game's own (sprites at y 151 + 8); this looks like the shared
layer's idea of the tube's curve, not the profile's, and is left to the maintainer.

**Not seen**: any place but the first, a scene that waits by itself (`10 '=70;' 01`, the dying
man's), the Spanish line, a yes/no question in play, Windows, the Thor, the core in
Furoshiki.
