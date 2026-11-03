// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// What the emulator's own files call. Every call from an upstream file into the reader goes
// through this header, and carries a "Reader fork" comment where it is made.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "reader/save_sync/host.h"

namespace Reader::Emu {

/**
 * The emulator says which program is running: at boot (the console's own menu first, with no
 * serial), when the disc's program takes over, and when it is gone again (an empty serial).
 * `serial` is the disc's ("SLPM-66302"), `crc` that of the running program.
 */
void OnGameChanged(const std::string& serial, uint32_t crc, const std::string& title);
void OnShutdown();

// Save sync (reader/save_sync): every game has a memory card of its own in the first slot,
// an ordinary card file "card.ps2" in a folder named by the game's serial in "reader-saves"
// of the memory cards folder, and that folder is what goes to the user's other devices.
/// The memory cards are about to be opened for the disc with this serial (empty: none, the
/// console's own menu). The game's card is made the first slot's, and its newest save is
/// fetched first (this waits for the service, a few seconds at most when it is away).
void BeforeMemoryCards(const std::string& serial, const std::string& title);
/// The first slot's card while a game's own is in use: its file. Empty otherwise.
std::string MemoryCardOverride();
/// A memory card was written to.
void OnMemoryCardWritten(unsigned slot);
/// Save sync is started if it was not (the settings' page asks before any game ran).
void EnsureSaveSync();
/// The app's settings for save sync (its page): applied now, or once the emulator's folders
/// are known.
void ConfigureSaveSync(const SaveSync::Host::Settings& settings);
/// A savestate was loaded: what the reader knew of the screen is no longer true.
void OnStateLoaded();
/// Once per frame of the display.
void OnFrame();

/// A frontend's function that loads the profile of the game that was just loaded. Runs on the
/// emulator's thread, with the game's ID as its disc gives it ("SLPM-66302").
void SetGameLoadedCallback(
    std::function<void(const std::string& game_id, const std::string& title)> callback);
/// A frontend's function for when the game is gone.
void SetGameStoppedCallback(std::function<void()> callback);

/// Whether the game of this ID has a text profile, for the game list. A frontend says how
/// that is known; without one the answer is false.
void SetProfileCheck(std::function<bool(const std::string& game_id)> check);
bool HasProfile(const std::string& game_id);

/// What the console is about to play: `frames` stereo frames of 16-bit samples at 48 kHz.
void OnAudio(const int16_t* samples, std::size_t frames);
/// The same, as the emulator's mixer hands it on: samples from -1 to 1.
void OnAudioFloat(const float* samples, std::size_t frames);

/// Presses or releases an input of the first controller, by the emulator's own number of it
/// (PadDualshock2::Inputs: 0 up, 1 right, 2 down, 3 left, 4 triangle, 5 circle, 6 cross,
/// 7 square, 8 select, 9 start, 10 L1, 11 L2, 12 R1, 13 R2).
void PressButton(int button, bool down);
/// Puts the game's sticks at rest (for when the reader's cursor takes the sticks).
void RestSticks();

// ---- Going on to the next choice (reader/core/next_choice.h) ----

/// Runs the game as fast as it goes, pressing ○ over and over, until its profile reports a
/// choice; the text on the way is not read. Stops at a choice, when nothing was said for 15
/// seconds of the game's time, at a press of one of the game's buttons, or when asked. Starts
/// it, or stops it when it is running. Any thread.
void ToggleNextChoice();
void StopNextChoice();
/// Whether it is running.
bool GoingToNextChoice();
/// Told when it starts (`running`) and when it ends (why, and how many texts were passed), on
/// the emulator's thread. A message about it is shown on the game's picture as well.
void SetNextChoiceCallback(std::function<void(bool running, int end, std::uint64_t passed)> callback);
/// Pad::SetControllerState: an input of a controller changed (a press of the player's ends it).
void OnPadInput(unsigned controller, unsigned bind, float value);

} // namespace Reader::Emu
