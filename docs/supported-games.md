# Consoles and supported games

[Back to README](../README.md)

## Consoles

| Console | Emulator | Windows & Linux | Android |
| --- | --- | --- | --- |
| PlayStation | [DuckStation](https://github.com/stenzek/duckstation) | Yes | Yes |
| PlayStation 2 | [PCSX2](https://github.com/PCSX2/pcsx2) ([ARMSX2](https://github.com/ARMSX2/ARMSX2) on Android) | Yes | Yes |
| PSP | [PPSSPP](https://github.com/hrydgard/ppsspp) | Yes | Yes |
| Dreamcast | [Flycast](https://github.com/flyinghead/flycast) | Yes | Yes |
| Nintendo 3DS | [Azahar](https://github.com/azahar-emu/azahar) | Yes | Yes |
| PS Vita | [Vita3K](https://github.com/Vita3K/Vita3K) | Yes | Yes |
| Nintendo DS | [melonDS](https://github.com/melonDS-emu/melonDS) | Not yet | Yes |

Emulators are built in as cores (similar to RetroArch), so there's nothing extra to install. Core and video settings can be configured globally, per console, or per game.

## Supported games (60 games)

Each game script is a small Lua file targeting a specific Japanese release (check the ID below). All scripts live in [sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts), organized by console and game. Furoshiki downloads and verifies them automatically under **Settings › Game scripts**, and new games can be added by pull request using a coding agent (see [Adding a game](adding-a-game.md)).

<details><summary><b>PlayStation</b> (11 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Kamaitachi no Yoru | かまいたちの夜 特別篇 | SLPS-01794 | Story text and choices |
| Double Cast | ダブルキャスト | SCPS-10053, SCPS-10054 | Film subtitles and choices |
| Aconcagua | アコンカグア | SCPS-10131, SCPS-10132 | Message windows |
| Gakkou de Atta Kowai Hanashi S | 学校であった怖い話S | SLPS-00404 | Story text and choices |
| Otogirisou Sosei-hen | 弟切草 蘇生篇 | SLPS-01645 | Story text and choices |
| Twilight Syndrome: Tansaku-hen | トワイライトシンドローム 探索編 | SLPS-00102 | Message window with speakers |
| Twilight Syndrome: Kyuumei-hen | トワイライトシンドローム 究明編 | SLPS-00405 | Message window with speakers |
| Moonlight Syndrome | ムーンライトシンドローム | SLPS-01001, SLPS-01002 | Text window with speakers |
| Memories Off | メモリーズオフ | SLPS-02296 | Text window with speakers, choices |
| Final Fantasy VII | ファイナルファンタジーVII | SLPS-00700, SLPS-00701, SLPS-00702 | Field message windows, choices |
| Final Fantasy IX | ファイナルファンタジーIX | SLPS-02000 to SLPS-02003, SCPS-45500 to SCPS-45503 | Dialogue windows with speakers, choices |

</details>

<details><summary><b>PlayStation 2</b> (21 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Ever17 | Ever17 -the out of infinity- Premium Edition | SLPM-65421 | Story text with speakers, choices |
| Never7 | Never7 -the end of infinity- | SLPS-25256 | Story text with speakers, choices |
| Remember11 | Remember11 -the age of infinity- | SLPM-65550 | Story text with speakers, choices |
| CLANNAD | CLANNAD | SLPM-66302 | Story text with speakers, choices |
| CLOCK ZERO | CLOCK ZERO 〜終焉の一秒〜 | SLPM-55281 | Dialogue, narration, speakers, choices |
| Fate/stay night [Realta Nua] | Fate/stay night [Realta Nua] | SLPM-66512 | Dialogue, narration, furigana |
| Higurashi no Naku Koro ni Matsuri | ひぐらしのなく頃に祭 カケラ遊び | SLPM-66913 | Story text with speakers and furigana |
| Amagami | アマガミ | SLPS-25918 | Message window with furigana, choices |
| EVE burst error PLUS | EVE burst error PLUS | SLPM-65320 | Message window |
| Utawarerumono | うたわれるもの 散りゆく者への子守唄 | SLPS-25679 | Message window with speakers |
| Shadow Hearts | シャドウハーツ | SLPS-25041 | Scene and talk windows with speakers |
| Dragon Quest V | ドラゴンクエストV 天空の花嫁 | SLPM-65555 | Message window |
| Silent Hill 2 | サイレントヒル2 | SLPM-65051 | Subtitles |
| Kanon | Kanon | SLPS-20121 | Story text with speakers, choices |
| Persona 3 | ペルソナ3 | SLPM-66445 | Message windows with speakers, choices |
| Persona 4 | ペルソナ4 | SLPM-66978 | Message windows with speakers, choices |
| Shin Megami Tensei III: Nocturne Maniax | 真・女神転生III NOCTURNE マニアクス | SLPM-65462 | Scene and message windows |
| Digital Devil Saga | DIGITAL DEVIL SAGA アバタール・チューナー | SLPM-65597 | Scene and message windows |
| Final Fantasy X International | ファイナルファンタジーX インターナショナル | SLPS-25088 | Message windows with speakers, subtitles, choices |
| Metal Gear Solid 2: Substance | METAL GEAR SOLID 2 SUBSTANCE | SLPM-67002 | Codec conversations, cutscene and radio subtitles |
| Metal Gear Solid 3: Snake Eater | METAL GEAR SOLID 3 SNAKE EATER | SLPM-65790 | Codec conversations, cutscene and radio subtitles |

</details>

<details><summary><b>PSP</b> (11 games)</summary>

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
| FLOWERS | FLOWERS | ULJM06390 | Dialogue, speakers, choices |

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

<details><summary><b>Nintendo 3DS</b> (3 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Shin Megami Tensei IV | 真・女神転生IV | 0004000000088A00 | Dialogue, narration and choices |
| Professor Layton vs. Phoenix Wright: Ace Attorney | レイトン教授VS逆転裁判 | 0004000000078B00 | Dialogue with speakers, narration, subtitles, puzzle texts |
| Phoenix Wright: Ace Attorney Trilogy | 逆転裁判123 成歩堂セレクション | 0004000000108800 | Dialogue |

</details>

<details><summary><b>Nintendo DS</b> (2 games, Android)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Wish Room | ウィッシュルーム 天使の記憶 | AWIJ | Message windows with speakers, choices |
| 999: Nine Hours, Nine Persons, Nine Doors | 極限脱出 9時間9人9の扉 | BSKJ | Novel text and talk windows with speakers, escape rooms, choices, file pages |

</details>

<details><summary><b>PS Vita</b> (4 games)</summary>

| Game | Japanese title | ID | What is read |
| --- | --- | --- | --- |
| Trails in the Sky FC Evolution | 英雄伝説 空の軌跡 FC Evolution | PCSG00488 | Talk windows with speakers and furigana |
| WHITE ALBUM 2: Shiawase no Mukougawa | WHITE ALBUM 2 幸せの向こう側 | PCSG00249 | Dialogue (ADV and NVL), speakers, furigana, choices |
| Oretachi ni Tsubasa wa Nai | 俺たちに翼はない | PCSG00299 | Dialogue and speakers |
| Zero Escape: Virtue's Last Reward | 極限脱出ADV 善人シボウデス | PCSG00020 | Dialogue and speakers |

</details>

For the latest up-to-date list of scripts (including newly merged community scripts), see [sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts).
