# The dictionary popup's page

The popup as a web page, the same on every platform: a page (`popup.html`), its layout and the
console's own theme (`base.css`), the script that draws it (`popup.js`) and the other themes
(`themes/*.css`). Since 2026-10-24.

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
  "lookups": "where", "css": ".popup { border-radius: 12px; }" }
```

## The classes, for a theme or the user's own CSS

| Selector | What |
| --- | --- |
| `.popup` | the whole popup; `data-console`, `data-game`, `data-corner`; `.no-mark` when unknown words are not to be marked |
| `.entry` | one headword; `data-index`, `data-state`; `.unknown` when Anki does not have it |
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
