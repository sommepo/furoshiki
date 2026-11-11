# The dictionary popup's page

The popup as a web page, the same on every platform: a page (`popup.html`), its layout and the
console's own theme (`base.css`), the script that draws it (`popup.js`) and the other themes
(`themes/*.css`). Since 2026-10-24.

**The looks** (`reader_popup_theme`): `console` (base.css in the console's own colours), `furoshiki`,
`yomitan-dark`, `paper`, `paper-dark`, and since 2026-11-09 (the maintainer: "several more
dictionary layout presets to make the dictionary quality rival Hachidori") `solarized-dark`,
`solarized-light`, `glass` (see-through, two thirds solid by itself), `dense` (everything run on
in lines, after Nazeka), `kuro` (a black sheet at 85 %, a block per dictionary, after JL),
`shoin` (a ruled serif page, senses numbered in the margin), `ookii` (large type) and `kumihan`
(組版: type and hairlines only, one accent), and since 2026-11-10 `kon` (Furoshiki 3's Kon: cream on indigo, a vermilion rule along the top and for the word not yet known). The new ones are set, not decorated (the maintainer,
2026-11-09: a refined, typographic look): hierarchy by size, weight and tracked small capitals,
hairlines where a box would be, and **every colour a variable** (`--ground`, `--text`, `--dim`,
`--accent`, `--rule`), so the user's own CSS re-colours a whole look in one `:root { … }`. A look
is one CSS file; **a new one is its file here and its name in the lists**: `ReaderSettings::
PopupThemes` (shared/reader/desktop/settings.cpp), `ReaderSettings.POPUP_THEMES` (Kotlin), the two
string arrays in `shared/android/res/values/strings_reader.xml`, and the settings dialog's list in
`shared/reader/qt/reader_dialogs.cpp`. A look sets its ground and corners as the variables
`--ground` and `--radius`, never as `.popup { background; border-radius }`: the sliders below
work through them. `tools/popup.py preview DIR ['{"opacity":70}']` writes each look as a page of
its own over a stand-in scene (a name's card, a word of two dictionaries, an inflected one), for
a headless browser's pictures.

| Where | Drawn by | How the files get there |
| --- | --- | --- |
| Furoshiki on Linux | Qt WebEngine (`shared/reader/qt/popup_web_engine.cpp`), where the Qt it is built with has it: `READER_HOME_WEB=1 tools/build-desktop.sh` builds with the official Qt in `~/.local/share/furoshiki-qt` (the toolchain's usual Qt has no WebEngine) | `tools/popup.py inc` makes `shared/reader/qt/popup_page.inc` (the page with its CSS and script put in, and the themes) |
| Furoshiki on Windows | the system's WebView2 (`popup_web_view2.cpp`), through `WebView2Loader.dll` beside the program (Microsoft's WebView2 SDK, fetched by CMake) | the same `popup_page.inc` |
| The Android apps | a WebView (`ui/PopupPage.kt`) | `shared/android/reader.gradle` copies this folder into each app's assets (`reader/popup/`) |
| A fork's own desktop app, or no engine (no WebView2 runtime, the JVM tests) | the popup draws itself as before (`LookupPopup`, `LookupPopupView`): the "classic" look | — |

After any change here: `tools/popup.py inc` (and `tools/popup.py check` says whether it was).

## What the app hands over

`Reader.show(model)`, the model made by `ReaderApp::PopupModel` (desktop) and `PopupModel.kt`
(Android), every string put into the page as text, never as markup:

```json
{ "corner": "bottom_left", "duplicates": false, "markUnknown": true, "scale": 100,
  "look": { "opacity": 85, "radius": -1, "compact": 2, "hideTags": false, "portrait": "small" },
  "console": "station-reader", "game": "SLPS-01794",
  "labels": { "add": "Add to Anki", "book": "Already in Anki: show the note", "forms": "forms",
              "dictionaries": "Open the dictionaries" },
  "entries": [ { "expression": "客", "reading": "きゃく", "reasons": [], "pitches": ["[0]"],
                 "frequencies": [ { "dictionary": "JPDB", "value": "1234" } ],
                 "history": "Looked up 2 times", "unknown": true, "state": "add",
                 "senses": [ { "heading": "JMdict", "dictionary": "JMdict", "number": 1,
                               "tags": ["n"], "glosses": ["guest", "visitor"], "notes": "" } ],
                 "forms": ["客"] } ],
  "message": { "text": "No entry for …", "dictionaries": false } }
```

An entry from the running game's names dictionary (a dictionary whose `furoshikiScope` has
the game: `docs/titles.md`) has `"name": true`, and so has each of its senses; the page tags it
名前 (`labels.name` may say it otherwise), and its senses do not say 名前 again.

**A name's card** (2026-11-09; the maintainer, of a name whose entry was a catchphrase run on
after its reading: "it should say the character name and ideally a portrait too"). Such an
entry also has `"card"`, read from its dictionary's one glossary item (`Names::CardOf`,
`NamesDictionaries.cardOf`; `tools/titles.py card_gloss` writes it):

```json
"card": { "id": "c53392", "kind": "person", "name": "真宮寺 是清", "reading": "しんぐうじ これきよ",
          "romaji": "Shinguuji Korekiyo", "role": "Main character", "also": ["超高校級の「民俗学者」"],
          "about": "In order to study folklore, …", "image": "data:image/jpeg;base64,…" }
```

The page draws it in place of the name's own senses (which hold the same as plain lines, for the
classic popup and for cards): the picture at the left, the full name under its reading part by
part, the romaji and what part it has, its other names, a sentence about it. `image` is the
picture from the dictionary's zip (`img/<id>.jpg`, the dev line's dictionaries only) as a data
address, read once and kept (`Names::Picture`, `NamesDictionaries.picture`: the zip is the one
`Ensure`/`ensure` was last asked about); without one the card has no picture. Other dictionaries'
senses of the same word follow the card as ever. `look.cards: false` keeps the card out (an app
that says who a name is in a place of its own: Furochibi's pane).

**The look's own settings** (`"look"`, 2026-11-09), each a reader setting on both platforms and,
in Furoshiki, a layered one (`popup.opacity`…) with the rest of the popup's:

| `look` | Setting | What |
| --- | --- | --- |
| `opacity` | `reader_popup_opacity` | how solid the ground is, 20 to 100 %; 0: as the look has it. The page sets `--opacity`; `.popup`'s background is `--ground` mixed with transparent by it. The popup's window is see-through on all three engines (WebView2's default background, Qt WebEngine's page and the Android WebView are transparent), so the game shows through. |
| `radius` | `reader_popup_radius` | the corners, 0 to 28 px; -1: the look's (`--radius`) |
| `compact` | `reader_popup_compact` | how many senses of a word before a "+N more" line (a tap shows the rest; `labels.more`); 0: all |
| `hideTags` | `reader_popup_hide_tags` | the senses' part-of-speech tags left out (`.popup.no-tags`) |
| `portrait` | `reader_popup_portrait` | a name's picture: `small`, `large` or `off` (`.popup[data-portrait]`) |
| `accent` | — | `#rrggbb` for `--accent`; no setting yet, a style file may carry it |

**No blur behind the popup**: a page can only blur what is in the page, and the game is another
window (or surface) under it, so a slider for it would do nothing. `glass` asks for
`backdrop-filter` all the same, for an engine that one day composes the two.

`Reader.ask(index)` puts the question about a card's audio in the place of that entry's buttons
(listen, with the audio, without; the page answers `listen`, `keep` or `drop`),
`Reader.askPlaying(bool)` turns listen into stop, `Reader.askDone()` gives the buttons back.
Then `Reader.setBox(index, state)` (`add`, `working`, `inDeck`, `checking`, `away`),
`Reader.setUnknown(index, bool)`, `Reader.setStyle(id, css)` for the `console`, `theme` and
`user` stylesheets, `Reader.scrollTo(y)`.

## What the page says back

A JSON message by the engine's way (`chrome.webview.postMessage`, Android's `readerAndroid.post`,
or the console line `reader:{...}` that Qt WebEngine reads): `ready` once; `size` after every
drawing (`height` of the content in CSS pixels, `scrolled`, `boxes` with each card button's
`index`, `kind` and place, `text`); `add`, `book` (with `index`) and `dictionaries` for a press.
The app waits for the first `size` before it shows the popup, so it comes up at its height.

## Settings, the user's CSS, and a style as one file

The look (`reader_popup_theme`), whether unknown words are marked, how often a word was looked
up, and the user's own CSS (`reader_popup_css`, the page's `user` stylesheet) are reader
settings. In Furoshiki they are also common settings on its CONSOLES page, for all consoles,
one console or one game (`home/src/popup_style.*`), the CSS there as files in
`program/popup-styles/`. A popup style travels as one file, written and read by Furoshiki's
*Export / Import popup style* and the Android apps' *Export the popup style… / Import a popup
style…*:

```json
{ "format": 1, "kind": "reader-popup-style", "theme": "yomitan-dark", "markUnknown": true,
  "lookups": "where", "css": ".popup { border-radius: 12px; }",
  "look": { "opacity": 85, "radius": -1, "compact": 2, "hideTags": false, "portrait": "small" } }
```

`look` is since 2026-11-09; a reader from before it passes it by, and a file without it changes
none of those settings.

## Designs (Furoshiki's popup customiser, since 2026-11-10)

The user's own CSS is gone from Furoshiki: its customiser makes a **design** over a look
(`home/src/popup_design.*`) and hands the page the stylesheet made from it, through the same
`Reader.setStyle("user", css)`. In the customiser's preview the page is put in **design mode**,
`Reader.design({pick, sizes, blocks, parts})` (null: off) and `Reader.pick(id)`: a click marks an
element (`.design-picked`) and says `{"type": "pick", "element"}`; a block of an entry or a part of
its headline dragged says `{"type": "move", "which": "entry"|"head", "id", "before"}`; the picked
element's corner handle says `{"type": "value", "key", "value"}` as it is dragged. A popup over a
game never enters it.

## The classes, for a theme or the user's own CSS

| Selector | What |
| --- | --- |
| `.popup` | the whole popup; `data-console`, `data-game`, `data-corner`; `.no-mark` when unknown words are not to be marked |
| `.entry` | one headword; `data-index`, `data-state`; `.unknown` when Anki does not have it |
| `.tag.name[data-category="name"]` | 名前, beside the reading of a name in the running game (an entry with `"name": true`, from the game's names dictionary); `.sense.name` is one of its senses |
| `.name-card[data-kind]` (`.pictured` with a picture) > `img.portrait`, `.who` > `.who-name` (a `ruby` per part of the name), `.who-line` > `.romaji`, `.tag.role[data-category="role"]`; `.also` > `.alias`; `.about` | a name's card; `.popup[data-portrait="small|large|off"]` |
| `.sense.later`, `.link.more`, `.entry.all` | compact definitions: the senses past the first few, the line that shows them, an entry with all shown |
| `.popup.no-tags` | part-of-speech tags are not to be shown |
| `.head`, `.headword` (a `ruby` with the reading as `rt`, hidden unless a theme shows it), `.reading`, `.reasons`, `.pitch` | the headline |
| `.tags.frequencies` > `.tag.frequency[data-dictionary]` > `.tag-name`, `.tag-value` | frequency lists |
| `.lookups` | how often it was looked up |
| `.dictionary-name[data-dictionary]` | a dictionary's name where several have senses |
| `.senses` > `.sense[data-dictionary]` > `.number`, `.tag[data-category="part-of-speech"]`, `.glosses` > `.gloss`, `.notes` | the senses |
| `.forms` > `.tag[data-category="forms"]`, `.form` | other spellings |
| `.card-buttons` > `.card.add`, `.card.book` (`data-state`) | the card buttons, at the foot of their entry |
| `.message`, `.link` | a message instead of words |

Variables the console's look sets (`base.css`): `--text`, `--dim`, `--accent`, `--ground`,
`--edge`, `--box`, `--box-pressed`, `--sign`, `--book`, `--font`, and `--scale` from the popup's
text size.
