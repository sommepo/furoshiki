// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <cstdint>
#include <string>

namespace Reader::Emu {

/// Called once when the emulator is set up.
void Install();

// ---- What the emulator's core calls (src/core, at "Reader fork" comments) ----

/// The running game changed (System::UpdateRunningGame): a disc was booted, put in or taken
/// out, or a savestate brought another. `code` is the disc's serial ("SLPS-01794"), empty
/// when there is no game; `image` the disc (void: core/cdrom's CDImage), or null.
void OnRunningGameChanged(const std::string& code, const std::string& title, void* image);
/// The console is switched off (System::Shutdown).
void OnShutdown();
/// A frame of the console has been run (System::DoRunFrame).
void OnFrame();
/// A savestate was loaded (System::DoLoadState).
void OnStateLoaded();
/// Whether the running profile has hooks in the game's code: the CPU then runs its
/// interpreter, which asks HookAt about every instruction.
[[nodiscard]] bool AnyHooks();
/// The CPU is about to run the instruction at `pc`: a hook there is carried out.
void HookAt(uint32_t pc);

/// A frontend's function that loads the profile of the game that was just loaded. Runs on
/// the thread that loads the game, with the game's ID as its disc gives it ("SLPS-01794").
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
/// A memory card is about to be read from its file (`slot` 0 for the first). For a game's own
/// card in the first slot this is when another device's newer save takes the file's place;
/// it waits for the service a few seconds at most.
void BeforeMemoryCard(int slot, const std::string& path);
/// The game's card was written to its file (save sync sends it a moment later).
void OnMemoryCardWritten(const std::string& path);
/// The file a game's own card in the first slot is kept in when save sync is built in:
/// "saves/<serial>/<serial>_1.mcd" in the app's data folder. Empty otherwise (the emulator's
/// own place is used then), and when the serial cannot name a folder.
std::string MemoryCardPathOf(const std::string& game_id);
/// The app's data folder (the emulator's user folder), as the frontend says it; save sync
/// starts once it is known.
void SetDataDirectory(const std::string& directory);

/// The running game's name as the emulator's list of games gives it, or empty.
std::string GameTitle();

/// What the console is about to play: `frames` stereo frames of 16-bit samples at 44.1 kHz,
/// left first (the SPU's output).
void OnAudio(const int16_t* samples, std::size_t frames);

/// A key or a controller's button went down or up, before the emulator maps it to anything.
/// True when the reader keeps it for itself: its cursor's button, its clicks, its statistics
/// button.
bool FilterKey(int device, int key_code, bool down);
/// Presses or releases a button of the first controller, by the emulator's number for it
/// (DigitalController::Button: 3 Start, 4 up, 5 right, 6 down, 7 left, 12 triangle, 13 circle,
/// 14 cross, 15 square). Through the poster when one is set (SetPressPoster), else at once.
void PressButton(int bit, bool down);
/// How a press reaches the controller from another thread: the poster runs it on the
/// emulation thread later (Android's frontend, whose UI thread presses while the emulation
/// thread may destroy the controllers). None by default: the press is made at once.
void SetPressPoster(std::function<void(std::function<void()> press)> poster);
/// Puts the game's stick at rest (for when the reader's cursor takes the sticks).
void RestSticks();
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
