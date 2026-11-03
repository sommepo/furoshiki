# Azahar Reader

A fork of the [Azahar](https://github.com/azahar-emu/azahar) 3DS emulator for Android
that turns a game into something you can read and mine: tap a word **in the game's own
text** and its dictionary entry opens, make a card from it, and have your reading
counted the same way as in VN Library.

It only does this for games that have a **text profile** (see
[supported games](#supported-games)). Other games run exactly as in Azahar.

- [Text profiles: API and rules](profiles.md)
- [Headless harness for writing profiles](harness.md)
- [How it is built](architecture.md)
- Game notes: [Shin Megami Tensei IV](games/smt4.md)

## Install

The app is called **Azahar Reader** and has its own application ID, so it installs
beside regular Azahar and does not replace it. Build it with
`tools/reader/build-android.sh`; the APK is written to
`src/android/app/build/outputs/apk/vanilla/relWithDebInfo/`.

On first start, choose a user folder as in Azahar. Choosing the folder your Azahar
already uses shares saves and settings between the two apps (do not run both at once). The
setup then asks the reader's three questions, each of which can be passed and done later in
Reader settings: save sync (for playing on more than one device), a dictionary (nothing can be
looked up without one) and Anki (where cards go).

## Reading

Start a supported game and play as usual. Nothing is drawn over the game.

- **Tap a word in a text box** to look it up. A small popup opens beside the word:
  every dictionary entry that starts at the tapped character, longest match first,
  with how the word was inflected, frequencies and pitch, and one line per sense. The
  word is **outlined** in the game's picture while the popup is open, and the outline
  glides to the next word you look up (*Mark the word looked up*: outline, highlight or
  not at all). The popup stands right next to the word and stays put while you go to its
  **＋**. Tap another word to look that up; tap anywhere else to close.
- The popup is as small as what it says, and no taller than about a quarter of the
  screen: longer entries scroll. **Reader → Looking words up** has its largest height
  and its text size.
- The popup **closes by itself** when it has been left alone for a while: 15 seconds unless
  you set another time (*Close the popup after*; 0 keeps it open until you close it). A finger
  on it, or the cursor resting on it or on its word, keeps it open. With the cursor it can also
  close as soon as the cursor has left the word and the popup (*Close the popup when the
  cursor leaves*, under Cursor).
- **A text box never passes taps to the game.** A touch that begins inside one is
  taken by the reader, so looking a word up cannot advance the text or pick an
  answer. Everything outside the text boxes is the game's, as always. Advance with
  the buttons.
- This follows the screen layout you use: one screen per display, both screens on one
  display, or a hybrid layout. The same text shown twice can be tapped in both places.
- The boxed **＋** at the bottom left of an entry makes a card for that word (another
  corner can be chosen under **Reader → Looking words up**). When
  Anki has the word already the box shows an **open book** instead, which opens the
  note in Anki. **Reader** in the in-game menu has the rest: a card for the sentence
  alone, statistics, the backlog, dictionaries, Anki setup and the settings.
- The top right corner shows the **session so far**, small and dim: characters read,
  characters per hour and reading time. It can be switched off under **Reader →
  Statistics options**.

Outside a game, the app's own list has two entries: **Reading statistics**, and
**Reader**. The reader's page begins with *Statistics* (with what was read today) and
*Game scripts* (the games that can be read, and scripts of your own); under them is one
entry per section of settings, each a page of its own: looking words up, the cursor, the
reader panel, dictionaries, Anki and cards, statistics options, save sync, text output, the
CRT picture, and the game list (the HOME menu, only the games that can be read). These pages
are the same in every reader app. In the game list, a game whose text can be read has a
**boxed book** in the corner of its picture, and at first only those games are listed (every
game, in a setting).

Only text in the game's text boxes is covered: dialogue, narration, prompts and
selection lists. Menus and other loose text are not.

### The cursor

Words can also be reached without a finger on the upper screen. **Hold ZL**: the
bottom screen dims and becomes a trackpad, and a cursor appears on the main screen.

- A finger moved over the bottom screen moves the cursor, as on a laptop's trackpad; a
  stroke across the bottom screen takes it across the main screen. Either stick moves
  it too, slowly when pushed a little.
- A word the cursor **rests on** is looked up, as if tapped. On a character the cursor
  is a frame around that character; elsewhere it is a ring. Where no word begins
  (punctuation, say) nothing happens, and a popup that is open stays.
- A **tap on the trackpad clicks** where the cursor is: on the popup's box to make a
  card, beside the popup to close it.
- With each new line the cursor starts just before the first character. Let go of the
  button and the game has its bottom screen and sticks back; a popup that is open stays
  until the next line or a tap.

While it is held, the bottom screen's touches and the sticks do not reach the game,
and the button itself never does while a game with a text profile runs. **Reader
settings → Cursor** chooses the button (ZL, ZR, L, R, Select, Start, or none) and the
cursor's speed. With *Press once instead of holding*, one press switches the cursor on
and the next switches it off; the game's other buttons work meanwhile, so text can be
advanced with the cursor on. Any other key can hold the cursor as well: bind *Reader: cursor (hold)*
under Settings → Controls → Hotkeys. Where no display shows the bottom screen, the
whole main screen is the trackpad and nothing is dimmed.

Two more switches there are for a cursor that goes only where it is taken:

- *Frame the character under the cursor*, on unless switched off. Off: the cursor
  stays a ring over the text as well, and moves freely instead of going from
  character to character.
- *Jump to the start of each new line*, on unless switched off. Off: the cursor stays
  where it was left, also when a new line comes and when it is switched on again (the
  very first time it starts in the middle of the main screen). A cursor that was left
  like this has not been pointed at what is under it: it looks a word up once it has
  been moved, however little, or on a tap on the trackpad.

### The panel

For a line the game does not place on screen (or with *Tap words in the game's own
text* switched off), the **reader panel** shows the line as text of its own, with the
speaker and the game's furigana, and words are tapped there instead. On a device with
two screens it goes on the second screen. It has buttons for the backlog, a sentence
card, the menu, hiding it, and **▶** to press the game's advance button; a long press
on the text copies the line. Text you have met before is tinted, like read text in VN
Library. **Reader → Reader panel** has its size, position and opacity, and
*Only when the game's text cannot be tapped*, which is on by default: switch it off to
have the panel all the time.

Four actions can be bound to controller buttons in Azahar's control settings, next to
its own hotkeys: show or hide the panel, make a sentence card, open the backlog, and
hold for the cursor.

## Dictionaries

**Reader → Dictionaries → Import dictionary** takes Yomitan dictionary archives
(`.zip`): term dictionaries for definitions, frequency and pitch dictionaries for the
extra lines. Several files can be picked at once. A large dictionary takes a minute
or two to import. The order of the list is the order entries are shown in; each
dictionary can be switched off or deleted.

Nothing is bundled and nothing is downloaded: bring the dictionaries you already use.

## Cards

**Reader → Anki setup** chooses where cards go:

- **AnkiDroid on this device**, through AnkiDroid's own interface for other apps
  (press *Connect* and allow access once), or
- **AnkiConnect**: Anki on a computer, or AnkiConnect Android on this device
  (`127.0.0.1:8765`). Enter its address, and the API key if you set one. Media is sent
  inline, so the other side does not need to reach this app's files.

Then pick the deck and note type and say what each field gets. A field is a template:
markers such as `{expression}`, `{sentence}`, `{screenshot}` or `{audio}`, with any
text around them. *Suggest from field names* fills in common note types. The **＋** in
the lookup popup makes a card for that word; *Make a sentence card* (in the in-game
menu, on the panel, or bound to a button) makes one for the line alone.

**Words Anki already has.** When a popup opens, its boxes say at once whether Anki has
a note whose first field is what a card for that word would put there: the app keeps a
copy of the deck on the device and asks Anki itself afterwards, to confirm. If Anki has
the word, the box shows the open book and no second card is made. A turning ring means
it is not known yet (the first moments after the app starts, before the deck was read);
it makes a card all the same. Only the deck cards go to (with its subdecks) is looked
at, unless *A word in any deck counts as already there* is ticked. With *Allow duplicate
cards* the ＋ stays beside the book. If Anki does not answer, the ＋ has a small grey dot,
and the question is put again later.

| Marker | |
| --- | --- |
| `{expression}` `{reading}` `{furigana}` `{furigana-html}` | the word (on a sentence card: the line, so that a note type made for words still gets a first field) |
| `{glossary}` `{glossary-first}` `{glossary-plain}` | definitions |
| `{sentence}` `{sentence-plain}` | the line, with or without the word in bold |
| `{sentence-furigana}` `{sentence-furigana-html}` | the line with the game's furigana |
| `{screenshot}` | the game picture, without the reader panel or on-screen controls |
| `{audio}` | what the game played during the line |
| `{word-audio}` | The word as somebody says it, from JapanesePod101's dictionary (fetched when the card is made; empty when there is no recording) |
| `{speaker}` `{source}` | speaker name, game title |
| `{frequencies}` `{frequency-rank}` `{pitch-positions}` | from frequency and pitch dictionaries |
| `{part-of-speech}` `{inflection}` `{matched-text}` | details of the match |

A card belongs to the line the popup was opened on, even if the game has moved on.
The screenshot is of that line when it was one of the last 30 (the number is a
setting); the audio is the game's whole output from when the line appeared until the
next line or the configured maximum (20 seconds), music and effects included. The
newest line keeps its audio for as long as it stays on screen; an older line has audio
only while it is among the last 90 seconds the game played.

**Line audio on a card.** That audio is not a voice track, and nothing guesses whether
a line was spoken: a game without voices has a clip for every line too, of its music.
So what a card does with it is a choice:

- *Always add it*, *Ask for each card* (as installed) or *Never add it*.
- **Reader → Anki and cards → Line audio on cards** is the choice for every game.
- **Reader → Line audio on this game's cards**, in the in-game menu and in the panel's
  menu, is one game's own choice, which comes first: *Never* for a game without voices.

A card that asks waits for the answer before it is sent. The question is three buttons
with signs and no words: **▶** plays the clip (**■** stops it; the game is not silenced
meanwhile: what it plays at that moment is heard along with it), a speaker with a tick
makes the card with the audio, a speaker with a cross without it, and a tap beside the
question makes none. The
picture is the one of the moment the card was asked for, not of the answer.

## Statistics

Days, sessions and reading time are counted as VN Library counts them; characters more
strictly:

- **characters** are the Japanese characters of the text: kana and kanji. Punctuation,
  symbols, digits, Latin letters and spaces are not reading and are not counted; nor are
  speakers' names and furigana; choices are counted separately;
- a line shown again **in the same session** (after a menu, a loaded save, or when the game
  repeats itself) is counted once; met again in a later session it is **reread**, not new;
- **reading time** runs while the game is in front, a line has been shown and you
  have advanced or looked something up within the last five minutes; it stops when
  the game is paused or in the background;
- a break shorter than four hours continues the **session**;
- a **reading day** starts at 04:01.

**Reading statistics** (or **Reader → Statistics**) shows today, the running game, all games, the last 30 reading
days and the sessions; tap a session to delete it. Statistics can be paused, exported
as JSON or CSV and cleared per game or entirely. Clearing removes sessions and totals
only: the backlog and what counts as already read stay. Everything is stored on the
device.

**Pausing** is also on a controller button of your choice (*Statistics options → Button
that pauses statistics*; none until you pick one; the game does not hear of that button).
While the statistics are paused two small bars show in the top right corner of the game,
before the session line or alone when that line is switched off.

**Adding them up with other devices.** The save sync service (below) also keeps each reader
app's statistics side by side. With its address and code entered under *Reader → Save sync*,
this app sends what it has counted (every reading day of every game: characters, time, lines;
no text) each time a game is closed. Nothing is sent until both are entered, and nothing
comes back: this app shows its own numbers, and whoever runs the service adds the devices up.

## Save sync

**Reader → Save sync** keeps each game's save the same on your devices, through the save sync
service the other reader apps use (Eden Reader's, run on a computer of yours). Enter the
service's address (for example `192.168.1.50:8642`) and its code; *This device's name* is what
the other devices call this one. Then:

- a game's save is fetched from the service **before the game starts**, when the service's is
  the newer one, and this device's is sent when it is;
- the save is sent **a moment after the game saves**, and once more when it stops;
- when the app starts, every game that has a save here is brought in line;
- when a save changed on this device and on another one since they last agreed, nothing is
  replaced: the game appears under *Saves changed on two devices*, and you choose which to
  keep (the other is kept on the service). A save replaced on this device is kept in save
  sync's backup folder first.

A 3DS game's save here is its **save data** (`sdmc/Nintendo 3DS/…/title/<high>/<low>/data` in
the user folder), named on the service by the game's program ID; a game's **extra data**
(extdata) is not synced yet. *Sync now* asks the service whether the address and code are
right, then brings every game in line. The desktop app has the same in *Reader → Save sync…*.

The user folder is a folder chosen with Android's file picker, which only the emulator's own
file functions reach: save sync copies a game's save data into a folder of the app's own
(`files/reader-savesync/<a name for the user folder>`), syncs that copy, and writes a save
fetched from the service back into the user folder, beside the old one first, so that a
failure on the way leaves the old save as it was.

## Text output for other tools

**Reader → Text output → WebSocket text server** sends each new line to
anything connected on port 6677: a texthooker page in a browser on the same device,
or a mining tool. Plain text, `{"sentence": …}` and a fuller JSON event are available.
It listens on this device only unless *Allow other devices* is on; there is no
password, so anything that can reach the port can read the text. Copying each line to
the clipboard is a separate option.

## Supported games

The app shows this list itself (**Reader → Game scripts**), from what each profile says in
its first lines; the notes here say more. A script of your own is installed there too
(*Install from a file*); it wins over the bundled one for its game. The list of scripts to
install from is published with Furoshiki (`sommepo/furoshiki`, made by the repository's
`tools/public-tree.py`); until that repository is public it cannot be fetched.

| Game | Title ID | Notes |
| --- | --- | --- |
| 真・女神転生IV (Japan) | 0004000000088A00 | [Dialogue, narration and choices, page by page, tappable in the game's picture](games/smt4.md). Movie subtitles and menu text are not covered. |

A profile is tied to one build of a game and says so when it meets another.

## Limits

- No text without a profile for that game.
- Subtitles that are part of a video cannot be read.
- Audio on cards is the game's mixed output, not an isolated voice.
- The CPU JIT must stay enabled (it is by default); hooks do nothing under the
  interpreter.
