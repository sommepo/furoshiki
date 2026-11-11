// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A core of Furoshiki has no save sync of its own (the program is the client).
#if !defined(READER_CORE) && !defined(READER_NO_SAVE_SYNC)
#include "reader/save_sync/host.h"
#endif

// What the emulator's own files call. Every call from an upstream file into the reader goes
// through this header, and carries a "Reader fork" comment where it is made.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Reader::Emu {

/// Called once when the emulator is set up: from then on the reader hears of a game being
/// started and stopped, of each frame, of a loaded savestate and of a changed disc through
/// the emulator's own events.
void Install();

/// A frontend's function that loads the profile of the game that was just loaded. Runs on
/// the thread that loads the game, with the game's ID as its disc gives it ("HDR-0164").
void SetGameLoadedCallback(std::function<void(const std::string& game_id)> callback);
/// A frontend's function for when the game is gone.
void SetGameStoppedCallback(std::function<void()> callback);

/// One of the emulator's own menus asks for the reader ("menu": what the reader offers for
/// the running game; "settings": its own page). A frontend says how that is answered.
void SetMenuCallback(std::function<void(const std::string& what)> callback);
void OpenMenu(const std::string& what);

/// Whether the game of this ID has a text profile, for the game list. A frontend says how
/// that is known; without one the answer is false.
void SetProfileCheck(std::function<bool(const std::string& game_id)> check);
bool HasProfileCheck();
bool HasProfile(const std::string& game_id);

// ---- Save sync (reader/save_sync) ----

/// Starts save sync if it has not started yet and the app knows its data folder. True when
/// it runs (switched on or not).
bool EnsureSaveSync();
/// A memory unit is about to be read from its file (`port` is "A1" for the first
/// controller's first slot). For a game's own memory unit this is when another device's
/// newer save takes the file's place; it waits for the service a few seconds at most.
void BeforeMemoryUnit(const std::string& port);
/// The game wrote to a memory unit (save sync sends it a moment later).
void OnSaveWritten(const std::string& port);
/// The folder a game's own memory unit is kept in, made if need be: one per game, named by
/// the game's ID. Empty when the ID cannot name a folder.
std::string SaveFolderOf(const std::string& game_id);

/// The running disc's name as its own header gives it, or empty.
std::string GameTitle();

/// What the console is about to play: `frames` stereo frames of 16-bit samples at 44.1 kHz,
/// left first.
void OnAudio(const int16_t* samples, std::size_t frames);

/// A key or a controller's button went down or up, before the emulator maps it to anything.
/// True when the reader keeps it for itself: its cursor's button, its clicks, its statistics
/// button.
bool FilterKey(int device, int key_code, bool down);
/// Presses or releases a button of the first controller, by the number of its bit in the
/// console's button word (DreamcastKey: 1 B, 2 A, 3 Start, 4 up, 5 down, 6 left, 7 right,
/// 9 Y, 10 X).
void PressButton(int bit, bool down);
/// Pulls a trigger of the first controller all the way, or lets it go (0 left, 1 right).
void PressTrigger(int trigger, bool down);
/// Puts the game's stick at rest (for when the reader's cursor takes the sticks).
void RestSticks();
/// The first controller's stick, each way from -100 to 100 (the development channel).
void MoveStick(int x, int y);
// The first controller\'s right stick (the second stick\'s bytes the game is sent), -100 to 100.
void MoveRightStick(int x, int y);
/// A stick moved. True when the game must see it at rest instead.
bool FilterStick(int stick, float x, float y);

// ---- For writing profiles (the harness and the development control channel) ----

/// Writes the console's main memory to a file, as it is at this moment.
bool DumpMemory(const std::string& path);


/// The Android app's Save sync page hands its settings here (reader_jni_common.cpp): save sync
/// is started if it is not yet, and given them.
#if !defined(READER_CORE) && !defined(READER_NO_SAVE_SYNC)
void ConfigureSaveSync(const SaveSync::Host::Settings& settings);
#endif

/// Going on to the next choice (reader/core/next_choice.h): not in this emulator yet
/// (Reader::ConsoleTraits::HasNextChoice is false); these do nothing.
void ToggleNextChoice();
[[nodiscard]] bool GoingToNextChoice();
void SetNextChoiceCallback(std::function<void(bool running, int end, std::uint64_t passed)> callback);

} // namespace Reader::Emu

#ifdef READER_WATCH
/// A debug build's write watch (Karakuri: built with -DREADER_WATCH, run with the recompiler off,
/// since its writes go straight to memory and are not seen here). Every write the processor
/// makes into the watched range is recorded with the instruction's address (pc) and the
/// return address (pr). Development channel: `watch ADDR LEN [MAX]`, `watch-log [clear]`.
namespace Reader::Watch {
extern volatile bool active;
extern volatile bool reading;
void Hit(std::uint32_t addr, std::uint64_t value, std::uint32_t size);
/// Reads, from `watch-read ADDR LEN [MAX]`: recorded in the same log, marked "read".
void Read(std::uint32_t addr, std::uint32_t size);
void SetRead(std::uint32_t addr, std::uint32_t len, std::uint32_t max_hits);
void Set(std::uint32_t addr, std::uint32_t len, std::uint32_t max_hits);
std::string Log(bool clear);
} // namespace Reader::Watch
#endif

