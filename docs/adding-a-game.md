# Adding a game

This is the one checklist for adding support for a game, from the game named to the report that
says it is done. It is written for a coding agent that has never seen this repository. Each step
links to the fork's own guide for depth. Where something here could not be confirmed when this
file was written, it says "unconfirmed".

The public repository (`sommepo/furoshiki`) has everything a game's script needs: the forks, their
harnesses and tests, the title records and the tools of steps 2 and 9. It has no Switch fork, no
playthrough guide library and no publishing scripts, so the steps about those, and about the
maintainer's own machine and devices, are only in the maintainer's copy of this file. That is why
the step numbers below have gaps there.

**A game is now usually added in the scripts repository**,
[sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts): its `AGENTS.md` writes
the script against an installed Furoshiki, on any system, with nothing built. This checklist is
for what that way cannot do: a game that needs a character table, a change to an emulator, or a
game the maintainer adds to their own line of work.

The steps, in order:

1. Set up a branch (and, in the maintainer's checkout, a worktree).
2. Find the game's file and its ID.
3. Choose the fork and how the text is reached.
4. Run the game in the fork's harness, under a memory cap.
5. Write the profile.
6. Character tables, if the game needs one.
7. Tests, notes and the fork's lists of games.
8. See it read: what "done" requires.
9. The title record and its names.
11. Android and the other places the script reaches.
13. Commit.
14. Report.

## Rules that apply to every step

- Run every test binary, harness, emulator and heavy script under a hard memory limit with a
  wall-clock timeout. Each fork has
  `tools/reader/limit.sh` (a transient systemd scope, 4 GB by default, `-m 8G` for more) and its
  harness scripts already use it. For anything else:
  `systemd-run --user --scope --quiet -p MemoryMax=4G -p MemorySwapMax=0 -- timeout 600 <command>`.
  Disc and archive parsers get `MemoryMax=2G`. If a capped run is killed, fix the code (an
  unbounded list, a bad header parse); never run it again without the cap. Look at `free -g`
  before starting a build.
- Builds are bounded. Use the fork's build scripts, which cap themselves; keep parallel jobs
  at the scripts' defaults (`READER_JOBS`, 8).
- No game data in Git, ever. Game files, anything extracted or derived from them (scripts,
  text, fonts, character tables, dumps, screenshots, harness output), BIOS files, keys and
  firmware live under the fork's `private/` (ignored by Git) or in `input/`. A profile holds
  addresses and decoding logic only. Tests use made-up text.
- Wait on a background job by its exit, not with `pgrep -f <name>` in a loop (it matches its
  own shell and never ends). Run long jobs in the background and let them report when they exit,
  or wait on a PID or a marker file, with a time limit.

## 1. A branch and a worktree

In a clone of the public repository, fetch the submodules of the fork you need and work on a
branch of your own:

```sh
git clone https://github.com/sommepo/furoshiki && cd furoshiki
git submodule update --init --recursive -- <fork>
git switch -c <game-slug>
```

Each fork's `tools/reader/env.sh` names the toolchains its build scripts expect, and a file
`<fork>/private/env.sh` (ignored by Git) can point them at your own. The first build of a fork is a
full build of the emulator.


## 2. The game's file and its ID

**Where games are.** Use your own dumps, kept in the fork's `private/<game>/` (ignored by Git).
Nothing here downloads games, BIOS, keys or firmware, and nothing is to be added that does.


**The console comes from the file, not from the request.** A request can name the wrong
console (a game asked for "on the PS2" that is the PSP port). Read the disc or file itself
before choosing a fork, and when it differs from the request, say so and ask before going on.
For a disc image, `tools/discid.py` reads the ID from the image's first 256 MB without
unpacking it (a `.zip` is read from inside, its largest file). Run it under a cap:

```sh
systemd-run --user --scope --quiet -p MemoryMax=1G -p MemorySwapMax=0 -- \
    timeout 120 python3 -I tools/discid.py "<the game's file>"
```

It was tried on the files in `input/` (2026-11-21): the two discs of Dies Irae Amantes Amentes
gave `PSP UMD_DATA.BIN ULJM-06107` and `ULJM-06108` (a PSP game, though asked for as PS2),
Persona 4 `PS2 SYSTEM.CNF BOOT2 SLPM_669.78`, Final Fantasy VII disc 1
`PS1 SYSTEM.CNF BOOT SLPS_007.00`. A `.chd` is compressed and a GameCube disc is no console of
ours: both say "no ID found". What each console's file says:

- PSP: an ISO with `PSP_GAME/` and `UMD_DATA.BIN` at its root; the first field of
  `UMD_DATA.BIN`, before `|`, is the disc ID (`ULJM-06107`).
- PS2: `SYSTEM.CNF` with a `BOOT2 = cdrom0:\SLPM_669.78;1` line; the serial is `SLPM-66978`.
- PlayStation: `SYSTEM.CNF` with `BOOT = cdrom:\SLPS_007.00;1`; the serial is `SLPS-00700` (the
  apps may use DuckStation's database serial instead: see the table).
- Dreamcast: `IP.BIN` at the start of the data track begins `SEGA SEGAKATANA`; the product
  number is its ten bytes at 0x40 (`HDR-0164`, `T-14301M`). Dreamcast games here are mostly
  `.chd`, which this step cannot read: start the game in Flycast Reader and read `dc.py status`.
- Switch: an `.nsp` or `.xci`; the title ID is usually in the file's name in brackets, and the
  harness's `modules` and `status` confirm it.
- 3DS: a decrypted `.3ds` or `.cci`; `python3 tools/reader/ctr.py info ROM` in `azahar/`.
- Nintendo DS: an `.nds`; the four-letter game code is the four bytes at 0x0C of the file.
- PS Vita: a dumped folder named by its title ID (`PCSG00488`), with `sce_sys/param.sfo`.
- A `.chd` of any console: start it in the fork's app or harness and read its `status`
  (`chdman` was not installed where this was written).

**The release.** Scripts are written for the Japanese release. A Western release is a different
program with a different ID and is not a substitute.

**The ID** names the profile file (`<ID>.lua`), its data files (`<ID>.<name>`) and the title
record. The file name's form is the console's, as the existing profiles have it:

| Console | Disc says | Profile file | `-- reader: version` line |
| --- | --- | --- | --- |
| PSP | `ULJM-06107` | `ULJM06107.lua`, no hyphen (as every file in `ppsspp/assets/reader/profiles/`) | `ULJM-06107`, the disc's own form, with a disc version where there is one (`ULJS-00219, disc version 1.01`) |
| PS2 | `SLPM_669.78` | `SLPM-66978.lua` | `SLPM-66978, disc version 1.01` |
| PlayStation | `SLPS_007.00` | `SLPS-00700.lua` (and the boot file's form too where the database's serial differs) | as PS2 |
| Dreamcast | `HDR-0164`, `T-14301M` | `HDR-0164.lua`, `T14301M.lua`: as `dc.py status` gives `title_id` (check the existing files) | the product number and disc |
| Switch, 3DS | 16 hex digits | `<TITLEID>.lua`, capitals | the game's version |
| DS, Vita | `BSKJ`, `PCSG00488` | `BSKJ.lua`, `PCSG00488.lua` | as the file |

The title record's `ids` use the profile file's form (`ULJM06232`).

**A game on several discs** has one profile per disc ID, and every disc's ID goes in the one title
record. Grisaia no Kajitsu on the PSP is the example: `ULJM06232.lua` and `ULJM06233.lua` are one
body twice (a test in `ppsspp/reader/desktop/tests/console.cpp` holds them to it), each header
says which disc it is and names the other, one notes page (`ppsspp/docs/reader/games/grisaia.md`)
covers both, and `titles/grisaia-no-kajitsu.json` lists both IDs. Furoshiki shows the discs as
one game when their files are named alike but for `(Disc N)` (Redump's naming) in one folder
(`home/src/discs.h`, and `Discs.kt` on Android); keep that naming when unpacking. Check every
disc's ID: Dies Irae's two discs are two IDs. A PlayStation game also keeps one memory card for
all its discs, under the first disc's ID (`station/AGENTS.md`, "Save sync").

| Console | ID | How to read it | Build check in the profile |
| --- | --- | --- | --- |
| Switch (`eden/`) | title ID, 16 hex digits (`01001CC017BB2000`) | the harness's `modules` and `status`; often in the file's name | `reader.build_id`, the `main` executable's build ID (from `modules`) |
| 3DS (`azahar/`) | title ID, 16 hex digits (`0004000000078B00`) | `python3 tools/reader/ctr.py info ROM` (needs a decrypted dump) | a few instruction words read at load, or the code's SHA-256 (`docs/reader/profiles.md`) |
| PSP (`ppsspp/`) | disc ID (`ULJM05718`) | `UMD_DATA.BIN` (above); the harness's `status` (`game=`) | `reader.build_id`, a checksum of the executable on the disc |
| PS2 (`pcsx2/`, `armsx2/`) | disc serial (`SLPM-66302`) | `SYSTEM.CNF` (above); `dc.py status` (`title_id`) | `reader.build_id`, the program's CRC as the title bar shows it |
| PlayStation (`station/`) | disc serial (`SLPS-01794`) | `SYSTEM.CNF` gives the boot file's form; the apps take DuckStation's game database serial, Furoshiki's core the boot file's: they can differ (FF9: `SCPS-45500` and `SLPS_020.00`), and then the game needs a profile under both | `reader.build_id`, a CRC-32 of the boot program |
| Dreamcast (`flycast/`) | product number (`HDR-0164`, `T14301M`) | `dc.py status` (`title_id`) | `reader.build_id`, a checksum of the disc header (one per disc) |
| Nintendo DS (`melonds/`) | the cartridge's game code, four capitals (`AWIJ`) | the harness's `status` | unconfirmed: the existing scripts are bound by game code |
| PS Vita (`vita3k/`) | title ID (`PCSG00488`) | the folder's name in a dump | `reader.build_id`, the module's NID, eight hex digits |

The ID rule that `tools/scripts-index.py` accepts: capitals, digits and hyphens, four to sixteen,
with a digit among them, or four capitals.

Before writing anything, check that the game is not already there:
`python3 tools/titles.py list | grep -i <ID-or-name>`, and `ls <profiles folder>/<ID>.lua`.

## 3. The fork, and how the text is reached

The console decides the fork (`forks.json` lists them). The profile folders:

| Console | Fork | Profiles folder | Fork's guide |
| --- | --- | --- | --- |
| Switch | `eden/` | `eden/src/android/app/src/main/assets/reader/profiles/` | `eden/AGENTS.md`, "Adding a game"; `eden/docs/reader/profiles.md` (the whole API), `harness.md` (a recipe per engine) |
| 3DS | `azahar/` | `azahar/src/android/app/src/main/assets/reader/profiles/` | `azahar/.agents/skills/reader-profile/SKILL.md`, `azahar/docs/reader/profiles.md`, `harness.md` |
| PSP | `ppsspp/` | `ppsspp/assets/reader/profiles/` | `ppsspp/AGENTS.md`, "Adding a game" |
| PS2 | `pcsx2/`, and the same file in `armsx2/` | `pcsx2/reader/profiles/` and `armsx2/ARMSX2/app/src/main/assets/reader/profiles/` (byte for byte the same) | `pcsx2/READER.md`, "Game scripts" and "Games" |
| PlayStation | `station/` | `station/reader/profiles/` | `station/AGENTS.md`, "Adding a game" |
| Dreamcast | `flycast/` | `flycast/reader/profiles/` | `flycast/AGENTS.md`, "Adding a game" |
| Nintendo DS | `melonds/` | `melonds/reader/profiles/` | `melonds/AGENTS.md`, `melonds/docs/reader/harness.md`, `games.md` |
| PS Vita | `vita3k/` | `vita3k/reader/profiles/` | `vita3k/AGENTS.md`, "How the Vita is to a profile" |

The `AGENTS.md`, `READER.md` and `MAINTAINER.md` files named here and below are in the
maintainer's checkout only, and so is `eden/`. In a clone of the public repository, read the
fork's `docs/reader/` pages instead; `azahar/docs/reader/profiles.md` has the whole profile API.

A console with no fork is a new emulator: `docs/adding-an-emulator.md` and `tools/new-fork.py`.
That is a project of its own; ask before starting one.

How the profile gets the text, in order of preference:

1. **Reading memory once a frame** (`reader.on_frame`), with `-- reader: no hooks` in the header.
   The game runs as it would without the reader (natively, or on the fast path), which matters
   on a handheld. Engines that keep their text and windows in objects allow it. The PS2 forks
   allow nothing else: **our PS2 cores run no hooks**.
2. **A hook** (`reader.hook(address, fn)`) where the text exists only while a function runs. On
   the Switch a hooked game runs on the recompiler, which is slower; on the PSP hooks are cheap.
   On the Vita hook addresses are odd (Thumb code).
3. **A hook description** made from LunaHook's or Agent's knowledge of the game
   (`docs/core-hooks.md`): no places on screen, so a tap cannot find a word in the picture. This
   is what Furoshiki offers a user for a game without a script; it is not a substitute for a
   script of ours.
4. **OCR** of the picture (Furoshiki's OCR on a computer and on Android, `home/docs/furoshiki-3.md`
   section 8 and `android/AGENTS.md`, "OCR"; Furochibi's service for a game on the TV). It is the
   fallback when the console has no fork, or when the maintainer asks for a game to be read
   that way. A game read by Furochibi's OCR still gets a title record (step 9).

**Find out what the game is made with, and read what was learnt of that engine before.**
The notes of earlier games say what was tried, what the text looked like in memory and what went
wrong. Where they are:

- Switch: `eden/docs/reader/games/`. Only Eden's `AGENTS.md` has a table from engine signs (the
  files in the game: `global-metadata.dat` for Unity, `*.pak` for Unreal...) to a recipe in
  `eden/docs/reader/harness.md`; `tools/reader/harness.sh --rom FILE --list-romfs` lists the
  files.
- PSP: `ppsspp/docs/reader/games/`. There is no table of engines. Look at the game's files
  (`PSP_GAME/USRDIR/`, the extensions of its archives) and at the harness's `modules` (the names
  of the modules the game loads), and read the notes of games by the same developer or
  publisher first: CHAOS;HEAD and STEINS;GATE are one engine (5pb./MAGES), Da Capo and Grisaia
  are visual novel ports with a message window in memory, 428 hooks the routine that draws a
  character.
- PS2: the notes are paragraphs in `pcsx2/READER.md`, "Games", not separate files: one engine
  often covers several games (Ever17 and Remember11 are KID's, Persona 3 and 4 Atlus's).
- PlayStation `station/docs/reader/games/`, Dreamcast `flycast/docs/reader/games/`, 3DS
  `azahar/docs/reader/games/`, DS `melonds/docs/reader/games/` and `games.md` (what is known of
  other DS games' text), Vita `vita3k/AGENTS.md`, "The games".

**What LunaHook and Agent know of the game** can be a quick start for finding its text routine
(the address of the code that handles a line, and the register its text is in; PSP, PS2, Vita
and Switch only). Their data is fetched into a private folder and never copied into this
repository (LunaHook's tables are GPL-3.0, this repository GPL-2.0-or-later: `docs/core-hooks.md`).
The converter is a program of Furoshiki's tests, `core-hooks` (`home/tests/core_hooks_tool.cpp`,
built when `HOME_TESTS` is on):

```sh
cd home
tools/build-desktop.sh core-hooks          # home/build-desktop/core-hooks (a full configure the first time)
mkdir -p private/core-hooks/luna private/core-hooks/agent
for f in ppsspp_1 PCSX2_1 vita3k_1 yuzu_1; do
  curl -fsSL -o "private/core-hooks/luna/$f.cpp" \
    "https://raw.githubusercontent.com/HIllya51/LunaTranslator/main/src/NativeImpl/LunaHook/LunaHook/emulators/$f.cpp"
done
curl -fsSL -o private/core-hooks/agent.zip https://codeload.github.com/0xDC00/scripts/zip/refs/heads/main
unzip -q -j -o private/core-hooks/agent.zip '*.js' -d private/core-hooks/agent
tools/limit.sh build-desktop/core-hooks profile private/core-hooks/luna private/core-hooks/agent psp ULJM06107
```

The console is `psp`, `ps2`, `psvita` or `switch`; the ID is as the tables write it (a PSP game's
without the hyphen, a PS2 game's with it). `core-hooks why LUNA AGENT ID` says why a game's rows
did not convert, `core-hooks count LUNA AGENT` how many did. The addresses are the same ones
Furoshiki's *Add a title* fetches (`home/src/hook_fetch.cpp`). A fresh worktree has no
`home/build-desktop`: the first `build-desktop.sh` configures and builds Furoshiki, which needs
the toolchains that `home/tools/env.sh` names (the Flycast fork's). The downloaded files are only
read, never run. Unconfirmed: whether the `unzip -j` layout is exactly what the program reads
from `program/hooks/scripts` (it reads every `*.js` of the folder, named `PSP_*`, `PSVita_*`,
`NS_*`, `PS2_*`).

## 4. The harness: running the game under a cap

Each fork has a way to run a game with no display and drive it by commands, of two kinds:

- **A harness** of its own (Switch, 3DS, PSP, DS): the emulator runs only while a command runs
  frames, and the commands are `run N`, `press`, `hold`/`release`, `shot`, `find HEX`,
  `findtext TEXT` (Switch, PSP, DS; Shift-JIS, UTF-8 and UTF-16 tried), `findptr`, `mem ADDR LEN`,
  `memfile`, `poke`, `lua CODE`, `profile PATH`, `unload`, `events` (what the profile emitted),
  `layouts` (where it placed the text), `status`, savestates (`save`/`load` on the PSP and DS,
  `savestate`/`loadstate` on the 3DS, none on the Switch). Each fork's harness page has the full
  list; the code is `eden/src/reader_harness/harness.cpp`, `azahar/src/reader_harness/harness.cpp`,
  `ppsspp/reader/harness/harness.cpp`, `melonds/reader/harness/harness.cpp`.
- **The app itself without a display** (PS2, PlayStation, Dreamcast, and Furoshiki for the Vita):
  the game runs at its own pace and is driven through the app's development channel, a socket
  at `private/desktop/sock`, with `dc.py` or `ps.py`. Its commands are the shared ones in
  `shared/reader/qt/reader_control.cpp` and the app's own (`HostCommand`). The shared ones:
  `status` (JSON: `title_id`, the profile's state and error, the current line with its speaker
  and window, the choices, every placed text with its characters' cells), `pad BIT [MS]`, `lua
  CODE`, `click X Y`, `press`/`release`/`move X Y`, `key`, `grab PATH` (the game's picture with
  the reader's layer and popup), `grab-window PATH`, `wait MS`, `lookup TEXT`, `next-choice`,
  `progress`, `backlog`, `action NAME` (`action reader_reload_profile` loads the game's script
  again). There is no `run`, `find`, `findtext`, `events` or `layouts` there: memory is searched
  in a dump (the recipe below), and what the profile reported is read from `status`. The apps'
  own: PCSX2 Reader (`pcsx2/reader/qt/host_commands.cpp`) `pad NAME [MS]` (`up right down left
  triangle circle cross square select start` and the shoulders), `dump PATH`, `state save|load
  N`, `pause on|off`, `speed turbo|normal`, `iop PATH`, `vram PATH`, `profile PATH`, `home`,
  `home-key`, `home-click`, `home-grab`; Station Reader (`station/reader/qt/host.cpp`) `dump`,
  `state save|load N`, `speed N`, `home-key`, `home-click`; Flycast Reader
  (`flycast/reader/qt/host.cpp`) `dump`, `state save|load N`, `analog X Y [MS]`, `analog2`,
  `trigger L|R [MS]`, `watch ADDR LEN [MAX]`, `watch-read`, `watch-log` (a debug build),
  `home-key`, `home-click`.

**Memory caps, as the scripts set them** (each a `READER_*_MEMORY_MAX` variable to change): Eden's
harness 12G, Azahar's harness through `limit.sh` 4G, PPSSPP's harness 4G, melonDS's harness 3G,
PCSX2's `screen.sh` 10G and `desktop.sh` 8G, Station's and Flycast's `desktop.sh` 4G, Furoshiki's
`home/tools/desktop.sh` 4G (each stand-in screen 1G more). Before starting one, look at `free -g`
and at what else runs (`systemd-cgls --user` lists the scopes). Paths with spaces must be quoted. Keep screenshots
and dumps in `<fork>/private/<game>/`.

**Switch** (`eden/`; keys in `eden/private/keys/`):

```sh
cd eden
tools/reader/build-host.sh                                   # once: build-host/bin/eden-reader-harness
tools/reader/harness.sh --rom private/<game>/base.nsp &
python3 tools/reader/hc.py "run 600" "shot private/<game>/shots/a.png" modules status
python3 tools/reader/hc.py "press a" "run 120" "findtext こんにちは"
python3 tools/reader/hc.py "profile private/<game>/<ID>.lua" "run 60" events layouts
```

No savestates: use the game's own save and a `--script` of `press` and `run` lines to replay.

**3DS** (`azahar/`):

```sh
cd azahar
tools/reader/build-host.sh
tools/reader/limit.sh build-host/bin/Release/azahar-reader-harness \
    --user-dir private/harness/user --rom "private/<game>/game.3ds" --socket private/harness/sock &
python3 tools/reader/hc.py "run 600" "shot private/harness/shots/a.png" status
```

`savestate PATH` and `loadstate PATH` make every later step quick.

**PSP** (`ppsspp/`):

```sh
cd ppsspp
tools/reader/harness.sh --rom private/<game>/game.iso &
python3 tools/reader/hc.py "run 600" "shot private/<game>/shots/a.png" modules status
python3 tools/reader/hc.py "save private/<game>/s1.state"    # load PATH to come back
```

**PS2** (`pcsx2/`; the BIOS in `pcsx2/private/desktop/config/PCSX2Reader/bios`):

```sh
cd pcsx2
tools/reader/build-desktop.sh
tools/reader/screen.sh -- "$PWD/private/<game>/game.iso" &
python3 tools/reader/dc.py status "pad circle 100" "grab $PWD/private/<game>/shots/a.png"
python3 tools/reader/dc.py "state save 1" "dump $PWD/private/<game>/ram1.bin"
python3 tools/reader/dc.py "profile $PWD/private/<game>/<SERIAL>.lua" status
tools/reader/desktop-stop.sh
```

`screen.sh` starts the game given after `--` from any absolute path (a `.iso`, `.chd` or `.bin`);
quote it. The emulator's own game list scans only `private/desktop/games/` (the
`RecursivePaths` that `screen.sh` writes into a new `PCSX2.ini`), so link a game there to see it
in the list. Paths given to `dc.py` are absolute, as the app's working folder is
`private/desktop`.

**PlayStation** (`station/`; the console's BIOS in `private/desktop/data/duckstation/bios`):

```sh
cd station
tools/reader/build-desktop.sh
tools/reader/desktop.sh --rom private/<game>/game.cue &
python3 tools/reader/ps.py --socket private/desktop/sock status "pad 13 150" \
    "grab $PWD/private/desktop/shots/a.png"
python3 tools/reader/ps.py --socket private/desktop/sock "state save 1" "dump $PWD/private/<game>/ram.bin"
tools/reader/desktop-stop.sh
```

This app's channel has no `profile` command: a profile being written goes into the test home's
own profiles folder (`private/desktop/data/duckstation/reader/profiles/<ID>.lua`), where it wins
over a bundled one, and `action reader_reload_profile` loads it again.

**Dreamcast** (`flycast/`; no BIOS needed for most games):

```sh
cd flycast
tools/reader/build-desktop.sh
tools/reader/desktop.sh --rom private/<game>/game.gdi &
python3 tools/reader/dc.py --socket private/desktop/sock status "pad 2 100" \
    "grab $PWD/private/desktop/shots/a.png"
python3 tools/reader/dc.py --socket private/desktop/sock "state save 1" "dump $PWD/private/<game>/ram.bin"
tools/reader/desktop-stop.sh
```

As in Station, a profile being written goes into the test home's profiles folder
(`private/desktop/data/flycast-reader/reader/profiles/<ID>.lua`) and
`action reader_reload_profile` loads it again.

**Nintendo DS** (`melonds/`; the real BIOS and firmware in `private/harness/system/`, else
FreeBIOS):

```sh
cd melonds
tools/reader/build-core.sh --harness
tools/reader/harness.sh --rom private/games/GAME.nds &
python3 tools/reader/hc.py --socket private/harness/sock "run 600" "shot private/harness/shots/a.png" status
```

**PS Vita** (`vita3k/`): there is no harness of its own. The game is run in Furoshiki on a
stand-in screen (`home/tools/desktop.sh --play FILE &`, with the core from
`vita3k/tools/reader/build-core.sh`; firmware in Furoshiki's `consoles/psvita/system`) and driven
with a fork's `ps.py` or `dc.py` at `home/private/desktop/sock` (`home/AGENTS.md`, "Build and
test"). Unconfirmed beyond the one game read so far.

`tools/reader/layout_check.py SHOT OUT < layouts` (Eden, Azahar, PPSSPP, melonDS) draws the
reported cells over a screenshot; in the apps, `grab` draws the reader's own marks over the
picture.

**Finding the text of a PS2 game** (and of a PlayStation or Dreamcast game, with that app's
names for the same commands), as the PS2 scripts in `pcsx2/READER.md`, "Games" were found:

1. Play to a line with `pad`, `grab` it, and note what it says.
2. `dump $PWD/private/<game>/ram1.bin` while it is up (the EE's main memory from address 0, so an
   offset in the file is the address a profile reads).
3. Search the dump for the line's bytes in the encodings games use, with a capped Python step:

   ```sh
   systemd-run --user --scope --quiet -p MemoryMax=1G -- timeout 120 python3 -I -c '
   import sys
   data = open(sys.argv[1], "rb").read(); line = sys.argv[2]
   for enc in ("shift_jis", "euc_jp", "utf-16-le", "utf-8"):
       needle = line.encode(enc); at = data.find(needle)
       while at >= 0:
           print(enc, hex(at)); at = data.find(needle, at + 1)
   ' "$PWD/private/<game>/ram1.bin" "こんにちは"
   ```

   Take a few characters from the middle of the line (a name or a code can break it up). No hit
   in any encoding means a table of the game's own (step 6), or text that exists only as
   pictures.
4. Go to the next line, `dump ram2.bin`, and compare the two dumps around each hit (and the bytes
   that changed elsewhere) to find what says which line is up, how far it is typed out, whether
   the window is open, and where the speaker is.
5. Confirm each address in the running game: `lua return mem.u32(0x...)`,
   `lua return text.sjis(mem.read(0x..., 64))`, at the line, after it, with the window closed,
   in a menu.
6. Write the profile from those reads, load it with `profile PATH`, and compare `status`
   (`current`, `choices`, `texts`) with `grab` line by line.

ペルソナ4 and アマガミ in `pcsx2/READER.md`, "Games" are worked examples (Persona 4 a font in an
order of its own, so a table; Amagami a script in memory matched against what is drawn); the
Remember11 and Never7 paragraphs show a message window and a cursor found in memory. The PS2
forks read memory only (no hooks), so a routine that draws text is not a way in there.

Look at every screenshot you compare against. Before pressing a button that saves (a save menu,
"go on to the next choice" in a save screen), know which slot it writes; test homes are the fork's
own and never the maintainer's real setup.

## 5. The profile

The file and its place: `<profiles folder>/<ID>.lua` (step 3). While it grows, keep it in
`<fork>/private/<game>/` and load it with the harness's `profile PATH`; move it into the profiles
folder when it works. For a PS2 game, put the same file in both `pcsx2/reader/profiles/` and
`armsx2/ARMSX2/app/src/main/assets/reader/profiles/` and keep them identical.

**The API** is one engine in `shared/reader/core/` for every fork; `azahar/docs/reader/profiles.md`
(and Eden's copy, `eden/docs/reader/profiles.md`) describes all of it and each fork's guide says what differs for its console (memory map,
registers, picture size). In short: `reader.on_frame`, `reader.hook`, `reader.emit{...}`,
`reader.layout(window, {...})`, `reader.picture()`, `reader.frame()`, `reader.game_id`,
`reader.base`, `reader.build_id`, `reader.data(name)`, `reader.builder()`, `reader.changed`,
`reader.cursor`, `reader.progress{...}`, `reader.action`, `reader.on_state_loaded`; `mem.u8` to
`mem.u64`, `mem.read`, `mem.cstr`, `mem.wstr`; `text.sjis`, `text.eucjp`, `text.utf16le`,
`text.hash`, `text.len`. No file, network or process access.

**The header.** A profile begins with comment lines the apps read (*Game scripts*, *Supported
games*) and that `tools/scripts-index.py` puts in the published list:

```lua
-- reader: no hooks
-- reader: title 穢翼のユースティア (Aiyoku no Eustia)
-- reader: reads The message window: narration and dialogue with speakers and furigana, and choices
-- reader: version Base game (version 0)
-- reader: touch press
```

- `title` is the Japanese title as on the title screen, then the English name in brackets.
- `reads` says in one line what is read. `version` the release or disc version it was written for.
- `no hooks` only when the profile never calls `reader.hook`.
- `audio never` for a game whose lines have no voices (its cards get no audio by default).
- `touch press` for a visual novel played by touch (a tap beside the text presses the advance
  button, a tap on an answer picks it through `reader.cursor`); `touch game` for a game made for
  a touch screen. Role-playing and action games get neither. `buttons <names>` adds a column of
  on-screen buttons, and only for what a tap cannot do (found by trying the game).
- `api 2` when it uses `reader.progress`, `api 3` when it uses `reader.action`.
- After the header, a comment with the game, the build it is for, how the game keeps its text,
  and what is not covered.

**Bind it to the build.** The profile stops with a clear `error(...)` on any other build:

```lua
local BUILD = "D04A06CF236A6A3BBD657495C8BF1792"
if reader.build_id:sub(1, #BUILD) ~= BUILD then
    error("this profile is for build " .. BUILD .. ", the game is " .. tostring(reader.build_id))
end
```

**A message window** read once a frame, reported once when it is up and withdrawn when it goes:

```lua
local shown = nil
reader.on_frame(function()
    local open = mem.u8(WINDOW + 0x10) == 1
    local raw = open and mem.cstr(mem.u32(WINDOW + 0x20), 1024) or ""
    if raw == "" then
        if shown then reader.layout("message", nil); shown = nil end
        return
    end
    if reader.changed("message", raw) then
        shown = raw
        reader.emit{ text = text.sjis(raw), id = text.hash(raw), window = "message" }
        reader.layout("message", { space = { reader.picture() }, box = { 40, 330, 560, 120 },
                                   glyphs = cells_of(raw) })
    end
end)
```

A speaker is `speaker = "岡部"` in the same `emit`, never inside `text`. Find it even when the
game draws the name as a picture (a table from the picture to the name, kept in the profile only
when it is not game data, else in a character table).

**Furigana** with the builder, which keeps the ruby offsets right:

```lua
local b = reader.builder()
b:add("今日は")
b:add_ruby("鳳凰院", "ほうおういん")
b:add("凶真だ")
reader.emit{ text = b:text(), ruby = b:ruby(), speaker = name, window = "message" }
```

**Choices** go on the channel `choice`, the answers one a line, once while the choice is up. This
is what makes *On to the next choice* work: Furoshiki (and PCSX2 Reader and ARMSX2 Reader) run the
game fast with the console's yes button pressed until a script reports a choice, so a choice that
is not reported is run through and answered with whatever yes picks. Report the cursor too, so a
tap can pick an answer:

```lua
reader.emit{ text = table.concat(answers, "\n"), channel = "choice", window = "choice" }
if reader.cursor then reader.cursor(index) end   -- 0-based, -1 on none; reader.cursor(nil) once it is gone
```

Command menus that are not story choices can go on the channel `menu` (tappable, never counted).

**Routes or progress** the player may set by hand (routes read in another copy of the game) use
`reader.progress`, which is the only place besides `reader.action` where a profile may write the
game's memory (`mem.w8`...), and needs `-- reader: api 2`:

```lua
reader.progress{
    title = "Routes", note = "Saved with the game's system data.",
    items = { { id = "tia", label = "ティア", group = "Endings" } },
    done = function(id) return mem.u8(FLAGS + 3) & 1 == 1 end,
    mark = function(id, on) mem.w8(FLAGS + 3, on and 1 or 0) end,
}
```

`pcsx2/reader/profiles/SLPM-66302.lua` (CLANNAD) is the example.

**What a good profile does** (and what its tests hold it to):

- Each line once, when it is on screen, not when it is loaded into memory. Not again when a menu
  closes over it or the window is hidden and shown.
- Nothing while it cannot be read: not under the backlog or menus, not while the window is
  hidden, not what a skip runs through (a line counts once it has stayed a few frames).
- The speaker, furigana as the game gives them, choices as choices.
- Where each character is (`reader.layout`, a cell per code point in the units of
  `reader.picture()`), so that a tap or click finds its word. Text without places still goes to
  the panel and the backlog; say so in the notes when that is all there is.
- Only what was asked for. A message window is a fine scope; menus, item lists and credits are
  left out on purpose unless the maintainer asks, and the notes say so.
- Text read after the game has filled in names and numbers.

## 6. Character tables

A game that draws text from a font of its own (kanji as pictures, a code table) needs a table
from its codes to characters. That table is made from the game, so it is game data:

- It lives in `<fork>/private/<game>/<ID>.charset` and never in Git.
- The profile asks for it with `reader.data("charset")` (which finds `<ID>.charset` beside the
  profile or in the user's profiles folder) and says clearly, in the text it reports, when it is
  missing.
- `*.charset` is ignored by Git everywhere (the top `.gitignore`). `git status` must still
  never show one; check before every commit.
- `tools/reader/private-data.sh` copies it beside the profiles for a build of one's own. Only
  `eden/`, `pcsx2/`, `armsx2/` and `station/` have that script. In `ppsspp/`, `flycast/`,
  `azahar/`, `vita3k/` and `melonds/` a table reaches a build only by being put beside the
  profile by hand (or into the user's profiles folder on a device); a `private-data.sh` for one of
  them is a change to ask the maintainer about. A PS2 table goes into both `pcsx2/private/<game>/`
  and `armsx2/private/<game>/`, as each fork's script copies its own.
- Never fill a gap in a table with a guess or another edition's text. Readings that could not be
  confirmed stay out and are named.
- A game whose script needs a table cannot be in the public list (`tools/scripts-index.py` leaves
  it out).

## 7. Tests, notes and the fork's lists of games

**Tests** with made-up memory and made-up text, one per rule of the profile; break each rule
once on purpose and see its test fail:

- Switch: `eden/src/reader_tests/<game>_profile.cpp`, added to `eden/src/reader_tests/CMakeLists.txt`
  (`dr3_profile.cpp` is a short one to copy). Then `tools/reader/test-host.sh`,
  `test-android.sh`, `test-desktop.sh`.
- PS2: tests with made-up memory have gone in `pcsx2/reader/desktop/tests/` (FFX's in
  `touch_play_test.cpp`); `tools/reader/test-desktop.sh`.
- Other forks: their `tools/reader/test-desktop.sh` and `test-android.sh` (or `test-host.sh`)
  must still pass. A profile-specific test there has been the exception (unconfirmed whether one
  is expected).

**The lists of bundled games** in each fork's console tests. Find them by searching for an ID the
fork already has, and add the new one beside it:

```sh
git grep -n "<an existing ID of that console>" -- '<fork>/**/tests/*' '<fork>/**/test/**' 'shared/reader/desktop/tests/*'
```

They have been `eden/src/reader/desktop/tests/console.cpp` and `ConsoleTest.kt` (with the list of
games without voices for `audio never`), `ppsspp/reader/desktop/tests/console.cpp` and
`ppsspp/android/test/.../ConsoleTest.kt`, `station/reader/desktop/tests/console.cpp`.

Notes go in `<fork>/docs/reader/games/<game>.md` (for the PS2, a paragraph in `pcsx2/READER.md`, "Games", as every PS2 game has; melonDS: `melonds/docs/reader/games/`), in the
shape of the others: what is read; what is not, and why; how the game keeps its text (offsets and
structures, so that another build is a matter of finding them again); the game's buttons; how it
was checked, by level; what to know about it in the harness. Add to the fork's harness page
whatever the next game of the same engine should not have to find out again.

**The fork's own lists.** Add the game wherever the fork's docs list its games
(`docs/reader/README.md` and the like).

## 8. Seeing it read: what "done" requires

There are three levels of evidence and they are not the same: a test with made-up text, the
harness (or the app without a display) on the real game, a real device. "Done" for a script is
the second level, from a new game through the opening, with all of this seen and compared
against screenshots:

- Lines of the message window reported once each, at the moment the picture shows them, and
  nothing during films, menus, the title or a skip.
- The speaker on lines that have one; furigana where the game draws them.
- At least one choice reached and reported on the channel `choice`, with its answers.
- The places of the characters checked over a screenshot (`layout_check.py`, or a click on a word
  in the app without a display and the popup's grab).
- A word looked up. In the apps without a display a `click X Y` on a word, then `grab`, shows the
  popup with the word marked over its glyphs; Furoshiki's development channel also has
  `lookup TEXT`.
- On the Switch, both handheld and docked placement.
- If the console has *On to the next choice* in that app (`HasNextChoice` in the fork's
  `console.h`; in Furoshiki every core that names its yes button), one run of it from a line to
  the next choice, where it stops with the choice up.

Furoshiki (the one program) plays these games as cores: `home/tools/desktop.sh --play FILE &`
runs it on a stand-in screen with the forks' core builds (each fork's
`tools/reader/build-core.sh`). Seeing the game read there too is better, and is what the
maintainer plays on a computer; say whether it was done.

What is not seen is said, not guessed: later chapters, other routes, a device, Windows.

## 9. The title record and its names

Every game has a title record (`docs/titles.md` is the format). After the profile and its header:

```sh
python3 tools/titles.py search "<name>" --platform <console> --key <key>
python3 tools/titles.py search "<name>" --key <key>          # again without --platform
python3 tools/titles.py add <key> --title "<title>" --ids <ID> [<ID>...] --vndb v<N>
```

- Search with `--platform` and without it. VNDB lists a title's platforms from its releases, and
  a console port is often filed under the original's entry or missing its console: Dies Irae
  Amantes Amentes, a PSP release, is a release of `v548` (Dies irae ~Acta est Fabula~). With no
  hits, `search` says so and suggests the next try; try the Japanese title, the romaji and a
  shorter part of the name too.
- `--platform` takes VNDB's codes; the ones of our consoles, as `spine_platform()` in
  `tools/titles.py` names them: `swi` (Switch), `n3d` (3DS), `nds` (DS), `psp`, `psv` (Vita),
  `ps1`, `ps2`, `drc` (Dreamcast). `search` prints an `add` command for each hit and the title
  already using it.
- A port or an edition with a subtitle of its own (`~Amantes amentes~`, `Plus Situation
  Portable`, `Remastered`) takes the VNDB entry of the work it is a release of, which is what
  VNDB's characters belong to; it gets an entry of its own only where VNDB has one for it (a
  different work: a fan disc, a sequel, a new story). Its key is made from the base title
  (`dies-irae`, as `steins-gate` covers STEINS;GATE ELITE's and the PSP game's IDs), unless the
  edition is a different work. `--title` is the title as the game's own title screen writes it,
  with its subtitle. If another edition of the work already has a record (`search` shows it in
  brackets), add the new IDs to that record (`add <its key> --ids <new IDs>` keeps the rest)
  rather than making a second.
- `--ids` lists every disc and edition of the title that has a profile. One ID belongs to one
  title only (`add` refuses a clash). A collection with several VNDB entries adds `--also`.
- `add` fetches the names into `titles/names/<key>.json` and the notes into
  `titles/notes/<key>.json` (VNDB's API, one request a second). Kanji aliases are read with JMdict
  from `input/JMdict_english.zip` (the EDRDG's `JMdict_e` file, zipped; the main checkout's is
  found from a worktree).
- Write `"english"` into `titles/<key>.json` by hand: the English title, or the romaji where it
  has none (`"Aiyoku no Eustia"`). `jiten` or `tracker_title` (the study tracker's name for it)
  only when you can match it exactly.
- A game VNDB does not have (role-playing and action games) gets its record without `--vndb` and
  its names by hand, from the speakers met in the backlog and Japanese wiki pages, with
  `"source": {"vndb": null, "drafted": "<date>", "from": [...]}`. Notes for those are one or two
  sentences of our own, never copied.
- Read the names file: readings, aliases, `needs_check` entries. Add places and things the game
  names (`"id": "place:<slug>"`). Fill in readings you can confirm; leave out what you cannot and
  name it in `needs_check`. Then set `"checked": true`. The recent games' files were set so in
  the same commit.
- `python3 tools/titles.py check` must not list the new game: it names profiles without a title,
  names files not checked, bad entries and IDs no profile has (older titles still listed as not
  checked are not yours to fix).

Box pictures (`titles.py spines`, `covers`) need the maintainer's ScreenScraper account and are
not in Git; they are not part of adding a game unless asked.


## 11. Android, the desktop program and devices

**How a script reaches the apps.** No list needs editing for a console that already has a core:

- Furoshiki for Android, a dev build, bundles every script and data file in the profiles folders
  of step 3 (`android/app/build.gradle.kts`, `scriptFolders`, `readerFront`), and every names
  dictionary made from `titles/` (`readerNames`). A published build bundles none and installs
  them from the published list (*Game scripts* on its Settings page).
- Furoshiki on a computer, a dev build, builds them in the same way; a published one installs them
  from the list.
- A user's own script wins over a bundled one: on Android
  `Android/data/io.github.sommepo.furoshiki/files/reader/profiles/<ID>.lua` (and `<ID>.charset`),
  on a computer `program/reader/profiles/` in Furoshiki's shared folder (on Windows
  `%LOCALAPPDATA%\Furoshiki\program\reader\profiles`). That is how a script is tried on a device
  without building an app.
- Box art on Android is fetched by the file's name or serial; an ID that the public thumbnails
  name otherwise gets an entry in `android/app/src/main/assets/front/covers.json`.
- A new console on Android is more work (`android/AGENTS.md`, "A new console").



## 13. Commit

- `git status` first. Files you did not change belong to someone else.
- Commit on your branch, by path, never `git add -A` or `git add .` at the top
  (it would take `input/` and other sessions' files):

  ```sh
  git add <new files...>
  git commit --only -m "<message>" -- <every path you changed or added>
  ```

- A commit message says what changed and how it was checked, by level, and what was not seen. End
  it with the attribution line your instructions give.
- A commit whose subject touches `pcsx2/` or `armsx2/` ends its subject with `(AI-assisted)`, as
  `pcsx2/READER.md`, "Ground rules" asks (upstream PCSX2's rule for every message): for example
  `FINAL FANTASY X INTERNATIONAL (PS2, SLPS-25088): a game script for PCSX2 Reader and ARMSX2
  Reader (AI-assisted)`.
- Never commit game data, a `.charset`, a screenshot, a dump, `private/`, `input/`,
  `CLAUDE.local.md` or a build folder. `git diff --cached --stat` before committing.
- In a clone of the public repository, offer the game as a pull request to `sommepo/furoshiki`.
  The public repository is made from the maintainer's line of work, so they bring it in there.
- Never discard, reset or force-push anything that exists.

## 14. Report

The report is short, with what the maintainer has to do last. It says:

- the branch and the commits, and the files of each;
- what was seen at which level: which scenes, which lines, which choices, which word was looked
  up, in the harness or in Furoshiki or on a device, each with "seen" or "not seen";
- what is not read, on purpose or not yet;
- what the game needs that is not in Git (a character table, a BIOS, keys) and where it is;
- the title record's state (`checked`, readings left in `needs_check`);
- what waits on the maintainer: a merge, a publish, the hold list, the public README row, a
  theme in the kit, a device test.
