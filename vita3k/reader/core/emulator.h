// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What Vita3K's own files tell the reader's side, at their "Reader fork" comments. A build
// without a reader leaves these empty, and the upstream app behaves as it always did. Filled by
// a core (reader/core_library/vita3k_core.cpp) before a game starts.

#include "reader/core/registers.h"

namespace Reader::Emu {

struct Events {
    /// Whether the instruction at `address` is hooked. Any guest thread, as code is translated.
    bool (*is_hooked)(u32 address) = nullptr;
    /// A hooked instruction is about to run. Any guest thread.
    void (*hook)(const GuestRegisters& registers) = nullptr;
    /// No hook can be carried out at `address`.
    void (*hook_unusable)(u32 address, const char* why) = nullptr;
    /// The loader put a module into memory: `path` as the game names it ("app0:eboot.bin").
    /// `module_nid` differs between two builds of it.
    void (*module_loaded)(const char* name, const char* path, u32 base, u32 size, u32 module_nid) = nullptr;
};
inline Events events;

} // namespace Reader::Emu

// The Android app has the reader in its own library (reader/core/emulator.cpp); a core has it in
// the program.
#ifdef READER_ANDROID
#include <cstdint>
#include <functional>
#include <string>

struct EmuEnvState;
namespace app {
class AppSessionController;
}

namespace Reader::Emu {

// ---- What Vita3K's frontend calls (sdl-frontend/src/session.cpp, audio/src/audio.cpp) ----

/// A game is about to be loaded.
void GameLoading(EmuEnvState& emu, app::AppSessionController& session);
/// Once a tick of the frontend's loop while the game runs: the reader's presses, its frame.
void Frame(EmuEnvState& emu, const app::AppSessionController& session);
/// The game has ended, or never started.
void GameEnded();
/// A port of the game is about to play `frames` of 16-bit sound.
void OnAudio(const void* port, const std::int16_t* samples, std::size_t frames, unsigned channels, int rate);

// ---- What the bridge to Kotlin calls (reader/android/reader_jni.cpp) ----

void SetGameLoadedCallback(std::function<void(const std::string& game_id)> callback);
void SetGameStoppedCallback(std::function<void()> callback);
void SetMenuCallback(std::function<void(const std::string& what)> callback);
void OpenMenu(const std::string& what);
std::string GameTitle();
/// Presses or releases a button by its bit in the Vita's own word.
void PressButton(int bit, bool down);
void RestSticks();
/// Whether the game is running now (not stopped by the app's pause menu).
bool Running();
/// {window width, height, picture left, top, right, bottom}, false while no game is shown.
bool PictureRect(int (&values)[6]);

/// Going on to the next choice: not in this emulator.
void ToggleNextChoice();
[[nodiscard]] bool GoingToNextChoice();
void SetNextChoiceCallback(std::function<void(bool running, int end, std::uint64_t passed)> callback);

} // namespace Reader::Emu
#endif
