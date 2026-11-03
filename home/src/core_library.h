// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The emulators' cores (cores/reader_core.h): found in the program's folder, loaded, and what
// they say of themselves checked before anything of theirs is used. One program, each emulator
// a core it loads (docs/one-program.md); a core once loaded stays loaded.

#include <cstddef>
#include <type_traits>
#include <QList>
#include <QString>
#include <QStringList>
#include "core_table.h"

namespace Home {

/// The cores' files in `folder`: reader_core_<name>.so (.dll on Windows), by name.
[[nodiscard]] QStringList FindCores(const QString& folder);

/// Loads the core in `path` and checks it (CheckCore). Nothing, with `error` set, when it is
/// not a core, is of another version of the interface, or says something that cannot be used.
[[nodiscard]] bool LoadCore(const QString& path, Core& core, QString& error);

/// What is wrong with a core's table and description, or nothing: every function the program
/// calls is there, the console has a key and a name, every register name is in the snapshot.
[[nodiscard]] QStringList CheckCore(const reader_core_api& api, const reader_core_info& info);

/// The core's settings' flags (READER_CORE_SETTING_AT_START...) for settings entry `index`.
[[nodiscard]] uint32_t SettingFlags(const reader_core_info& info, uint32_t index);
/// How many controllers the console takes (1 for a core that does not say).
[[nodiscard]] uint32_t PortCount(const reader_core_info& info);
/// Which of its screens take a pointer or a finger (bit n screen n; 0 for none or a core that
/// does not say).
[[nodiscard]] uint32_t PointerScreens(const reader_core_info& info);
/// Its savestate slots, numbered from 1: 10 for a core that does not say but takes savestates,
/// as the reader apps had them.
[[nodiscard]] int32_t StateSlots(const reader_core_info& info);
/// The kinds of controller a port may have (none: one kind only).
[[nodiscard]] QList<reader_core_device> Devices(const reader_core_info& info);
} // namespace Home
