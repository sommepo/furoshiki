<p align="center"><img src="home/publish/media/logo.svg" width="128" alt="Furoshiki logo"></p>

<h1 align="center">Furoshiki</h1>

<p align="center"><b>A Japanese game launcher for Windows, Linux, and Android with built-in emulators, direct text extraction, dictionary popups, and Anki sentence mining.</b><br>
Point at, tap, or step through exact in-game text with a controller, look up words on screen, and send cards straight to Anki.</p>

<p align="center"><b>Latest release (0.5.0):</b>
<a href="../../releases/download/v0.5.0/Furoshiki-0.5.0-android.apk">Android 0.5.0</a> ·
<a href="../../releases/download/v0.5.0/Furoshiki-0.5.0-windows.zip">Windows 0.5.0</a> ·
<a href="../../releases/download/v0.5.0/Furoshiki-0.5.0-linux.tar.gz">Linux 0.5.0</a> ·
<a href="../../releases/download/v0.5.0/Furoshiki-ocr-helper.zip">OCR helper (Windows)</a> ·
<a href="../../releases">all releases</a></p>

<p align="center">
<a href="#game-scripts">Game scripts</a> ·
<a href="#controller-first-experience">Controller reading</a> ·
<a href="#android-dual-screen-ocr-and-gamenative">Android &amp; dual-screen</a> ·
<a href="#reading-statistics-across-devices">Statistics</a> ·
<a href="docs/setup.md">Setup</a> ·
<a href="docs/supported-games.md">Supported games</a>
</p>

<p align="center"><img src="home/publish/media/popup-lookup.gif" width="760" alt="Controller reading mode over CLANNAD (PS2) on the desktop: the popup for 常習犯 steps through its dictionary entries and changes height to fit each one, then the cursor moves to なのだ, the Grammar tab opens and a button press unfolds the grammar entry"></p>

<p align="center"><sub>CLANNAD (PS2) on the desktop with the Kon popup at 80% opacity and blur, showing Jitendex, 明鏡, 大辞泉, frequency badges, and a pitch accent graph from Kanjium (all dictionaries are imported by the user).</sub></p>

## Game scripts

Furoshiki reads exact Japanese text and character positions straight from emulator RAM using lightweight Lua **game scripts** ([sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts)). Seven emulator cores on Android (PlayStation, PS2, PSP, Dreamcast, 3DS, PS Vita, DS) and six on Windows and Linux are built into one app alongside local Yomitan dictionaries, VNDB character name cards, and one-press Anki sentence mining (word, sentence, screenshot, and line audio).

- **Exact text, no OCR errors:** Scripts read dialogue, speakers, furigana, and choices directly from game memory, placing interactive hitboxes over every character on screen.
- **Character name cards:** Supported titles include [VNDB](https://vndb.org)-backed character dictionaries prioritized ahead of general dictionaries.
- **Install inside the app:** Download and update scripts directly from **Settings › Game scripts** without updating Furoshiki.
- **Contribute new games:** Furoshiki is community-extensible. Game scripts live in [sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts) and are designed to be written and verified against a running game with a coding agent (Claude Code, Codex, Gemini CLI, etc.) via pull request. See [Adding a game](docs/adding-a-game.md) to get started, or open a pull request or issue here for app improvements.

<p align="center"><img src="home/publish/media/popup-names.jpg" width="760" alt="Names tab popup for 美佐枝さん in CLANNAD (PS2) on the desktop, showing her portrait, full name 相楽美佐枝, romaji, role, and character description"></p>

<p align="center"><sub>A character name card on the <i>Names</i> tab in CLANNAD (PS2) on the desktop.</sub></p>

<p align="center"><img src="home/publish/media/game-scripts.png" width="760" alt="Settings › Game scripts on the desktop in the Kon theme listing supported games by console with script, names, library, cover, and spine status"></p>

<p align="center"><sub>Settings › Game scripts on the desktop in the Kon theme.</sub></p>

<details>
<summary><b>Currently supported games (60 games across 7 consoles)</b></summary>

| Console | Game | Japanese title | ID |
| --- | --- | --- | --- |
| PlayStation | Kamaitachi no Yoru | かまいたちの夜 特別篇 | SLPS-01794 |
| PlayStation | Double Cast | ダブルキャスト | SCPS-10053, SCPS-10054 |
| PlayStation | Aconcagua | アコンカグア | SCPS-10131, SCPS-10132 |
| PlayStation | Gakkou de Atta Kowai Hanashi S | 学校であった怖い話S | SLPS-00404 |
| PlayStation | Otogirisou Sosei-hen | 弟切草 蘇生篇 | SLPS-01645 |
| PlayStation | Twilight Syndrome: Tansaku-hen | トワイライトシンドローム 探索編 | SLPS-00102 |
| PlayStation | Twilight Syndrome: Kyuumei-hen | トワイライトシンドローム 究明編 | SLPS-00405 |
| PlayStation | Moonlight Syndrome | ムーンライトシンドローム | SLPS-01001, SLPS-01002 |
| PlayStation | Memories Off | メモリーズオフ | SLPS-02296 |
| PlayStation | Final Fantasy VII | ファイナルファンタジーVII | SLPS-00700 to SLPS-00702 |
| PlayStation | Final Fantasy IX | ファイナルファンタジーIX | SLPS-02000 to SLPS-02003, SCPS-45500 to SCPS-45503 |
| PlayStation 2 | Ever17 | Ever17 -the out of infinity- Premium Edition | SLPM-65421 |
| PlayStation 2 | Never7 | Never7 -the end of infinity- | SLPS-25256 |
| PlayStation 2 | Remember11 | Remember11 -the age of infinity- | SLPM-65550 |
| PlayStation 2 | CLANNAD | CLANNAD | SLPM-66302 |
| PlayStation 2 | CLOCK ZERO | CLOCK ZERO 〜終焉の一秒〜 | SLPM-55281 |
| PlayStation 2 | Fate/stay night [Realta Nua] | Fate/stay night [Realta Nua] | SLPM-66512 |
| PlayStation 2 | Higurashi no Naku Koro ni Matsuri | ひぐらしのなく頃に祭 カケラ遊び | SLPM-66913 |
| PlayStation 2 | Amagami | アマガミ | SLPS-25918 |
| PlayStation 2 | EVE burst error PLUS | EVE burst error PLUS | SLPM-65320 |
| PlayStation 2 | Utawarerumono | うたわれるもの 散りゆく者への子守唄 | SLPS-25679 |
| PlayStation 2 | Shadow Hearts | シャドウハーツ | SLPS-25041 |
| PlayStation 2 | Dragon Quest V | ドラゴンクエストV 天空の花嫁 | SLPM-65555 |
| PlayStation 2 | Silent Hill 2 | サイレントヒル2 | SLPM-65051 |
| PlayStation 2 | Kanon | Kanon | SLPS-20121 |
| PlayStation 2 | Persona 3 | ペルソナ3 | SLPM-66445 |
| PlayStation 2 | Persona 4 | ペルソナ4 | SLPM-66978 |
| PlayStation 2 | Shin Megami Tensei III: Nocturne Maniax | 真・女神転生III NOCTURNE マニアクス | SLPM-65462 |
| PlayStation 2 | Digital Devil Saga | DIGITAL DEVIL SAGA アバタール・チューナー | SLPM-65597 |
| PlayStation 2 | Final Fantasy X International | ファイナルファンタジーX インターナショナル | SLPS-25088 |
| PlayStation 2 | Metal Gear Solid 2: Substance | METAL GEAR SOLID 2 SUBSTANCE | SLPM-67002 |
| PlayStation 2 | Metal Gear Solid 3: Snake Eater | METAL GEAR SOLID 3 SNAKE EATER | SLPM-65790 |
| PSP | Steins;Gate | STEINS;GATE | ULJM05887 |
| PSP | Chaos;Head Love Chu Chu! | CHAOS;HEAD らぶChu☆Chu! | ULJM05821 |
| PSP | 428: Shibuya Scramble | 428 ～封鎖された渋谷で～ | ULJS00219 |
| PSP | Danganronpa | ダンガンロンパ 希望の学園と絶望の高校生 | NPJH50372 |
| PSP | Trails in the Sky FC | 英雄伝説 空の軌跡FC | ULJM05170 |
| PSP | Tokimeki Memorial Forever with You | ときめきメモリアル forever with you | ULJM05078 |
| PSP | Grisaia no Kajitsu | グリザイアの果実 -LE FRUIT DE LA GRISAIA- | ULJM06232, ULJM06233 |
| PSP | Da Capo I&II Plus Situation Portable | D.C.I&II P.S.P. | ULJM05718, ULJM05719 |
| PSP | My Merry May with be | My Merry May with be | ULJM05603 |
| PSP | Dunamis 15 | DUNAMIS15 | ULJM06119 |
| PSP | FLOWERS | FLOWERS | ULJM06390 |
| Dreamcast | Shenmue II (Windows only for now) | シェンムーII | HDR-0164 |
| Dreamcast | What's Shenmue | What's シェンムー 〜湯川(元)専務をさがせ〜 | 6107179 |
| Dreamcast | Sakura Taisen | サクラ大戦 | HDR-0072 |
| Dreamcast | Segagaga | セガガガ | HDR-0151 |
| Dreamcast | Rent a Hero No.1 | レンタヒーロー No.1 | HDR-0074 |
| Dreamcast | Kita e. White Illumination | 北へ。White Illumination | T14301M |
| Dreamcast | July | July | T35401M |
| Dreamcast | Ayanami Raising Project | 新世紀エヴァンゲリオン 綾波育成計画 | T46303M |
| Nintendo 3DS | Shin Megami Tensei IV | 真・女神転生IV | 0004000000088A00 |
| Nintendo 3DS | Professor Layton vs. Phoenix Wright: Ace Attorney | レイトン教授VS逆転裁判 | 0004000000078B00 |
| Nintendo 3DS | Phoenix Wright: Ace Attorney Trilogy | 逆転裁判123 成歩堂セレクション | 0004000000108800 |
| Nintendo DS | Wish Room (Android) | ウィッシュルーム 天使の記憶 | AWIJ |
| Nintendo DS | 999: Nine Hours, Nine Persons, Nine Doors (Android) | 極限脱出 9時間9人9の扉 | BSKJ |
| PS Vita | Trails in the Sky FC Evolution | 英雄伝説 空の軌跡 FC Evolution | PCSG00488 |
| PS Vita | WHITE ALBUM 2: Shiawase no Mukougawa | WHITE ALBUM 2 幸せの向こう側 | PCSG00249 |
| PS Vita | Oretachi ni Tsubasa wa Nai | 俺たちに翼はない | PCSG00299 |
| PS Vita | Zero Escape: Virtue's Last Reward | 極限脱出ADV 善人シボウデス | PCSG00020 |

</details>

See [Consoles and supported games](docs/supported-games.md) for emulator details and what each script reads, and [Adding a game](docs/adding-a-game.md) to add your own.

## Controller-first experience

Furoshiki is built to be used from a sofa or handheld with just a controller. Library browsing, statistics, settings, in-game menus, dictionary popups, and Anki card creation all work without a keyboard or mouse (mouse and keyboard work everywhere too).

<p align="center"><img src="home/publish/media/shelf.gif" width="760" alt="Browsing the Furoshiki game shelf on the desktop in the Nocturne theme: titles open to show covers and reading figures, then the shelf changes console from All down to Dreamcast"></p>

<p align="center"><sub>Browsing the library shelf on the desktop in the Nocturne theme.</sub></p>

On the desktop, tap `Select` in-game to drop a cursor onto the first word in the current line that isn't in Anki yet. Step word by word with the D-pad, jump between unknown words with `LB`/`RB`, switch tabs (*Words*, *Grammar*, *Kanji*, *Names*) with `LT`/`RT`, send a card with `Y`/`B`, and advance dialogue with `A`.

<p align="center"><img src="home/publish/media/controller-reading.gif" width="760" alt="Controller reading mode on the desktop walking word by word along a CLANNAD (PS2) line with the D-pad, the blurred Kon popup following each word and advancing to the next line"></p>

<p align="center"><sub>Pointer-free controller reading on the desktop in CLANNAD (PS2): stepping word by word with the D-pad and advancing dialogue with <code>A</code>.</sub></p>

<p align="center"><img src="home/publish/media/controller-kanji.gif" width="760" alt="Controller reading mode on the desktop in CLANNAD (PS2), opening the Kanji tab for 常習犯 with RT, focusing the popup with X, and stepping through 常, 習, and 犯 with the D-pad"></p>

<p align="center"><sub>Stepping through 常, 習, and 犯 on the <i>Kanji</i> tab with the D-pad (using imported kanji dictionaries including KANJIDIC).</sub></p>

See [Reading, popups, and controller controls](docs/reading.md) for the full button map, keyboard shortcuts, and customization options.

## Android dual-screen, OCR, and GameNative

On Android, Furoshiki supports touch, controllers, and dual-screen handhelds such as the AYN Thor. The top display runs your library and game while the lower display shows game details, acts as a trackpad or lower-screen text panel in-game, and holds thumb controls for menus, statistics, and settings.

<p align="center"><img src="home/publish/media/thor-shelf.jpg" width="620" alt="Furoshiki's library on the AYN Thor: the PS1 games on the top display, Moonlight Syndrome's cover, figures and last line on the lower display"></p>

<p align="center"><sub>Library on the AYN Thor: games on the top display, cover, stats, and last line on the lower display (a checkmark marks games with a script).</sub></p>

<p align="center"><img src="home/publish/media/thor-popup.jpg" width="620" alt="撮影 looked up in STEINS;GATE (PSP) on the AYN Thor: the popup at 80% opacity over the blurred game, the lower display as the trackpad"></p>

<p align="center"><sub>Looking up 撮影 over STEINS;GATE (PSP) on the AYN Thor using the lower display as a trackpad, with <code>L1</code>/<code>R1</code> switching tabs.</sub></p>

<p align="center"><img src="home/publish/media/thor-popup-kanji.jpg" width="620" alt="The Kanji tab for 撮 in STEINS;GATE (PSP) on the AYN Thor, from 旺文社漢字典"></p>

<p align="center"><img src="home/publish/media/thor-menu.jpg" width="620" alt="The game menu over STEINS;GATE (PSP) on the AYN Thor, with the lower display's Pause, Backlog, Text panel, Explain, Guide and Menu keys"></p>

For games without a script, **on-device OCR** (`meikiocr`, plus optional Google Lens) works out of the box on Android with no extra download. Furoshiki also integrates with [GameNative](https://github.com/utkarshdalal/GameNative) (and any other Android app) so you can launch Windows/Steam games on Android and read them with screen-capture OCR.

<p align="center"><img src="home/publish/media/thor-ocr.jpg" width="620" alt="Persona 4 on Android using on-device OCR to look up 難易度 directly on screen"></p>

<p align="center"><img src="home/publish/media/thor-gamenative.jpg" width="620" alt="GameNative PC games shelf on the AYN Thor"></p>

<p align="center"><sub>On-device OCR (Persona 4) and GameNative PC games on the AYN Thor (glasses mark titles with OCR configured).</sub></p>

See [Android, dual-screen, OCR, and GameNative](docs/android.md) for text panel mode, GameNative setup, and Android controls.

## Reading statistics across devices

The **Statistics** tab tracks your reading history across every title: daily, weekly, monthly, and yearly character goals (set with an inline soroban abacus), reading streaks, pace, a milestone ruler, a 52-week daily dot heatmap, and a per-period breakdown across *Week*, *Month*, *Year*, and *All time*.

Statistics are stored as per-device JSON files (`<device-id>.json`, plus `goals.json`) in a folder you can point at [Syncthing](https://syncthing.net/), a network share, or a cloud folder under **Settings › Sync & About › Statistics folder**. Because each device writes only its own file and reads the others, your Windows, Linux, and Android reading totals combine cleanly with no merge conflicts.

<p align="center"><img src="home/publish/media/statistics.png" width="760" alt="Furoshiki Statistics tab on the desktop in the Nocturne theme showing yearly and daily character goals, streak, reading pace, milestone ruler, 52-week dot heatmap, characters arc, and peak days"></p>

<p align="center"><sub>The Statistics tab on the desktop in the Nocturne theme (sample reading history).</sub></p>

<p align="center"><img src="home/publish/media/statistics-soroban.gif" width="760" alt="Setting the daily character goal on the inline soroban abacus in the Statistics tab on the desktop in the Kon theme, sliding beads from 10.5K down to 9K"></p>

<p align="center"><sub>Setting a character goal on the inline soroban abacus on the desktop.</sub></p>

<p align="center"><img src="home/publish/media/thor-statistics.jpg" width="620" alt="Furoshiki's Statistics on the AYN Thor: figures and the year of days on the top display, the period and goal keys on the lower display"></p>

<p align="center"><sub>Statistics on the AYN Thor with period and goal controls on the lower display.</sub></p>

## Documentation

| Guide | What it covers |
| --- | --- |
| [Setup and installation](docs/setup.md) | Downloads, dependencies, Windows/Linux/Android setup, BIOS folders, dictionary import, and Syncthing statistics sharing |
| [Consoles and supported games](docs/supported-games.md) | Built-in emulator cores and the full list of scripted games by console |
| [Reading, popups, and controller controls](docs/reading.md) | Dictionary lookups, VNDB character names, Anki cards, backlog, keyboard shortcuts, and full controller reading bindings |
| [Desktop features, OCR, themes, and settings](docs/desktop.md) | Windows PC game hooks, OCR helper (`meikiocr`, OneOCR, Chrome Screen AI, Google Lens), library themes, CRT filter, and Settings |
| [Android, dual-screen, OCR, and GameNative](docs/android.md) | Touch/controller play, AYN Thor trackpad & text panel modes, on-device OCR, and GameNative integration |
| [Adding a game](docs/adding-a-game.md) | Writing and verifying a new game script with a coding agent via `sommepo/furoshiki-scripts` |
| [Building from source](docs/building.md) | Repository layout, submodules, and Linux/Windows/Android build scripts |

## Help and licence

Report bugs or ask questions in [Issues](../../issues). Please do not report Furoshiki issues to upstream emulator projects. Furoshiki is not affiliated with them, or with Sony, Sega, or Nintendo.

Furoshiki and its cores are free software under the [GNU General Public License, version 3](COPYING) (also included in `home/`, `cores/`, and `shared/`). Each bundled emulator retains its own licence and notices in its folder; see [NOTICES.md](NOTICES.md) for full attributions, [titles/LICENSE](titles/LICENSE) for VNDB character name data (ODbL), and each script's header for [0xDC00's Agent](https://github.com/0xDC00/scripts) and [LunaHook](https://github.com/HIllya51/LunaTranslator) credits.
