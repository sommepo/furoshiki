# Double Cast (SCPS-10053, SCPS-10054)

Yarudora Series Vol. 1: Double Cast (Sony Computer Entertainment / Production I.G, 1998), the
Japanese two-disc set; both discs carry the same boot program, `AC46E621`. Profiles:
`reader/profiles/SCPS-10053.lua` (disc 1) and `SCPS-10054.lua` (disc 2), the same file but for
its version line. They hook the game's code (so the game runs on the interpreter).

## What is read

- **The lines under the film**: every dialogue and narration line the game writes in its
  message window below the picture, once it is written out, with each character's place.
  A line the game stops in the middle of (it waits for ○ there, the red drop) comes in two
  pieces, the second when it is finished. A speaker the picture does not show stands on a line
  of its own above the words (`電話の声` / `「あの女には近づくな…」`) and is read with them.
- **Choices**: the answers (one per line, in the order shown: the game shuffles them each
  time), each at its bar, and the answer the game's cursor is on, so that with *Play by touch*
  a tap on an answer picks it. The game's small はい / いいえ windows (save, settings) are read
  the same way.
- **The game's other windows**: the memory card messages (`セーブデータを選択して下さい`,
  `データ１にセーブします / よろしいですか？`), `ここまでの思い出を記録しますか？`, the settings'
  help line and `設定を終了します / よろしいですか？`, `ダブルキャスト　ＤＩＳＣ　１をセットして下さい`.
- **Not read**: anything drawn as a picture (the title's 始めから / 各種設定, the settings'
  menu entries, 操作説明), and the words spoken in the films, which the game shows as text
  only when its setting says so (below).

**The game's setting matters**: the lines are only written when *各種設定, 表示, 台詞設定* is
*文字+音声* or *文字のみ*. A new memory card starts at *音声のみ* (voices alone, nothing
written), and then there is nothing to read but the choices. The setting is kept on the
game's card, which both discs share (disc 1's, `saves/SCPS-10053/`, since 2026-10-25; before,
disc 2 had a card and a setting of its own).

## How the game keeps and draws its text

Found on disc 1 (2026-10-25) from the boot program (disassembled from the disc into
`private/double-cast/`, not in Git) and the app's development channel:

- Each scene's script (`S0xx/SCN0xx.SSC` on the disc, loaded around `0x80199000`) holds its
  text in Shift-JIS with the hiragana as single bytes, `A6`..`DD` as in half-width katakana
  (`A2`/`A3` are 「」, `DE`/`DF` the voicing marks, `A0` before a byte keeps that byte as it
  is). A table at `0x8005EB44` turns them into full-width Shift-JIS (`0x80029224` does it, a
  voiced kana from the tables after it).
- The game has up to 16 windows of `0x1EC` bytes each from `0x8008B280`. Of a window: `+0`
  (s8) 1 while it is shown; `+8`, `+10` its place on the screen; `+12`, `+14` its size; `+16`
  its style (an index into 30-byte records at `0x80080FA0`; one whose `+2` is 8 shows each
  line of its texture as a bar of its own, `+22` apart on the screen: the choices); `+24` the
  answer its cursor is on (-1 none); `+268`, `+270` its texture's place in video memory;
  `+274` its texture's width; `+280` a line's height in it (16); `+300`, `+302` where the
  text begins from the window's place. The message window is the second (`0x8008B46C`; 264×48
  at 0,164, text from 26,182), the choices the third (`0x8008B658`).
- `0x800281EC` writes a string into a window; it draws one glyph a frame through
  `0x80029884` (`DrawGlyph`: a0 the glyph's Shift-JIS code, a3 its x in the window's texture,
  the stack's `+16` its y; the window is the caller's s3), which renders the glyph from the
  game's font (`SYSTEM/FONTD.DAT`) and loads it into video memory. Full-width glyphs are 12
  pixels apart, lines 16. `0x800290D8` (a0 the window) clears the lines before a new line and
  when a line goes; `0x800291B8` clears the whole texture.
- The sprites that show a window's texture (built at `0x80024800`) put its text at the
  window's place plus `+300`, `+302`; a bar window's line *n* at `+22 × n` below the first.
- The picture is 320×224: DuckStation shows the 240 lines the game draws without 8 at the
  top and 8 at the bottom, so the profile takes 8 from every y.
- The choices' answers come in an order of the game's own each time (the same answer was
  first, second or third on different runs); the window's texture has them in the order
  shown, and `+24` counts the bars from the top.

The profile hooks `DrawGlyph` and the two clearing routines, keeps each window's glyphs, and
reports a window's text when no glyph has come for six frames; a glyph drawn at or before the
last one's place starts the window's text anew (the game does not always clear it first). A
line that ends three cells or more short of the window's right edge ends there (a new line in
the reported text); a line that runs to the edge goes on. Leading spaces of a line are left
out (the game begins narration with a full-width space). A window that is closed (`+0` not 1)
or cleared has its text withdrawn.

## How it was checked

On Linux without a display (2026-10-25), desktop app (`build-desktop`, not rebuilt) with the
user's SCPH-5500 BIOS, in a test home of its own, the profile in the test home's
`reader/profiles` (the user's folder: the bundled copy is in the program only after a build).
The CRT picture was off in that home (it made the software drawing too slow to play on a
loaded machine).

- **Disc 1 from boot** (the profile loaded at boot, nothing reloaded): 始めから with 台詞設定 at
  文字+音声, the opening narration (`人は誰でも様々な顔を持っている` … `そしてどれがボクの本当の
  顔なんだろう`) and the first chapters played on with ○ (the first answer each time) through
  two of the game's save prompts: some 140 distinct lines and choices reported, compared with the pictures at a dozen places
  (the opening, `「君はいったい、誰？」`, `「ウソだろ？　まさか…記憶喪失！？」`, `「いや、そうじゃなく
  て、…ゴメン、言い方悪かったかな」` in two pieces and on two lines, `『こうして僕と美月の同居生活
  がはじまった』`, the save prompts); the cells measured against the glyphs' pixels in the
  window's picture (within a pixel or two of the 320×224 picture, horizontally and
  vertically).
- **The first choice** (`そんな…またダマそうと` / `う～ん…` / `病院で診てもらった方がいいよ`),
  from a savestate taken before it, with *Play by touch* on: a click on each of the three bars
  (in the order the game shuffled them that time) picked that answer, each leading to its own
  reply (`「少しずつ君のパターンが読めてきたよ」`, `「いや、そうじゃなくて、…」`, `「なにも覚えていな
  い？」`; the last the same as down, down, ○ by hand). The cursor was reported 0, 1, 2 as down
  moved it.
- **A word**: a click on 言 of `言い方` opened its entries (言い方 いいかた) with the word
  marked over its three glyphs.
- **Disc 2 from boot**: the profile loaded for SCPS-10054; the settings' help line and
  `設定を終了します / よろしいですか？` with はい / いいえ (cursor followed) were read in place,
  and 始めから on disc 2 answered `ダブルキャスト　ＤＩＳＣ　１をセットして下さい`, read in place.
  **Disc 2's story was not reached** (it follows from disc 1's end); it is the same program with
  the same kind of scripts, but its text was not seen read.

## Not done, not known

- **Text on screen when a savestate is loaded** is not read (it is only in the picture); the
  next line is.
- **Speakers** are not reported as such: the name line is part of the text.
- A half-width character the script keeps as it is (`?`, `｣` after a `?` in `「…ないの?｣`) is
  reported half-width, as the game shows it.
- **Not seen**: disc 2's story, an ending, timed choices running out (the timed choice's
  sparkles were seen, its answers read), the in-game menu (Select did not open it in the test),
  the □ button that hides the window (pressed once, nothing changed then), the Android app,
  Furoshiki's core, Windows.
