# Kamaitachi no Yoru: Tokubetsu-hen (SLPS-01794)

Chunsoft's sound novel (1998), the Japanese disc; boot program `SLPS_017.94`, build
`AD2BBA84`. Profile: `reader/profiles/SLPS-01794.lua`.

## What is read

The story's text, which the game draws straight over the scene with no window: each
paragraph once, when the game stops to wait for a press, with every glyph's place, so that a
word is looked up where it is drawn. Earlier paragraphs of the page stay clickable until the
page is cleared. A choice's answers, as a choice, while the game shows one, and which answer
the game's cursor is on, so that in the Android app's *Play by touch* (and with the
desktop's) a tap on an answer picks it. The welcome page of a new game (ペンション『シュプール』へ
ようこそ。お客様のお名前は…) is read the same way: its words as a paragraph, そのまま はじめる / 名前を
変える as a choice. When the game leaves the story for its title or its menus (also when the
attract demo after the title ends) the page is withdrawn, so a tap there is the game's
again. Nothing else: the title's menu (redrawn every frame, never waited on) is not
reported. The game has no voices (`-- reader: audio never`).

## How the game keeps its text

VN Library's study of this disc (the maintainer's texthook project,
`docs/chunsoft-ps1-investigation.md` and `web/adapters/kamaitachi-text.mjs`, which runs the
game's own text routines) named the places; they hold in the running game:

- `0x800380EC` draws one glyph: `a0` x, `a1` y (in the 320×240 picture, the glyph's top left),
  `a2` its Shift-JIS code. `0x8483` and `0x8484` are the blinking marks that ask for a press,
  not text.
- `0x800792DA` (u8) is the width of the glyph drawn last: when the routine is entered for the
  next glyph it holds the previous one's (the font is proportional, 5 to 16 pixels).
- `0x800792DC` (u8) is the colour of the glyph being drawn: 8 for the story's text; 1, 2, …
  for the answers of a choice.
- `0x8007943A` (u8) is 1 while the game waits for a press.
- `0x80079295` (u8) is 1 while a choice is up.

Found on this disc (2026-10-19), with the app's development channel:

- While a choice is up the game draws its pointer (`0x848B`, a ▶ in its font; text.sjis makes
  it щ) through the same routine every frame, at the line of the answer the cursor is on
  (on the welcome page: x 50, y 105 or 127). The answers themselves are drawn once, each in
  its colour. The profile takes the pointer for no text and no new page, and reports the
  answer whose first line it is beside (`reader.cursor`).
- `0x8007942C` (u32) is the routine the game runs each turn: `0x801855D0` in the story (also
  on the welcome page and in the attract demo), `0x801091C0` at the title, the memory card
  question and the save books. Found by comparing dumps of eight moments of the story with
  three of the title and its menus; watched live through title, memory card, books, welcome
  page and story, and from the demo back to the title. (`0x80079290`, which looked like it
  at first, is 255 at the title but also in stretches of the story while text is read.)
- The welcome page's first glyphs come a few frames before the story's routine does; the
  save book's 未使用, drawn once seconds before, does not. So when the story's routine begins
  the profile keeps only the glyphs of the last half second.

The profile hooks the glyph routine (so the game runs on the interpreter, see `AGENTS.md`),
keeps the page's glyphs, and emits what was drawn since the last wait when the wait flag
rises. A glyph drawn above the last line means the page was cleared. A line that ends more
than a few glyphs short of the right edge ends its sentence (a new line in the text); one
that runs to the edge goes on (the game wraps a word anywhere).

## How it was checked

On Linux without a display (2026-10-19), with the user's SCPH-5500 BIOS: the disc booted,
its opening movie played, and after its title the game went on into the story by itself (no
button pressed). Paragraphs were reported as the pictures show them (「おいしい！」,
真理はスープを一口飲むと、声をもらした。, 小さな角切り野菜のたくさん入った、ミネストローネとかいうイタリア料理だ。, …), and a
click on 一旦 opened its entry, the word marked over its glyphs.

That first run had the profile reloaded by hand: loaded at boot, it failed with "hook: address
800380EC is not mapped" (on the user's Windows PC and on the Thor too), because the emulator
names the disc before it makes the console's memory. A game heard of before then is now started
on the first frame (`reader/core/emulator.cpp`). Run again on Linux the same day, in a home of
its own and with nothing reloaded, the profile loaded at boot and the story's text was reported
by itself after the title (食堂のテーブルにはすでに、ナイフやフォークがセットされていた。, …,
ただ彼女を見つめていた。). Not run again on Windows or the Thor.

**The fixes for touch play** (Linux without a display, 2026-10-19, *Play by touch* on in the
desktop app; the build of the same day):

- *The name page*: on the Thor its 39 characters were counted but a tap opened nothing. The
  page was reported, then the game drew its pointer beside そのまま はじめる, higher than the
  last line (名前を変える): the profile took that for a new page, withdrew the text, and
  reported the pointer alone as a paragraph (щ, seen the same way after the demo's choices).
  (The tap on そのまま はじめる that "picked" it on the Thor was a tap on the picture, which pressed
  ○ for the answer lit.) Now, from a savestate at the memory card question, ○ twice: the
  page came as one paragraph, ペンション『シュプール』へようこそ。 / お客様のお名前は　　　透　　様。 /
  おつれ様は　　真理　　様ですね。, the choice as そのまま　　　はじめる / 名前を変える, the cursor
  reported as 0. A click on 名前 opened its entry (名前 なまえ); a click on 名前を変える moved the
  cursor and confirmed it (では、お名前をどうぞ……。). From a savestate at the welcome page a
  click on the picture went on with では、ごゆっくり『かまいたちの夜』の世界をお楽しみ下さい.
- *The title after the demo*: from a savestate in the attract demo (its last pages, 家だって
  狭いんだから」 …), the game went back to its title by itself; before the fix the page stayed
  reported over the title, as on the Thor. Now the routine's word turned to `0x801091C0` and
  the page was withdrawn (no text reported, the title on the picture). A click there was the
  game's: three clicks went on past the title and the memory card question; a single click
  just after the title came up did nothing, twice (the game may not take a press then; a
  press of ○ over the development channel did).
- *A choice in the story*: from the welcome page, ○ through the first pages (the story's
  text reported throughout, also where `0x80079290` is 255) to the first choice, ぼく達は……。 /
  Ａ一旦部屋に戻って着替えると、玄関脇の談話室で落ち合った。 / Ｂ一旦部屋に戻って着替えてから、夕食
  までどちらかの部屋で話でもしようということになった。: the question as a paragraph, the
  answers as a choice of two lines, the cursor reported as 0. A click on Ｂ's second line
  moved the cursor and confirmed it; the story went on with Ｂ's sentence and the cursor was
  withdrawn. Ａ by a click was not tried (with the cursor already on it, a click confirms
  it at once).

A choice loaded from a savestate is not reported: the game does not draw its answers again,
and the profile starts a page afresh after a load.

**Not seen**: the name entry beyond its first line, a card.
