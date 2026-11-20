<p align="center"><img src="home/publish/media/logo.svg" width="128" alt="Furoshiki's logo: ふ in blue on white"></p>

<h1 align="center">Furoshiki</h1>

<p align="center"><b>A game launcher with sentence mining built in, for Japanese games.</b><br>
Look up a word in the game's own text and add it to Anki, from the couch with a controller. For Windows and Android.</p>

<p align="center"><b>Latest releases:</b>
<a href="../../releases/download/v0.3.0/Furoshiki-0.3.0-android.apk">Android 0.3.0</a> ·
<a href="../../releases/download/v0.3.0/Furoshiki-0.3.0-windows.zip">Windows 0.3.0</a> ·
<a href="../../releases/download/v0.3.0/Furoshiki-ocr-helper.zip">OCR helper (Windows)</a> ·
<a href="../../releases">all releases</a></p>

<p align="center"><img src="home/publish/media/lookup-remember11.gif" width="760" alt="Remember11 on the PS2: the pointer moves along a line and 視線, そらし, 肩越し and 目を向けた each open in a popup the moment it reaches them"></p>

<p align="center"><sub>Remember11 (PS2) on Windows. The frame around the game is the optional CRT filter.</sub></p>

Furoshiki keeps your Japanese games in one library and plays them itself: emulators for seven
consoles are built in (six on Windows). While you play, it reads the game's text, so you can look a word up where it stands
on the screen and send the sentence to Anki with a screenshot and the line's audio. You do not
need a second window beside the game, and nothing is copied and pasted.

A game's text is read in one of three ways:

| How | What you get | Where |
| --- | --- | --- |
| A **game script** written for that game | The exact text from the game's memory, and where each character is drawn. You point at the game's own text, as in the picture above. | [38 games](#supported-games), Windows and Android |
| **OCR** of the picture | The text as an OCR engine reads it. Good for any game, with the odd misread character. | Windows and Android |
| A **text hook**, or lines from another tool | The exact text of a PC game, shown in a strip at the edge of the screen. | Windows |

## Made for a controller

Furoshiki is meant to be used from a sofa or a handheld, with a controller in your hands and no
keyboard or mouse. The library, every settings page, the game menu, the dictionary popup and
making cards all work from the controller alone. The mouse and keyboard still work everywhere.

- **Any controller.** An Xbox, PlayStation, Switch or 8BitDo pad, or the Steam Controller, works
  as soon as it is plugged in or paired. The first time a controller is used, Furoshiki asks one
  question, "Press the button marked A", so it knows whether your pad has Xbox, Nintendo or
  PlayStation letters. From then on every hint on screen shows the letters printed on your
  controller.
- **Shortcuts in the same setup.** The next step sets the buttons you read with: backlog, pause
  the reading timer, pause the game, the Quick menu, a card for the line. A button that reading
  already uses gets a warning, and the free ones are suggested.
- **The popup takes the controller.** While a popup is open the game hears nothing. The d-pad
  goes from entry to entry, the shoulder buttons from tab to tab (Words, Grammar, Kanji, Names),
  and the top and right face buttons make a card, without and with the line's audio. The game gets the
  controller back when the popup closes.
- **Reading without a pointer** (Windows). Tap Select in a game and a cursor lands on the first
  word of the line you do not know yet. The d-pad moves word by word, the bumpers jump to the next
  word not in Anki, and the bottom button still advances the game. See
  [Reading with a controller](#reading-with-a-controller).
- **A pointer when you want one.** The Steam Controller's trackpad, or any button you give the
  *Pointer (hold)* action, moves a pointer over the game, and on the AYN Thor the lower screen is a
  trackpad. Using a pointer ends controller reading at once.

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

Each emulator is built into Furoshiki as a core, the way RetroArch does it, so there is nothing
else to install. Settings can be made for all consoles, for one console or for one game.

## Download

Version 0.3.0 has both, and the OCR helper for Windows.

| File | For | Notes |
| --- | --- | --- |
| [Furoshiki-0.3.0-windows.zip](../../releases/download/v0.3.0/Furoshiki-0.3.0-windows.zip) | Windows 10 or 11, 64-bit | Unzip it anywhere and run `Furoshiki.exe`. |
| [Furoshiki-0.3.0-android.apk](../../releases/download/v0.3.0/Furoshiki-0.3.0-android.apk) | Android 10 or later, 64-bit ARM | Open the file to install it, and allow installs from your browser or file manager when Android asks. |
| [Furoshiki-ocr-helper.zip](../../releases/download/v0.3.0/Furoshiki-ocr-helper.zip) | Windows, optional | Only if you want OCR on Windows. See [OCR on Windows](#ocr-on-windows). |

On Windows, the PS2 and Vita cores need the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe). The
dictionary popup is drawn with Microsoft's WebView2 runtime, which Windows 11 and an up-to-date
Windows 10 already have. Without it the popup still works, in a plainer look.

There is no Linux download. The Windows program also builds for Linux
([Building from source](#building-from-source)).

Nothing from a console maker is included. You bring your own games, dumped from discs or
cartridges you own, and the BIOS files listed below.

## Setting up on Windows

1. Start `Furoshiki.exe`. The program is not signed, so Windows may show "Windows protected your
   PC": choose *More info*, then *Run anyway*. The first start opens Setup, three steps you can
   skip. The first, Dictionary, downloads [Jitendex](https://jitendex.org), a free dictionary
   (39 MB), or takes your own Yomitan dictionary zips. Lookups need at least one dictionary. You
   can add more later in Settings › Reading › Dictionaries.
2. The second step, Games, asks for the folder your games are in. Put the BIOS files your
   consoles need in the folders in the table below first, or come back to this later with
   **Add game** at the end of the shelf, then *Disc or file*.
3. The third step, Anki, is optional. With Anki open and the
   [AnkiConnect](https://ankiweb.net/shared/info/2055492159) add-on installed, Furoshiki sets up
   cards with a deck and note type you already have: the deck with "mining" in its name (or the
   first that is not Default), and the note type most of that deck's notes use. It fills the
   fields it recognises by name. Settings › Cards › Anki setup changes any of this.
4. To install a game's script, open Settings › Consoles, choose the console, then *Game scripts*.
   The Quick menu in a game (F1 or right-click) has *Game scripts…* too. Start the game again
   after installing its script.

Settings › About › Setup opens the three steps again.

| Console | You need | Folder in `%LOCALAPPDATA%\Furoshiki` |
| --- | --- | --- |
| PlayStation | A BIOS, such as `SCPH-5500.bin` | `program\consoles\ps1\system` |
| PlayStation 2 | A BIOS | `program\consoles\ps2\system` |
| PSP | Nothing | |
| Dreamcast | Nothing. Your own `dc_boot.bin` is optional. | `program\consoles\dc\system\dc` |
| Nintendo 3DS | Decrypted games. `aes_keys.txt` only if a game asks for it. | `program\consoles\3ds\system` |
| PS Vita | The firmware: `PSVUPDAT.PUP`, and `PSP2UPDAT.PUP` if you have it | `program\consoles\psvita\system` |

## Setting up on Android

1. Install the APK and open Furoshiki. Tap *Allow file access* when it asks, so it can find your
   games where they are.
2. Setup asks where your games are. Choose *Add games folder*, once or once for each console if
   each has its own. A `Games` folder on the device or its SD card is found without asking.
3. Put BIOS files in `Games/BIOS/PS1`, `Games/BIOS/PS2` and `Games/BIOS/Vita`. The other consoles
   need none.
4. Put Yomitan dictionary zips in `Games/Dictionaries`. They are imported the next time
   Furoshiki starts.
5. Install [AnkiDroid](https://github.com/ankidroid/Anki-Android) if you want cards. Furoshiki
   asks for its permission once, then uses your mining deck and its note type, as on Windows.

Every game found is listed, and the scripts for supported games are downloaded for you. Box art
is fetched too. Settings › Library › Game scripts shows each console's list.

## Reading a game

<p align="center"><img src="home/publish/media/title-page.png" width="760" alt="428: Shibuya Scramble's page in the library: its cover, Continue, 51,703 characters read in 10 hours 3 minutes at 5,142 an hour, and its characters' names with their readings"></p>

<p align="center"><sub>A game's page on Windows: what you have read of it, and the character names that come with its script.</sub></p>

On Windows you look a word up by pointing at it or clicking it. On Android you tap it. The popup
shows entries from your dictionaries, with the plain form of an inflected word found for you. A
supported game also has its characters' names and their readings, which are looked up before the
general dictionaries.

A card has the word, the sentence, a screenshot and the line's audio, which can be trimmed to the
voice. On Android the card buttons are in the popup. On Windows these are the keys:

| Key | What it does |
| --- | --- |
| Ctrl+D | Add a card for the sentence |
| Ctrl+W | Add a card for the word you looked up |
| Ctrl+Z | Undo the last card |
| Ctrl+H | Backlog |
| Ctrl+G | Pause game |
| Ctrl+T | Pause reading timer |
| Ctrl+N | Skip to the next choice |
| F1 or right-click | Quick menu |
| Esc | Game menu |
| F2 | Settings |

The backlog keeps every line you have read, with its speaker where the game gives one, and you
can make a card from several lines of it. For each game Furoshiki counts the characters you read,
the time, and your speed in characters per hour. The count stays on your device.

With a controller, the Steam Controller's trackpad moves the pointer with no setup. With another
controller, give a button the *Pointer (hold)* action in Settings › Controls › Button mapping.
While you hold that button, the sticks move the pointer. Or read without a pointer, below.

### Reading with a controller

On Windows you can also read with a plain controller and no pointer. In a game, tap Select to
start reading. A cursor lands on the first word of the line that is not in Anki yet, or on its
first word, and the popup opens beside it.

| Button | What it does |
| --- | --- |
| D-pad left and right | Previous or next word. Hold it to keep going. |
| D-pad up and down | The line above or below. In vertical text the two pairs swap. |
| LB and RB | Previous or next word that is not in Anki |
| Right stick left and right | Make the match one character shorter or longer |
| Right stick up and down | Scroll the popup |
| LT and RT | Previous or next tab of the popup (Words, Grammar, Kanji, Names) |
| A | Advance the game, with the console's own confirm button. The cursor lands on the new line. |
| Y | Add a card for the word |
| B | Add a card with the line's audio |
| X | Go into the popup to pick another entry or tab. X or A comes back to the text. |

The letters are those of an Xbox pad, by place: A is the bottom button, B the right, X the left
and Y the top. On a pad with other letters, the hints over the game show its own.
| Select | Stop reading |

Start and the left stick stay the game's. When the game shows a choice, every button goes back
to the game until the next line. Moving the mouse or pressing one of its buttons stops reading,
and the pointer works as before. This works in games with a script, and in
console games read by OCR, where nothing reports a choice.

Settings › Controls › *Start reading with* changes the button: Select, L3, R3, two buttons
together (Select + A, L3 + R3, LB + RB), or *Always during dialogue*, which needs no button. The
same page turns the *Reading mark* and the *Reading hints* over the game on and off.

## Games without a script

A console game with no script still plays, and can be read by OCR. PC games are read by a text
hook or by OCR.

### On Windows

<p align="center"><img src="home/publish/media/add-game.png" width="760" alt="The shelf on Windows with Add game open at its end: A, disc or file, by an emulator; B, game window, by hook or OCR; C, texthooker or clipboard, as lines"></p>

**Add game**, at the end of the shelf, asks where the game is:

| Choice | Use it for | Read by |
| --- | --- | --- |
| Disc or file | A console game | Its script, or OCR if it has none |
| Game window | A PC game that is running | A text hook, or OCR if no hook fits |
| Texthooker or clipboard | Any other source of text | Lines from a texthooker's WebSocket, or text copied to the clipboard |

For a PC game Furoshiki starts the hook itself. It can use
[Agent](https://github.com/0xDC00/agent) scripts, LunaHook or
[Textractor](https://github.com/Artikash/Textractor). These are their authors' own projects. They
are not part of this repository, and Furoshiki downloads one only when you choose it.

Text read by a hook or sent as lines appears in a strip at the edge of the screen, and you look
words up there. With OCR you can also look words up on the game's window. Cards, the backlog and
the reading count work as they do for a game with a script.

### OCR on Windows

OCR needs the OCR helper, a separate download because it comes with its own copy of Python and an
OCR model of about 1 GB. Unzip `Furoshiki-ocr-helper.zip` into Furoshiki's folder and run
`install.cmd`. It needs no administrator rights. Furoshiki starts and stops the helper by itself.
[The helper's own README](home/ocr-helper/README.md) has the details.

You choose the engine. [meikiocr](https://github.com/rtr46/meikiocr) is the default; OneOCR and
Chrome Screen AI also read on your computer, while Google Lens sends each text area to Google.

To read a console game by OCR, open the Game menu (Esc) and choose *Use OCR for this game*, then
*Set text area…*. A game can have several text areas, and an area can be read only when you press
Ctrl+Shift+R, which suits a menu or a status screen.

### OCR on Android

<p align="center"><img src="home/publish/media/thor-ocr.jpg" width="760" alt="Persona 4, a game with no script, on Android: its message box read by OCR, and 難易度 tapped in the game's own text opens なんいど, degree of difficulty"></p>

On Android, OCR needs nothing extra. It runs on the device with meikiocr and sends nothing
anywhere. Google Lens reads better and sends each text area to Google. Choose between them in
Settings › Reading › OCR engine.

## The library

<p align="center"><img src="home/publish/media/shelf.gif" width="760" alt="The shelf on Windows: thirteen games stand as box spines, and each opens to its cover and reading count as the selection passes along"></p>

Your games stand on a shelf. The selected one opens to its cover and shows what you have read of
it. Enter opens its page, and Enter again plays it.

On Windows, F on a game looks for its box art: Steam's art for a Steam game, and
[VNDB](https://vndb.org)'s cover and spine scans for others. The pictures are kept in
`%LOCALAPPDATA%\Furoshiki\program\art`, and you can put your own there as `<ID>.cover.jpg` and
`<ID>.spine.jpg`, with the ID shown on the game's page. A game with no spine picture gets one
drawn in its console's style. On Android, covers are fetched without asking.

F4 changes the theme. From the top left: Kon, Paper, Mono and Solarized.

<p align="center"><img src="home/publish/media/themes.png" width="760" alt="The same shelf in four themes: Kon, dark blue; Paper, cream; Mono, black; Solarized, dark teal"></p>

## Picture

A CRT filter is available for every console except the PS Vita. It has scan lines, a phosphor
pattern and curved glass, with sliders for each and ready-made looks. It is on in the picture at
the top of this page. On Windows with an HDR screen, the lit phosphor can be brighter than white
on the Dreamcast, PSP and PS2.

Each core's own picture settings, such as upscaling, can be set for all consoles, one console or
one game.

## Settings

<p align="center"><img src="home/publish/media/settings.png" width="760" alt="The Reading page of the settings on Windows: Click to look up, Look up on hover, Highlight looked-up word, Dictionaries, and More with ten further rows"></p>

F2 opens the settings on Windows. Each page shows a few rows and keeps the others behind *More*.
Press `/` to search for a setting by name, and Backspace to reset the selected row. Everything
can be reached with the keyboard, the mouse or a controller alone.

The pages are Statistics, Library, Reading, Popup, Cards, Consoles, Controls, Keys, Display and
About. The Popup page has several themes, an *Opacity* row so the game shows through,
*Fit height to entry*, and a popup editor with a place for your own CSS.

## On Android

Furoshiki works by touch or with a controller. Where the device has no buttons it shows a stick
and buttons over the game, and a game can be pinned to the home screen.

The Game menu has save states, fast forward and skip to the next choice.

<p align="center"><img src="home/publish/media/thor-menu.jpg" width="620" alt="The game menu over STEINS;GATE on Android: Resume, More settings, Save state, Load state, Fast forward, Skip to next choice, Shortcut buttons, Quit game"></p>

### Handhelds with two displays

The pictures below are from an AYN Thor. The shelf is on the upper display, with a page for each
console. The lower display shows the selected game and the button that plays it.

<p align="center"><img src="home/publish/media/thor-shelf.jpg" width="620" alt="The AYN Thor's two displays. Above, the PSP shelf with STEINS;GATE's cover open among box spines and the list of consoles at the right. Below, STEINS;GATE's reading count with Continue and Game settings"></p>

<p align="center"><sub>A tick on a spine means the game is read by its script.</sub></p>

In a game, the lower display is where you look words up. For a 3DS or DS game it is the console's
lower screen instead. There are two ways to look a word up.

With the trackpad, you move a pointer over the game's own text. The word under the pointer is
looked up, and the popup opens over the game.

<p align="center"><img src="home/publish/media/thor-trackpad.jpg" width="620" alt="STEINS;GATE on the AYN Thor: 会見 outlined in the game's message window and looked up over the game, かいけん, interview; the lower display is a trackpad above the keys Pause, Pause stats, Log, Line and Menu"></p>

With **Text panel**, the sentence is also shown below the trackpad. Tap a word in it, and the
popup opens on the lower display, so nothing covers the game.

<p align="center"><img src="home/publish/media/thor-lookup.jpg" width="620" alt="The same sentence with Line on: it is shown on the lower display under the trackpad, and 会見 tapped there opens its entry on the lower display"></p>

**Pause** stops the game, **Pause timer** stops the reading timer, **Backlog** opens the backlog
and **Menu** opens the Game menu. In the popup, `+` adds the card and the speaker button adds it with
the line's audio.

### GameNative and other apps

[GameNative](https://github.com/utkarshdalal/GameNative) runs Windows games from Steam and other
stores on Android. Furoshiki can read those games, and games in any other app, by OCR. It starts
the game, asks once for permission to capture the screen, and reads the text. You look words up
over the game, or in the text panel on a second display. If the screen has much on it besides
the game's text, **Text area** marks where the text is.

<p align="center"><img src="home/publish/media/thor-gamenative.jpg" width="620" alt="The AYN Thor's two displays: the GameNative shelf with nine PC games above; below, PARANORMASIGHT's reading count and Continue"></p>

<p align="center"><sub>Glasses on a spine mean the game is read by OCR. A question mark means OCR is not set up for it yet.</sub></p>

To have your GameNative games listed and started from Furoshiki:

1. In GameNative, open Settings › Interface › Frontend Sync, turn it on and choose a folder. A
   new one such as `Games/GameNative` is a good choice.
2. In Furoshiki, choose the same folder. Setup asks for it, and Settings › Library has
   *Add GameNative folder*.

The games then have a GameNative shelf. A game from any other app is added with **Add game**,
then *Add game from another app*: choose the app and give the game a name.

## Supported games

A game script is a small Lua file for one release of one game, so check the ID. Scripts are for
the Japanese release. Furoshiki downloads scripts from the lists in this repository and checks
each file against its list before installing it.

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

## Help

Report a problem or ask a question in [Issues](../../issues). Please do not report Furoshiki's
problems to the emulators' projects. Furoshiki is not affiliated with them, or with Sony, Sega or
Nintendo.

## Building from source

| Folder | What it is |
| --- | --- |
| `home/` | Furoshiki for Windows and Linux |
| `android/` | Furoshiki for Android |
| `shared/` | The lookup and card code both use |
| `home/ocr-helper/` | The OCR helper (Python). `package.sh` makes its zip. |
| `cores/` | The interface between Furoshiki and the cores |
| `station/`, `pcsx2/`, `armsx2/`, `ppsspp/`, `flycast/`, `azahar/`, `vita3k/`, `melonds/` | One emulator each, with its core and its game scripts |

The build scripts run on Linux, for all three targets. Each emulator's `tools/reader/env.sh`
lists the compilers and toolkits the scripts expect.

1. Fetch an emulator's third-party code:
   `git submodule update --init --recursive -- <folder>`
2. Build its core: `<emulator>/tools/reader/build-core-windows.sh`, `build-core-android.sh`, or
   `build-core.sh` for Linux.
3. Build Furoshiki. For Windows: `HOME_PUBLIC=1 home/tools/build-windows.sh`, then
   `HOME_PUBLIC=1 home/tools/package-windows.sh`. For Android: `android/tools/build.sh`.

## Licence

Furoshiki and the cores are free software under the
[GNU General Public License, version 3](COPYING). The folders `home/`, `cores/` and `shared/`
each have a copy. Each emulator keeps its own licence and its authors' notices in its folder.
[NOTICES.md](NOTICES.md) lists what is in the packages and under which terms. It also explains
why Flycast's code, whose `LICENSE` file is the GPLv2 text, is distributed here under version 3.
