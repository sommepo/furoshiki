// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include "reader/core/types.h"
// A core of Furoshiki has no save sync of its own (the program is the client).
#if !defined(READER_CORE) && !defined(READER_NO_SAVE_SYNC)
#include "reader/save_sync/host.h"
#endif

// How the reader layer every reader fork shares is tied to this emulator, where it asks for it
// by name: the text engine's way into the game (GameHost; emulator.cpp), save sync's start and
// settings (citra_qt/reader/save_sync_glue.cpp on a computer, jni/save_sync_glue.cpp on
// Android), the desktop windows' presses (citra_qt/reader/host.cpp) and going on to the next
// choice. Every call from an upstream file into the reader carries a "Reader fork" comment.

namespace Core {
class System;
}

namespace Reader {
class Host;
struct GuestRegisters;
}

namespace Reader::Emu {

/// What the engine needs of the emulator (Reader::Host) for the game `system` is starting as
/// process `process_id`: its memory, its translated code, whether the code running is the
/// game's, its log. Handed to Engine::OnGameLoading.
Host& GameHost(Core::System& system, u32 process_id);

/// What Azahar's files tell the reader (each at a "Reader fork" comment): the game `system`
/// started as process `process_id` (core.cpp), the game stopped, a frame presented
/// (video_core/gpu.cpp), a savestate loaded (savestate.cpp), and the hooks in the game's code
/// as the JIT translates and runs it (arm_dynarmic_reader.cpp). In the app they go to the text
/// engine (emulator_engine.cpp); in a core of Reader Home, to the program
/// (reader_core_library/azahar_core.cpp).
void OnGameStarted(Core::System& system, u32 process_id, u64 title_id);
void OnGameStopped();
void OnFrame();
void OnStateLoaded();
[[nodiscard]] bool IsHooked(u32 pc);
void DispatchHook(const GuestRegisters& registers);
void ReportUnusableHook(u32 pc, const std::string& why);

/// A program's title ID as the reader names a game (console.h, GameIdPattern): sixteen
/// hexadecimal digits in capitals, "0004000000088A00".
[[nodiscard]] std::string GameId(u64 title_id);
/// The other way round: 0 when `game_id` is not a title ID.
[[nodiscard]] u64 TitleId(std::string_view game_id);

/// Starts save sync if it is not yet; whether it is now.
bool EnsureSaveSync();
/// The settings of the Android app's Save sync page (reader/SaveSync.kt): starts save sync
/// where it can and hands them over.
#if !defined(READER_CORE) && !defined(READER_NO_SAVE_SYNC)
void ConfigureSaveSync(const SaveSync::Host::Settings& settings);
#endif

/// Presses or lets go of a button of the console, by its number (console.h: the Android app's
/// numbers, NativeLibrary.ButtonType): touch play's presses, as a device of the reader's own.
void PressButton(int button, bool down);
/// Shows the game the sticks anew: at rest while the reader's cursor has them (the next time
/// the console reads them, a few milliseconds later).
void RestSticks();

/// The console's buttons as the HID service has read them (its PadState bits), handed to the
/// reader's input filter as changes of the emulator's NativeButtons: what the reader takes for
/// its own is left out, what it presses for the player is put in. Called by the HID service,
/// on the emulation thread.
[[nodiscard]] u32 FilterPad(u32 pad);
/// The circle pad, each way from -1 to 1. True when the game must see it at rest.
[[nodiscard]] bool FilterStick(int stick, float x, float y);
/// ZL and ZR as the controller has them now, handed to the reader's input filter as changes of
/// NativeButton ZL and ZR (14, 15). The game reads them through the extra buttons' channel
/// (ir:USER), which it opens or not; the HID service hands them over every time it reads the
/// pad, so that the reader hears them in every game. Emulation thread.
void FilterExtraButtons(bool zl, bool zr);
/// Whether the reader took ZL (0) or ZR (1) for its own while it is held: the game sees it
/// let go.
[[nodiscard]] bool ExtraButtonTaken(int which);

/// Going on to the next choice: not in this emulator yet (Console.HAS_NEXT_CHOICE is false);
/// these do nothing.
inline void ToggleNextChoice() {}
[[nodiscard]] inline bool GoingToNextChoice() {
    return false;
}
inline void SetNextChoiceCallback(
    std::function<void(bool running, int end, std::uint64_t passed)>) {}

} // namespace Reader::Emu
