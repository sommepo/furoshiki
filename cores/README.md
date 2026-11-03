# Cores: each emulator as a library Furoshiki loads

The one program (`home/docs/one-program.md`) loads each emulator as a **core**: a library
(`reader_core_<key>.dll` on Windows, `.so` on Linux) with the small C interface in
`reader_core.h`. Furoshiki holds the window, the settings, the controller and the reader;
a core runs its console's game and answers the reader's questions about it.

| | |
| --- | --- |
| `reader_core.h` | The interface: one exported function, `reader_core_get`, and the tables each side hands the other. Plain C, so a clang-cl core (PCSX2) and MinGW ones meet. |
| `test/test_core.cpp` | A core of a made-up console, for the program's tests (`home/tests/cores_test.cpp`). The smallest example of what a core does. |
| `../station/reader/core_library/` | **Station Reader's core**, `reader_core_station` (2026-10-21): the first core of a real emulator, the example for one with no libretro build (`station/AGENTS.md` says how it is built). |
| `libretro/libretro_core.*` | **The libretro host**: the frontend inside a core made from an emulator's libretro build (linked into the same module). It runs the game on a thread of its own, makes an OpenGL context in the program's window (EGL on X11, WGL on Windows) with a framebuffer for the emulator, draws the picture letterboxed through the reader's CRT shader at the window's size, hands the sound to the program (`play`), reads the program's controller as libretro's joypad, keeps savestates, maps settings onto core options. A fork's glue fills a `ReaderLibretro::Glue`. |
| `../armsx2/ARMSX2/app/src/main/cpp/reader/core_library/` | **ARMSX2 Reader's core**, `reader_core_armsx2` (2026-11-02), for Android alone: `armsx2_core.cpp` is PCSX2 Reader's glue (below) on ARMSX2's emulator, a window on the app's Surface, Oboe for its sound, the memory card code taken from `../pcsx2/reader/core`. The same interface as PCSX2's, with OpenGL ES where that has Vulkan. |
| `../pcsx2/reader/core_library/` | **PCSX2 Reader's core**, `reader_core_pcsx2` (2026-10-22): PCSX2 without its Qt frontend, a glue of its own (`pcsx2_core.cpp`, PCSX2's `Host` as its `gsrunner` has it), built with clang (clang-cl for Windows). A CPU thread for each game, ended at `stop` (`pcsx2/READER.md`, "A core of Furoshiki"). |
| `../eden/src/reader_core_library/` | **Eden Reader's core**, `reader_core_eden` (2026-10-21): a glue of its own like Station's (Eden has no libretro build), made from the fork's headless harness and `yuzu_cmd`: Eden's system without a frontend, its Vulkan renderer drawing into the program's window (an `EmuWindow` of the native window, the CRT picture in it), the program's controller on Eden's virtual gamepad, Eden's sound played by its own SDL. Eden's reader calls go through its own `emulator.h`, as Azahar's do (`eden/AGENTS.md`, "A core of Furoshiki"). |
| `../vita3k/reader/core_library/` | **Vita3K Reader's core**, `reader_core_vita3k` (2026-10-31): PS Vita games, a glue of its own in the place of Vita3K's SDL frontend: its renderer draws with Vulkan into the program's window through a `renderer::FrameHost`, the controller and a finger go in as Vita3K's keyboard and mouse, and an `AudioAdapter` of the core's mixes the Vita's sound ports for the program. Guest threads are host threads, so hooks (dynarmic's, as Azahar's) come from many. Sora no Kiseki FC Evolution plays and is read on Linux; its Windows core, built with clang-cl as PCSX2's, is loaded by Windows on the maintainer's PC, with no game started there yet (`../vita3k/AGENTS.md`). |
| `../flycast/reader/core_library/`, `../ppsspp/reader/core_library/`, `../azahar/src/reader_core_library/` | **Flycast Reader's, PPSSPP Reader's and Azahar Reader's cores** (2026-10-21), made with the libretro host: each fork's glue (`flycast_core.cpp`, `ppsspp_core.cpp`, `azahar_core.cpp`) is its `emulator.cpp`'s twin. Azahar's tells the program where each of the 3DS's two screens is (`screen_rect`) and draws the CRT picture on each screen itself. |

State (2026-10-22): the interface, Furoshiki's loader, the reader's engine and windows in
the program, and six cores (Station's, Flycast's, PPSSPP's, Azahar's, PCSX2's, Eden's) are built
and tested on Linux; PlayStation, Dreamcast, PSP, 3DS, PS2 and Switch games play and are read
in Furoshiki's window (`home/AGENTS.md`, "The cores"). The Windows builds of the program and
of the cores cross-compile (each fork's `tools/reader/build-core-windows.sh`) and go into one
package (`home/tools/package-windows.sh`); PCSX2's, built with clang-cl for Microsoft's runtime,
with its own libraries beside it in `cores/`. All six are installed on the maintainer's PC and
load and start there (2026-10-23); of their games only Steins;Gate (PPSSPP's) has been played
there (`../home/docs/one-program.md`, step 5).

**A core whose emulator sets itself up once per thread** (PCSX2: its memory, recompilers and
GS thread in `CPUThreadInitialize`) does so on a thread of each game's and takes it all down
again at `stop`: the program calls `stop` when it leaves a game and when it exits, never
`shutdown`, so whatever still runs then runs while the program ends. The host table the
program hands a core at `init` lives as long as the process (a core keeps it): when the
program's session for a core goes, the table is emptied but for `log`. The libretro host takes
its libretro core down itself as the process ends (`retro_deinit` from an `atexit` handler:
PPSSPP's worker threads abort the process if they are destroyed running), and loads no game
it has no window to draw into (PPSSPP's loading thread is only joined by its frames).

All the cores are checked side by side in one process by `home/tests/cores_test.cpp`
(`READER_HOME_CORE_FOLDER`): each describes itself, starts, refuses a missing file, ends a game
it cannot load, and stops, and the process exits cleanly (2026-10-22: the six, eight runs).

The tables grew at their ends on 2026-10-21 (a side reads a field only where the other's
`struct_size` reaches it): the host's `hook_count` (a core that runs an interpreter while
there are hooks asks it after `hooks_changed`) and `picture_rect` (where the core draws the
picture in the window), the core's `grab` (a picture of the game or of the window, handed
over on the core's thread), and the settings `picture.crt` (the CRT sliders, for a core that
draws the shared shader) and `emulation.speed`; then the host's `play` (the program plays a
core's sound, for a core with no output of its own) and the core's `identify` (what a file
is, without running it: the program lists a console's games with it, and gives a file of a
type several cores take to the one that knows it).

**A libretro-made core's buttons** are libretro's joypad bits (bit n is
`RETRO_DEVICE_ID_JOYPAD_n`), whatever the console calls them; its glue translates its
`console.h`'s buttons for `button_yes` and the rest. **Its emulator's own CRT pass** is left
out in a core build (Flycast's renderer is told no; PPSSPP's presentation leaves it out with
`READER_CORE`): the host draws the shader once, at the window's size.

**On Android** (2026-11-02, `../android/`) the host draws with OpenGL ES on the app's Surface
(`READER_CORE_WINDOW_ANDROID`, `READER_LIBRETRO_ANDROID` in `libretro_core.cpp`), asks a core for an
OpenGL ES context, and follows the Surface as the app goes to the back and returns: the context
stays with all the emulator made in it, and only where it draws changes. One core a process there.
PPSSPP's and Flycast's cores are built for it (each fork's `tools/reader/build-core-android.sh`).

## What a fork does to become a core

A fork keeps its own app while its core is made; the core is one more CMake target,
`reader_core`, built from the same sources without the fork's front end.

1. **Describe the console** (`reader_core_info`): the values of the fork's `console.h`
   (`AppKey` is `app_key`, so that statistics and cards keep their name), and its
   processor as `registers.h` reads it: one `reader_core_register_name` per name a
   profile may use (aliases share an index), the alignment of a hook, the highest address.
2. **Run without a window of its own**: the emulator's headless or SDL front end is the
   starting point (`home/docs/one-program.md` lists each fork's). The picture goes into the
   native window `set_window` gives; no Qt, no settings file, no controller code of its own.
   On X11 the core opens a connection of its own to the display for its thread (Xlib's are
   not shared between threads) and flushes it after it changes its window; a window it is
   given may be small at first, so it takes `set_window`'s size again before drawing.
3. **Hand over what `emulator.cpp` hands the engine today**, through the host table instead
   of `Reader::Engine` calls: `game_loading`, `module_loaded`, `game_loaded` (the same ID as
   the fork's), `frame`, `state_loaded`, `is_hooked`/`hook` (the registers as a snapshot of
   values), `audio`, `saved`, and ask `pad` for the controller once a frame.
4. **Answer the reader**: `read_memory`, `write_memory` (all or nothing, only memory a game
   owns), `memory_regions`, `hooks_changed`, `running_game_code`, `press_button`.
5. **Take its settings** in `apply_settings`, as text by key: the common ones
   (`READER_CORE_SETTING_*`) mapped onto its own, and its own under its console's key
   (`ps2.blending`). It writes no settings file.
6. **Export only `reader_core_get`**: build with hidden visibility (`-fvisibility=hidden`),
   link its third-party libraries in statically, so that two cores' copies of zlib or fmt
   never meet in one process. A linker version script (`{ global: reader_core_get; local: *; }`)
   makes sure of it on Linux, as Station's does; on Windows the one `dllexport` does. Link
   with `--no-undefined`: a frontend file that slips in (one function of it named by the
   emulator's display, say) shows as a missing symbol then, not as a crash later.

Rules that hold for every core: callbacks come from the core's own threads (one, or several:
Eden's hooks come from its four emulated processor cores and its frames from its presentation
thread, as in its app, and the program's engine takes them as Eden's app's does); memory is read
mostly from within them, and now and then from the program's thread (safe to do while the
game runs, as the forks' emulators allow it today); a core is never unloaded; its fault handlers (fast memory) let a fault outside its own memory pass on; a
core with an SDL of its own inside tells it to leave the process's signals alone
(`SDL_HINT_NO_SIGNAL_HANDLERS`), or the program no longer ends on SIGTERM.

**Two windows** (2026-11-02): `set_second_window`, a new entry at the table's end, gives a
two-screen console's lower screen a window of its own (the Android app on a device with two
displays). The libretro host then cuts the core's one picture by `screen_rects` and shows a part
in each, in one context; `screen_rect` tells each screen's place in its own window. Android only
so far; elsewhere it answers -1.

**A game's own art** (2026-11-03): `game_art`, at the table's end, writes the picture a game's
file carries, for a console no catalogue covers (Eden's: the app's list of Switch games).

A core with no libretro build hands the app its sound by `play` where the desktop's core has a
sound output of its own (`station_core.cpp`'s `HostAudioStream`).

A glue's `default_options` are what `fixed_options` are for an option the program may change:
taken only where its settings said nothing. Fixed ones go in after the settings, so an option that
is also a setting must not be among them: until this day, Azahar's layout and language settings did
nothing, on the desktop too.

## What a core says it can do beyond the first version (2026-10-24)

To take over everything the separate reader apps did (`home/docs/parity-and-release.md`), the
interface grew, without a new major version: every new entry is appended to its table and read
only where the table's `struct_size` reaches it, so a core built before still loads, with none
of what follows. A core says what it does in `reader_core_info.features` (`READER_CORE_HAS_*`),
and `CheckCore` refuses one that says more than its table has:

| Feature | The core | The program |
| --- | --- | --- |
| `SAVE_SYNC` | `save_locations` (where a game keeps its saves, named as the console's app named them); calls `saves_opening` before the emulator first reads or makes a save, and waits; calls `saves_written` for **every** change of a save, the emulator's own too (a checksum, a flush at closing, a card formatted, a savestate putting a card back), all of them before `game_stopped` | fetches the newest save in `saves_opening`, sends after `saves_written` |
| `DISCS` | `disc_count`, `disc_current`, `disc_label`, `disc_insert` (answered by `disc_changed`), `disc_add` for a console without playlists | a "Change disc" entry |
| `RUMBLE` | calls `rumble(port, strong, weak)` when a controller's motors change | the controller's rumble |
| `POINTER` | `pointer` on the screens `pointer_screens` names (a 3DS's lower, a Switch's touch screen, a light gun) | sends the mouse or a finger where the reader does not take it |
| `STATES` | `state_slots`, `state_info` (exists, when), calls `state_done` after `save_state`/`load_state` | the savestate menu with every slot |
| `PATCHES`, `CODES` | `patches` (a game's patches, by name), the `cheats.patches` and `cheats.codes` settings | the cheats page |

Also: `port_count` and `devices` (kinds of controller per port, `set_port_device`); `setting_flags`,
one per setting (`AT_START`: takes effect when a game starts; `PER_GAME`); and `identify` says
the ID `game_loaded` will, because the program identifies a game first and hands over its own
settings before `load_game`.

The rule for saves comes from CLANNAD (2026-10-23): PCSX2 wrote its card's checksum when the card
was closed, after save sync had sent it, so every device's card looked changed and a newer save
from another device was asked about instead of taken. A byte of a save that changes without
`saves_written` is such a fault.

`cores/test/test_core.cpp` has every feature, and `home/tests/cores_test.cpp` drives each.
