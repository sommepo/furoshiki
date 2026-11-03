# Azahar Reader on a computer (Windows)

The same reader as in the Android app, in Azahar's desktop app: click a word **in the game's
own text** and its dictionary entry opens, make a card from it, and have your reading
counted. It reads the same games with the same text profiles: a profile is written once and
serves both.

What the reader does, and why, is described in [README.md](README.md). This page has what is
different on a computer, and how the desktop build is made and tried.

> **State (2026-10-09).** The Windows build is made on the development machine (Linux) and
> **has been started on no Windows computer yet.** What is described below was seen with the
> same program built for Linux, on a machine without a display (see
> [How it was checked](#how-it-was-checked)).

## Install (Windows)

Unpack `AzaharReader-windows.zip` anywhere and start `azahar-reader.exe`.

It keeps its saves and settings in a folder of its own, `%APPDATA%\AzaharReader`, so it runs
beside an Azahar on the same computer and neither touches the other's data. (A folder `user`
beside the program is used instead when there is one.) It is set up like Azahar.

## The 3DS's look

The app looks like the console, as the Android app does, and does so from its first start.

- **The game list is the 3DS's HOME menu, on two screens**, one above the other as the
  console has them. The **upper screen** shows the chosen game: a strip with the date and
  the time, the game's picture slowly bobbing, and in a bubble its name (in Japanese where
  the game has one), its maker and whether its text can be read. The **lower screen** has
  the games' icons as tiles in a grid of two rows, the chosen one in a frame that glows
  (it hops when chosen) with its name in a bubble, a boxed book on a game whose text can be
  read; six small buttons along the top (System Settings, Reader, Statistics, Supported
  games, Game folders, View) and a wide **Open** button along the bottom.
- The arrow keys move over the grid, **Enter** opens the game, **Up** from the upper row goes
  to the small buttons. With the mouse a click chooses a game and a click on the chosen one,
  or on Open, starts it; the wheel moves along the grid; a right click has the game's
  options. **Typing narrows the grid** to the games whose name has what was typed
  (Backspace, Esc).
- **Every window has the console's colours**: pale grey and its blue, dark with a dark
  system, or one of the two always (the **View** button, *Theme*).
- **The reader's windows are laid out as the console's settings**: a strip with the window's
  name, the window, a strip with a wide round **Back** button.
- *Reader → 3DS HOME menu as the game list* and *3DS colours in every window* switch the two
  off and on; the View button's *The emulator's game list* does the first as well. Azahar's
  own list is what it was, with everything its right click offers.

**The dictionary popup and the pointer over a game are the console's too** (2026-10-10): the
popup is the lower screen's dark grey pinstriped panel with white type and light bevelled
boxes for the plus and the book; the pointer is a touch of the stylus, a white-and-charcoal
ring with a fainter one around it, for the mouse and for the controller's cursor alike. The
word's mark and the session line are as they were. Drawn in
`shared/reader/qt/reader_overlay.cpp` ("The console's look").

## Reading

Start a supported game and play as usual.

- **Click a word in a text box** (or touch it, on a touch screen) to look it up. A small
  popup opens beside the word: every dictionary entry that starts at that character, longest
  match first. The word is marked in the game's picture while the popup is open. Click
  another word to look that up; click anywhere else to close. Left alone, the popup closes
  by itself after 15 seconds (*Reader settings: Close the popup after*; 0 keeps it open).
- **A text box never passes clicks to the game**, so looking a word up cannot advance the
  text or touch the lower screen under it. Advance with the controller or the keyboard.
- **Both screens**: text on the upper and on the lower screen can be clicked, wherever the
  chosen screen layout puts them **in the main window**. A screen shown on its side (the
  upright layouts) or in the second window (*Separate Windows*: the lower screen) cannot.
- **Point at a word** to look it up without clicking: while **Shift** is held, or always, or
  never (*Reader settings → Look a word up by pointing at it*; Shift unless changed).
- The boxed **＋** in the popup makes a card for that word. When Anki has the word already
  the box shows an **open book** instead, which opens the note in Anki's browser.
- The top right corner of the picture shows the **session so far**: characters read,
  characters per hour and reading time. **Ctrl+T** pauses and resumes the statistics, as does
  a controller button if one is chosen in the settings (*Statistics*); two small bars in that
  corner say that they are paused.
- **Hold ZL** (*Reader settings → Cursor*; L until 2026-10-20, ZL as on Android since, heard
  in every game whether or not it opens the extra buttons' channel, and let go for the game
  while the reader has it): the circle pad then moves a cursor over the game's
  picture instead of reaching the game, and a word it rests on is looked up. A **menu button**
  of the controller's (none until chosen there) opens the Reader menu over the game, and the
  d-pad, A and B then go through the menu and the reader's windows.
- The **Reader** menu has the rest: **Statistics** first, with what was read today in the
  entry itself, then **Game scripts** (the profiles the app has and your own; the list to
  install from is published with Furoshiki, once that repository is public); a card of the sentence alone (**Ctrl+D**) and the backlog
  (**Ctrl+H**); dictionaries, Anki setup, the line audio of this game's cards, **In this
  game** (what a game's profile offers to do), the settings, **Save sync**; reloading the
  text profile. The game is paused while one of these windows is
  open and goes on by itself when it is closed.
- A game whose text can be read has a **boxed book** on its picture, in the HOME menu and in
  Azahar's own list.

Cards go through **AnkiConnect** (the add-on for Anki on a computer; `http://127.0.0.1:8765`
unless changed, *Reader → Anki setup*). A card's picture is both screens, its audio (Ogg
Opus) what the game played while the line was up; whether a card gets that audio is the same
choice of three as in the Android app. The first start asks about a dictionary and about
Anki, once each, in place of the setup screen the app does not have.

## What is not there

- **The reader panel.** A line the game does not place on screen can be read and looked up
  in the backlog only.
- **Exclusive full screen** and **the second window**: what the reader draws over the game
  (the mark, the session line, the popup) are windows of their own over the main window's
  picture.

## Profiles of your own

A file `reader\profiles\<TITLEID>.lua` in the app's folder (*Reader → Open the profiles
folder*) wins over the bundled profile for that game. *Reader → Reload text profile* loads it
into the running game.

## How it is built

Everything is built on Linux, the Windows app by cross-compiling. The compilers for this
machine are this fork's own (`tools/reader/env.sh`); Qt for both systems, the MinGW
cross-compiler and Qt's host tools are the ones Eden Reader's setup unpacked
(`READER_EDEN_TOOLCHAINS`, `~/.local/share/eden-reader` unless set: in that repository,
`tools/reader/setup.sh windows desktop`), plus Qt's Multimedia module for Windows, which the
emulator's camera wants (`aqt install-qt windows desktop 6.9.3 win64_mingw -m qtmultimedia`
into `qt/official` there). SQLite, zlib, QuaZip and Opus, which the reader's logic needs, are
fetched at configure time at fixed versions (`externals/reader_desktop/CMakeLists.txt`).

```sh
tools/reader/build-windows.sh              # build-windows/bin/Release/azahar.exe
tools/reader/package-windows.sh            # build-windows/package/AzaharReader-windows.zip

tools/reader/build-desktop.sh              # the same app for Linux: build-desktop/bin/Release/azahar
tools/reader/test-desktop.sh               # tests of the reader's logic (82)
tools/reader/desktop.sh --rom FILE &       # run it here, without a display
python3 tools/reader/dc.py status "click 320 405" "grab private/desktop/shots/a.png"
tools/reader/desktop-stop.sh
```

- **Windows**: Ubuntu's MinGW (GCC 13) against the official Qt for MinGW; the emulator's own
  bundled libraries built from source; Vulkan, OpenGL and the software renderer; sound
  through SDL2. `package-windows.sh` does what Qt's deployment tool does on Windows and then
  runs `pe_check.py`: every function a file of the package imports from another file of the
  package must exist there, or packaging fails.
- **Linux**: this build exists to develop and try the reader layer. It is run with Qt's
  `offscreen` platform and **the emulator's software renderer** (written once into the test
  install's settings by `desktop.sh`), which draws into the window's own picture: slow, and
  the only renderer whose picture can be copied without a display. `dc.py` talks to the app
  (clicks in the picture, key presses, pictures of what the windows show; the commands are in
  `shared/reader/qt/reader_control.cpp`). That channel is off unless the environment
  variable `READER_DESKTOP_CONTROL` names a socket, which only `desktop.sh` does.

## Where the code is

| Path | |
| --- | --- |
| `src/reader/desktop/` | The reader's logic, free of the emulator: dictionaries and lookup, deinflection, glossaries, cards and AnkiConnect, statistics, the backlog database, audio clips, the text server. Eden Reader's, with the console's two screens in `screen_text`. Tests in `tests/`. |
| `src/reader/qt/` | What is on screen and what ties it to the emulator: the session (`reader_session`), the mark and session line over the game (`reader_overlay`), the popup and the audio question, clicks and the menu (`reader_controller`), the reader's windows, media for cards, the control channel. Eden Reader's desktop layer, adapted. |
| `src/reader/qt/host.*`, `install.h` | What ties the reader's windows (the reader layer every reader fork shares, from the Flycast Reader fork: see `AGENTS.md`) to Azahar, this fork's own: what the main window calls, the two screens, the picture, the development channel's commands of its own. `reader_look.*`: the popup's and pointer's 3DS look. |
| `src/reader/qt/threeds_home.*` | The 3DS's look: the HOME menu that lies over the game list (`ThreeDsHome`), the colours of every window and the dressing of the reader's windows (`ThreeDsLook`). |
| `src/reader/core/` | The text engine, shared with Android. |
| `src/android/app/src/main/assets/reader/profiles/` | The game profiles, taken into the desktop program as they are. |

Upstream files touched for the desktop app (each change carries a "Reader fork" comment):
`src/citra_qt/bootmanager.{h,cpp}` (the reader gets the pointer first),
`src/citra_qt/citra_qt.{h,cpp}` (the reader and the HOME menu made, a game's start and end,
pause and resume, the colours after the theme, the window's title),
`src/citra_qt/game_list_p.h` (the badge; a game's picture, Japanese name and maker for the
HOME menu), `src/citra_qt/CMakeLists.txt`, `src/CMakeLists.txt`, `CMakeLists.txt` (the
reader's sources and libraries; one line that broke cross-compiling),
`src/common/common_paths.h` (the app's own folder), `src/core/hle/service/hid/hid.cpp` (the
buttons and the circle pad go through the reader's input filter, and its presses for the
player come in; `core/reader/emulator.cpp` has both).

## How it was checked

- **Logic**: 82 tests with made-up text and dictionaries written for the test
  (`tools/reader/test-desktop.sh`): Eden Reader's, with the catalogue test naming this fork's
  bundled game and the layout tests knowing the lower screen.
- **The app, on Linux without a display** (2026-10-09), with the software renderer, Shin
  Megami Tensei IV (the user's cartridge image) and JMdict (527,258 entries):
  - the HOME menu in both themes: the upper screen with the game's picture, its Japanese
    name, its maker and "Its text can be read"; the lower screen with the tile, its badge,
    the name bubble, the small buttons (the chosen one named, Statistics with today's
    numbers) and Open;
  - the game started from the menu, by a click on Open and by Enter; the menu gone while the
    game runs and back when it is stopped;
  - the game's profile loaded (three hooks) and its first line read, with the cell of every
    character on the upper screen as the window draws it (default layout, 800×960: looked at
    in a grab with the cells outlined);
  - a click on a word of that line: the popup beside it with the dictionary's entries, the
    word marked, and the game's text not advanced;
  - the reader's settings and the statistics as settings pages (strip, page, Back).
- **Windows**: the program links and is packaged with everything it imports
  (`pe_check.py`). **It was started on no Windows computer.** Most worth a look there,
  because the Linux run cannot show it: whether it starts at all; the see-through window over
  the game's picture with a graphics card drawing the game (the mark, the session line); a
  card's picture, which under Vulkan or OpenGL comes from the renderer and was never taken
  here; fonts; sound; the question about folders of older Citra or Lime3DS installs, which is
  the emulator's own.
- **Not seen anywhere**: cards (the code is Eden Reader's, where they were made against a
  stand-in for AnkiConnect), the line audio and its question, the backlog window, a text on
  the lower screen clicked in this app, layouts other than the default, a touch screen, the
  menus of the small buttons and the right click (a menu waits in a loop of its own, which
  the control channel does not get through), more than one game in the grid (so no
  scrolling), typing to search, an empty games list, and how any of it moves (the grabs are
  stills).
