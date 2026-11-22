<p align="center"><img src="home/publish/media/logo.svg" width="128" alt="Furoshiki logo"></p>

<h1 align="center">Furoshiki</h1>

<p align="center"><b>A Japanese game launcher for Windows and Android with built-in dictionary lookups and Anki sentence mining.</b><br>
Look up words directly in your games and send cards to Anki — even from the couch with a controller.</p>

<p align="center"><b>Latest releases:</b>
<a href="../../releases/download/v0.3.1/Furoshiki-0.3.1-android.apk">Android 0.3.1</a> ·
<a href="../../releases/download/v0.3.1/Furoshiki-0.3.1-windows.zip">Windows 0.3.1</a> ·
<a href="../../releases/download/v0.3.1/Furoshiki-0.3.1-linux.tar.gz">Linux 0.3.1</a> ·
<a href="../../releases/download/v0.3.1/Furoshiki-ocr-helper.zip">OCR helper (Windows)</a> ·
<a href="../../releases">all releases</a></p>

<p align="center"><img src="home/publish/media/lookup-remember11.gif" width="760" alt="Hovering words in Remember11 on PS2 to open instant dictionary popups"></p>

<p align="center"><sub>Remember11 (PS2) on Windows with the optional CRT filter enabled.</sub></p>

Furoshiki combines your Japanese game library, built-in console emulators (seven cores on Android,
six on Windows), and reading tools in a single app. While you play, you can look up words right on
the game screen and send the sentence, screenshot, and line audio straight to Anki — no separate
texthooker window or copy-pasting required.

Game text is captured in one of three ways:

| Method | How it works | Availability |
| --- | --- | --- |
| **Game script** | Reads exact text and character positions straight from emulator memory so you can point at or tap the game's own text. | [38 games](#supported-games), Windows & Android |
| **OCR** | Reads text directly from the game image. Works with any game, with occasional OCR errors. | Windows & Android |
| **Text hook / external lines** | Captures exact text from PC games (or a WebSocket/clipboard feed) into a lookup bar at the edge of the screen. | Windows |

## Contents

**Overview**
- [Made for a controller](#made-for-a-controller)
- [Consoles](#consoles)
- [Download](#download)
- [Reading a game](#reading-a-game)
- [Supported games](#supported-games)
- [Adding a game](#adding-a-game)
- [Help](#help) · [Building from source](#building-from-source) · [Licence](#licence)

**Windows**
- [Setting up on Windows](#setting-up-on-windows)
- [Reading with a controller](#reading-with-a-controller)
- [Games without a script (PC games, texthookers, clipboard)](#on-windows)
- [OCR on Windows](#ocr-on-windows)
- [Library and themes](#library-and-themes)
- [Display and video](#display-and-video)
- [Settings](#settings)

**Android**
- [Setting up on Android](#setting-up-on-android)
- [OCR on Android](#ocr-on-android)
- [On Android (touch, controller, and in-game menu)](#on-android)
- [Dual-screen handhelds](#dual-screen-handhelds)
- [GameNative and other apps](#gamenative-and-other-apps)

## Made for a controller

Furoshiki is designed to work completely from a sofa or handheld with just a controller — library
browsing, settings, in-game menus, dictionary popups, and Anki card creation all work without a
keyboard or mouse (though mouse and keyboard work everywhere too).

- **Works with any pad:** Plug in or pair an Xbox, PlayStation, Switch, 8BitDo, or Steam
  Controller. On first use, Furoshiki asks you to press the button marked A so it knows your
  controller's face-button layout and displays matching prompts everywhere.
- **Built-in shortcut setup:** The same setup flow binds your reading shortcuts (backlog, pause
  reading timer, pause game, Quick menu, create card), flags conflicts, and suggests free buttons.
- **Controller-driven popups:** While a popup is open, controller input goes to the popup instead
  of the game. Use the D-pad to move between entries, shoulder buttons to switch tabs (*Words*,
  *Grammar*, *Kanji*, *Names*), and the top/right face buttons to add a card without or with
  audio. Closing the popup returns control to the game.
- **Pointer-free reading (Windows):** Tap `Select` in-game to place a cursor on the first word in
  the line that isn't in Anki yet. Step word-by-word with the D-pad, jump between unknown words
  with the bumpers, and advance dialogue with the bottom face button. See
  [Reading with a controller](#reading-with-a-controller).
- **Pointer mode when you want it:** The Steam Controller trackpad works out of the box, any
  button can be bound to *Pointer (hold)*, and on the AYN Thor the lower screen acts as a
  trackpad. Using the pointer exits controller reading mode automatically.

## Consoles

| Console | Emulator | Windows | Android |
| --- | --- | --- | --- |
| PlayStation | [DuckStation](https://github.com/stenzek/duckstation) | Yes | Yes |
| PlayStation 2 | [PCSX2](https://github.com/PCSX2/pcsx2) ([ARMSX2](https://github.com/ARMSX2/ARMSX2) on Android) | Yes | Yes |
| PSP | [PPSSPP](https://github.com/hrydgard/ppsspp) | Yes | Yes |
| Dreamcast | [Flycast](https://github.com/flyinghead/flycast) | Yes | Yes |
| Nintendo 3DS | [Azahar](https://github.com/azahar-emu/azahar) | Yes | Yes |
| PS Vita | [Vita3K](https://github.com/Vita3K/Vita3K) | Yes | Yes |
| Nintendo DS | [melonDS](https://github.com/melonDS-emu/melonDS) | Not yet | Yes |

Emulators are built in as cores (similar to RetroArch), so there's nothing extra to install. Core
and video settings can be configured globally, per console, or per game.

## Download

Release **0.3.1** includes Windows, Android, Linux, and the optional Windows OCR helper:

| File | Platform | Notes |
| --- | --- | --- |
| [Furoshiki-0.3.1-windows.zip](../../releases/download/v0.3.1/Furoshiki-0.3.1-windows.zip) | Windows 10/11 (64-bit) | Extract anywhere and run `Furoshiki.exe`. |
| [Furoshiki-0.3.1-linux.tar.gz](../../releases/download/v0.3.1/Furoshiki-0.3.1-linux.tar.gz) | Linux (64-bit, glibc 2.38+) | Extract anywhere and run `Furoshiki/reader-home`. Requires X11 or Xwayland, OpenGL and Vulkan drivers, and a Japanese font. |
| [Furoshiki-0.3.1-android.apk](../../releases/download/v0.3.1/Furoshiki-0.3.1-android.apk) | Android 10+ (arm64) | Sideload the APK and allow installs from your browser or file manager when prompted. |
| [Furoshiki-ocr-helper.zip](../../releases/download/v0.3.1/Furoshiki-ocr-helper.zip) | Windows (optional) | Only needed for OCR on Windows. See [OCR on Windows](#ocr-on-windows). |

- **Windows dependencies:** The PS2 and Vita cores require the
  [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
  The dictionary popup uses Microsoft WebView2 (preinstalled on Windows 11 and updated Windows 10;
  falls back to a simpler popup if absent).
- **Linux:** Settings, dictionaries, saves, and BIOS files live in `~/.local/share/Furoshiki`
  (place console BIOS files under `program/consoles/<console>/system`).

No games, BIOS, or firmware files are included. Supply your own dumps along with the system files
below.

## Setting up on Windows

1. Launch `Furoshiki.exe`. Because the binary isn't code-signed, Windows SmartScreen may warn you
   (*More info* › *Run anyway*). First launch opens a 3-step Setup wizard (all steps can be
   skipped and revisited later via Settings › About › Setup).
2. **Dictionary:** Download [Jitendex](https://jitendex.org) (39 MB) automatically or import your
   own Yomitan `.zip` dictionaries. At least one dictionary is required for lookups; manage them
   anytime in Settings › Reading › Dictionaries.
3. **Games:** Point Furoshiki to your game folder(s), or add them later via **Add game** at the
   end of the shelf › *Disc or file*. Place required BIOS/firmware files in the folders below.
4. **Anki (optional):** With Anki open and
   [AnkiConnect](https://ankiweb.net/shared/info/2055492159) installed, Furoshiki auto-selects
   your mining deck (or first non-Default deck) and its primary note type, mapping recognized
   field names automatically. Adjust anytime in Settings › Cards › Anki setup.
5. **Game scripts:** Install scripts under Settings › Consoles › `<Console>` › *Game scripts*, or
   from the in-game Quick menu (`F1` or right-click) › *Game scripts…*, then restart the game.

| Console | Required files | Folder in `%LOCALAPPDATA%\Furoshiki` |
| --- | --- | --- |
| PlayStation | BIOS (e.g. `SCPH-5500.bin`) | `program\consoles\ps1\system` |
| PlayStation 2 | BIOS | `program\consoles\ps2\system` |
| PSP | None | — |
| Dreamcast | None (`dc_boot.bin` optional) | `program\consoles\dc\system\dc` |
| Nintendo 3DS | Decrypted games (`aes_keys.txt` only if required by a game) | `program\consoles\3ds\system` |
| PS Vita | Firmware (`PSVUPDAT.PUP`, plus `PSP2UPDAT.PUP` if available) | `program\consoles\psvita\system` |

## Setting up on Android

1. Install the APK, open Furoshiki, and grant *Allow file access* so it can scan your games.
2. Choose *Add games folder* during Setup (a top-level `Games` folder on internal storage or SD
   card is detected automatically).
3. Place BIOS/firmware files in `Games/BIOS/PS1`, `Games/BIOS/PS2`, and `Games/BIOS/Vita` (other
   consoles don't need them).
4. Place Yomitan `.zip` dictionaries in `Games/Dictionaries`; they're imported on the next launch.
5. Install [AnkiDroid](https://github.com/ankidroid/Anki-Android) for Anki mining. Furoshiki
   requests permission once and auto-configures your mining deck and note type just like Windows.

Supported game scripts and box art are downloaded automatically once games are found. You can
inspect each console's scripts under Settings › Library › Game scripts.

## Reading a game

<p align="center"><img src="home/publish/media/title-page.png" width="760" alt="428: Shibuya Scramble title page showing box art, reading stats, and bundled character names"></p>

<p align="center"><sub>Game detail page on Windows showing reading stats and bundled character names.</sub></p>

Hover or click a word on Windows (or tap it on Android) to open a dictionary popup with automatic
deinflection. Supported games also include character names and readings, prioritized ahead of
general dictionaries.

Each Anki card includes the word, full sentence, screenshot, and line audio (optionally trimmed to
voice). On Android, card buttons live in the popup; on Windows, use these shortcuts:

| Key | Action |
| --- | --- |
| `Ctrl+D` | Add sentence card |
| `Ctrl+W` | Add word-only card |
| `Ctrl+Z` | Undo last card |
| `Ctrl+H` | Open backlog |
| `Ctrl+G` | Pause game |
| `Ctrl+T` | Pause reading timer |
| `Ctrl+N` | Skip to next choice |
| `F1` / Right-click | Quick menu |
| `Esc` | Game menu |
| `F2` | Settings |

The backlog stores every line read in the session (with speaker names when available) and lets you
create multi-line cards. Furoshiki also tracks characters read, play time, and reading speed
(characters/hour) locally per title.

For pointer control on a gamepad, the Steam Controller trackpad works automatically; on other
controllers, bind any button to *Pointer (hold)* in Settings › Controls › Button mapping so the
analog sticks move the cursor while held.

### Reading with a controller

<p align="center"><img src="home/publish/media/controller-reading.gif" width="760" alt="Controller reading mode stepping through words in Remember11 without a pointer, with auto-height popup and in-popup controller glyphs"></p>

<p align="center"><sub>Pointer-free controller reading on Windows: stepping through words with the D-pad, auto-height popup resizing, and controller glyphs on the popup and hint bar.</sub></p>

On Windows, you can read without a pointer at all. Tap `Select` in-game to drop a cursor onto the
first word in the current line that isn't already in Anki (or the first word of the line) and open
its popup:

| Button | Action |
| --- | --- |
| D-pad Left / Right | Previous / next word (hold to repeat) |
| D-pad Up / Down | Line above / below (swapped in vertical text) |
| `LB` / `RB` | Previous / next word not in Anki |
| Right stick Left / Right | Shorten / lengthen word selection by one character |
| Right stick Up / Down | Scroll popup |
| `LT` / `RT` | Previous / next popup tab (*Words*, *Grammar*, *Kanji*, *Names*) |
| `A` | Advance dialogue (sends console confirm; cursor moves to the new line) |
| `Y` | Add card for the word |
| `B` | Add card with line audio |
| `X` | Focus popup to pick another entry or tab (`X` or `A` returns to text) |
| `Select` | Exit controller reading |

*Button labels above use Xbox layout by position (`A` bottom, `B` right, `X` left, `Y` top); on
PlayStation or Nintendo pads, on-screen prompts match your controller.*

<p align="center"><img src="home/publish/media/popup-tabs.gif" width="760" alt="Switching between Words and Grammar tabs inside the dictionary popup using LT and RT"></p>

<p align="center"><sub>Switching between <i>Words</i> and <i>Grammar</i> tabs inside the popup with <code>LT</code> / <code>RT</code>.</sub></p>

`Start` and the left stick always stay mapped to the game. When a game presents a choice, all
buttons pass through to the game until the next dialogue line. Moving or clicking the mouse exits
controller reading mode immediately. This works in scripted console games as well as OCR-read
console games (though OCR cannot auto-detect choices).

Customize the activation trigger in Settings › Controls › *Start reading with* (`Select`, `L3`,
`R3`, chorded combos like `Select + A` / `L3 + R3` / `LB + RB`, or *Always during dialogue*). You
can also toggle the on-screen *Reading mark* and *Reading hints* there.

## Games without a script

Console games without a script still run normally and can be read with OCR. PC games can be read
with a text hook or OCR.

### On Windows

<p align="center"><img src="home/publish/media/add-game.png" width="760" alt="Add game wizard on Windows showing Disc or file, Game window, and Texthooker or clipboard"></p>

Use **Add game** at the end of the shelf to choose a source:

| Option | Use for | Capture method |
| --- | --- | --- |
| **Disc or file** | Console games | Game script, or OCR if no script exists |
| **Game window** | Running PC games | Text hook, or OCR if no hook works |
| **Texthooker or clipboard** | Any other text source | WebSocket texthooker or clipboard |

For PC games, Furoshiki launches the hook backend automatically using
[Agent](https://github.com/0xDC00/agent) scripts, LunaHook, or
[Textractor](https://github.com/Artikash/Textractor) (downloaded on demand when selected; none are
bundled in this repo).

Hooked or streamed lines appear in a lookup bar at the edge of the screen; OCR also supports
in-place lookups over the game window. Anki cards, backlog, and reading stats work the same way.

### OCR on Windows

Windows OCR uses a separate **OCR helper** download because it bundles its own Python runtime and
~1 GB OCR model. Extract `Furoshiki-ocr-helper.zip` into your Furoshiki folder and run
`install.cmd` (no admin rights needed). Furoshiki manages starting and stopping the helper
automatically — see [home/ocr-helper/README.md](home/ocr-helper/README.md) for details.

Supported engines:
- **Local:** [meikiocr](https://github.com/rtr46/meikiocr) (default), `OneOCR`, `Chrome Screen AI`
- **Cloud:** `Google Lens` (sends cropped text regions to Google)

To enable OCR on a console game, open the Game menu (`Esc`) › *Use OCR for this game* › *Set text
area…*. You can configure multiple regions, including on-demand regions triggered with
`Ctrl+Shift+R` for menus or status screens.

### OCR on Android

<p align="center"><img src="home/publish/media/thor-ocr.jpg" width="760" alt="Persona 4 on Android using on-device OCR to look up 難易度 directly on screen"></p>

Android OCR works out of the box with no extra download, running `meikiocr` locally on-device.
`Google Lens` is also available (sends cropped text regions to Google) under Settings › Reading ›
OCR engine.

## Library and themes

<p align="center"><img src="home/publish/media/shelf.gif" width="760" alt="Browsing the Furoshiki game shelf on Windows"></p>

Games appear as box spines on a shelf; selecting one opens its cover and reading stats. Press
`Enter` to open its detail page, and `Enter` again to launch.

On Windows, press `F` on a game to fetch box art (Steam art for Steam titles,
[VNDB](https://vndb.org) cover/spine scans for others). Art is stored in
`%LOCALAPPDATA%\Furoshiki\program\art` as `<ID>.cover.jpg` and `<ID>.spine.jpg` (using the ID
shown on the game's page), so you can drop in custom images anytime. Games without a spine scan
get a generated console-styled spine. On Android, covers are fetched automatically.

Press `F4` to switch between the four built-in themes — **Kon**, **Paper**, **Mono**, and
**Solarized**:

<p align="center"><img src="home/publish/media/themes.png" width="760" alt="Furoshiki shelf in Kon, Paper, Mono, and Solarized themes"></p>

## Display and video

- **CRT filter:** Available on every console except PS Vita, with presets and sliders for
  scanlines, phosphor mask, and screen curvature (shown in the top Remember11 GIF). On Windows
  HDR displays, lit phosphor highlights can exceed SDR white on Dreamcast, PSP, and PS2.
- **Core video settings:** Resolution scaling and per-core graphics options can be set globally,
  per console, or per game.

## Settings

<p align="center"><img src="home/publish/media/settings.png" width="760" alt="Furoshiki Popup settings screen on Windows showing Fit height to entry and theme options"></p>

Press `F2` to open Settings on Windows. Each category shows common options first with advanced
rows behind *More*. Press `/` to search by name and `Backspace` to reset a row. Fully navigable
with keyboard, mouse, or controller.

Categories: *Statistics*, *Library*, *Reading*, *Popup*, *Cards*, *Consoles*, *Controls*, *Keys*,
*Display*, and *About*. The *Popup* page includes multiple themes, an *Opacity* slider, *Fit
height to entry*, and a popup editor with custom CSS support.

## On Android

Furoshiki on Android supports both touch and controller play, with virtual on-screen controls when
no physical pad is present and home-screen game shortcuts.

The in-game menu provides save states, fast forward, and skip to next choice:

<p align="center"><img src="home/publish/media/thor-menu.jpg" width="620" alt="In-game overlay menu over Grisaia no Kajitsu on Android"></p>

### Dual-screen handhelds

The screenshots below are from an AYN Thor. The library shelf runs on the top display (one page
per console), while the lower display shows the selected game's stats and launch button:

<p align="center"><img src="home/publish/media/thor-shelf.jpg" width="620" alt="Furoshiki library across both displays on the AYN Thor"></p>

<p align="center"><sub>A checkmark on a spine indicates a game script is available.</sub></p>

In-game, the lower display acts as your lookup surface (or the console's bottom screen for 3DS and
DS games), with two lookup modes:

- **Trackpad mode:** Move a pointer over the upper screen to look up words directly on the game.

<p align="center"><img src="home/publish/media/thor-trackpad.jpg" width="620" alt="STEINS;GATE on the AYN Thor using the lower display as a trackpad"></p>

- **Text panel mode:** Displays the current sentence below the trackpad so you can tap words and
  read definitions on the lower screen without covering the game.

<p align="center"><img src="home/publish/media/thor-lookup.jpg" width="620" alt="STEINS;GATE on the AYN Thor with Text panel showing the line and popup on the lower display"></p>

Quick buttons along the bottom provide **Pause**, **Pause timer**, **Backlog**, and **Menu**. In
the popup, `+` adds a card and the speaker icon adds a card with line audio.

### GameNative and other apps

[GameNative](https://github.com/utkarshdalal/GameNative) runs Windows games from Steam and other
stores on Android. Furoshiki can launch and read GameNative titles — or games in any other Android
app — via screen-capture OCR, showing popups over the game or in the lower-screen text panel. Use
**Text area** to crop OCR to the dialogue box if other UI is on screen.

<p align="center"><img src="home/publish/media/thor-gamenative.jpg" width="620" alt="GameNative PC games shelf on the AYN Thor"></p>

<p align="center"><sub>Glasses on a spine indicate OCR is configured; a question mark means OCR isn't set up yet.</sub></p>

To integrate GameNative:

1. In GameNative, enable Settings › Interface › Frontend Sync and pick a folder (e.g.
   `Games/GameNative`).
2. Select that same folder in Furoshiki during Setup or via Settings › Library › *Add GameNative
   folder*.

To add a game from any other app, use **Add game** › *Add game from another app*, pick the app,
and enter a title.

## Supported games

Each game script is a small Lua file targeting a specific Japanese release (check the ID below).
Scripts live in [furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts), organized by
console; Furoshiki downloads and verifies them automatically before installing.

<details><summary><b>PlayStation</b> (4 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Kamaitachi no Yoru | かまいたちの夜 特別篇 | SLPS-01794 | Story text and choices |
| Double Cast | ダブルキャスト | SCPS-10053, SCPS-10054 | Film subtitles and choices |
| Aconcagua | アコンカグア | SCPS-10131, SCPS-10132 | Message windows |
| Gakkou de Atta Kowai Hanashi S | 学校であった怖い話S | SLPS-00404 | Story text and choices |

</details>

<details><summary><b>PlayStation 2</b> (11 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Ever17 | Ever17 -the out of infinity- Premium Edition | SLPM-65421 | Story text with speakers, choices |
| Never7 | Never7 -the end of infinity- | SLPS-25256 | Story text with speakers, choices |
| Remember11 | Remember11 -the age of infinity- | SLPM-65550 | Story text with speakers, choices |
| CLANNAD | CLANNAD | SLPM-66302 | Story text with speakers, choices |
| Higurashi no Naku Koro ni Matsuri | ひぐらしのなく頃に祭 カケラ遊び | SLPM-66913 | Story text with speakers and furigana |
| Amagami | アマガミ | SLPS-25918 | Message window with furigana, choices |
| EVE burst error PLUS | EVE burst error PLUS | SLPM-65320 | Message window |
| Utawarerumono | うたわれるもの 散りゆく者への子守唄 | SLPS-25679 | Message window with speakers |
| Shadow Hearts | シャドウハーツ | SLPS-25041 | Scene and talk windows with speakers |
| Dragon Quest V | ドラゴンクエストV 天空の花嫁 | SLPM-65555 | Message window |
| Silent Hill 2 | サイレントヒル2 | SLPM-65051 | Subtitles |

</details>

<details><summary><b>PSP</b> (10 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Steins;Gate | STEINS;GATE | ULJM05887 | Message window with speakers and furigana, phone mails |
| Chaos;Head Love Chu Chu! | CHAOS;HEAD らぶChu☆Chu! | ULJM05821 | Message window with furigana |
| 428: Shibuya Scramble | 428 ～封鎖された渋谷で～ | ULJS00219 | Story text with furigana, TIPS, choices |
| Danganronpa | ダンガンロンパ 希望の学園と絶望の高校生 | NPJH50372 | Message box with speakers |
| Trails in the Sky FC | 英雄伝説 空の軌跡FC | ULJM05170 | Talk windows with speakers and furigana, choices |
| Tokimeki Memorial Forever with You | ときめきメモリアル forever with you | ULJM05078 | Message window with speakers, choices |
| Grisaia no Kajitsu | グリザイアの果実 -LE FRUIT DE LA GRISAIA- | ULJM06232, ULJM06233 | Message window with speakers, choices |
| Da Capo I&II Plus Situation Portable | D.C.I&II P.S.P. | ULJM05718, ULJM05719 | Message window with speakers and furigana, choices |
| My Merry May with be | My Merry May with be | ULJM05603 | Story text with speakers, choices |
| Dunamis 15 | DUNAMIS15 | ULJM06119 | Message window with speakers and furigana |

</details>

<details><summary><b>Dreamcast</b> (8 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Shenmue II (Windows only for now) | シェンムーII | HDR-0164 | Subtitles |
| What's Shenmue | What's シェンムー 〜湯川(元)専務をさがせ〜 | 6107179 | Subtitles |
| Sakura Taisen | サクラ大戦 | HDR-0072 | Message window, timed choices |
| Segagaga | セガガガ | HDR-0151 | Message window |
| Rent a Hero No.1 | レンタヒーロー No.1 | HDR-0074 | Message window |
| Kita e. White Illumination | 北へ。White Illumination | T14301M | Message window |
| July | July | T35401M | Message window with speakers, choices |
| Ayanami Raising Project | 新世紀エヴァンゲリオン 綾波育成計画 | T46303M | Message window |

</details>

<details><summary><b>Nintendo 3DS</b> (2 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Shin Megami Tensei IV | 真・女神転生IV | 0004000000088A00 | Dialogue, narration and choices |
| Professor Layton vs. Phoenix Wright: Ace Attorney | レイトン教授VS逆転裁判 | 0004000000078B00 | Dialogue with speakers, narration, subtitles, puzzle texts |

</details>

<details><summary><b>Nintendo DS</b> (2 games, Android)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Wish Room | ウィッシュルーム 天使の記憶 | AWIJ | Message windows with speakers, choices |
| 999: Nine Hours, Nine Persons, Nine Doors | 極限脱出 9時間9人9の扉 | BSKJ | Novel text and talk windows with speakers, escape rooms, choices, file pages |

</details>

<details><summary><b>PS Vita</b> (1 game)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Trails in the Sky FC Evolution | 英雄伝説 空の軌跡 FC Evolution | PCSG00488 | Talk windows with speakers and furigana |

</details>

## Adding a game

Game scripts live in [sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts) and
are designed to be written with a coding agent (Claude Code, Codex, Gemini CLI, etc.) on Windows,
Linux, or macOS. The agent launches the game in Furoshiki, locates its text in memory, writes the
Lua script, and verifies it against the running game.

**Requirements:** Furoshiki 0.3.2+, Python 3.9+, a coding agent, and your own dump of the Japanese
release.

1. Fork [furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts), clone it locally, and
   start your agent inside the repo.
2. Tell the agent which game to add along with the paths to your dump and Furoshiki — for example:
   *"Add support for Steins;Gate on PSP. The game is at `D:\Games\PSP\steins-gate.iso` and
   Furoshiki is in `C:\Furoshiki`."*
3. The agent follows [AGENTS.md](https://github.com/sommepo/furoshiki-scripts/blob/main/AGENTS.md)
   and summarizes what it verified in-game (plus anything it couldn't test).
4. Play through a few scenes to test it. The agent saves the script directly into your local
   scripts folder, which takes precedence over downloaded scripts:
   - **Windows:** `%LOCALAPPDATA%\Furoshiki\program\reader\profiles`
   - **Linux:** `~/.local/share/Furoshiki/program/reader/profiles`
   - **Android:** `Android/data/io.github.sommepo.furoshiki/files/reader/profiles`
5. Have the agent open a pull request. Once merged, everyone can install the script from Settings ›
   Consoles › *Game scripts* without updating the app.

To share an unmerged script with someone else, just send them the `.lua` file to drop into their
scripts folder above.

> **Note:** Games that render text with a custom bitmap font rather than standard encodings require
> a game-specific character table. Because those tables can't be redistributed, those games can't
> be added to the public scripts repo.

## Help

Report bugs or ask questions in [Issues](../../issues). Please do not report Furoshiki issues to
upstream emulator projects. Furoshiki is not affiliated with them, or with Sony, Sega, or
Nintendo.

## Building from source

| Folder | Description |
| --- | --- |
| `home/` | Desktop app (Windows and Linux) |
| `android/` | Android app |
| `shared/` | Shared lookup and Anki card code |
| `home/ocr-helper/` | Python OCR helper (`package.sh` builds its zip) |
| `cores/` | Core interface between Furoshiki and emulators |
| `station/`, `pcsx2/`, `armsx2/`, `ppsspp/`, `flycast/`, `azahar/`, `vita3k/`, `melonds/` | Emulator forks, cores, and game scripts |

All build scripts run on Linux. Each emulator's `tools/reader/env.sh` lists the expected compilers
and toolchains.

1. Fetch an emulator's submodules:
   ```bash
   git submodule update --init --recursive -- <folder>
   ```
2. Build its core: `<emulator>/tools/reader/build-core-windows.sh`, `build-core-android.sh`, or
   `build-core.sh` (Linux).
3. Build Furoshiki:
   - **Windows:** `HOME_PUBLIC=1 home/tools/build-windows.sh`, then `HOME_PUBLIC=1 home/tools/package-windows.sh`
   - **Android:** `android/tools/build.sh`

## Licence

Furoshiki and its cores are free software under the
[GNU General Public License, version 3](COPYING) (also included in `home/`, `cores/`, and
`shared/`). Each bundled emulator retains its own licence and notices in its folder.
[NOTICES.md](NOTICES.md) lists all package attributions and explains why Flycast (whose upstream
`LICENSE` is GPLv2) is distributed here under GPLv3.
Character name data comes from [VNDB](https://vndb.org) under the Open Database License
([titles/LICENSE](titles/LICENSE)). On Android, third-party notices and full licence texts are
available under Settings › About › Open-source licences.
