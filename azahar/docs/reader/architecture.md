# How the reader is built

```
game code (ARM11, JIT-compiled)
   │  hooked instruction → host call with the guest registers
   ▼
Reader::Engine (src/reader/core/)            runs the game's Lua profile, reads guest memory
   │  TextEvent {text, ruby, speaker, channel, id, meta, frame, audio position, window}
   │  TextLayout {window, where each character and the text box are on a screen}
   ▼
frontend
   ├─ Android: jni/reader_jni.cpp → ReaderNative → ReaderSession → panel, backlog,
   │           statistics, dictionary, cards, WebSocket server
   └─ Linux:   src/reader_harness (headless, for development and tests)
```

## Core (C++)

`src/reader/core/`

- **engine** — owns the Lua state of the running profile, the hook table, per-frame and
  state-loaded callbacks, and the event callback. Follows the emulation lifecycle
  through four calls added to upstream code: game loaded (`core.cpp`), shutdown
  (`core.cpp`), savestate loaded (`savestate.cpp`), every VBlank (`video_core/gpu.cpp`).
  A profile survives a savestate load; the JIT is rebuilt and picks the hooks up again.
- **lua_api** — the `reader`, `mem` and `text` tables. Lua's `io`, `os`, `package` and
  `debug` libraries are not built, and compiled chunks are not loaded. A count hook
  ends a callback that runs away, and the allocator caps what a script may hold. The
  C++ side never calls an allocating Lua function outside a protected call: at the cap
  those raise an error, and an unprotected Lua error ends the process.
- **text_codec** — CP932 and UTF-16 to UTF-8, UTF-8 repair, hashing, JSON escaping.
  Events always leave the engine as valid UTF-8.
- **audio_tap** — a ring of the most recent DSP output, off until a frontend enables it
  (`audio_core/dsp_interface.cpp` feeds it). Events carry the tap position, so a line's
  audio can be cut out later.

`src/core/arm/dynarmic/arm_dynarmic_reader.cpp` is the JIT side. Dynarmic offers two
translation callbacks; the reader uses both:

- *before an instruction is read*: when the instruction is hooked and is not the first
  of its block, the block is ended there. A hooked instruction therefore always starts
  a block and fires once per execution.
- *before an instruction is translated*: emit three host calls that pass R0–R12, SP, LR
  and the address through IR values. Passing them through the IR matters: the JIT's
  get/set elimination can keep the latest register values out of the state structure
  within a block, so reading the state from the callback would see stale registers.

A **conditional instruction cannot be hooked**, and the glue refuses it (so does
`reader.hook`, earlier and with a readable error). Dynarmic turns a condition into a
property of the whole block, and only while the block is still empty. With the hook
call already in the block it ends the block before the instruction and links it to
itself: the hook then fires forever. This was found by the JIT tests, not in a game.

Hooks cost nothing where there are none: the check happens at translation time, and
only hooked addresses get extra code. Adding or removing hooks clears the JIT caches.
Hooks need the JIT; the interpreter is not instrumented.

Lua is vendored in `externals/lua` and **compiled as C++**, so its errors are C++
exceptions and unwind cleanly through the bindings.

## Android (Kotlin)

`src/android/app/src/main/java/org/citra/citra_emu/reader/`

| Package | |
| --- | --- |
| `ReaderNative` | JNI bridge. Finds the profile (the app's own folder, `files/reader/profiles`, first, then app assets). Text crosses JNI as UTF-8 byte arrays, never as JNI strings. |
| `ReaderGlue` | This app's own ties to the shared layer: the application's start, the two screens' places (`screenRects`), the emulated buttons and sticks on their way to the game (`onButton`, `onStick`), the title ID as the reader names a game. |
| `ReaderSession` | One per running game. Receives events on the emulation thread, counts and stores them on a worker thread, notifies listeners on the main thread. Owns the text server, clipboard output and media capture. |
| `model` | `TextLine`: the parsed event. Ruby offsets are code points. |
| `stats` | `ReadingRules` (the counting rules), `ActivityTracker` (lines and heartbeats to counters), store interface. |
| `store` | `ReaderDatabase` (SQLite): backlog, seen sources, sessions, per-session per-day totals. |
| `dict` | Yomitan archive import (streamed), `Deinflector`, `DictionaryLookup`, glossary rendering. |
| `anki` | `AnkiClient` with AnkiDroid (content provider) and AnkiConnect (HTTP, written to a socket: Android refuses plain HTTP through its own client) implementations; `CardBuilder` fills field templates. Only AnkiConnect actions that AnkiConnect Android has too are used. |
| `mining` | Screenshots by `PixelCopy` of the game surface (so overlays are never in them), audio clips from the tap encoded as AAC, `Miner` ties them to a card and asks Anki which words it already has. |
| `hook` | `TextHookServer`: a small RFC 6455 server. |
| `cursor` | `HoverCursor`: the state of the held cursor (who holds its button, where it is, what the sticks say). The overlays attach to it; the emulation screen passes buttons and sticks through it (by `ReaderGlue`) before the game gets them. |
| `screen` | Text in the game's own picture, in plain Kotlin: `ScreenGeometry` (where the emulator draws each console screen on a display surface), `PlacedText` (the characters of a text box on that surface: what a finger is on, where a word is), `ScreenTexts` (pairs lines with the layouts reported for them; the two arrive separately). |
| `ui` | `ReaderOverlay` (one per display), `ScreenTextLayer` (the invisible layer that takes taps on the game's text), `LookupLayer` and `LookupPopupView` (dictionary popup), `ReaderTextView` (the panel's text: furigana and tap position), and the screens. |

Changes to upstream files are small and marked "Reader fork": the overlay is added in
`EmulationFragment` and `SecondaryDisplay`, the session follows the emulation state,
four hotkeys join the hotkey lists, buttons and sticks pass through `ReaderGlue` where
they are handed to the core (`HotkeyUtility`, `EmulationActivity`, `InputOverlay`), the
application's start calls `ReaderGlue.onApplication`, and there is one more entry in the
in-game menu and on the home screen. Reader strings are in `strings_reader.xml` (the same in
every reader fork) and `strings_console.xml` (this app's own).

Most of this package is the reader layer every reader fork shares, which the build writes in
from the repository's `shared/android`; `AGENTS.md` lists what is this app's own.

### Tapping the game's text

The profile reports the cell of every character in pixels of the console screen it is
on. The emulator knows where it draws each console screen on each display surface
(`ReaderGlue.screenRects`, read from the frontend's framebuffer layout), so cells
become rectangles of the view that lies over that surface. `ScreenTextLayer` asks for
the arrangement again on every touch: layouts can be switched while a game runs, and
nothing announces it.

A touch that begins in a text box is the reader's; one that begins anywhere else is
never looked at, and reaches the game through the views beneath. Hit areas are the
drawn cells grown to meet their neighbours, with the gap above a row (where the game
puts furigana) belonging to that row, and are never larger than the console screen the
text is on, because next to it may be the touch screen.

### The cursor

The cursor's button is an emulated button (ZL unless chosen otherwise), so whatever is
mapped to it holds the cursor: a key, a trigger's axis, the on-screen button. Where
those are handed to the core, `ReaderGlue.onButton` asks `HoverCursor.onButton` first,
which takes the button whenever a game with a loaded profile runs; `HoverCursor.onStick`
takes stick movement while the cursor is held (or, with the toggle setting, from the press
that switches it on to the one that switches it off; a press counts once per source, as a
held key repeats). While it holds them the native input filter shows the game the sticks
at rest (`ReaderNative.holdSticks`); they are still handed to the core, so the game sees
where they are as soon as the button is let go. The statistics' button is taken by
`ReaderGlue.onButton` too. A paused or closed
game lets go of the cursor, because no button release reaches a paused game.

Each `ReaderOverlay` is a surface of the cursor. The one whose display shows the
console's bottom screen dims that area and, in `dispatchTouchEvent`, takes every
gesture that begins there while the cursor is held: movement moves the cursor (the
whole gesture stays the trackpad's, to its end), a short touch without movement is a
click. The main screen's overlay draws the cursor, asks the text layer what is under
it, and looks a word up as soon as the cursor is on a character (quietly: where no word
begins nothing is said, and an open popup goes); the popup's own word, a word the popup
covers and the gap between that word's row and the popup are the popup's and are not
looked up (`PlacedText.keptByPopup`, the desktop's rule too); a click is passed to
its own views as a tap at the cursor. Two displays are two windows, so the
state lives in the one `HoverCursor` object rather than in a view.

### The popup and what Anki already has

`LookupPopupView` is only as large as what it says, up to a height that is a setting.
Each headword's part ends in a boxed button at its bottom right; the last line of its
text ends in a blank as wide as the box, so the text never runs under it. A box whose
part continues below what the popup shows is moved up to the popup's bottom edge (it
stays within its own part), so the first entry's box is in the popup's corner without
scrolling.

Whether Anki has a word is first answered on the device, by a copy of the deck
(`AnkiIndex`, the same in Kotlin and in C++): what the note type's first field holds in
each note of the deck cards go to (or of the whole collection). It is read once in pages
(`findNotes` for `"Field:_*"`, then the fields of 200 notes at a time), brought up to date
every three minutes with the notes changed in the last day (`edited:1`), read whole again
every half hour (which is how deleted notes leave it), and kept in a file
(`files/reader/anki_index.json`, on a computer `anki_index.json` in the reader's folder),
so that it answers at once after a restart and while Anki cannot be reached. When results
are shown, `Miner.knownNow` gives each word's box from the copy before the popup is drawn:
the open book, the plus, or a turning ring where the copy cannot tell yet. Then
`Miner.confirm` asks Anki about all of them in one request (AnkiConnect's `multi`;
AnkiDroid is asked word by word, on the device), off the main thread and with short
timeouts, for notes whose first field equals what a card for that word would put there
(`AnkiSearch.fieldIs`); the answers correct the boxes and the copy. Where Anki does not
answer, the plus gets a grey dot; after a failure Anki is left alone for 20 seconds, so an
Anki that is not there costs one attempt, not one per word. The same search runs again
before a note is sent, because AnkiConnect Android and AnkiDroid add whatever they are
given.

### Activity totals

Activity is stored as one row per session and reading day. Any total is a sum over
those rows, and deleting a session removes exactly what it contributed. Clearing
activity never touches the backlog or the seen sources. The tracker also keeps the
open session's running totals (`ActivityTracker.session`); `ReaderSession` passes them
on when they change, and the overlay on the main screen shows them in its top right
corner (`SessionSummary`).

## Tests

- `tools/reader/test-host.sh` — C++ tests (Catch2, in upstream's `tests` target, tag
  `[reader]`): the text codec; the engine and the Lua API on fake guest memory; hooks
  through the real JIT on small ARM and Thumb programs; and the bundled SMT IV profile
  against made-up messages, with the calls the game makes in the order it makes them.
  `Engine::StartDetached` runs the engine without an emulated system for this.
- `tools/reader/test-android.sh` — JVM tests, no device. Pure logic (rules, tracker,
  deinflection, card building), SQLite and dictionary import under Robolectric, the
  WebSocket server and AnkiConnect client against real sockets, and the overlay and
  screens laid out and drawn by Robolectric's native graphics and driven by real touch
  events (the cursor too: trackpad strokes as touch events, buttons and sticks the way
  the emulation screen passes them on). With `READER_PREVIEW_DIR` set, the UI tests
  write PNGs of what they drew.
- The harness runs the core against a real game: see [harness.md](harness.md).

What these do not cover is the device itself. Tried on an AYN Thor (two displays, the
top screen on the upper and the bottom screen on the lower): setup, dictionary import,
the game's text tapped word by word in real dialogue, a selection list tapped on the
lower display, the popup with its boxes on both displays, the session line, the
cursor (the real L2 held, strokes on the lower display, the left stick, the click), the
backlog, the statistics screen, the controller, and cards for words and for a sentence
with picture and audio, sent to a stand-in for AnkiConnect on the development machine.
Four faults only showed there: Android refuses plain HTTP, its regular expressions are
stricter than the desktop JVM's, icons coloured by the app theme are invisible on the
second display, and the audio encoder waited after every buffer.
Not yet tried: a real AnkiDroid or AnkiConnect Android (they hold the user's
collection), savestates, a long session, a device with one display.
