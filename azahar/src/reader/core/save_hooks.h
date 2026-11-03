// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "common/common_types.h"

// What the emulator tells save sync (src/save_sync), which the frontend that has it ties in
// (the desktop app): the core knows nothing of save sync, and a frontend without it leaves
// these empty.
namespace Reader::SaveHooks {

/// A game was loaded and is about to run: its save is brought in line first (this waits for
/// the service, a few seconds at most). The program ID.
inline std::function<void(u64 program_id)> game_loading;
/// The game made its save final (FS ControlArchive, CloseArchive).
inline std::function<void()> save_committed;
/// The game stopped.
inline std::function<void()> game_stopped;

} // namespace Reader::SaveHooks
