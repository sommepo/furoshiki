// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The program's emulator.h: what the shared reader windows (shared/reader/qt, built here with
// HOME_CORES) ask of "the emulator". In a fork this header is the emulator's own and its
// emulator.cpp answers with that emulator; here the emulator is whichever core runs the game
// (cores/reader_core.h), and src/reader_emu.cpp answers through it. Only what the shared
// windows call is here; what a fork's own frontend calls besides is the program's business
// (src/reader_host.cpp).

#include <cstdint>
#include <functional>
#include <string>
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/console_host.h"
#endif

namespace Home {
class CoreSession;
}

namespace Reader::Emu {

/// The core whose game the reader reads from now on, or none (Home's game window says).
void SetSession(Home::CoreSession* session);
[[nodiscard]] Home::CoreSession* Session();

/// Presses or lets go of a button of the console's first controller, by its bit in the
/// console's button word (the core's description lists them): the reader's touch play.
void PressButton(int bit, bool down);
/// Puts the game's sticks at rest (the reader's cursor takes the sticks): the program's
/// controller is the one that says, and it hears of that through the reader's input filter.
void RestSticks();

/// Writes the game's memory, every region the core says it has, one after the other, to a
/// file (the development channel's "dump").
bool DumpMemory(const std::string& path);

/// Going on to the next choice (reader/core/next_choice.h): no core offers it yet
/// (ConsoleTraits::HasNextChoice is false); these do nothing.
void ToggleNextChoice();
[[nodiscard]] bool GoingToNextChoice();
void SetNextChoiceCallback(std::function<void(bool running, int end, std::uint64_t passed)> callback);
/// Whether save sync is there (the Save sync window asks; the program starts it for each
/// console as its core is started: main_window.cpp).
bool EnsureSaveSync();
#ifndef READER_NO_SAVE_SYNC
/// The Android app's Save sync page handed its settings over (reader/SaveSync.kt).
void ConfigureSaveSync(const SaveSync::ConsoleHost::Settings& settings);
#endif

} // namespace Reader::Emu
