# ウィッシュルーム 天使の記憶 (AWIJ)

Wish Room: Tenshi no Kioku (Cing, Nintendo, 2007), known elsewhere as Hotel Dusk: Room 215. The
Japanese cartridge, `AWIJ`. Script: `reader/profiles/AWIJ.lua` (2026-11-14). Title record
`titles/wish-room.json` (VNDB v697), guide `wish-room`.

## Playing it

- **Held like a book.** The core turns the picture a quarter to the left by itself ("nds.rotation"
  auto): the upper screen on the left, the lower one (the touch screen) on the right. At the first
  question choose **右利き**; 左利き would want the setting on "right", and the script's places
  assume the left turn.
- **Everything is the stylus.** A tap must be held for a few frames: the harness's `touch X Y`
  with its default `hold=4` is not taken at the hotel door, `hold=10` or more is. The text goes on
  with A as well as with a tap. Walking is a finger held on the map; the notebook is the fourth
  icon at the foot of the lower screen (`touch 12 108` in the harness). **The magnifier (third
  icon, `touch 13 78`) lights up only when Kyle stands at something to look at, facing it**;
  while it is dark a tap on it is taken as a step on the map. At the front desk: walk along the
  counter until Kyle is under the bell's end of it, face up, the magnifier lights; tap the bell
  twice (`touch 63 20`), then ring it three times (`touch 175 95`). Things handed over (the pen,
  the key, the pamphlet) are taken with two taps; the registration card is filled in by drawing
  along each of its fields (`drag`).
- In the harness: `touch 125 133` is 右利き, `touch 111 103` the title's START, `touch 96 96 hold=30`
  opens the hotel's door. Harness coordinates are the lower screen's own (256×192, not turned): a
  point (px, py) of the turned lower screen as seen in a screenshot's right half is
  `touch (255 - py) (px - 192)`.

## What is read

The message engine keeps the page on screen in one object (its class word `0x020F3704`), which the
engine's word at `0x020FDF8C` points at while a message window is up (it points at other objects
while Kyle walks). The object:

| Offset | |
| --- | --- |
| `+0x08` | State: 1 while a page is being typed, 2 once it is all there, 3 with the ▼. |
| `+0x28`, `+0x128` | Two slots of 0x100 bytes, each a page as copied from the room's script, its line breaks made `0x0A`, ended by `0x0A 0x0A`. Pages go to either slot. |
| `+0x228`, `+0x22C` | The typewriter's place in each slot (an address). |

A page begins with its header, `%c0%d1%w0%p0000102%n0`: `%d` the screen (1 the upper), `%p` the
person (five digits) and the face (two). Then the text, with codes: `%cN` a colour (`%c2` blue,
Kyle's thoughts), `%mNNN` a gesture, `%eNN` an effect, `%sNNNNNNN` a sound, `%bNNNN` the music,
`%r…` a jump. The script emits a page once it is all there, with its speaker, its lines joined
(they break mid-sentence), and lays out each character: 12 dots wide (6 for a half-width one),
16 dots a line, in each screen's turned picture (`space` 192×256, so the program places it on the
screen the core says).

The people, from the script (who says what to whom) and VNDB:

| %p | | %p | |
| --- | --- | --- | --- |
| 00001 | カイル (Kyle Hyde) | 00009 | アイリス (Iris) |
| 00002 | ダニング (Dunning Smith) | 00010 | ケビン (Kevin Woodward) |
| 00003 | ローザ (Rosa Fox) | 00011 | メリッサ (Melissa Woodward) |
| 00004 | ルイス (Louis Franco) | 00015 | エド (Ed Vincent, on the phone) |
| 00005 | ミラ (Mila Evans) | 00016 | レイチェル (Rachel) |
| 00006 | ジェフ (Jeff Angel) | 00101 | Kyle's narration over the black screen: no speaker |
| 00007 | ヘレン (Helen Parker) | 65533 | what happens ("ローザは…出て行った"): no speaker |
| 00008 | マーティン (Martin Summer) | 65534 | what is looked at, and empty pages: no speaker |

00014 and 00017 have pages with no text (effects only).

In a conversation a page may start with `%!` before its header (a line that can be pressed,
突っ込み, drawn in orange with a yellow mark); each speaker's window is at the foot of the screen
`%d` names, under their portrait, Kyle's and the other's alike. The black screen's narration
(00101) is set higher, at its own place.

**What Kyle picks from** is an object of another class, `0x020F3840`, which `0x020FDF8C` points at
while it is up: up to four rows of 0x20 bytes from `+0x3E`, each a mark and the words. `◆` marks
an answer (two buttons over the other's portrait: the answers to a question, and the questions a
突っ込み offers); `д　` (drawn as a "?" box) a question of 尋ねる, on the notebook's page. The script
emits the rows as one choice (channel "choice", one row a line, without the marks) and lays them
out; once one is picked its row scrolls the words in full and the object goes, which is the same
choice, not a new one.

## The script on the cartridge

`data/local/jp/textdata.wpf` (and `textdata_ex.wpf`), indexed by `textdata.tbl`: 0x20-byte entries,
a name of 0x18 bytes, then size and offset. Each file in the `.wpf` has a 0x20-byte header (its
name), then a 0x10-byte one: `12 3D DA 01` (`00` stored as it is), the size unpacked, the size
packed. Packed: a flag byte, low bit first, 1 a byte as it is, 0 a copy of three bytes: a 16-bit
place `p` and a length `n + 4`; the copy starts `(out - (p + 0x103)) & 0xFFFF` bytes back (zeros
before the start). A room's `EPn_Rnnn.txt` unpacked is a count, that many offsets (from the end of
the table) and the pages, each ended by `00 00`, its lines by `00`. In memory the offsets are made
addresses (the room's text lies whole in RAM while the room is played). `EPn_BOOK.txt` is of
another form (not looked at). `tools/reader/wpf.py ARCHIVE` lists, unpacks (`--extract DIR`) and
prints a script a page a line (`--text`): `tools/reader/wpf.py FILES/data/local/jp/textdata
--match EP1_R102.txt --text` (FILES: the cartridge unpacked by `ndsfs.py --extract`).

## Checked (2026-11-14, Linux harness only)

- The opening's narration on the black lower screen (10 pages, Kyle's monologue): every page once,
  when it is all typed, laid out over its characters (screenshots with the boxes drawn).
- Chapter 1's first lines in the lobby, Kyle's thoughts in the window at the foot of the upper
  screen with the "Kyle" plate: speaker カイル, laid out over the characters; the window withdrawn
  when Kyle walks.
- A savestate loaded mid-page: the page is reported again.
- What is looked at (the front desk with the magnifier): at the foot of the lower screen.
- The whole check-in with Dunning (about 80 pages): his lines and Kyle's on either screen with
  their speakers and places, the `%!` lines, the answer choice (スイートルームにして… / 安い部屋で
  いい), a 突っ込み's questions (いい名前だろう / 何を気にしてる？), 尋ねる's two questions
  (荷物が届いている… / 俺と同じ名前の客…) and the answer that follows; each choice once, laid out
  over its rows.

## Not yet seen, not read

- **The nameplate before an introduction.** The game shows "???" over Dunning until he gives
  his name; the script names him (by `%p`) from his first line.
- **The notebook's pages** (人物's profiles, items, memos): the lists are handwriting (pictures);
  a person's profile (【ブライアン・ブラッドリー】俺が捜している親友…) is in the script's text but
  shown by the notebook's own object (`0x020F8C64`), not read yet.
- The opening's film-strip subtitles (「イエス、ボス」…) are pictures (`opimg_jpn.wpf`): not read.
- A choice of three or four rows (the places of the third and fourth are a guess).
- The left-handed turn ("nds.rotation" right): the places would be wrong.
- Any device.
