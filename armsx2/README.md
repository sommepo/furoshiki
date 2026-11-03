# ARMSX2 Reader

Read Japanese PS2 games on Android like a visual novel in a browser with Yomitan: move the
cursor onto a word in the game's own text and its dictionary entry opens. Make Anki cards,
keep a backlog and reading statistics.

The Windows version is [PCSX2 Reader](https://github.com/sommepo/pcsx2-reader).

One of four [reader emulators](https://github.com/sommepo/readers) for PS2, PSP and Dreamcast games.

<table>
  <tr>
    <td width="50%"><img src="docs/reader/media/two-screens-demo.gif" alt="The lower screen moves the cursor over words in Never7's text, the outline gliding after it"><br><sub>Two screens: the lower one is a trackpad</sub></td>
    <td width="50%"><img src="docs/reader/media/one-screen-demo.gif" alt="Touching words in Never7's text"><br><sub>One screen: touch a word</sub></td>
  </tr>
</table>

> A fork of [ARMSX2](https://github.com/ARMSX2/ARMSX2) v6. It is not ARMSX2: please report
> nothing about it to the ARMSX2 or PCSX2 projects. ARMSX2's README is
> [README.armsx2.md](README.armsx2.md). The reading layer is AI-written.

## Features

- **Look up words in the game's text.** Move the cursor onto a word (with the controller, or
  on a two-screen handheld with the lower screen as a trackpad). Touching a word works too.
  Uses Yomitan dictionaries.
- **The word is outlined, and the outline glides** from word to word as you move along the
  line. Highlight or no mark at all instead, in Reader settings.
- **The popup stands right next to the word** and stays put while you go to its **+**, which
  sits in the bottom left of each entry (or any corner you choose).
- **Anki cards** in AnkiDroid, with the sentence and a picture of the scene.
- **Backlog and reading statistics.**
- **Next Choice**: run a visual novel at full speed to its next choice.
- **The PS2's own menu** as the game list. **PS2 Menu** in the app's library switches it back
  on.
- **CRT picture** with a tube's frame around it (Reader settings).
- **Play by touch** on a phone: a tap on the picture goes on, a tap on an answer picks it, and
  the controller's buttons stand in a small column at the right edge that a tab tucks away. On
  at first only on a device without controls of its own.
- **Two-screen handhelds** (AYN Thor): the lower screen is a trackpad for the cursor, and the
  controller stays the game's.
- **Only the games that can be read are listed** at first (all of them, in a setting).
- **Routes finished elsewhere**: mark the CLANNAD routes you finished on another copy, so the
  game's own progress follows.

<table>
  <tr>
    <td width="50%"><img src="docs/reader/media/two-screens.jpg" alt="A lookup with the cursor, the lower screen as trackpad"><br><sub>Cursor lookup</sub></td>
    <td width="50%"><img src="docs/reader/media/browser.jpg" alt="The PS2-style Browser"><br><sub>The Browser: a book marks a supported game</sub></td>
  </tr>
  <tr>
    <td><img src="docs/reader/media/crt.jpg" alt="The game with the CRT picture on"><br><sub>CRT picture</sub></td>
    <td><img src="docs/reader/media/menu.jpg" alt="The PS2-style main menu"><br><sub>The PS2 menu</sub></td>
  </tr>
</table>

## Get it

Download `ARMSX2Reader-<version>-android.apk` from the [releases](../../releases) (arm64). It
installs beside ARMSX2. You need your own PS2 BIOS and games.

## Game scripts

A game is read through a **game script**: a small Lua file that tells the app where the game
keeps its text. The app installs them from the list in
[PCSX2 Reader's repository](https://github.com/sommepo/pcsx2-reader#game-scripts), which has the
supported games: **System Configuration → Game Scripts**. New games appear there without an
app update. You can also install a script from a file.

## Building

See [AGENTS.md](AGENTS.md).

## Licence

GPL-3.0-or-later, as ARMSX2 and PCSX2 ([COPYING.GPLv3](COPYING.GPLv3)).
