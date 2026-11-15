# What is known of each game's text (2026-11-14)

What was found while the core was made, for whoever writes the scripts. Wish Room has its script
(2026-11-14): `wish-room.md`.

## ウィッシュルーム 天使の記憶 (AWIJ)

Its script and what it reads: `wish-room.md`. What was known before it:


- Held like a book: the core turns the picture to the left by itself; choose **右利き**
  (right-handed) at the first question. Everything is done with the stylus: in the harness the
  title's START is `touch 111 103`, and a tap anywhere on the lower screen goes on.
- Its text is in `data/local/jp/textdata.wpf` (and `textdata_ex.wpf`), indexed by `textdata.tbl`:
  0x20-byte entries, a name of 0x18 bytes, then size and offset; each file in the `.wpf` has a
  0x20-byte header, then its data, compressed (it starts `12 3D DA 01`, an LZ form with `FF` flag
  bytes). Per episode and room: `EP1_R102.txt`, `.blk`, `.chk`, `.kif`, and `EP1_.ttk`. The text
  inside is Shift-JIS with codes: `%c0%d1%w0%p0000201%n0あんたの部屋の鍵だ`, `00` a line break
  within a message, `00 00` its end.
- In memory, a room's item remarks lay decoded like that (around `0x0226B000` early in episode 1).
  The dialogue lines being shown (the opening's 「あんたの声は、オレのイカレタ脳ミソに響きすぎる」)
  were **not** found as Shift-JIS in RAM: they are probably drawn straight from the compressed
  data or from glyph codes. The way in is likely the routine that draws a character (find the
  font, `findptr` to it, hook its readers).
- `data/local/jp/SL_JPN.txt`, `SR_JPN.txt`: the save and load screens' strings, plain Shift-JIS.

## 極限脱出 9時間9人9の扉 (BSKJ)

**Read by `reader/profiles/BSKJ.lua` since 2026-11-14: `games/999.md`.** The novel's text is in
memory in a Shift-JIS of the game's own (hiragana as the single bytes `A6`–`DD`); every text
window goes through two methods of one class and one routine that draws a character, which the
script hooks. Scripts are `scr/*.fsb`
(SIR0 containers); `etc/` has the font (`kanji.dat`, `kanji_s.dat`), the system messages
(`sysmes.dat`) and `game_text.ini`. Played upright (no turn): START, then A on はじめから.

## 銀河鉄道999 DS (B39J)

A test cartridge only (not the "999" that was asked for): it boots to Culture Brain's logo.
