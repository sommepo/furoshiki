# Games of our cores read by hook (LunaHook's and Agent's knowledge)

The maintainer, 2026-11-13: "add a title for core games without direct support via hook (luna and
agent do also hook emulated games too so they should be supported by hook)". A game played in one
of our cores that has no script of ours can be read with what LunaHook (LunaTranslator's hook
engine) or 0xDC00's Agent scripts know of it on an emulator: an address of the game's own code,
the register its text's pointer is in at that address, the encoding, and the text's filters.
Our engine hooks the same emulated address itself: no Frida, no other program, on the desktop
and, as the engine is shared, on Android. This file has what was read (2026-11-13), what was
decided, what is built and what was proven.

## What was found

### LunaHook (in LunaTranslator)

- **Where**: `github.com/HIllya51/LunaTranslator`, `src/NativeImpl/LunaHook/LunaHook/emulators/`,
  one table a emulator, each in its own C++ file: `ppsspp_1.cpp` (PSP), `PCSX2_1.cpp` (PS2),
  `vita3k_1.cpp` (PS Vita), `yuzu_1.cpp` (Switch), `rpcs3_1.cpp` (PS3); read at commit
  `85c2234f` (2026-11-13). No 3DS, PS1 or Dreamcast table.
- **Form**: a C++ array of `{address, {type, offset, padding, text_fun, filter_fun, ids[, version]}}`:
  ```
  {0x8870320, {0, 3, 0, 0, ULJM06040_1, "ULJM05887"}},
  {0x14AC38, {0, PCSX2_REG_OFFSET(s4), 0, 0, FSLPM66302, "SLPM-66302"}},
  {0xAFCF80, {DIRECT_READ, 0, 0, 0, 0, "SLPM-65550"}},
  {0x8001B580, {FULL_STRING | CODEC_UTF8, 0, 0, 0, F0100BDD01AAE4000, 0x01005E5023A0A000ull, "1.0.0"}},
  ```
  `address` is the game's (emulated) address; LunaHook hooks the emulator's JIT where it
  compiles a block starting there. `type`: flags (`CODEC_UTF8`, `CODEC_UTF16`, Shift-JIS
  otherwise; `FULL_STRING` a whole line a call; `USING_CHAR` a character a call; `DATA_INDIRECT`;
  `DIRECT_READ` (PS2) memory read on a timer, not a hook). `offset`: which argument holds the
  text's pointer: PPSSPP's `emu_arg[i]` is MIPS `a0 + i` (so `r(4+i)`; 0xd is `s1`), PCSX2's is
  the register's number (`PCSX2_REG_OFFSET(a1)`), Vita3K's ARM `r(i)`, yuzu's `x(i)` (`w(i)` for a
  32-bit game). `padding` is added to the pointer. `text_fun` (a C++ function that reads the text
  itself) and `filter_fun` (a C++ function that changes it: `StringFilter`, `StringReplacer`,
  `re::sub` with ECMAScript patterns, caches of the last lines, anything else) are per game.
  The ids are the disc's or title's ID as our cores give them (`ULJM05887`, `SLPM-66302`,
  `PCSG00488`; the Switch's as a 64-bit number), with the version for the Switch.
- **How many** (rows, games by ID): PSP 415 rows, 354 IDs; PS2 400, 414; Switch 1,221, 460;
  Vita 308, 183; PS3 10, 10.
- **Licence**: GPL-3.0 (its `LICENSE`).

### Agent (0xDC00's scripts)

- **Where**: `github.com/0xDC00/scripts` (read at `2bb955a9`, 2026-11-11), one JavaScript file a
  game, flat: `NS_*.js` 272, `PSVita_*.js` 51, `PSP_*.js` 22, `3DS_*.js` 15, `PS2_*.js` 9,
  `PS3_*.js` 2 (and PC 182). There is no index: the game's ID is in the header's `@name`
  (`[ULJM05701] Hanakisou`; the 3DS's scripts have none).
- **Form**: `setHook({ 0x88719dc: mainHandler.bind_(null, 1, "text"), … })` (Switch: by version,
  `[0x80462DD4 - 0x80004000]`), `mainHandler = trans.send(handler, '200+')`, and `handler(regs,
  index, …)` in JavaScript: `regs[index].value.readShiftJisString()` and then whatever the
  author wrote (`.replace(/…/g, '')`, conditions, state, several reads). `regs[i]` is the
  emulator library's: PPSSPP's `a0..a3` (`r(4+i)`), Vita3K's and Citra's `r(i)`, yuzu's `x(i)`;
  PCSX2's scripts use `setHookEE` and `this.context.<reg>` instead.
- **Licence**: MIT ("Copyright (c) 2021 0xDC00").

### What that allows here (GPL-2.0-or-later)

Neither project's data is put in this repository. LunaHook's tables are GPL-3.0: a description
made from them is a work based on them, which this GPL-2.0-or-later repository could only carry
by becoming GPL-3.0 as a whole, and the maintainer did not ask for that. So, as `docs/hooks.md`
decided for the engines, they are **fetched when the user asks**, into the user's own data folder
(`<shared folder>/program/hooks/luna-tables`, four source files and LunaTranslator's licence;
`scripts/` for Agent's, the kit that exists already), and converted there on the user's machine;
the descriptions written for a game carry the source's name and licence in their first lines.
Agent's scripts are MIT and could be committed with their notice; they are fetched all the same,
for one way for both.

## The design

**A hook description** is a profile like any other (`<ID>.lua` in the user's profiles folder),
whose first lines say `-- reader: by hook`, and which calls one function the engine has for it,
`reader.hooks{…}` (`shared/reader/core/hook_description.inc`, part of the profiles' prelude):

```lua
-- reader: title STEINS;GATE
-- reader: by hook
reader.hooks{
    source = "LunaHook ppsspp_1.cpp", only = { "0x8870320" },
    { name = "0x8870320", at = 0x8870320, from = "r7", text = "sjis",
      filters = { { "cut", "%K" }, { "cutb", "\129\153" }, { "re", "\\{(.*?)\\}\\[(.*?)\\]", "$1" } } },
    { name = "window", read = 0xAFCF80, text = "sjis" },  -- memory looked at every frame
}
```

Each hook: `at` (a hooked address; `base = "main"` counts it from the game's executable) or
`read` (memory looked at every frame, still for three frames before it is taken; what does not
decode as text is passed over), `from` (the register the pointer is in), `wide` (a 64-bit
pointer), `follow` (the pointer is read from that offset first), `add`, `text` (`sjis`, `utf8`,
`utf16`), `char` (the register is one character), `whole` (each call is a whole line),
`wait`, `channel`, `filters`. Filters, in order: `cut`, `swap` (plain text), `cutb`, `swapb` (of the
game's bytes, before they are decoded: as LunaHook's `StringFilter` works on its buffer), `re`
(ECMAScript regular expressions, ECMAScript's common part: `text.resub`, written in Lua in the
same file, as Lua's patterns are not ECMAScript's), `upto`, `trim`, `once [n]` (dropped when among
the last n), `japanese` (dropped when all ASCII), `skip`, `need`, `deny`. What a hook gives is
gathered until it is quiet (`wait` frames, 6 by default) and said as one line (the same piece
said again is one piece); each line carries `meta.hook`. `only` keeps a description's chosen
hooks. `reader.hooks_seen()` says, a hook a line, how often each was called, how many lines it
gave and its last line: what a tool shows to choose the dialogue's hook.

**Without places.** A line read by hook has no place on the screen: the game's own text cannot be
pointed at. The line goes where every line goes: statistics, the backlog (where its words are
looked up), cards, the text server (Furochibi), names. Showing it in the text strip that window
hooks use, over the game, is not done yet.

**The converter** (`home/src/core_hooks.*`, Qt Core): LunaHook's rows and Agent's scripts into
descriptions, by rule, never by guess; a row or script that does anything the rules do not know
is left out and counted by why. LunaHook: the flags, the register by the emulator's numbering,
`padding` as `add`; its filter function translated statement by statement (`StringFilter`,
`StringReplacer`, `StringCharReplacer`, `CharFilter`, `CharReplacer`, `strReplace`, `re::sub`,
`strSplit(…)[0]`, an `lru_cache` or last-line check, `all_ascii`, a call of another such
function; the declarations and `buffer->from` around them). The Switch's addresses are counted
from 0x80004000 (where yuzu puts a 64-bit game's main) and hooked from the game's `main` module;
a 32-bit game's are left out. **Our PS2 cores run no hooks** (PCSX2's and ARMSX2's profiles read
memory each frame), so the PS2's code hooks are left out and its `DIRECT_READ` rows converted.
Agent: the `setHook` table, `bind_` arguments for the register and the hook's name, the
handler's body if it only reads `regs[i].value(.add(n)).read…String()` and replaces
(`.replace`, `.replaceAll`, `.trim`) before it returns.

### What converts (2026-11-13's upstream)

| | looked at | converted | games with a description |
|---|---|---|---|
| LunaHook (rows) | 2,344 | 1,034 | PSP 239, PS Vita 124, Switch 197, PS2 107 |
| Agent (scripts) | 369 | 41 | PSP 4, PS Vita 6, Switch 31 |

LunaHook's left out, by why: text read by a C++ function of its own 503; a filter that does more
than cut, swap and replace 358; a code hook on the PS2 128; a filter with blocks of its own (`if`
with several statements, loops) 84; UTF-32 52; a regular expression of Shift-JIS bytes 41; a
32-bit Switch game 41; a filter made from a template 39; a pointer to a pointer 34; a character a
call 22; big-endian 6; a filter not in the tables 2. Agent's: a handler that does more than read
and replace 271, PS2's (`setHookEE`, its own register reads) 9, a handler not found 14, an entry
not read 18, others 16; five scripts converted in part. `core-hooks count` (built with the tests:
`build-desktop/core-hooks count LUNA_EMULATORS AGENT_SCRIPTS`) says it again for any checkout;
`core-hooks why LUNA AGENT ID` says why one game's rows did not convert;
`core-hooks profile LUNA AGENT CONSOLE ID` prints a game's description.

## Adding such a title

1. *Add a title* (the shelf's last spine, or Settings), door **A, A DISC OR FILE**, the console;
   **H** (*a game no script reads, by hook*).
2. The page lists that console's games found in its folders that no script reads, each with what
   is known (`AGENT · 2 HOOKS`, `LUNAHOOK · 1 HOOK`, `NO HOOK KNOWN`). The first time, the last
   row, *Fetch LunaHook's and Agent's hooks* (0.5 MB of LunaHook's tables, 1.5 MB of Agent's
   scripts, into `program/hooks`), fetches them and converts them into `program/hooks/core-hooks.json`.
3. Enter on the game: its description (Agent's first, the maintainer's "agent scripts take
   priority") is written as `program/reader/profiles/<ID>.lua`; the games are looked for again and
   it stands on the shelf like a scripted game. A description never replaces a script of ours or
   the user's (`Install` refuses one that is not by hook).

**Choosing among a description's hooks** is in the code (`CoreHooks::Choose` writes `only`;
`reader.hooks_seen()` says what each gave) but has no page yet: today a game's every converted
hook is read, each line saying its hook. The development channel: `lua reader.hooks_seen()`.

**Android**: the engine is the same, and `ReaderNative.profileSource` reads the user's profiles
folder first, so a description written on the desktop (or by `core-hooks profile`) and put in
`Android/data/<the app>/files/reader/profiles/<ID>.lua` is read there, as a script is
(`android/AGENTS.md`). No page on Android fetches or converts yet: that needs the converter in
Kotlin (or the desktop's output carried over) and a row in the game's menu.

## What was proven, and how

No disc in the forks' `private/` is of a game that LunaHook or Agent knows and we have no script
for (our PSP discs all have scripts; of the PS2's, FF12, Biohazard 4 and Wallace & Gromit are in
neither table; the Vita's Sora FC is Agent's, but its handlers do more than read and replace).
So two games **with** a script of ours were read **by the converted upstream description alone**,
which the user's profiles folder puts in front of the bundled script (`tools/desktop.sh` with a
copy of `home/private/readme-demo`, the main checkout's cores, the worktree's program):

- **STEINS;GATE** (PSP, ULJM05887, PPSSPP's core): LunaHook's `{0x8870320, {0, 3, 0, 0,
  ULJM06040_1, "ULJM05887"}}` converted to a hook at 0x8870320 on `r7` with its filter's sixteen
  steps (the `%K` commands cut, ☆ cut and two characters swapped as Shift-JIS bytes, furigana
  `{漢字}[かんじ]` made `漢字` by its regular expression). From a new game, 14 lines in a row,
  each as the window showed it, none twice: 目の前には少女。 / 首を傾げながら俺に呼びかけてくる。 /
  どう見ても中学生にしか見えないあどけなさの残る顔つきからは、今まさに敵地に潜入しようとしているにもかかわらず、緊張感が微塵も感じられない。 /
  【まゆり？】「誰かと電話中？」 / 聞かれてはまずい会話をしているのだから、向こうも気を利かせて黙ったのだろう。;
  the statistics counted them (341 characters); 会話 looked up (`lookup`).
- **Remember11** (PS2, SLPM-65550, PCSX2's core): LunaHook's `{0xAFCF80, {DIRECT_READ, …}}`,
  memory read every frame (no hook: the description says `-- reader: no hooks`). Before the game's
  text was there the place held bytes that are no text, 29 "lines" of them in the first run: what
  does not decode is now passed over. From the title's NEW GAME, the opening read line by line:
  旧ソ連の宇宙船ヴォストーク６号に乗り込んだ彼女は、ひとり、完全な静寂と虚無に支配された宇宙空間を漂いながら…… /
  その心もとない孤独感と…… / 同時にすべての束縛から逃れたことによる解放感を、１羽の『カモメ』になぞらえて地球の管制官に送ったのだった。 /
  『私はカモメ』……素敵な表現だと思う。 / 私には、そんな気の利いた言いまわしは思い浮かばない。 /
  少々の皮肉を込めて言い表わすとするなら、さしづめ『私は籠女』とでもいったところだろうか。;
  230 characters counted. The game keeps the whole line there before it is typed out, so each
  line is said as it begins, as LunaHook says it.
- **The wizard's step** on the stand-in screen (a folder with FF12, which no script and no table
  reads): H on the PS2's row listed it as `NO HOOK KNOWN`; the last row fetched LunaHook's four
  tables and Agent's scripts from GitHub and converted them: "671 games known: LunaHook 1034 of
  2344 hooks, Agent 41 of 369 scripts", 744 descriptions. Enter on a game with a hook (its
  description written, the games looked for again) was tested by `reader-home-tests` only, as no
  disc here is such a game.

Tests: `reader-home-tests` (made-up LunaHook rows of each kind and Agent scripts: what converts,
what does not and why, the profile written, `Install` beside a script of ours, `Choose`), the
shared desktop tests' `[hooks]` (the regular expressions; a made-up game read at a hook and by
memory, every filter) in every fork's list; `tools/shared.py check` clean.

## Not done or not seen

Windows; Android (by hand only, as said); a game with no script of ours (no such disc); the Switch's and the Vita's descriptions (no
running game); the Switch's offset from 0x80004000 is LunaHook's reading of yuzu, not checked
against Eden's `main`; choosing a hook on a page; the strip for lines without places; a
description for the 3DS (neither project has an ID'd one).
