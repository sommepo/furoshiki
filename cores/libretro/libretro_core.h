/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A core of Reader Home made from an emulator's libretro build (cores/reader_core.h): the
 * emulator's libretro core is linked into the same module, and libretro_core.cpp is the
 * frontend it talks to, without a window of its own. It runs the game on a thread of its own,
 * makes an OpenGL context in the window Reader Home gives (EGL on X11, WGL on Windows) with a
 * framebuffer for the emulator to draw into, and draws the picture into the window through
 * the reader's CRT shader (reader/core/crt.h), letterboxed; it hands the sound to the program
 * to play (reader_core_host.play), reads the program's controller for the console's, keeps
 * savestates, and maps the program's settings onto the emulator's core options.
 *
 * What is the emulator's own (its console's description, its memory for the reader, how the
 * reader hears of its games) is the fork's glue: a file of the fork's (Flycast's
 * reader/core_library/flycast_core.cpp) fills a Glue and exports reader_core_get with
 * MakeApi's table. The fork's emulator calls its own Reader::Emu functions as in its app; the
 * glue answers them through Host().
 *
 * The console's button word for a libretro core is libretro's joypad: bit n is
 * RETRO_DEVICE_ID_JOYPAD_n (B 0, Y 1, SELECT 2, START 3, UP 4, DOWN 5, LEFT 6, RIGHT 7, A 8,
 * X 9, L 10, R 11, L2 12, R2 13, L3 14, R3 15), whatever the console calls its buttons.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include "reader_core.h"

namespace ReaderLibretro {

/// What the fork's glue says of its emulator.
struct Glue {
    /// The console's description (its buttons in libretro's numbering, see above).
    reader_core_info info{};
    /// The reader's way into the emulator's memory (reader_core_api's, any thread).
    size_t (*read_memory)(uint64_t address, void* buffer, size_t size) = nullptr;
    int32_t (*write_memory)(uint64_t address, const void* buffer, size_t size) = nullptr;
    uint32_t (*memory_regions)(reader_core_region* regions, uint32_t capacity) = nullptr;
    void (*hooks_changed)() = nullptr;
    int32_t (*identify)(const char* path, char* id, size_t id_size, char* title, size_t title_size) = nullptr;
    /// A setting of the program's (by its key) as the emulator's core options: `set` takes an
    /// option's name and value. Called on the game's thread, or before a game.
    void (*apply_setting)(const std::string& key, const std::string& value,
                          const std::function<void(const std::string& option, const std::string& value)>& set) = nullptr;
    /// Core options the core always has here, whatever the program says ("option", "value",
    /// ... , nullptr): the renderer libretro_core.cpp can show, say.
    const char* const* fixed_options = nullptr;
    /// The same pairs for what the program may change: taken only where its settings said nothing.
    const char* const* default_options = nullptr;
    /// What is plugged into the console's first two ports, as libretro names devices
    /// (RETRO_DEVICE_JOYPAD, 1, unless the glue says otherwise).
    unsigned port_device = 1;
    /// Called on the game's thread before a game is loaded (folders the emulator wants made).
    void (*before_load)() = nullptr;
    /// Called on the game's thread once the game is loaded (after the emulator's own start).
    void (*game_loaded)() = nullptr;
    /// Called on the game's thread before the game is unloaded.
    void (*game_unloading)() = nullptr;
    /// For a console with two screens: where each is in the core's picture, as parts of its
    /// width and height from the top left ({x, y, width, height}, a width of 0 for one not
    /// shown); false while the core does not know. Called on the game's thread after a frame.
    bool (*screen_rects)(float rects[2][4]) = nullptr;
    /// Whether the code running now is the game's own (reader_core_api.running_game_code);
    /// null: always.
    int32_t (*running_game_code)() = nullptr;
    /// The emulator draws the reader's CRT picture itself (on each of its screens): the
    /// picture is then put into the window as it is.
    bool crt_in_core = false;
    /// Each retro_run is one of the console's frames, whatever sound it gave (PPSSPP's: its
    /// sound comes from a thread of its own, unevenly over the runs): paced by the frame alone.
    bool one_frame_a_run = false;
    /// Since 2026-10-24 (reader_core_api's of the same names; info.features says which there
    /// are). The host itself answers savestates (READER_CORE_HAS_STATES), discs through the core's
    /// disk control, rumble, every port, the pointer and codes (libretro's cheats); the glue says
    /// where a game keeps its saves (and calls Host()->saves_opening and saves_written as its
    /// emulator opens and writes them) and which patches it has.
    int32_t (*save_locations)(const char* game_id, reader_core_location_fn each, void* context) = nullptr;
    int32_t (*patches)(const char* game_id, reader_core_patch_fn each, void* context) = nullptr;
};

/// The table reader_core_get hands over, made once from the glue (which must stay alive).
const reader_core_api* MakeApi(const Glue& glue);

/// What the program gave at init: for the glue's Reader::Emu functions (null before init).
const reader_core_host* Host();
/// Whether the program's table has `field` filled (a program built before it was added has not).
#define READER_LIBRETRO_HOST_HAS(field)                                                                  \
    (ReaderLibretro::Host() != nullptr &&                                                                \
     ReaderLibretro::Host()->struct_size >= offsetof(reader_core_host, field) + sizeof(ReaderLibretro::Host()->field) && \
     ReaderLibretro::Host()->field != nullptr)
/// The folders the program gave at init.
const std::string& DataFolder();
const std::string& SavesFolder();
const std::string& SystemFolder();
/// The folder the core's own library is in (where a package puts files a core brings along).
std::string CoreFolder();

/// The program's log (any thread).
void Log(uint32_t level, const std::string& message);

} // namespace ReaderLibretro
